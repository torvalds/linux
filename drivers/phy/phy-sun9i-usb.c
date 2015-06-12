/*
 * Allwinner sun9i USB phy driver
 *
 * Copyright (C) 2014-2015 Chen-Yu Tsai <wens@csie.org>
 *
 * Based on phy-sun4i-usb.c from
 * Hans de Goede <hdegoede@redhat.com>
 *
 * and code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/usb/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define SUNXI_AHB_INCR16_BURST_EN	BIT(11)
#define SUNXI_AHB_INCR8_BURST_EN	BIT(10)
#define SUNXI_AHB_INCR4_BURST_EN	BIT(9)
#define SUNXI_AHB_INCRX_ALIGN_EN	BIT(8)
#define SUNXI_ULPI_BYPASS_EN		BIT(0)

/* usb1 HSIC specific bits */
#define SUNXI_EHCI_HS_FORCE		BIT(20)
#define SUNXI_HSIC_CONNECT_DET		BIT(17)
#define SUNXI_HSIC_CONNECT_INT		BIT(16)
#define SUNXI_HSIC			BIT(1)

struct sun9i_usb_phy {
	struct phy *phy;
	void __iomem *pmu;
	struct reset_control *reset;
	struct clk *clk;
	struct clk *hsic_clk;
	enum usb_phy_interface type;
};

static void sun9i_usb_phy_passby(struct sun9i_usb_phy *phy, int enable)
{
	u32 bits, reg_value;

	bits = SUNXI_AHB_INCR16_BURST_EN | SUNXI_AHB_INCR8_BURST_EN |
		SUNXI_AHB_INCR4_BURST_EN | SUNXI_AHB_INCRX_ALIGN_EN |
		SUNXI_ULPI_BYPASS_EN;

	if (phy->type == USBPHY_INTERFACE_MODE_HSIC)
		bits |= SUNXI_HSIC | SUNXI_EHCI_HS_FORCE |
			SUNXI_HSIC_CONNECT_DET | SUNXI_HSIC_CONNECT_INT;

	reg_value = readl(phy->pmu);

	if (enable)
		reg_value |= bits;
	else
		reg_value &= ~bits;

	writel(reg_value, phy->pmu);
}

static int sun9i_usb_phy_init(struct phy *_phy)
{
	struct sun9i_usb_phy *phy = phy_get_drvdata(_phy);
	int ret;

	ret = clk_prepare_enable(phy->clk);
	if (ret)
		goto err_clk;

	ret = clk_prepare_enable(phy->hsic_clk);
	if (ret)
		goto err_hsic_clk;

	ret = reset_control_deassert(phy->reset);
	if (ret)
		goto err_reset;

	sun9i_usb_phy_passby(phy, 1);
	return 0;

err_reset:
	clk_disable_unprepare(phy->hsic_clk);

err_hsic_clk:
	clk_disable_unprepare(phy->clk);

err_clk:
	return ret;
}

static int sun9i_usb_phy_exit(struct phy *_phy)
{
	struct sun9i_usb_phy *phy = phy_get_drvdata(_phy);

	sun9i_usb_phy_passby(phy, 0);
	reset_control_assert(phy->reset);
	clk_disable_unprepare(phy->hsic_clk);
	clk_disable_unprepare(phy->clk);

	return 0;
}

static struct phy_ops sun9i_usb_phy_ops = {
	.init		= sun9i_usb_phy_init,
	.exit		= sun9i_usb_phy_exit,
	.owner		= THIS_MODULE,
};

static int sun9i_usb_phy_probe(struct platform_device *pdev)
{
	struct sun9i_usb_phy *phy;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *phy_provider;
	struct resource *res;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->type = of_usb_get_phy_mode(np);
	if (phy->type == USBPHY_INTERFACE_MODE_HSIC) {
		phy->clk = devm_clk_get(dev, "hsic_480M");
		if (IS_ERR(phy->clk)) {
			dev_err(dev, "failed to get hsic_480M clock\n");
			return PTR_ERR(phy->clk);
		}

		phy->hsic_clk = devm_clk_get(dev, "hsic_12M");
		if (IS_ERR(phy->clk)) {
			dev_err(dev, "failed to get hsic_12M clock\n");
			return PTR_ERR(phy->clk);
		}

		phy->reset = devm_reset_control_get(dev, "hsic");
		if (IS_ERR(phy->reset)) {
			dev_err(dev, "failed to get reset control\n");
			return PTR_ERR(phy->reset);
		}
	} else {
		phy->clk = devm_clk_get(dev, "phy");
		if (IS_ERR(phy->clk)) {
			dev_err(dev, "failed to get phy clock\n");
			return PTR_ERR(phy->clk);
		}

		phy->reset = devm_reset_control_get(dev, "phy");
		if (IS_ERR(phy->reset)) {
			dev_err(dev, "failed to get reset control\n");
			return PTR_ERR(phy->reset);
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy->pmu = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy->pmu))
		return PTR_ERR(phy->pmu);

	phy->phy = devm_phy_create(dev, NULL, &sun9i_usb_phy_ops);
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy->phy);
	}

	phy_set_drvdata(phy->phy, phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id sun9i_usb_phy_of_match[] = {
	{ .compatible = "allwinner,sun9i-a80-usb-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, sun9i_usb_phy_of_match);

static struct platform_driver sun9i_usb_phy_driver = {
	.probe	= sun9i_usb_phy_probe,
	.driver = {
		.of_match_table	= sun9i_usb_phy_of_match,
		.name  = "sun9i-usb-phy",
	}
};
module_platform_driver(sun9i_usb_phy_driver);

MODULE_DESCRIPTION("Allwinner sun9i USB phy driver");
MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_LICENSE("GPL");
