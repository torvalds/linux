/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 System Reset Controller (SRC) driver
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/regmap.h>
#include <dt-bindings/reset/imx7-reset.h>

struct imx7_src {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
};

enum imx7_src_registers {
	SRC_A7RCR0		= 0x0004,
	SRC_M4RCR		= 0x000c,
	SRC_ERCR		= 0x0014,
	SRC_HSICPHY_RCR		= 0x001c,
	SRC_USBOPHY1_RCR	= 0x0020,
	SRC_USBOPHY2_RCR	= 0x0024,
	SRC_MIPIPHY_RCR		= 0x0028,
	SRC_PCIEPHY_RCR		= 0x002c,
	SRC_DDRC_RCR		= 0x1000,
};

struct imx7_src_signal {
	unsigned int offset, bit;
};

static const struct imx7_src_signal imx7_src_signals[IMX7_RESET_NUM] = {
	[IMX7_RESET_A7_CORE_POR_RESET0] = { SRC_A7RCR0, BIT(0) },
	[IMX7_RESET_A7_CORE_POR_RESET1] = { SRC_A7RCR0, BIT(1) },
	[IMX7_RESET_A7_CORE_RESET0]     = { SRC_A7RCR0, BIT(4) },
	[IMX7_RESET_A7_CORE_RESET1]	= { SRC_A7RCR0, BIT(5) },
	[IMX7_RESET_A7_DBG_RESET0]	= { SRC_A7RCR0, BIT(8) },
	[IMX7_RESET_A7_DBG_RESET1]	= { SRC_A7RCR0, BIT(9) },
	[IMX7_RESET_A7_ETM_RESET0]	= { SRC_A7RCR0, BIT(12) },
	[IMX7_RESET_A7_ETM_RESET1]	= { SRC_A7RCR0, BIT(13) },
	[IMX7_RESET_A7_SOC_DBG_RESET]	= { SRC_A7RCR0, BIT(20) },
	[IMX7_RESET_A7_L2RESET]		= { SRC_A7RCR0, BIT(21) },
	[IMX7_RESET_SW_M4C_RST]		= { SRC_M4RCR, BIT(1) },
	[IMX7_RESET_SW_M4P_RST]		= { SRC_M4RCR, BIT(2) },
	[IMX7_RESET_EIM_RST]		= { SRC_ERCR, BIT(0) },
	[IMX7_RESET_HSICPHY_PORT_RST]	= { SRC_HSICPHY_RCR, BIT(1) },
	[IMX7_RESET_USBPHY1_POR]	= { SRC_USBOPHY1_RCR, BIT(0) },
	[IMX7_RESET_USBPHY1_PORT_RST]	= { SRC_USBOPHY1_RCR, BIT(1) },
	[IMX7_RESET_USBPHY2_POR]	= { SRC_USBOPHY2_RCR, BIT(0) },
	[IMX7_RESET_USBPHY2_PORT_RST]	= { SRC_USBOPHY2_RCR, BIT(1) },
	[IMX7_RESET_MIPI_PHY_MRST]	= { SRC_MIPIPHY_RCR, BIT(1) },
	[IMX7_RESET_MIPI_PHY_SRST]	= { SRC_MIPIPHY_RCR, BIT(2) },
	[IMX7_RESET_PCIEPHY]		= { SRC_PCIEPHY_RCR, BIT(2) | BIT(1) },
	[IMX7_RESET_PCIEPHY_PERST]	= { SRC_PCIEPHY_RCR, BIT(3) },
	[IMX7_RESET_PCIE_CTRL_APPS_EN]	= { SRC_PCIEPHY_RCR, BIT(6) },
	[IMX7_RESET_DDRC_PRST]		= { SRC_DDRC_RCR, BIT(0) },
	[IMX7_RESET_DDRC_CORE_RST]	= { SRC_DDRC_RCR, BIT(1) },
};

static struct imx7_src *to_imx7_src(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct imx7_src, rcdev);
}

static int imx7_reset_set(struct reset_controller_dev *rcdev,
			  unsigned long id, bool assert)
{
	struct imx7_src *imx7src = to_imx7_src(rcdev);
	const struct imx7_src_signal *signal = &imx7_src_signals[id];
	unsigned int value = assert ? signal->bit : 0;

	switch (id) {
	case IMX7_RESET_PCIEPHY:
		/*
		 * wait for more than 10us to release phy g_rst and
		 * btnrst
		 */
		if (!assert)
			udelay(10);
		break;

	case IMX7_RESET_PCIE_CTRL_APPS_EN:
		value = (assert) ? 0 : signal->bit;
		break;
	}

	return regmap_update_bits(imx7src->regmap,
				  signal->offset, signal->bit, value);
}

static int imx7_reset_assert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	return imx7_reset_set(rcdev, id, true);
}

static int imx7_reset_deassert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return imx7_reset_set(rcdev, id, false);
}

static const struct reset_control_ops imx7_reset_ops = {
	.assert		= imx7_reset_assert,
	.deassert	= imx7_reset_deassert,
};

static int imx7_reset_probe(struct platform_device *pdev)
{
	struct imx7_src *imx7src;
	struct device *dev = &pdev->dev;
	struct regmap_config config = { .name = "src" };

	imx7src = devm_kzalloc(dev, sizeof(*imx7src), GFP_KERNEL);
	if (!imx7src)
		return -ENOMEM;

	imx7src->regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(imx7src->regmap)) {
		dev_err(dev, "Unable to get imx7-src regmap");
		return PTR_ERR(imx7src->regmap);
	}
	regmap_attach_dev(dev, imx7src->regmap, &config);

	imx7src->rcdev.owner     = THIS_MODULE;
	imx7src->rcdev.nr_resets = IMX7_RESET_NUM;
	imx7src->rcdev.ops       = &imx7_reset_ops;
	imx7src->rcdev.of_node   = dev->of_node;

	return devm_reset_controller_register(dev, &imx7src->rcdev);
}

static const struct of_device_id imx7_reset_dt_ids[] = {
	{ .compatible = "fsl,imx7d-src", },
	{ /* sentinel */ },
};

static struct platform_driver imx7_reset_driver = {
	.probe	= imx7_reset_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= imx7_reset_dt_ids,
	},
};
builtin_platform_driver(imx7_reset_driver);
