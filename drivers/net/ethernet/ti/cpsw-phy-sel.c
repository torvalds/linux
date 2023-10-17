// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * Module Author: Mugunthan V N <mugunthanvnm@ti.com>
 *
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/of.h>

#include "cpsw.h"

/* AM33xx SoC specific definitions for the CONTROL port */
#define AM33XX_GMII_SEL_MODE_MII	0
#define AM33XX_GMII_SEL_MODE_RMII	1
#define AM33XX_GMII_SEL_MODE_RGMII	2

#define AM33XX_GMII_SEL_RMII2_IO_CLK_EN	BIT(7)
#define AM33XX_GMII_SEL_RMII1_IO_CLK_EN	BIT(6)
#define AM33XX_GMII_SEL_RGMII2_IDMODE	BIT(5)
#define AM33XX_GMII_SEL_RGMII1_IDMODE	BIT(4)

#define GMII_SEL_MODE_MASK		0x3

struct cpsw_phy_sel_priv {
	struct device	*dev;
	u32 __iomem	*gmii_sel;
	bool		rmii_clock_external;
	void (*cpsw_phy_sel)(struct cpsw_phy_sel_priv *priv,
			     phy_interface_t phy_mode, int slave);
};


static void cpsw_gmii_sel_am3352(struct cpsw_phy_sel_priv *priv,
				 phy_interface_t phy_mode, int slave)
{
	u32 reg;
	u32 mask;
	u32 mode = 0;
	bool rgmii_id = false;

	reg = readl(priv->gmii_sel);

	switch (phy_mode) {
	case PHY_INTERFACE_MODE_RMII:
		mode = AM33XX_GMII_SEL_MODE_RMII;
		break;

	case PHY_INTERFACE_MODE_RGMII:
		mode = AM33XX_GMII_SEL_MODE_RGMII;
		break;

	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		mode = AM33XX_GMII_SEL_MODE_RGMII;
		rgmii_id = true;
		break;

	default:
		dev_warn(priv->dev,
			 "Unsupported PHY mode: \"%s\". Defaulting to MII.\n",
			phy_modes(phy_mode));
		fallthrough;
	case PHY_INTERFACE_MODE_MII:
		mode = AM33XX_GMII_SEL_MODE_MII;
		break;
	}

	mask = GMII_SEL_MODE_MASK << (slave * 2) | BIT(slave + 6);
	mask |= BIT(slave + 4);
	mode <<= slave * 2;

	if (priv->rmii_clock_external) {
		if (slave == 0)
			mode |= AM33XX_GMII_SEL_RMII1_IO_CLK_EN;
		else
			mode |= AM33XX_GMII_SEL_RMII2_IO_CLK_EN;
	}

	if (rgmii_id) {
		if (slave == 0)
			mode |= AM33XX_GMII_SEL_RGMII1_IDMODE;
		else
			mode |= AM33XX_GMII_SEL_RGMII2_IDMODE;
	}

	reg &= ~mask;
	reg |= mode;

	writel(reg, priv->gmii_sel);
}

static void cpsw_gmii_sel_dra7xx(struct cpsw_phy_sel_priv *priv,
				 phy_interface_t phy_mode, int slave)
{
	u32 reg;
	u32 mask;
	u32 mode = 0;

	reg = readl(priv->gmii_sel);

	switch (phy_mode) {
	case PHY_INTERFACE_MODE_RMII:
		mode = AM33XX_GMII_SEL_MODE_RMII;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		mode = AM33XX_GMII_SEL_MODE_RGMII;
		break;

	default:
		dev_warn(priv->dev,
			 "Unsupported PHY mode: \"%s\". Defaulting to MII.\n",
			phy_modes(phy_mode));
		fallthrough;
	case PHY_INTERFACE_MODE_MII:
		mode = AM33XX_GMII_SEL_MODE_MII;
		break;
	}

	switch (slave) {
	case 0:
		mask = GMII_SEL_MODE_MASK;
		break;
	case 1:
		mask = GMII_SEL_MODE_MASK << 4;
		mode <<= 4;
		break;
	default:
		dev_err(priv->dev, "invalid slave number...\n");
		return;
	}

	if (priv->rmii_clock_external)
		dev_err(priv->dev, "RMII External clock is not supported\n");

	reg &= ~mask;
	reg |= mode;

	writel(reg, priv->gmii_sel);
}

static struct platform_driver cpsw_phy_sel_driver;
static int match(struct device *dev, const void *data)
{
	const struct device_node *node = (const struct device_node *)data;
	return dev->of_node == node &&
		dev->driver == &cpsw_phy_sel_driver.driver;
}

void cpsw_phy_sel(struct device *dev, phy_interface_t phy_mode, int slave)
{
	struct device_node *node;
	struct cpsw_phy_sel_priv *priv;

	node = of_parse_phandle(dev->of_node, "cpsw-phy-sel", 0);
	if (!node) {
		node = of_get_child_by_name(dev->of_node, "cpsw-phy-sel");
		if (!node) {
			dev_err(dev, "Phy mode driver DT not found\n");
			return;
		}
	}

	dev = bus_find_device(&platform_bus_type, NULL, node, match);
	if (!dev) {
		dev_err(dev, "unable to find platform device for %pOF\n", node);
		goto out;
	}

	priv = dev_get_drvdata(dev);

	priv->cpsw_phy_sel(priv, phy_mode, slave);

	put_device(dev);
out:
	of_node_put(node);
}
EXPORT_SYMBOL_GPL(cpsw_phy_sel);

static const struct of_device_id cpsw_phy_sel_id_table[] = {
	{
		.compatible	= "ti,am3352-cpsw-phy-sel",
		.data		= &cpsw_gmii_sel_am3352,
	},
	{
		.compatible	= "ti,dra7xx-cpsw-phy-sel",
		.data		= &cpsw_gmii_sel_dra7xx,
	},
	{
		.compatible	= "ti,am43xx-cpsw-phy-sel",
		.data		= &cpsw_gmii_sel_am3352,
	},
	{}
};

static int cpsw_phy_sel_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct cpsw_phy_sel_priv *priv;

	of_id = of_match_node(cpsw_phy_sel_id_table, pdev->dev.of_node);
	if (!of_id)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to alloc memory for cpsw phy sel\n");
		return -ENOMEM;
	}

	priv->dev = &pdev->dev;
	priv->cpsw_phy_sel = of_id->data;

	priv->gmii_sel = devm_platform_ioremap_resource_byname(pdev, "gmii-sel");
	if (IS_ERR(priv->gmii_sel))
		return PTR_ERR(priv->gmii_sel);

	priv->rmii_clock_external = of_property_read_bool(pdev->dev.of_node, "rmii-clock-ext");

	dev_set_drvdata(&pdev->dev, priv);

	return 0;
}

static struct platform_driver cpsw_phy_sel_driver = {
	.probe		= cpsw_phy_sel_probe,
	.driver		= {
		.name	= "cpsw-phy-sel",
		.of_match_table = cpsw_phy_sel_id_table,
	},
};
builtin_platform_driver(cpsw_phy_sel_driver);
