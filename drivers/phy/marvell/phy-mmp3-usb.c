// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 * Copyright (C) 2018,2019 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/soc/mmp/cputype.h>

#define USB2_PLL_REG0		0x4
#define USB2_PLL_REG1		0x8
#define USB2_TX_REG0		0x10
#define USB2_TX_REG1		0x14
#define USB2_TX_REG2		0x18
#define USB2_RX_REG0		0x20
#define USB2_RX_REG1		0x24
#define USB2_RX_REG2		0x28
#define USB2_ANA_REG0		0x30
#define USB2_ANA_REG1		0x34
#define USB2_ANA_REG2		0x38
#define USB2_DIG_REG0		0x3C
#define USB2_DIG_REG1		0x40
#define USB2_DIG_REG2		0x44
#define USB2_DIG_REG3		0x48
#define USB2_TEST_REG0		0x4C
#define USB2_TEST_REG1		0x50
#define USB2_TEST_REG2		0x54
#define USB2_CHARGER_REG0	0x58
#define USB2_OTG_REG0		0x5C
#define USB2_PHY_MON0		0x60
#define USB2_RESETVE_REG0	0x64
#define USB2_ICID_REG0		0x78
#define USB2_ICID_REG1		0x7C

/* USB2_PLL_REG0 */

/* This is for Ax stepping */
#define USB2_PLL_FBDIV_SHIFT_MMP3		0
#define USB2_PLL_FBDIV_MASK_MMP3		(0xFF << 0)

#define USB2_PLL_REFDIV_SHIFT_MMP3		8
#define USB2_PLL_REFDIV_MASK_MMP3		(0xF << 8)

#define USB2_PLL_VDD12_SHIFT_MMP3		12
#define USB2_PLL_VDD18_SHIFT_MMP3		14

/* This is for B0 stepping */
#define USB2_PLL_FBDIV_SHIFT_MMP3_B0		0
#define USB2_PLL_REFDIV_SHIFT_MMP3_B0		9
#define USB2_PLL_VDD18_SHIFT_MMP3_B0		14
#define USB2_PLL_FBDIV_MASK_MMP3_B0		0x01FF
#define USB2_PLL_REFDIV_MASK_MMP3_B0		0x3E00

#define USB2_PLL_CAL12_SHIFT_MMP3		0
#define USB2_PLL_CALI12_MASK_MMP3		(0x3 << 0)

#define USB2_PLL_VCOCAL_START_SHIFT_MMP3	2

#define USB2_PLL_KVCO_SHIFT_MMP3		4
#define USB2_PLL_KVCO_MASK_MMP3			(0x7<<4)

#define USB2_PLL_ICP_SHIFT_MMP3			8
#define USB2_PLL_ICP_MASK_MMP3			(0x7<<8)

#define USB2_PLL_LOCK_BYPASS_SHIFT_MMP3		12

#define USB2_PLL_PU_PLL_SHIFT_MMP3		13
#define USB2_PLL_PU_PLL_MASK			(0x1 << 13)

#define USB2_PLL_READY_MASK_MMP3		(0x1 << 15)

/* USB2_TX_REG0 */
#define USB2_TX_IMPCAL_VTH_SHIFT_MMP3		8
#define USB2_TX_IMPCAL_VTH_MASK_MMP3		(0x7 << 8)

#define USB2_TX_RCAL_START_SHIFT_MMP3		13

/* USB2_TX_REG1 */
#define USB2_TX_CK60_PHSEL_SHIFT_MMP3		0
#define USB2_TX_CK60_PHSEL_MASK_MMP3		(0xf << 0)

#define USB2_TX_AMP_SHIFT_MMP3			4
#define USB2_TX_AMP_MASK_MMP3			(0x7 << 4)

#define USB2_TX_VDD12_SHIFT_MMP3		8
#define USB2_TX_VDD12_MASK_MMP3			(0x3 << 8)

/* USB2_TX_REG2 */
#define USB2_TX_DRV_SLEWRATE_SHIFT		10

