// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * Based on vendor driver:
 * Copyright (C) 2013 Marvell Inc.
 * Author: Chao Xie <xiechao.mail@gmail.com>
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>

/* USB PXA1928 PHY mapping */
#define PHY_28NM_PLL_REG0			0x0
#define PHY_28NM_PLL_REG1			0x4
#define PHY_28NM_CAL_REG			0x8
#define PHY_28NM_TX_REG0			0x0c
#define PHY_28NM_TX_REG1			0x10
#define PHY_28NM_RX_REG0			0x14
#define PHY_28NM_RX_REG1			0x18
#define PHY_28NM_DIG_REG0			0x1c
#define PHY_28NM_DIG_REG1			0x20
#define PHY_28NM_TEST_REG0			0x24
#define PHY_28NM_TEST_REG1			0x28
#define PHY_28NM_MOC_REG			0x2c
#define PHY_28NM_PHY_RESERVE			0x30
#define PHY_28NM_OTG_REG			0x34
#define PHY_28NM_CHRG_DET			0x38
#define PHY_28NM_CTRL_REG0			0xc4
#define PHY_28NM_CTRL_REG1			0xc8
#define PHY_28NM_CTRL_REG2			0xd4
#define PHY_28NM_CTRL_REG3			0xdc

/* PHY_28NM_PLL_REG0 */
#define PHY_28NM_PLL_READY			BIT(31)

#define PHY_28NM_PLL_SELLPFR_SHIFT		28
#define PHY_28NM_PLL_SELLPFR_MASK		(0x3 << 28)

#define PHY_28NM_PLL_FBDIV_SHIFT		16
#define PHY_28NM_PLL_FBDIV_MASK			(0x1ff << 16)

#define PHY_28NM_PLL_ICP_SHIFT			8
#define PHY_28NM_PLL_ICP_MASK			(0x7 << 8)

#define PHY_28NM_PLL_REFDIV_SHIFT		0
#define PHY_28NM_PLL_REFDIV_MASK		0x7f

/* PHY_28NM_PLL_REG1 */
#define PHY_28NM_PLL_PU_BY_REG			BIT(1)

#define PHY_28NM_PLL_PU_PLL			BIT(0)

/* PHY_28NM_CAL_REG */
#define PHY_28NM_PLL_PLLCAL_DONE		BIT(31)

#define PHY_28NM_PLL_IMPCAL_DONE		BIT(23)

#define PHY_28NM_PLL_KVCO_SHIFT			16
#define PHY_28NM_PLL_KVCO_MASK			(0x7 << 16)

#define PHY_28NM_PLL_CAL12_SHIFT		20
#define PHY_28NM_PLL_CAL12_MASK			(0x3 << 20)

#define PHY_28NM_IMPCAL_VTH_SHIFT		8
#define PHY_28NM_IMPCAL_VTH_MASK		(0x7 << 8)

#define PHY_28NM_PLLCAL_START_SHIFT		22
#define PHY_28NM_IMPCAL_START_SHIFT		13

/* PHY_28NM_TX_REG0 */
#define PHY_28NM_TX_PU_BY_REG			BIT(25)

#define PHY_28NM_TX_PU_ANA			BIT(24)

#define PHY_28NM_TX_AMP_SHIFT			20
#define PHY_28NM_TX_AMP_MASK			(0x7 << 20)

/* PHY_28NM_RX_REG0 */
#define PHY_28NM_RX_SQ_THRESH_SHIFT		0
#define PHY_28NM_RX_SQ_THRESH_MASK		(0xf << 0)

/* PHY_28NM_RX_REG1 */
#define PHY_28NM_RX_SQCAL_DONE			BIT(31)

/* PHY_28NM_DIG_REG0 */
#define PHY_28NM_DIG_BITSTAFFING_ERR		BIT(31)
#define PHY_28NM_DIG_SYNC_ERR			BIT(30)

#define PHY_28NM_DIG_SQ_FILT_SHIFT		16
#define PHY_28NM_DIG_SQ_FILT_MASK		(0x7 << 16)

#define PHY_28NM_DIG_SQ_BLK_SHIFT		12
#define PHY_28NM_DIG_SQ_BLK_MASK		(0x7 << 12)

#define PHY_28NM_DIG_SYNC_NUM_SHIFT		0
#define PHY_28NM_DIG_SYNC_NUM_MASK		(0x3 << 0)

#define PHY_28NM_PLL_LOCK_BYPASS		BIT(7)

/* PHY_28NM_OTG_REG */
#define PHY_28NM_OTG_CONTROL_BY_PIN		BIT(5)
#define PHY_28NM_OTG_PU_OTG			BIT(4)

