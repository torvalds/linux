// SPDX-License-Identifier: GPL-2.0+
/*
 * StarFive JH7110 USB 2.0 PHY driver
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 * Author: Minda Chen <minda.chen@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/usb/of.h>

#define USB_125M_CLK_RATE		125000000
#define USB_CLK_MODE_OFF		0x0
#define USB_CLK_MODE_RX_NORMAL_PWR	BIT(1)
#define USB_LS_KEEPALIVE_OFF		0x4
#define USB_LS_KEEPALIVE_ENABLE		BIT(4)

#define USB_PDRSTN_SPLIT		BIT(17)
#define SYSCON_USB_SPLIT_OFFSET		0x18

struct jh7110_usb2_phy {
	struct phy *phy;
	void __iomem *regs;
	struct regmap *sys_syscon;
	struct clk *usb_125m_clk;
	struct clk *app_125m;
	enum phy_mode mode;
};

static void usb2_set_ls_keepalive(struct jh7110_usb2_phy *phy, bool set)
{
	unsigned int val;

	/* Host mode enable the LS speed keep-alive signal */
	val = readl(phy->regs + USB_LS_KEEPALIVE_OFF);
	if (set)
		val |= USB_LS_KEEPALIVE_ENABLE;
	else
		val &= ~USB_LS_KEEPALIVE_ENABLE;

	writel(val, phy->regs + USB_LS_KEEPALIVE_OFF);
}

static int usb2_phy_set_mode(struct phy *_phy,
			     enum phy_mode mode, int submode)
{
	struct jh7110_usb2_phy *phy = phy_get_drvdata(_phy);

	switch (mode) {
	case PHY_MODE_USB_HOST:
	case PHY_MODE_USB_DEVICE:
	case PHY_MODE_USB_OTG:
		break;
	default:
		return -EINVAL;
	}

	if (mode != phy->mode) {
		dev_dbg(&_phy->dev, "Changing phy to %d\n", mode);
		phy->mode = mode;
		usb2_set_ls_keepalive(phy, (mode != PHY_MODE_USB_DEVICE));
	}

	/* Connect usb 2.0 phy mode */
	regmap_update_bits(phy->sys_syscon, SYSCON_USB_SPLIT_OFFSET,
			   USB_PDRSTN_SPLIT, USB_PDRSTN_SPLIT);

	return 0;
}

static int jh7110_usb2_phy_init(struct phy *_phy)
{
	struct jh7110_usb2_phy *phy = phy_get_drvdata(_phy);
	int ret;
	unsigned int val;

	ret = clk_set_rate(phy->usb_125m_clk, USB_125M_CLK_RATE);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->app_125m);
	if (ret)
		return ret;

	val = readl(phy->regs + USB_CLK_MODE_OFF);
	val |= USB_CLK_MODE_RX_NORMAL_PWR;
	writel(val, phy->regs + USB_CLK_MODE_OFF);

	return 0;
}

static int jh7110_usb2_phy_exit(struct phy *_phy)
{
	struct jh7110_usb2_phy *phy = phy_get_drvdata(_phy);

	clk_disable_unprepare(phy->app_125m);

	return 0;
}

static const struct phy_ops jh7110_usb2_phy_ops = {
	.init		= jh7110_usb2_phy_init,
	.exit		= jh7110_usb2_phy_exit,
	.set_mode	= usb2_phy_set_mode,
	.owner		= THIS_MODULE,
};

static int jh7110_usb_phy_probe(struct platform_device *pdev)
{
	struct jh7110_usb2_phy *phy;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->usb_125m_clk = devm_clk_get(dev, "125m");
	if (IS_ERR(phy->usb_125m_clk))
		return dev_err_probe(dev, PTR_ERR(phy->usb_125m_clk),
			"Failed to get 125m clock\n");

	phy->app_125m = devm_clk_get(dev, "app_125m");
	if (IS_ERR(phy->app_125m))
		return dev_err_probe(dev, PTR_ERR(phy->app_125m),
			"Failed to get app 125m clock\n");

	phy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->regs))
		return dev_err_probe(dev, PTR_ERR(phy->regs),
			"Failed to map phy base\n");

	phy->phy = devm_phy_create(dev, NULL, &jh7110_usb2_phy_ops);
	if (IS_ERR(phy->phy))
		return dev_err_probe(dev, PTR_ERR(phy->phy),
			"Failed to create phy\n");

	phy_set_drvdata(phy->phy, phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	phy->sys_syscon =
		syscon_regmap_lookup_by_compatible("starfive,jh7110-sys-syscon");
	if (IS_ERR(phy->sys_syscon))
		return dev_err_probe(dev, PTR_ERR(phy->sys_syscon),
				     "Failed to get sys-syscon\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id jh7110_usb_phy_of_match[] = {
	{ .compatible = "starfive,jh7110-usb-phy" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, jh7110_usb_phy_of_match);

static struct platform_driver jh7110_usb_phy_driver = {
	.probe	= jh7110_usb_phy_probe,
	.driver = {
		.of_match_table	= jh7110_usb_phy_of_match,
		.name  = "jh7110-usb-phy",
	}
};
module_platform_driver(jh7110_usb_phy_driver);

MODULE_DESCRIPTION("StarFive JH7110 USB 2.0 PHY driver");
MODULE_AUTHOR("Minda Chen <minda.chen@starfivetech.com>");
MODULE_LICENSE("GPL");