/* USB2_RX_REG0 */
#define USB2_RX_SQ_THRESH_SHIFT_MMP3		4
#define USB2_RX_SQ_THRESH_MASK_MMP3		(0xf << 4)

#define USB2_RX_SQ_LENGTH_SHIFT_MMP3		10
#define USB2_RX_SQ_LENGTH_MASK_MMP3		(0x3 << 10)

/* USB2_ANA_REG1*/
#define USB2_ANA_PU_ANA_SHIFT_MMP3		14

/* USB2_OTG_REG0 */
#define USB2_OTG_PU_OTG_SHIFT_MMP3		3

struct mmp3_usb_phy {
	struct phy *phy;
	void __iomem *base;
};

static unsigned int u2o_get(void __iomem *base, unsigned int offset)
{
	return readl_relaxed(base + offset);
}

static void u2o_set(void __iomem *base, unsigned int offset,
		unsigned int value)
{
	u32 reg;

	reg = readl_relaxed(base + offset);
	reg |= value;
	writel_relaxed(reg, base + offset);
	readl_relaxed(base + offset);
}

static void u2o_clear(void __iomem *base, unsigned int offset,
		unsigned int value)
{
	u32 reg;

	reg = readl_relaxed(base + offset);
	reg &= ~value;
	writel_relaxed(reg, base + offset);
	readl_relaxed(base + offset);
}

static int mmp3_usb_phy_init(struct phy *phy)
{
	struct mmp3_usb_phy *mmp3_usb_phy = phy_get_drvdata(phy);
	void __iomem *base = mmp3_usb_phy->base;

	if (cpu_is_mmp3_a0()) {
		u2o_clear(base, USB2_PLL_REG0, (USB2_PLL_FBDIV_MASK_MMP3
			| USB2_PLL_REFDIV_MASK_MMP3));
		u2o_set(base, USB2_PLL_REG0,
			0xd << USB2_PLL_REFDIV_SHIFT_MMP3
			| 0xf0 << USB2_PLL_FBDIV_SHIFT_MMP3);
	} else if (cpu_is_mmp3_b0()) {
		u2o_clear(base, USB2_PLL_REG0, USB2_PLL_REFDIV_MASK_MMP3_B0
			| USB2_PLL_FBDIV_MASK_MMP3_B0);
		u2o_set(base, USB2_PLL_REG0,
			0xd << USB2_PLL_REFDIV_SHIFT_MMP3_B0
			| 0xf0 << USB2_PLL_FBDIV_SHIFT_MMP3_B0);
	} else {
		dev_err(&phy->dev, "unsupported silicon revision\n");
		return -ENODEV;
	}

	u2o_clear(base, USB2_PLL_REG1, USB2_PLL_PU_PLL_MASK
		| USB2_PLL_ICP_MASK_MMP3
		| USB2_PLL_KVCO_MASK_MMP3
		| USB2_PLL_CALI12_MASK_MMP3);
	u2o_set(base, USB2_PLL_REG1, 1 << USB2_PLL_PU_PLL_SHIFT_MMP3
		| 1 << USB2_PLL_LOCK_BYPASS_SHIFT_MMP3
		| 3 << USB2_PLL_ICP_SHIFT_MMP3
		| 3 << USB2_PLL_KVCO_SHIFT_MMP3
		| 3 << USB2_PLL_CAL12_SHIFT_MMP3);

	u2o_clear(base, USB2_TX_REG0, USB2_TX_IMPCAL_VTH_MASK_MMP3);
	u2o_set(base, USB2_TX_REG0, 2 << USB2_TX_IMPCAL_VTH_SHIFT_MMP3);

	u2o_clear(base, USB2_TX_REG1, USB2_TX_VDD12_MASK_MMP3
		| USB2_TX_AMP_MASK_MMP3
		| USB2_TX_CK60_PHSEL_MASK_MMP3);
	u2o_set(base, USB2_TX_REG1, 3 << USB2_TX_VDD12_SHIFT_MMP3
		| 4 << USB2_TX_AMP_SHIFT_MMP3
		| 4 << USB2_TX_CK60_PHSEL_SHIFT_MMP3);

	u2o_clear(base, USB2_TX_REG2, 3 << USB2_TX_DRV_SLEWRATE_SHIFT);
	u2o_set(base, USB2_TX_REG2, 2 << USB2_TX_DRV_SLEWRATE_SHIFT);

	u2o_clear(base, USB2_RX_REG0, USB2_RX_SQ_THRESH_MASK_MMP3);
	u2o_set(base, USB2_RX_REG0, 0xa << USB2_RX_SQ_THRESH_SHIFT_MMP3);

	u2o_set(base, USB2_ANA_REG1, 0x1 << USB2_ANA_PU_ANA_SHIFT_MMP3);

	u2o_set(base, USB2_OTG_REG0, 0x1 << USB2_OTG_PU_OTG_SHIFT_MMP3);

	return 0;
}