#define PHY_28NM_CHGDTC_ENABLE_SWITCH_DM_SHIFT_28 13
#define PHY_28NM_CHGDTC_ENABLE_SWITCH_DP_SHIFT_28 12
#define PHY_28NM_CHGDTC_VSRC_CHARGE_SHIFT_28	10
#define PHY_28NM_CHGDTC_VDAT_CHARGE_SHIFT_28	8
#define PHY_28NM_CHGDTC_CDP_DM_AUTO_SWITCH_SHIFT_28 7
#define PHY_28NM_CHGDTC_DP_DM_SWAP_SHIFT_28	6
#define PHY_28NM_CHGDTC_PU_CHRG_DTC_SHIFT_28	5
#define PHY_28NM_CHGDTC_PD_EN_SHIFT_28		4
#define PHY_28NM_CHGDTC_DCP_EN_SHIFT_28		3
#define PHY_28NM_CHGDTC_CDP_EN_SHIFT_28		2
#define PHY_28NM_CHGDTC_TESTMON_CHRGDTC_SHIFT_28 0

#define PHY_28NM_CTRL1_CHRG_DTC_OUT_SHIFT_28	4
#define PHY_28NM_CTRL1_VBUSDTC_OUT_SHIFT_28	2

#define PHY_28NM_CTRL3_OVERWRITE		BIT(0)
#define PHY_28NM_CTRL3_VBUS_VALID		BIT(4)
#define PHY_28NM_CTRL3_AVALID			BIT(5)
#define PHY_28NM_CTRL3_BVALID			BIT(6)

struct mv_usb2_phy {
	struct phy		*phy;
	struct platform_device	*pdev;
	void __iomem		*base;
	struct clk		*clk;
};

static int wait_for_reg(void __iomem *reg, u32 mask, u32 ms)
{
	u32 val;

	return readl_poll_timeout(reg, val, ((val & mask) == mask),
				   1000, 1000 * ms);
}

static int mv_usb2_phy_28nm_init(struct phy *phy)
{
	struct mv_usb2_phy *mv_phy = phy_get_drvdata(phy);
	struct platform_device *pdev = mv_phy->pdev;
	void __iomem *base = mv_phy->base;
	u32 reg;
	int ret;

	clk_prepare_enable(mv_phy->clk);

	/* PHY_28NM_PLL_REG0 */
	reg = readl(base + PHY_28NM_PLL_REG0) &
		~(PHY_28NM_PLL_SELLPFR_MASK | PHY_28NM_PLL_FBDIV_MASK
		| PHY_28NM_PLL_ICP_MASK	| PHY_28NM_PLL_REFDIV_MASK);
	writel(reg | (0x1 << PHY_28NM_PLL_SELLPFR_SHIFT
		| 0xf0 << PHY_28NM_PLL_FBDIV_SHIFT
		| 0x3 << PHY_28NM_PLL_ICP_SHIFT
		| 0xd << PHY_28NM_PLL_REFDIV_SHIFT),
		base + PHY_28NM_PLL_REG0);

	/* PHY_28NM_PLL_REG1 */
	reg = readl(base + PHY_28NM_PLL_REG1);
	writel(reg | PHY_28NM_PLL_PU_PLL | PHY_28NM_PLL_PU_BY_REG,
		base + PHY_28NM_PLL_REG1);

	/* PHY_28NM_TX_REG0 */
	reg = readl(base + PHY_28NM_TX_REG0) & ~PHY_28NM_TX_AMP_MASK;
	writel(reg | PHY_28NM_TX_PU_BY_REG | 0x3 << PHY_28NM_TX_AMP_SHIFT |
		PHY_28NM_TX_PU_ANA,
		base + PHY_28NM_TX_REG0);

	/* PHY_28NM_RX_REG0 */
	reg = readl(base + PHY_28NM_RX_REG0) & ~PHY_28NM_RX_SQ_THRESH_MASK;
	writel(reg | 0xa << PHY_28NM_RX_SQ_THRESH_SHIFT,
		base + PHY_28NM_RX_REG0);

	/* PHY_28NM_DIG_REG0 */
	reg = readl(base + PHY_28NM_DIG_REG0) &
		~(PHY_28NM_DIG_BITSTAFFING_ERR | PHY_28NM_DIG_SYNC_ERR |
		PHY_28NM_DIG_SQ_FILT_MASK | PHY_28NM_DIG_SQ_BLK_MASK |
		PHY_28NM_DIG_SYNC_NUM_MASK);
	writel(reg | (0x1 << PHY_28NM_DIG_SYNC_NUM_SHIFT |
		PHY_28NM_PLL_LOCK_BYPASS),
		base + PHY_28NM_DIG_REG0);

	/* PHY_28NM_OTG_REG */
	reg = readl(base + PHY_28NM_OTG_REG) | PHY_28NM_OTG_PU_OTG;
	writel(reg & ~PHY_28NM_OTG_CONTROL_BY_PIN, base + PHY_28NM_OTG_REG);

	/*
	 *  Calibration Timing
	 *		   ____________________________
	 *  CAL START   ___|
	 *			   ____________________
	 *  CAL_DONE    ___________|
	 *		   | 400us |
	 */

	/* Make sure PHY Calibration is ready */
	ret = wait_for_reg(base + PHY_28NM_CAL_REG,
			   PHY_28NM_PLL_PLLCAL_DONE | PHY_28NM_PLL_IMPCAL_DONE,
			   100);
	if (ret) {
		dev_warn(&pdev->dev, "USB PHY PLL calibrate not done after 100mS.");
		goto err_clk;
	}
	ret = wait_for_reg(base + PHY_28NM_RX_REG1,
			   PHY_28NM_RX_SQCAL_DONE, 100);
	if (ret) {
		dev_warn(&pdev->dev, "USB PHY RX SQ calibrate not done after 100mS.");
		goto err_clk;
	}
	/* Make sure PHY PLL is ready */
	ret = wait_for_reg(base + PHY_28NM_PLL_REG0, PHY_28NM_PLL_READY, 100);
	if (ret) {
		dev_warn(&pdev->dev, "PLL_READY not set after 100mS.");
		goto err_clk;
	}

	return 0;
err_clk:
	clk_disable_unprepare(mv_phy->clk);
	return ret;
}

