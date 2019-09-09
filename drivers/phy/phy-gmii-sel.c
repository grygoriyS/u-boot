// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments CPSW Port's PHY Interface Mode selection Driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <common.h>
#include <dm.h>
#include <dm/device.h>
#include <dm/lists.h>
#include <generic-phy.h>
#include <phy.h>
#include <regmap.h>
#include <syscon.h>

/* AM33xx SoC specific definitions for the CONTROL port */
#define AM33XX_GMII_SEL_MODE_MII	0
#define AM33XX_GMII_SEL_MODE_RMII	1
#define AM33XX_GMII_SEL_MODE_RGMII	2

enum {
	PHY_GMII_SEL_PORT_MODE,
	PHY_GMII_SEL_RGMII_ID_MODE,
	PHY_GMII_SEL_RMII_IO_CLK_EN,
	PHY_GMII_SEL_LAST,
};

struct phy_gmii_sel_reg_field {
	unsigned int reg;
	unsigned int mask;
	unsigned int shift;
};

#define REG_FIELD(_reg, _mask, _shift) {			\
				.reg = _reg,			\
				.mask = _mask << _shift,	\
				.shift = _shift,	\
				}


struct phy_gmii_sel_phy_priv {
	struct phy_gmii_sel_priv *priv;
	u32		id;
	struct phy	*if_phy;
	int		rmii_clock_external;
	int		phy_if_mode;
	const struct phy_gmii_sel_reg_field *regfields;
};

struct phy_gmii_sel_soc_data {
	u32 num_ports;
	u32 features;
	const struct phy_gmii_sel_reg_field (*regfields)[PHY_GMII_SEL_LAST];
};

struct phy_gmii_sel_priv {
	struct udevice *dev;
	const struct phy_gmii_sel_soc_data *soc_data;
	struct regmap *regmap;
	struct phy_gmii_sel_phy_priv *if_phys;
};

static int phy_gmii_sel_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct phy_gmii_sel_priv *priv = dev_get_priv(phy->dev);
	const struct phy_gmii_sel_soc_data *soc_data;
	const struct phy_gmii_sel_reg_field *regfield;
	struct phy_gmii_sel_phy_priv *if_phy;
	struct udevice *dev = phy->dev;
	int ret, rgmii_id = 0;
	u32 gmii_sel_mode = 0;

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	if_phy = &priv->if_phys[phy->id - 1];
	soc_data = if_phy->priv->soc_data;

	switch (submode) {
	case PHY_INTERFACE_MODE_RMII:
		gmii_sel_mode = AM33XX_GMII_SEL_MODE_RMII;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		gmii_sel_mode = AM33XX_GMII_SEL_MODE_RGMII;
		break;

	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		gmii_sel_mode = AM33XX_GMII_SEL_MODE_RGMII;
		rgmii_id = 1;
		break;

	case PHY_INTERFACE_MODE_MII:
		mode = AM33XX_GMII_SEL_MODE_MII;
		break;

	default:
		dev_warn(dev,
			 "port%u: unsupported mode: \"%s\". Defaulting to MII.\n",
			 if_phy->id, phy_string_for_interface(submode));
		return -EINVAL;
	}

	if_phy->phy_if_mode = submode;

	dev_err(dev, "%s id:%u mode:%u:%u rgmii_id:%d rmii_clk_ext:%d\n",
		__func__, if_phy->id, mode, submode, rgmii_id,
		if_phy->rmii_clock_external);

	regfield = &if_phy->regfields[PHY_GMII_SEL_PORT_MODE];
	dev_err(dev, "%s field %x %x %d\n", __func__,
			regfield->reg,
			regfield->mask,
			regfield->shift);
	ret = regmap_update_bits(priv->regmap,
				 regfield->reg,
				 regfield->mask,
				 gmii_sel_mode << regfield->shift);
	if (ret) {
		dev_err(dev, "port%u: set mode fail %d", if_phy->id, ret);
		return ret;
	}

	if (soc_data->features & BIT(PHY_GMII_SEL_RGMII_ID_MODE)) {
		regfield = &if_phy->regfields[PHY_GMII_SEL_RGMII_ID_MODE];
		ret = regmap_update_bits(priv->regmap,
					 regfield->reg,
					 regfield->mask,
					 rgmii_id << regfield->shift);
		if (ret)
			return ret;
	}

	if (soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN)) {
		regfield = &if_phy->regfields[PHY_GMII_SEL_RMII_IO_CLK_EN];
		ret = regmap_update_bits(priv->regmap,
					 regfield->reg,
					 regfield->mask,
					 if_phy->rmii_clock_external << regfield->shift);
	}

	dev_err(dev, "%s done\n", __func__);
	return ret;
}

