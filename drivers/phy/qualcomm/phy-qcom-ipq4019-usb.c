// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018 John Crispin <john@phrozen.org>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

struct ipq4019_usb_phy {
	struct device		*dev;
	struct phy		*phy;
	void __iomem		*base;
	struct reset_control	*por_rst;
	struct reset_control	*srif_rst;
};

static int ipq4019_ss_phy_power_off(struct phy *_phy)
{
	struct ipq4019_usb_phy *phy = phy_get_drvdata(_phy);

	reset_control_assert(phy->por_rst);
	msleep(10);

	return 0;
}

static int ipq4019_ss_phy_power_on(struct phy *_phy)
{
	struct ipq4019_usb_phy *phy = phy_get_drvdata(_phy);

	ipq4019_ss_phy_power_off(_phy);

	reset_control_deassert(phy->por_rst);

	return 0;
}

static const struct phy_ops ipq4019_usb_ss_phy_ops = {
	.power_on	= ipq4019_ss_phy_power_on,
	.power_off	= ipq4019_ss_phy_power_off,
};

static int ipq4019_hs_phy_power_off(struct phy *_phy)
{
	struct ipq4019_usb_phy *phy = phy_get_drvdata(_phy);

	reset_control_assert(phy->por_rst);
	msleep(10);

	reset_control_assert(phy->srif_rst);
	msleep(10);

	return 0;
}

static int ipq4019_hs_phy_power_on(struct phy *_phy)
{
	struct ipq4019_usb_phy *phy = phy_get_drvdata(_phy);

	ipq4019_hs_phy_power_off(_phy);

	reset_control_deassert(phy->srif_rst);
	msleep(10);

	reset_control_deassert(phy->por_rst);

	return 0;
}

static const struct phy_ops ipq4019_usb_hs_phy_ops = {
	.power_on	= ipq4019_hs_phy_power_on,
	.power_off	= ipq4019_hs_phy_power_off,
};

static const struct of_device_id ipq4019_usb_phy_of_match[] = {
	{ .compatible = "qcom,usb-hs-ipq4019-phy", .data = &ipq4019_usb_hs_phy_ops},
	{ .compatible = "qcom,usb-ss-ipq4019-phy", .data = &ipq4019_usb_ss_phy_ops},
	{ },
};
MODULE_DEVICE_TABLE(of, ipq4019_usb_phy_of_match);

static int ipq4019_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct ipq4019_usb_phy *phy;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->dev = &pdev->dev;
	phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->base)) {
		dev_err(dev, "failed to remap register memory\n");
		return PTR_ERR(phy->base);
	}

	phy->por_rst = devm_reset_control_get(phy->dev, "por_rst");
	if (IS_ERR(phy->por_rst)) {
		if (PTR_ERR(phy->por_rst) != -EPROBE_DEFER)
			dev_err(dev, "POR reset is missing\n");
		return PTR_ERR(phy->por_rst);
	}

	phy->srif_rst = devm_reset_control_get_optional(phy->dev, "srif_rst");
	if (IS_ERR(phy->srif_rst))
		return PTR_ERR(phy->srif_rst);

	phy->phy = devm_phy_create(dev, NULL, of_device_get_match_data(dev));
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy->phy);
	}
	phy_set_drvdata(phy->phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver ipq4019_usb_phy_driver = {
	.probe	= ipq4019_usb_phy_probe,
	.driver = {
		.of_match_table	= ipq4019_usb_phy_of_match,
		.name  = "ipq4019-usb-phy",
	}
};
module_platform_driver(ipq4019_usb_phy_driver);

MODULE_DESCRIPTION("QCOM/IPQ4019 USB phy driver");
MODULE_AUTHOR("John Crispin <john@phrozen.org>");
MODULE_LICENSE("GPL v2");
