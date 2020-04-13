// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments CPSW Port's PHY Interface Mode selection Driver
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Based on cpsw-phy-sel.c driver created by Mugunthan V N <mugunthanvnm@ti.com>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

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

struct phy_gmii_sel_phy_priv {
	struct phy_gmii_sel_priv *priv;
	u32		id;
	struct phy	*if_phy;
	int		rmii_clock_external;
	int		phy_if_mode;
	struct regmap_field *fields[PHY_GMII_SEL_LAST];
};

struct phy_gmii_sel_soc_data {
	u32 num_ports;
	u32 features;
	const struct reg_field (*regfields)[PHY_GMII_SEL_LAST];
};

struct phy_gmii_sel_priv {
	struct device *dev;
	const struct phy_gmii_sel_soc_data *soc_data;
	struct regmap *regmap;
	struct phy_provider *phy_provider;
	struct phy_gmii_sel_phy_priv *if_phys;
};

static int phy_gmii_sel_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct phy_gmii_sel_phy_priv *if_phy = phy_get_drvdata(phy);
	const struct phy_gmii_sel_soc_data *soc_data = if_phy->priv->soc_data;
	struct device *dev = if_phy->priv->dev;
	struct regmap_field *regfield;
	int ret, rgmii_id = 0;
	u32 gmii_sel_mode = 0;

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

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
	case PHY_INTERFACE_MODE_GMII:
		gmii_sel_mode = AM33XX_GMII_SEL_MODE_MII;
		break;

	default:
		dev_warn(dev, "port%u: unsupported mode: \"%s\"\n",
			 if_phy->id, phy_modes(submode));
		return -EINVAL;
	}

	if_phy->phy_if_mode = submode;

	dev_dbg(dev, "%s id:%u mode:%u rgmii_id:%d rmii_clk_ext:%d\n",
		__func__, if_phy->id, submode, rgmii_id,
		if_phy->rmii_clock_external);

	regfield = if_phy->fields[PHY_GMII_SEL_PORT_MODE];
	ret = regmap_field_write(regfield, gmii_sel_mode);
	if (ret) {
		dev_err(dev, "port%u: set mode fail %d", if_phy->id, ret);
		return ret;
	}

	if (soc_data->features & BIT(PHY_GMII_SEL_RGMII_ID_MODE) &&
	    if_phy->fields[PHY_GMII_SEL_RGMII_ID_MODE]) {
		regfield = if_phy->fields[PHY_GMII_SEL_RGMII_ID_MODE];
		ret = regmap_field_write(regfield, rgmii_id);
		if (ret)
			return ret;
	}

	if (soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN) &&
	    if_phy->fields[PHY_GMII_SEL_RMII_IO_CLK_EN]) {
		regfield = if_phy->fields[PHY_GMII_SEL_RMII_IO_CLK_EN];
		ret = regmap_field_write(regfield,
					 if_phy->rmii_clock_external);
	}

	return 0;
}

static const
struct reg_field phy_gmii_sel_fields_am33xx[][PHY_GMII_SEL_LAST] = {
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x650, 0, 1),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD(0x650, 4, 4),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD(0x650, 6, 6),
	},
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x650, 2, 3),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD(0x650, 5, 5),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD(0x650, 7, 7),
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
struct reg_field phy_gmii_sel_fields_dra7[][PHY_GMII_SEL_LAST] = {
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x554, 0, 1),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD((~0), 0, 0),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD((~0), 0, 0),
	},
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x554, 4, 5),
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

static const
struct reg_field phy_gmii_sel_fields_am654[][PHY_GMII_SEL_LAST] = {
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x4040, 0, 1),
		[PHY_GMII_SEL_RGMII_ID_MODE] = REG_FIELD((~0), 0, 0),
		[PHY_GMII_SEL_RMII_IO_CLK_EN] = REG_FIELD((~0), 0, 0),
	},
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_soc_am654 = {
	.num_ports = 1,
	.regfields = phy_gmii_sel_fields_am654,
};

static const struct of_device_id phy_gmii_sel_id_table[] = {
	{
		.compatible	= "ti,am3352-phy-gmii-sel",
		.data		= &phy_gmii_sel_soc_am33xx,
	},
	{
		.compatible	= "ti,dra7xx-phy-gmii-sel",
		.data		= &phy_gmii_sel_soc_dra7,
	},
	{
		.compatible	= "ti,am43xx-phy-gmii-sel",
		.data		= &phy_gmii_sel_soc_am33xx,
	},
	{
		.compatible	= "ti,dm814-phy-gmii-sel",
		.data		= &phy_gmii_sel_soc_dm814,
	},
	{
		.compatible	= "ti,am654-phy-gmii-sel",
		.data		= &phy_gmii_sel_soc_am654,
	},
	{}
};
MODULE_DEVICE_TABLE(of, phy_gmii_sel_id_table);

static const struct phy_ops phy_gmii_sel_ops = {
	.set_mode	= phy_gmii_sel_mode,
	.owner		= THIS_MODULE,
};

