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
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

/* AM33xx SoC specific definitions for the CONTROL port */
#define AM33XX_GMII_SEL_MODE_MII	0
#define AM33XX_GMII_SEL_MODE_RMII	1
#define AM33XX_GMII_SEL_MODE_RGMII	2

/* J72xx SoC specific definitions for the CONTROL port */
#define J72XX_GMII_SEL_MODE_QSGMII	4
#define J72XX_GMII_SEL_MODE_QSGMII_SUB	6

#define PHY_GMII_PORT(n)	BIT((n) - 1)

enum {
	PHY_GMII_SEL_PORT_MODE = 0,
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
	bool use_of_data;
	u64 extra_modes;
	u32 num_qsgmii_main_ports;
};

struct phy_gmii_sel_priv {
	struct device *dev;
	const struct phy_gmii_sel_soc_data *soc_data;
	struct regmap *regmap;
	struct phy_provider *phy_provider;
	struct phy_gmii_sel_phy_priv *if_phys;
	u32 num_ports;
	u32 reg_offset;
	u32 qsgmii_main_ports;
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

	case PHY_INTERFACE_MODE_QSGMII:
		if (!(soc_data->extra_modes & BIT(PHY_INTERFACE_MODE_QSGMII)))
			goto unsupported;
		if (if_phy->priv->qsgmii_main_ports & BIT(if_phy->id - 1))
			gmii_sel_mode = J72XX_GMII_SEL_MODE_QSGMII;
		else
			gmii_sel_mode = J72XX_GMII_SEL_MODE_QSGMII_SUB;
		break;