static int mmp3_usb_phy_calibrate(struct phy *phy)
{
	struct mmp3_usb_phy *mmp3_usb_phy = phy_get_drvdata(phy);
	void __iomem *base = mmp3_usb_phy->base;
	int loops;

	/*
	 * PLL VCO and TX Impedance Calibration Timing:
	 *
	 *                _____________________________________
	 * PU  __________|
	 *                        _____________________________
	 * VCOCAL START _________|
	 *                                 ___
	 * REG_RCAL_START ________________|   |________|_______
	 *               | 200us | 400us  | 40| 400us  | USB PHY READY
	 */

	udelay(200);
	u2o_set(base, USB2_PLL_REG1, 1 << USB2_PLL_VCOCAL_START_SHIFT_MMP3);
	udelay(400);
	u2o_set(base, USB2_TX_REG0, 1 << USB2_TX_RCAL_START_SHIFT_MMP3);
	udelay(40);
	u2o_clear(base, USB2_TX_REG0, 1 << USB2_TX_RCAL_START_SHIFT_MMP3);
	udelay(400);

	loops = 0;
	while ((u2o_get(base, USB2_PLL_REG1) & USB2_PLL_READY_MASK_MMP3) == 0) {
		mdelay(1);
		loops++;
		if (loops > 100) {
			dev_err(&phy->dev, "PLL_READY not set after 100mS.\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static const struct phy_ops mmp3_usb_phy_ops = {
	.init		= mmp3_usb_phy_init,
	.calibrate	= mmp3_usb_phy_calibrate,
	.owner		= THIS_MODULE,
};

static const struct of_device_id mmp3_usb_phy_of_match[] = {
	{ .compatible = "marvell,mmp3-usb-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, mmp3_usb_phy_of_match);

static int mmp3_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mmp3_usb_phy *mmp3_usb_phy;
	struct phy_provider *provider;

	mmp3_usb_phy = devm_kzalloc(dev, sizeof(*mmp3_usb_phy), GFP_KERNEL);
	if (!mmp3_usb_phy)
		return -ENOMEM;

	mmp3_usb_phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmp3_usb_phy->base)) {
		dev_err(dev, "failed to remap PHY regs\n");
		return PTR_ERR(mmp3_usb_phy->base);
	}

	mmp3_usb_phy->phy = devm_phy_create(dev, NULL, &mmp3_usb_phy_ops);
	if (IS_ERR(mmp3_usb_phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(mmp3_usb_phy->phy);
	}

	phy_set_drvdata(mmp3_usb_phy->phy, mmp3_usb_phy);
	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "failed to register PHY provider\n");
		return PTR_ERR(provider);
	}

	return 0;
}

static struct platform_driver mmp3_usb_phy_driver = {
	.probe		= mmp3_usb_phy_probe,
	.driver		= {
		.name	= "mmp3-usb-phy",
		.of_match_table = mmp3_usb_phy_of_match,
	},
};
module_platform_driver(mmp3_usb_phy_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("Marvell MMP3 USB PHY Driver");
MODULE_LICENSE("GPL v2");