static struct phy *phy_gmii_sel_of_xlate(struct device *dev,
					 struct of_phandle_args *args)
{
	struct phy_gmii_sel_priv *priv = dev_get_drvdata(dev);
	int phy_id = args->args[0];

	if (args->args_count < 1)
		return ERR_PTR(-EINVAL);
	if (!priv || !priv->if_phys)
		return ERR_PTR(-ENODEV);
	if (priv->soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN) &&
	    args->args_count < 2)
		return ERR_PTR(-EINVAL);
	if (phy_id > priv->soc_data->num_ports)
		return ERR_PTR(-EINVAL);
	if (phy_id != priv->if_phys[phy_id - 1].id)
		return ERR_PTR(-EINVAL);

	phy_id--;
	if (priv->soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN))
		priv->if_phys[phy_id].rmii_clock_external = args->args[1];
	dev_dbg(dev, "%s id:%u ext:%d\n", __func__,
		priv->if_phys[phy_id].id, args->args[1]);

	return priv->if_phys[phy_id].if_phy;
}

static int phy_gmii_sel_init_ports(struct phy_gmii_sel_priv *priv)
{
	const struct phy_gmii_sel_soc_data *soc_data = priv->soc_data;
	struct device *dev = priv->dev;
	struct phy_gmii_sel_phy_priv *if_phys;
	int i, num_ports, ret;

	num_ports = priv->soc_data->num_ports;

	if_phys = devm_kcalloc(priv->dev, num_ports,
			       sizeof(*if_phys), GFP_KERNEL);
	if (!if_phys)
		return -ENOMEM;
	dev_dbg(dev, "%s %d\n", __func__, num_ports);

	for (i = 0; i < num_ports; i++) {
		const struct reg_field *field;
		struct regmap_field *regfield;

		if_phys[i].id = i + 1;
		if_phys[i].priv = priv;

		field = &soc_data->regfields[i][PHY_GMII_SEL_PORT_MODE];
		dev_dbg(dev, "%s field %x %d %d\n", __func__,
			field->reg, field->msb, field->lsb);

		regfield = devm_regmap_field_alloc(dev, priv->regmap, *field);
		if (IS_ERR(regfield))
			return PTR_ERR(regfield);
		if_phys[i].fields[PHY_GMII_SEL_PORT_MODE] = regfield;

		field = &soc_data->regfields[i][PHY_GMII_SEL_RGMII_ID_MODE];
		if (field->reg != (~0)) {
			regfield = devm_regmap_field_alloc(dev,
							   priv->regmap,
							   *field);
			if (IS_ERR(regfield))
				return PTR_ERR(regfield);
			if_phys[i].fields[PHY_GMII_SEL_RGMII_ID_MODE] =
				regfield;
		}

		field = &soc_data->regfields[i][PHY_GMII_SEL_RMII_IO_CLK_EN];
		if (field->reg != (~0)) {
			regfield = devm_regmap_field_alloc(dev,
							   priv->regmap,
							   *field);
			if (IS_ERR(regfield))
				return PTR_ERR(regfield);
			if_phys[i].fields[PHY_GMII_SEL_RMII_IO_CLK_EN] =
				regfield;
		}

		if_phys[i].if_phy = devm_phy_create(dev,
						    priv->dev->of_node,
						    &phy_gmii_sel_ops);
		if (IS_ERR(if_phys[i].if_phy)) {
			ret = PTR_ERR(if_phys[i].if_phy);
			dev_err(dev, "Failed to create phy%d %d\n", i, ret);
			return ret;
		}
		phy_set_drvdata(if_phys[i].if_phy, &if_phys[i]);
	}

	priv->if_phys = if_phys;
	return 0;
}

static int phy_gmii_sel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct of_device_id *of_id;
	struct phy_gmii_sel_priv *priv;
	int ret;

	of_id = of_match_node(phy_gmii_sel_id_table, pdev->dev.of_node);
	if (!of_id)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->soc_data = of_id->data;

	priv->regmap = syscon_node_to_regmap(node->parent);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to get syscon %d\n", ret);
		return ret;
	}

	ret = phy_gmii_sel_init_ports(priv);
	if (ret)
		return ret;

	dev_set_drvdata(&pdev->dev, priv);

	priv->phy_provider =
		devm_of_phy_provider_register(dev,
					      phy_gmii_sel_of_xlate);
	if (IS_ERR(priv->phy_provider)) {
		ret = PTR_ERR(priv->phy_provider);
		dev_err(dev, "Failed to create phy provider %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver phy_gmii_sel_driver = {
	.probe		= phy_gmii_sel_probe,
	.driver		= {
		.name	= "phy-gmii-sel",
		.of_match_table = phy_gmii_sel_id_table,
	},
};
module_platform_driver(phy_gmii_sel_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Grygorii Strashko <grygorii.strashko@ti.com>");
MODULE_DESCRIPTION("TI CPSW Port's PHY Interface Mode selection Driver");
