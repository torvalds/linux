/*
 * IMG Pistachio USB PHY driver
 *
 * Copyright (C) 2015 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/phy/phy-pistachio-usb.h>

#define USB_PHY_CONTROL1				0x04
#define USB_PHY_CONTROL1_FSEL_SHIFT			2
#define USB_PHY_CONTROL1_FSEL_MASK			0x7

#define USB_PHY_STRAP_CONTROL				0x10
#define USB_PHY_STRAP_CONTROL_REFCLK_SHIFT		4
#define USB_PHY_STRAP_CONTROL_REFCLK_MASK		0x3

#define USB_PHY_STATUS					0x14
#define USB_PHY_STATUS_RX_PHY_CLK			BIT(9)
#define USB_PHY_STATUS_RX_UTMI_CLK			BIT(8)
#define USB_PHY_STATUS_VBUS_FAULT			BIT(7)

struct pistachio_usb_phy {
	struct device *dev;
	struct regmap *cr_top;
	struct clk *phy_clk;
	unsigned int refclk;
};

static const unsigned long fsel_rate_map[] = {
	9600000,
	10000000,
	12000000,
	19200000,
	20000000,
	24000000,
	0,
	50000000,
};

static int pistachio_usb_phy_power_on(struct phy *phy)
{
	struct pistachio_usb_phy *p_phy = phy_get_drvdata(phy);
	unsigned long timeout, rate;
	unsigned int i;
	int ret;

	ret = clk_prepare_enable(p_phy->phy_clk);
	if (ret < 0) {
		dev_err(p_phy->dev, "Failed to enable PHY clock: %d\n", ret);
		return ret;
	}

	regmap_update_bits(p_phy->cr_top, USB_PHY_STRAP_CONTROL,
			   USB_PHY_STRAP_CONTROL_REFCLK_MASK <<
			   USB_PHY_STRAP_CONTROL_REFCLK_SHIFT,
			   p_phy->refclk << USB_PHY_STRAP_CONTROL_REFCLK_SHIFT);

	rate = clk_get_rate(p_phy->phy_clk);
	if (p_phy->refclk == REFCLK_XO_CRYSTAL && rate != 12000000) {
		dev_err(p_phy->dev, "Unsupported rate for XO crystal: %ld\n",
			rate);
		ret = -EINVAL;
		goto disable_clk;
	}

	for (i = 0; i < ARRAY_SIZE(fsel_rate_map); i++) {
		if (rate == fsel_rate_map[i])
			break;
	}
	if (i == ARRAY_SIZE(fsel_rate_map)) {
		dev_err(p_phy->dev, "Unsupported clock rate: %lu\n", rate);
		ret = -EINVAL;
		goto disable_clk;
	}

	regmap_update_bits(p_phy->cr_top, USB_PHY_CONTROL1,
			   USB_PHY_CONTROL1_FSEL_MASK <<
			   USB_PHY_CONTROL1_FSEL_SHIFT,
			   i << USB_PHY_CONTROL1_FSEL_SHIFT);

	timeout = jiffies + msecs_to_jiffies(200);
	while (time_before(jiffies, timeout)) {
		unsigned int val;

		regmap_read(p_phy->cr_top, USB_PHY_STATUS, &val);
		if (val & USB_PHY_STATUS_VBUS_FAULT) {
			dev_err(p_phy->dev, "VBUS fault detected\n");
			ret = -EIO;
			goto disable_clk;
		}
		if ((val & USB_PHY_STATUS_RX_PHY_CLK) &&
		    (val & USB_PHY_STATUS_RX_UTMI_CLK))
			return 0;
		usleep_range(1000, 1500);
	}

	dev_err(p_phy->dev, "Timed out waiting for PHY to power on\n");
	ret = -ETIMEDOUT;

disable_clk:
	clk_disable_unprepare(p_phy->phy_clk);
	return ret;
}

static int pistachio_usb_phy_power_off(struct phy *phy)
{
	struct pistachio_usb_phy *p_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(p_phy->phy_clk);

	return 0;
}

static const struct phy_ops pistachio_usb_phy_ops = {
	.power_on = pistachio_usb_phy_power_on,
	.power_off = pistachio_usb_phy_power_off,
	.owner = THIS_MODULE,
};

static int pistachio_usb_phy_probe(struct platform_device *pdev)
{
	struct pistachio_usb_phy *p_phy;
	struct phy_provider *provider;
	struct phy *phy;
	int ret;

	p_phy = devm_kzalloc(&pdev->dev, sizeof(*p_phy), GFP_KERNEL);
	if (!p_phy)
		return -ENOMEM;
	p_phy->dev = &pdev->dev;
	platform_set_drvdata(pdev, p_phy);

	p_phy->cr_top = syscon_regmap_lookup_by_phandle(p_phy->dev->of_node,
							"img,cr-top");
	if (IS_ERR(p_phy->cr_top)) {
		dev_err(p_phy->dev, "Failed to get CR_TOP registers: %ld\n",
			PTR_ERR(p_phy->cr_top));
		return PTR_ERR(p_phy->cr_top);
	}

	p_phy->phy_clk = devm_clk_get(p_phy->dev, "usb_phy");
	if (IS_ERR(p_phy->phy_clk)) {
		dev_err(p_phy->dev, "Failed to get usb_phy clock: %ld\n",
			PTR_ERR(p_phy->phy_clk));
		return PTR_ERR(p_phy->phy_clk);
	}

	ret = of_property_read_u32(p_phy->dev->of_node, "img,refclk",
				   &p_phy->refclk);
	if (ret < 0) {
		dev_err(p_phy->dev, "No reference clock selector specified\n");
		return ret;
	}

	phy = devm_phy_create(p_phy->dev, NULL, &pistachio_usb_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(p_phy->dev, "Failed to create PHY: %ld\n",
			PTR_ERR(phy));
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, p_phy);

	provider = devm_of_phy_provider_register(p_phy->dev,
						 of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(p_phy->dev, "Failed to register PHY provider: %ld\n",
			PTR_ERR(provider));
		return PTR_ERR(provider);
	}

	return 0;
}

static const struct of_device_id pistachio_usb_phy_of_match[] = {
	{ .compatible = "img,pistachio-usb-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, pistachio_usb_phy_of_match);

static struct platform_driver pistachio_usb_phy_driver = {
	.probe		= pistachio_usb_phy_probe,
	.driver		= {
		.name	= "pistachio-usb-phy",
		.of_match_table = pistachio_usb_phy_of_match,
	},
};
module_platform_driver(pistachio_usb_phy_driver);

MODULE_AUTHOR("Andrew Bresticker <abrestic@chromium.org>");
MODULE_DESCRIPTION("IMG Pistachio USB2.0 PHY driver");
MODULE_LICENSE("GPL v2");