static const
struct phy_gmii_sel_reg_field
phy_gmii_sel_fields_am33xx[][PHY_GMII_SEL_LAST] = {
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x650, 0x3, 0),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD(0x650, 0x1, 4),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD(0x650, 0x1, 6),
	},
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x650, 0x3, 2),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD(0x650, 0x1, 5),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD(0x650, 0x1, 7),
	},
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_soc_am33xx = {
	.num_ports = 2,
	.features = BIT(PHY_GMII_SEL_RGMII_ID_MODE) |
		    BIT(PHY_GMII_SEL_RMII_IO_CLK_EN),
	.regfields = phy_gmii_sel_fields_am33xx,
};

static const
struct phy_gmii_sel_reg_field
phy_gmii_sel_fields_dra7[][PHY_GMII_SEL_LAST] = {
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x554, 0x3, 0),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD((~0), 0, 0),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD((~0), 0, 0),
	},
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x554, 0x3, 4),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD((~0), 0, 0),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD((~0), 0, 0),
	},
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_soc_dra7 = {
	.num_ports = 2,
	.regfields = phy_gmii_sel_fields_dra7,
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_soc_dm814 = {
	.num_ports = 2,
	.features = BIT(PHY_GMII_SEL_RGMII_ID_MODE),
	.regfields = phy_gmii_sel_fields_am33xx,
};

static int phy_gmii_sel_of_xlate(struct phy *phy,
				 struct ofnode_phandle_args *args)
{
	struct phy_gmii_sel_priv *priv = dev_get_priv(phy->dev);
	int phy_id = args->args[0];

	if (args->args_count < 1)
		return -EINVAL;
	if (!priv || !priv->if_phys)
		return -ENODEV;
	if (priv->soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN) &&
	    args->args_count < 2)
		return -EINVAL;
	if (phy_id > priv->soc_data->num_ports)
		return -EINVAL;
	if (phy_id != priv->if_phys[phy_id - 1].id)
		return -EINVAL;

	phy_id--;
	if (priv->soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN))
		priv->if_phys[phy_id].rmii_clock_external = args->args[1];
	dev_err(dev, "%s id:%u ext:%d\n", __func__,
		priv->if_phys[phy_id].id, args->args[1]);

	phy->id = priv->if_phys[phy_id].id;
	return 0;
}

static int phy_gmii_sel_init_ports(struct phy_gmii_sel_priv *priv)
{
	const struct phy_gmii_sel_soc_data *soc_data = priv->soc_data;
	struct udevice *dev = priv->dev;
	struct phy_gmii_sel_phy_priv *if_phys;
	int i, num_ports;

	num_ports = priv->soc_data->num_ports;

	if_phys = devm_kcalloc(priv->dev, num_ports,
			       sizeof(*if_phys), GFP_KERNEL);
	if (!if_phys)
		return -ENOMEM;
	dev_err(dev, "%s %d\n", __func__, num_ports);

	for (i = 0; i < num_ports; i++) {
		if_phys[i].id = i + 1;
		if_phys[i].priv = priv;

		if_phys[i].regfields = soc_data->regfields[i];
		dev_err(dev, "%s field %x %x %d\n", __func__,
			if_phys[i].regfields[0].reg,
			if_phys[i].regfields[0].mask,
			if_phys[i].regfields[0].shift);
	}

	priv->if_phys = if_phys;
	return 0;
}

static int phy_gmii_sel_probe(struct udevice *dev)
{
	struct phy_gmii_sel_priv *priv = dev_get_priv(dev);
	const struct phy_gmii_sel_soc_data *data;
	int ret;

	data = (const struct phy_gmii_sel_soc_data *)dev_get_driver_data(dev);

	priv->dev = dev;
	priv->soc_data = data;

	priv->regmap = syscon_node_to_regmap(ofnode_get_parent(dev_ofnode(dev)));
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to get syscon %d\n", ret);
		return ret;
	}

	ret = phy_gmii_sel_init_ports(priv);

	dev_err(dev, "============ phy phy_gmii_sel_probe %d\n", ret);

	return 0;
}

static const struct phy_ops phy_gmii_sel_ops = {
	.init_ext	= phy_gmii_sel_mode,
	.of_xlate	= phy_gmii_sel_of_xlate,
};

static const struct udevice_id phy_gmii_sel_id_table[] = {
	{
		.compatible	= "ti,am3352-phy-gmii-sel",
		.data		= (ulong)&phy_gmii_sel_soc_am33xx,
	},
	{
		.compatible	= "ti,dra7xx-phy-gmii-sel",
		.data		= (ulong)&phy_gmii_sel_soc_dra7,
	},
	{
		.compatible	= "ti,am43xx-phy-gmii-sel",
		.data		= (ulong)&phy_gmii_sel_soc_am33xx,
	},
	{
		.compatible	= "ti,dm814-phy-gmii-sel",
		.data		= (ulong)&phy_gmii_sel_soc_dm814,
	},
	{}
};

U_BOOT_DRIVER(phy_gmii_sel) = {
	.name	= "phy-gmii-sel",
	.id	= UCLASS_PHY,
	.of_match = phy_gmii_sel_id_table,
	.ops = &phy_gmii_sel_ops,
	.probe = phy_gmii_sel_probe,
	.priv_auto_alloc_size = sizeof(struct phy_gmii_sel_priv),
};