	default:
		goto unsupported;
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

unsupported:
	dev_warn(dev, "port%u: unsupported mode: \"%s\"\n",
		 if_phy->id, phy_modes(submode));
	return -EINVAL;
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
	},
	{
		[PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x554, 4, 5),
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
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x0, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x4, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x8, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0xC, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x10, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x14, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x18, 0, 2), },
	{ [PHY_GMII_SEL_PORT_MODE] = REG_FIELD(0x1C, 0, 2), },
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_soc_am654 = {
	.use_of_data = true,
	.regfields = phy_gmii_sel_fields_am654,
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_cpsw5g_soc_j7200 = {
	.use_of_data = true,
	.regfields = phy_gmii_sel_fields_am654,
	.extra_modes = BIT(PHY_INTERFACE_MODE_QSGMII),
	.num_ports = 4,
	.num_qsgmii_main_ports = 1,
};

static const
struct phy_gmii_sel_soc_data phy_gmii_sel_cpsw9g_soc_j721e = {
	.use_of_data = true,
	.regfields = phy_gmii_sel_fields_am654,
	.extra_modes = BIT(PHY_INTERFACE_MODE_QSGMII),
	.num_ports = 8,
	.num_qsgmii_main_ports = 2,
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
	{
		.compatible	= "ti,j7200-cpsw5g-phy-gmii-sel",
		.data		= &phy_gmii_sel_cpsw5g_soc_j7200,
	},
	{
		.compatible	= "ti,j721e-cpsw9g-phy-gmii-sel",
		.data		= &phy_gmii_sel_cpsw9g_soc_j721e,
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
	if (phy_id > priv->num_ports)
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

static int phy_gmii_init_phy(struct phy_gmii_sel_priv *priv, int port,
			     struct phy_gmii_sel_phy_priv *if_phy)
{
	const struct phy_gmii_sel_soc_data *soc_data = priv->soc_data;
	struct device *dev = priv->dev;
	const struct reg_field *fields;
	struct regmap_field *regfield;
	struct reg_field field;
	int ret;

	if_phy->id = port;
	if_phy->priv = priv;

	fields = soc_data->regfields[port - 1];
	field = *fields++;
	field.reg += priv->reg_offset;
	dev_dbg(dev, "%s field %x %d %d\n", __func__,
		field.reg, field.msb, field.lsb);

	regfield = devm_regmap_field_alloc(dev, priv->regmap, field);
	if (IS_ERR(regfield))
		return PTR_ERR(regfield);
	if_phy->fields[PHY_GMII_SEL_PORT_MODE] = regfield;

	field = *fields++;
	field.reg += priv->reg_offset;
	if (soc_data->features & BIT(PHY_GMII_SEL_RGMII_ID_MODE)) {
		regfield = devm_regmap_field_alloc(dev,
						   priv->regmap,
						   field);
		if (IS_ERR(regfield))
			return PTR_ERR(regfield);
		if_phy->fields[PHY_GMII_SEL_RGMII_ID_MODE] = regfield;
		dev_dbg(dev, "%s field %x %d %d\n", __func__,
			field.reg, field.msb, field.lsb);
	}

	field = *fields;
	field.reg += priv->reg_offset;
	if (soc_data->features & BIT(PHY_GMII_SEL_RMII_IO_CLK_EN)) {
		regfield = devm_regmap_field_alloc(dev,
						   priv->regmap,
						   field);
		if (IS_ERR(regfield))
			return PTR_ERR(regfield);
		if_phy->fields[PHY_GMII_SEL_RMII_IO_CLK_EN] = regfield;
		dev_dbg(dev, "%s field %x %d %d\n", __func__,
			field.reg, field.msb, field.lsb);
	}

	if_phy->if_phy = devm_phy_create(dev,
					 priv->dev->of_node,
					 &phy_gmii_sel_ops);
	if (IS_ERR(if_phy->if_phy)) {
		ret = PTR_ERR(if_phy->if_phy);
		dev_err(dev, "Failed to create phy%d %d\n", port, ret);
		return ret;
	}
	phy_set_drvdata(if_phy->if_phy, if_phy);

	return 0;
}

static int phy_gmii_sel_init_ports(struct phy_gmii_sel_priv *priv)
{
	const struct phy_gmii_sel_soc_data *soc_data = priv->soc_data;
	struct phy_gmii_sel_phy_priv *if_phys;
	struct device *dev = priv->dev;
	int i, ret;

	if (soc_data->use_of_data) {
		const __be32 *offset;
		u64 size;

		offset = of_get_address(dev->of_node, 0, &size, NULL);
		if (!offset)
			return -EINVAL;
		priv->num_ports = size / sizeof(u32);
		if (!priv->num_ports)
			return -EINVAL;
		priv->reg_offset = __be32_to_cpu(*offset);
	}

	if_phys = devm_kcalloc(dev, priv->num_ports,
			       sizeof(*if_phys), GFP_KERNEL);
	if (!if_phys)
		return -ENOMEM;
	dev_dbg(dev, "%s %d\n", __func__, priv->num_ports);

	for (i = 0; i < priv->num_ports; i++) {
		ret = phy_gmii_init_phy(priv, i + 1, &if_phys[i]);
		if (ret)
			return ret;
	}

	priv->if_phys = if_phys;
	return 0;
}

static int phy_gmii_sel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct phy_gmii_sel_soc_data *soc_data;
	struct device_node *node = dev->of_node;
	const struct of_device_id *of_id;
	struct phy_gmii_sel_priv *priv;
	u32 main_ports = 1;
	int ret;
	u32 i;

	of_id = of_match_node(phy_gmii_sel_id_table, pdev->dev.of_node);
	if (!of_id)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->soc_data = of_id->data;
	soc_data = priv->soc_data;
	priv->num_ports = priv->soc_data->num_ports;
	priv->qsgmii_main_ports = 0;

	/*
	 * Based on the compatible, try to read the appropriate number of
	 * QSGMII main ports from the "ti,qsgmii-main-ports" property from
	 * the device-tree node.
	 */
	for (i = 0; i < soc_data->num_qsgmii_main_ports; i++) {
		of_property_read_u32_index(node, "ti,qsgmii-main-ports", i, &main_ports);
		/*
		 * Ensure that main_ports is within bounds.
		 */
		if (main_ports < 1 || main_ports > soc_data->num_ports) {
			dev_err(dev, "Invalid qsgmii main port provided\n");
			return -EINVAL;
		}
		priv->qsgmii_main_ports |= PHY_GMII_PORT(main_ports);
	}

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