static int mv_usb2_phy_28nm_power_on(struct phy *phy)
{
	struct mv_usb2_phy *mv_phy = phy_get_drvdata(phy);
	void __iomem *base = mv_phy->base;

	writel(readl(base + PHY_28NM_CTRL_REG3) |
		(PHY_28NM_CTRL3_OVERWRITE | PHY_28NM_CTRL3_VBUS_VALID |
		PHY_28NM_CTRL3_AVALID | PHY_28NM_CTRL3_BVALID),
		base + PHY_28NM_CTRL_REG3);

	return 0;
}

static int mv_usb2_phy_28nm_power_off(struct phy *phy)
{
	struct mv_usb2_phy *mv_phy = phy_get_drvdata(phy);
	void __iomem *base = mv_phy->base;

	writel(readl(base + PHY_28NM_CTRL_REG3) |
		~(PHY_28NM_CTRL3_OVERWRITE | PHY_28NM_CTRL3_VBUS_VALID
		| PHY_28NM_CTRL3_AVALID	| PHY_28NM_CTRL3_BVALID),
		base + PHY_28NM_CTRL_REG3);

	return 0;
}

static int mv_usb2_phy_28nm_exit(struct phy *phy)
{
	struct mv_usb2_phy *mv_phy = phy_get_drvdata(phy);
	void __iomem *base = mv_phy->base;
	unsigned int val;

	val = readw(base + PHY_28NM_PLL_REG1);
	val &= ~PHY_28NM_PLL_PU_PLL;
	writew(val, base + PHY_28NM_PLL_REG1);

	/* power down PHY Analog part */
	val = readw(base + PHY_28NM_TX_REG0);
	val &= ~PHY_28NM_TX_PU_ANA;
	writew(val, base + PHY_28NM_TX_REG0);

	/* power down PHY OTG part */
	val = readw(base + PHY_28NM_OTG_REG);
	val &= ~PHY_28NM_OTG_PU_OTG;
	writew(val, base + PHY_28NM_OTG_REG);

	clk_disable_unprepare(mv_phy->clk);
	return 0;
}

static const struct phy_ops usb_ops = {
	.init		= mv_usb2_phy_28nm_init,
	.power_on	= mv_usb2_phy_28nm_power_on,
	.power_off	= mv_usb2_phy_28nm_power_off,
	.exit		= mv_usb2_phy_28nm_exit,
	.owner		= THIS_MODULE,
};

static int mv_usb2_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct mv_usb2_phy *mv_phy;

	mv_phy = devm_kzalloc(&pdev->dev, sizeof(*mv_phy), GFP_KERNEL);
	if (!mv_phy)
		return -ENOMEM;

	mv_phy->pdev = pdev;

	mv_phy->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mv_phy->clk)) {
		dev_err(&pdev->dev, "failed to get clock.\n");
		return PTR_ERR(mv_phy->clk);
	}

	mv_phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mv_phy->base))
		return PTR_ERR(mv_phy->base);

	mv_phy->phy = devm_phy_create(&pdev->dev, pdev->dev.of_node, &usb_ops);
	if (IS_ERR(mv_phy->phy))
		return PTR_ERR(mv_phy->phy);

	phy_set_drvdata(mv_phy->phy, mv_phy);

	phy_provider = devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id mv_usbphy_dt_match[] = {
	{ .compatible = "marvell,pxa1928-usb-phy", },
	{},
};
MODULE_DEVICE_TABLE(of, mv_usbphy_dt_match);

static struct platform_driver mv_usb2_phy_driver = {
	.probe	= mv_usb2_phy_probe,
	.driver = {
		.name   = "mv-usb2-phy",
		.of_match_table = mv_usbphy_dt_match,
	},
};
module_platform_driver(mv_usb2_phy_driver);

MODULE_AUTHOR("Rob Herring <robh@kernel.org>");
MODULE_DESCRIPTION("Marvell USB2 phy driver");
MODULE_LICENSE("GPL v2");
