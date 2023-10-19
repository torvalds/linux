// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner sun50i(H6) USB 3.0 phy driver
 *
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on phy-sun9i-usb.c, which is:
 *
 * Copyright (C) 2014-2015 Chen-Yu Tsai <wens@csie.org>
 *
 * Based on code from Allwinner BSP, which is:
 *
 * Copyright (c) 2010-2015 Allwinner Technology Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* Interface Status and Control Registers */
#define SUNXI_ISCR			0x00
#define SUNXI_PIPE_CLOCK_CONTROL	0x14
#define SUNXI_PHY_TUNE_LOW		0x18
#define SUNXI_PHY_TUNE_HIGH		0x1c
#define SUNXI_PHY_EXTERNAL_CONTROL	0x20

/* USB2.0 Interface Status and Control Register */
#define SUNXI_ISCR_FORCE_VBUS		(3 << 12)

/* PIPE Clock Control Register */
#define SUNXI_PCC_PIPE_CLK_OPEN		(1 << 6)

/* PHY External Control Register */
#define SUNXI_PEC_EXTERN_VBUS		(3 << 1)
#define SUNXI_PEC_SSC_EN		(1 << 24)
#define SUNXI_PEC_REF_SSP_EN		(1 << 26)

/* PHY Tune High Register */
#define SUNXI_TX_DEEMPH_3P5DB(n)	((n) << 19)
#define SUNXI_TX_DEEMPH_3P5DB_MASK	GENMASK(24, 19)
#define SUNXI_TX_DEEMPH_6DB(n)		((n) << 13)
#define SUNXI_TX_DEEMPH_6GB_MASK	GENMASK(18, 13)
#define SUNXI_TX_SWING_FULL(n)		((n) << 6)
#define SUNXI_TX_SWING_FULL_MASK	GENMASK(12, 6)
#define SUNXI_LOS_BIAS(n)		((n) << 3)
#define SUNXI_LOS_BIAS_MASK		GENMASK(5, 3)
#define SUNXI_TXVBOOSTLVL(n)		((n) << 0)
#define SUNXI_TXVBOOSTLVL_MASK		GENMASK(2, 0)

struct sun50i_usb3_phy {
	struct phy *phy;
	void __iomem *regs;
	struct reset_control *reset;
	struct clk *clk;
};

static void sun50i_usb3_phy_open(struct sun50i_usb3_phy *phy)
{
	u32 val;

	val = readl(phy->regs + SUNXI_PHY_EXTERNAL_CONTROL);
	val |= SUNXI_PEC_EXTERN_VBUS;
	val |= SUNXI_PEC_SSC_EN | SUNXI_PEC_REF_SSP_EN;
	writel(val, phy->regs + SUNXI_PHY_EXTERNAL_CONTROL);

	val = readl(phy->regs + SUNXI_PIPE_CLOCK_CONTROL);
	val |= SUNXI_PCC_PIPE_CLK_OPEN;
	writel(val, phy->regs + SUNXI_PIPE_CLOCK_CONTROL);

	val = readl(phy->regs + SUNXI_ISCR);
	val |= SUNXI_ISCR_FORCE_VBUS;
	writel(val, phy->regs + SUNXI_ISCR);

	/*
	 * All the magic numbers written to the PHY_TUNE_{LOW_HIGH}
	 * registers are directly taken from the BSP USB3 driver from
	 * Allwiner.
	 */
	writel(0x0047fc87, phy->regs + SUNXI_PHY_TUNE_LOW);

	val = readl(phy->regs + SUNXI_PHY_TUNE_HIGH);
	val &= ~(SUNXI_TXVBOOSTLVL_MASK | SUNXI_LOS_BIAS_MASK |
		 SUNXI_TX_SWING_FULL_MASK | SUNXI_TX_DEEMPH_6GB_MASK |
		 SUNXI_TX_DEEMPH_3P5DB_MASK);
	val |= SUNXI_TXVBOOSTLVL(0x7);
	val |= SUNXI_LOS_BIAS(0x7);
	val |= SUNXI_TX_SWING_FULL(0x55);
	val |= SUNXI_TX_DEEMPH_6DB(0x20);
	val |= SUNXI_TX_DEEMPH_3P5DB(0x15);
	writel(val, phy->regs + SUNXI_PHY_TUNE_HIGH);
}

static int sun50i_usb3_phy_init(struct phy *_phy)
{
	struct sun50i_usb3_phy *phy = phy_get_drvdata(_phy);
	int ret;

	ret = clk_prepare_enable(phy->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(phy->reset);
	if (ret) {
		clk_disable_unprepare(phy->clk);
		return ret;
	}

	sun50i_usb3_phy_open(phy);
	return 0;
}

static int sun50i_usb3_phy_exit(struct phy *_phy)
{
	struct sun50i_usb3_phy *phy = phy_get_drvdata(_phy);

	reset_control_assert(phy->reset);
	clk_disable_unprepare(phy->clk);

	return 0;
}

static const struct phy_ops sun50i_usb3_phy_ops = {
	.init		= sun50i_usb3_phy_init,
	.exit		= sun50i_usb3_phy_exit,
	.owner		= THIS_MODULE,
};

static int sun50i_usb3_phy_probe(struct platform_device *pdev)
{
	struct sun50i_usb3_phy *phy;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(phy->clk)) {
		if (PTR_ERR(phy->clk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get phy clock\n");
		return PTR_ERR(phy->clk);
	}

	phy->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(phy->reset)) {
		dev_err(dev, "failed to get reset control\n");
		return PTR_ERR(phy->reset);
	}

	phy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->regs))
		return PTR_ERR(phy->regs);

	phy->phy = devm_phy_create(dev, NULL, &sun50i_usb3_phy_ops);
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy->phy);
	}

	phy_set_drvdata(phy->phy, phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id sun50i_usb3_phy_of_match[] = {
	{ .compatible = "allwinner,sun50i-h6-usb3-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, sun50i_usb3_phy_of_match);

static struct platform_driver sun50i_usb3_phy_driver = {
	.probe	= sun50i_usb3_phy_probe,
	.driver = {
		.of_match_table	= sun50i_usb3_phy_of_match,
		.name  = "sun50i-usb3-phy",
	}
};
module_platform_driver(sun50i_usb3_phy_driver);

MODULE_DESCRIPTION("Allwinner H6 USB 3.0 phy driver");
MODULE_AUTHOR("Icenowy Zheng <icenowy@aosc.io>");
MODULE_LICENSE("GPL");
