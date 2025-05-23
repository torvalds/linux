// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 STMicroelectronics
 *
 * STMicroelectronics Generic PHY driver for STiH407 USB2.
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 */
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>

#define PHYPARAM_REG	0
#define PHYCTRL_REG	1

/* Default PHY_SEL and REFCLKSEL configuration */
#define STIH407_USB_PICOPHY_CTRL_PORT_CONF	0x6
#define STIH407_USB_PICOPHY_CTRL_PORT_MASK	0x1f

/* ports parameters overriding */
#define STIH407_USB_PICOPHY_PARAM_DEF		0x39a4dc
#define STIH407_USB_PICOPHY_PARAM_MASK		0xffffffff

struct stih407_usb2_picophy {
	struct phy *phy;
	struct regmap *regmap;
	struct device *dev;
	struct reset_control *rstc;
	struct reset_control *rstport;
	int ctrl;
	int param;
};

static int stih407_usb2_pico_ctrl(struct stih407_usb2_picophy *phy_dev)
{
	reset_control_deassert(phy_dev->rstc);

	return regmap_update_bits(phy_dev->regmap, phy_dev->ctrl,
				  STIH407_USB_PICOPHY_CTRL_PORT_MASK,
				  STIH407_USB_PICOPHY_CTRL_PORT_CONF);
}

static int stih407_usb2_init_port(struct phy *phy)
{
	int ret;
	struct stih407_usb2_picophy *phy_dev = phy_get_drvdata(phy);

	stih407_usb2_pico_ctrl(phy_dev);

	ret = regmap_update_bits(phy_dev->regmap,
				 phy_dev->param,
				 STIH407_USB_PICOPHY_PARAM_MASK,
				 STIH407_USB_PICOPHY_PARAM_DEF);
	if (ret)
		return ret;

	return reset_control_deassert(phy_dev->rstport);
}

static int stih407_usb2_exit_port(struct phy *phy)
{
	struct stih407_usb2_picophy *phy_dev = phy_get_drvdata(phy);

	/*
	 * Only port reset is asserted, phy global reset is kept untouched
	 * as other ports may still be active. When all ports are in reset
	 * state, assumption is made that power will be cut off on the phy, in
	 * case of suspend for instance. Theoretically, asserting individual
	 * reset (like here) or global reset should be equivalent.
	 */
	return reset_control_assert(phy_dev->rstport);
}

static const struct phy_ops stih407_usb2_picophy_data = {
	.init = stih407_usb2_init_port,
	.exit = stih407_usb2_exit_port,
	.owner = THIS_MODULE,
};

static int stih407_usb2_picophy_probe(struct platform_device *pdev)
{
	struct stih407_usb2_picophy *phy_dev;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *phy_provider;
	unsigned int syscon_args[2];
	struct phy *phy;

	phy_dev = devm_kzalloc(dev, sizeof(*phy_dev), GFP_KERNEL);
	if (!phy_dev)
		return -ENOMEM;

	phy_dev->dev = dev;
	dev_set_drvdata(dev, phy_dev);

	phy_dev->rstc = devm_reset_control_get_shared(dev, "global");
	if (IS_ERR(phy_dev->rstc)) {
		dev_err(dev, "failed to ctrl picoPHY reset\n");
		return PTR_ERR(phy_dev->rstc);
	}

	phy_dev->rstport = devm_reset_control_get_exclusive(dev, "port");
	if (IS_ERR(phy_dev->rstport)) {
		dev_err(dev, "failed to ctrl picoPHY reset\n");
		return PTR_ERR(phy_dev->rstport);
	}

	/* Reset port by default: only deassert it in phy init */
	reset_control_assert(phy_dev->rstport);

	phy_dev->regmap = syscon_regmap_lookup_by_phandle_args(np, "st,syscfg",
							       2, syscon_args);
	if (IS_ERR(phy_dev->regmap)) {
		dev_err(dev, "No syscfg phandle specified\n");
		return PTR_ERR(phy_dev->regmap);
	}

	phy_dev->param = syscon_args[PHYPARAM_REG];
	phy_dev->ctrl = syscon_args[PHYCTRL_REG];

	phy = devm_phy_create(dev, NULL, &stih407_usb2_picophy_data);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create Display Port PHY\n");
		return PTR_ERR(phy);
	}

	phy_dev->phy = phy;
	phy_set_drvdata(phy, phy_dev);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static const struct of_device_id stih407_usb2_picophy_of_match[] = {
	{ .compatible = "st,stih407-usb2-phy" },
	{ /*sentinel */ },
};

MODULE_DEVICE_TABLE(of, stih407_usb2_picophy_of_match);

static struct platform_driver stih407_usb2_picophy_driver = {
	.probe = stih407_usb2_picophy_probe,
	.driver = {
		   .name = "stih407-usb-genphy",
		   .of_match_table = stih407_usb2_picophy_of_match,
		   }
};

module_platform_driver(stih407_usb2_picophy_driver);

MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_DESCRIPTION("STMicroelectronics Generic picoPHY driver for STiH407");
MODULE_LICENSE("GPL v2");
