// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 NXP
 * Copyright 2022 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define PHY_REG_00		0x00
#define PHY_REG_01		0x04
#define PHY_REG_02		0x08
#define PHY_REG_08		0x20
#define PHY_REG_09		0x24
#define PHY_REG_10		0x28
#define PHY_REG_11		0x2c

#define PHY_REG_12		0x30
#define  REG12_CK_DIV_MASK	GENMASK(5, 4)

#define PHY_REG_13		0x34
#define  REG13_TG_CODE_LOW_MASK	GENMASK(7, 0)

#define PHY_REG_14		0x38
#define  REG14_TOL_MASK		GENMASK(7, 4)
#define  REG14_RP_CODE_MASK	GENMASK(3, 1)
#define  REG14_TG_CODE_HIGH_MASK	GENMASK(0, 0)

#define PHY_REG_15		0x3c
#define PHY_REG_16		0x40
#define PHY_REG_17		0x44
#define PHY_REG_18		0x48
#define PHY_REG_19		0x4c
#define PHY_REG_20		0x50

#define PHY_REG_21		0x54
#define  REG21_SEL_TX_CK_INV	BIT(7)
#define  REG21_PMS_S_MASK	GENMASK(3, 0)

#define PHY_REG_22		0x58
#define PHY_REG_23		0x5c
#define PHY_REG_24		0x60
#define PHY_REG_25		0x64
#define PHY_REG_26		0x68
#define PHY_REG_27		0x6c
#define PHY_REG_28		0x70
#define PHY_REG_29		0x74
#define PHY_REG_30		0x78
#define PHY_REG_31		0x7c
#define PHY_REG_32		0x80

/*
 * REG33 does not match the ref manual. According to Sandor Yu from NXP,
 * "There is a doc issue on the i.MX8MP latest RM"
 * REG33 is being used per guidance from Sandor
 */

#define PHY_REG_33		0x84
#define  REG33_MODE_SET_DONE	BIT(7)
#define  REG33_FIX_DA		BIT(1)

#define PHY_REG_34		0x88
#define  REG34_PHY_READY	BIT(7)
#define  REG34_PLL_LOCK		BIT(6)
#define  REG34_PHY_CLK_READY	BIT(5)

#define PHY_REG_35		0x8c
#define PHY_REG_36		0x90
#define PHY_REG_37		0x94
#define PHY_REG_38		0x98
#define PHY_REG_39		0x9c
#define PHY_REG_40		0xa0
#define PHY_REG_41		0xa4
#define PHY_REG_42		0xa8
#define PHY_REG_43		0xac
#define PHY_REG_44		0xb0
#define PHY_REG_45		0xb4
#define PHY_REG_46		0xb8
#define PHY_REG_47		0xbc

#define PHY_PLL_DIV_REGS_NUM 6

struct phy_config {
	u32	pixclk;
	u8	pll_div_regs[PHY_PLL_DIV_REGS_NUM];
};

static const struct phy_config phy_pll_cfg[] = {
	{
		.pixclk = 22250000,
		.pll_div_regs = { 0x4b, 0xf1, 0x89, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 23750000,
		.pll_div_regs = { 0x50, 0xf1, 0x86, 0x85, 0x80, 0x40 },
	}, {
		.pixclk = 24000000,
		.pll_div_regs = { 0x50, 0xf0, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 24024000,
		.pll_div_regs = { 0x50, 0xf1, 0x99, 0x02, 0x80, 0x40 },
	}, {
		.pixclk = 25175000,
		.pll_div_regs = { 0x54, 0xfc, 0xcc, 0x91, 0x80, 0x40 },
	}, {
		.pixclk = 25200000,
		.pll_div_regs = { 0x54, 0xf0, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 26750000,
		.pll_div_regs = { 0x5a, 0xf2, 0x89, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 27000000,
		.pll_div_regs = { 0x5a, 0xf0, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 27027000,
		.pll_div_regs = { 0x5a, 0xf2, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 29500000,
		.pll_div_regs = { 0x62, 0xf4, 0x95, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 30750000,
		.pll_div_regs = { 0x66, 0xf4, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 30888000,
		.pll_div_regs = { 0x66, 0xf4, 0x99, 0x18, 0x88, 0x45 },
	}, {
		.pixclk = 33750000,
		.pll_div_regs = { 0x70, 0xf4, 0x82, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 35000000,
		.pll_div_regs = { 0x58, 0xb8, 0x8b, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 36000000,
		.pll_div_regs = { 0x5a, 0xb0, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 36036000,
		.pll_div_regs = { 0x5a, 0xb2, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 40000000,
		.pll_div_regs = { 0x64, 0xb0, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 43200000,
		.pll_div_regs = { 0x5a, 0x90, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 43243200,
		.pll_div_regs = { 0x5a, 0x92, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 44500000,
		.pll_div_regs = { 0x5c, 0x92, 0x98, 0x11, 0x84, 0x41 },
	}, {
		.pixclk = 47000000,
		.pll_div_regs = { 0x62, 0x94, 0x95, 0x82, 0x80, 0x40 },
	}, {
		.pixclk = 47500000,
		.pll_div_regs = { 0x63, 0x96, 0xa1, 0x82, 0x80, 0x40 },
	}, {
		.pixclk = 50349650,
		.pll_div_regs = { 0x54, 0x7c, 0xc3, 0x8f, 0x80, 0x40 },
	}, {
		.pixclk = 50400000,
		.pll_div_regs = { 0x54, 0x70, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 53250000,
		.pll_div_regs = { 0x58, 0x72, 0x84, 0x03, 0x82, 0x41 },
	}, {
		.pixclk = 53500000,
		.pll_div_regs = { 0x5a, 0x72, 0x89, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 54000000,
		.pll_div_regs = { 0x5a, 0x70, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 54054000,
		.pll_div_regs = { 0x5a, 0x72, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 59000000,
		.pll_div_regs = { 0x62, 0x74, 0x95, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 59340659,
		.pll_div_regs = { 0x62, 0x74, 0xdb, 0x52, 0x88, 0x47 },
	}, {
		.pixclk = 59400000,
		.pll_div_regs = { 0x63, 0x70, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 61500000,
		.pll_div_regs = { 0x66, 0x74, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 63500000,
		.pll_div_regs = { 0x69, 0x74, 0x89, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 67500000,
		.pll_div_regs = { 0x54, 0x52, 0x87, 0x03, 0x80, 0x40 },
	}, {
		.pixclk = 70000000,
		.pll_div_regs = { 0x58, 0x58, 0x8b, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 72000000,
		.pll_div_regs = { 0x5a, 0x50, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 72072000,
		.pll_div_regs = { 0x5a, 0x52, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 74176000,
		.pll_div_regs = { 0x5d, 0x58, 0xdb, 0xA2, 0x88, 0x41 },
	}, {
		.pixclk = 74250000,
		.pll_div_regs = { 0x5c, 0x52, 0x90, 0x0d, 0x84, 0x41 },
	}, {
		.pixclk = 78500000,
		.pll_div_regs = { 0x62, 0x54, 0x87, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 80000000,
		.pll_div_regs = { 0x64, 0x50, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 82000000,
		.pll_div_regs = { 0x66, 0x54, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 82500000,
		.pll_div_regs = { 0x67, 0x54, 0x88, 0x01, 0x90, 0x49 },
	}, {
		.pixclk = 89000000,
		.pll_div_regs = { 0x70, 0x54, 0x84, 0x83, 0x80, 0x40 },
	}, {
		.pixclk = 90000000,
		.pll_div_regs = { 0x70, 0x54, 0x82, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 94000000,
		.pll_div_regs = { 0x4e, 0x32, 0xa7, 0x10, 0x80, 0x40 },
	}, {
		.pixclk = 95000000,
		.pll_div_regs = { 0x50, 0x31, 0x86, 0x85, 0x80, 0x40 },
	}, {
		.pixclk = 98901099,
		.pll_div_regs = { 0x52, 0x3a, 0xdb, 0x4c, 0x88, 0x47 },
	}, {
		.pixclk = 99000000,
		.pll_div_regs = { 0x52, 0x32, 0x82, 0x01, 0x88, 0x47 },
	}, {
		.pixclk = 100699300,
		.pll_div_regs = { 0x54, 0x3c, 0xc3, 0x8f, 0x80, 0x40 },
	}, {
		.pixclk = 100800000,
		.pll_div_regs = { 0x54, 0x30, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 102500000,
		.pll_div_regs = { 0x55, 0x32, 0x8c, 0x05, 0x90, 0x4b },
	}, {
		.pixclk = 104750000,
		.pll_div_regs = { 0x57, 0x32, 0x98, 0x07, 0x90, 0x49 },
	}, {
		.pixclk = 106500000,
		.pll_div_regs = { 0x58, 0x32, 0x84, 0x03, 0x82, 0x41 },
	}, {
		.pixclk = 107000000,
		.pll_div_regs = { 0x5a, 0x32, 0x89, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 108000000,
		.pll_div_regs = { 0x5a, 0x30, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 108108000,
		.pll_div_regs = { 0x5a, 0x32, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 118000000,
		.pll_div_regs = { 0x62, 0x34, 0x95, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 118800000,
		.pll_div_regs = { 0x63, 0x30, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 123000000,
		.pll_div_regs = { 0x66, 0x34, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 127000000,
		.pll_div_regs = { 0x69, 0x34, 0x89, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 135000000,
		.pll_div_regs = { 0x70, 0x34, 0x82, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 135580000,
		.pll_div_regs = { 0x71, 0x39, 0xe9, 0x82, 0x9c, 0x5b },
	}, {
		.pixclk = 137520000,
		.pll_div_regs = { 0x72, 0x38, 0x99, 0x10, 0x85, 0x41 },
	}, {
		.pixclk = 138750000,
		.pll_div_regs = { 0x73, 0x35, 0x88, 0x05, 0x90, 0x4d },
	}, {
		.pixclk = 140000000,
		.pll_div_regs = { 0x75, 0x36, 0xa7, 0x90, 0x80, 0x40 },
	}, {
		.pixclk = 144000000,
		.pll_div_regs = { 0x78, 0x30, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 148352000,
		.pll_div_regs = { 0x7b, 0x35, 0xdb, 0x39, 0x90, 0x45 },
	}, {
		.pixclk = 148500000,
		.pll_div_regs = { 0x7b, 0x35, 0x84, 0x03, 0x90, 0x45 },
	}, {
		.pixclk = 154000000,
		.pll_div_regs = { 0x40, 0x18, 0x83, 0x01, 0x00, 0x40 },
	}, {
		.pixclk = 157000000,
		.pll_div_regs = { 0x41, 0x11, 0xa7, 0x14, 0x80, 0x40 },
	}, {
		.pixclk = 160000000,
		.pll_div_regs = { 0x42, 0x12, 0xa1, 0x20, 0x80, 0x40 },
	}, {
		.pixclk = 162000000,
		.pll_div_regs = { 0x43, 0x18, 0x8b, 0x08, 0x96, 0x55 },
	}, {
		.pixclk = 164000000,
		.pll_div_regs = { 0x45, 0x11, 0x83, 0x82, 0x90, 0x4b },
	}, {
		.pixclk = 165000000,
		.pll_div_regs = { 0x45, 0x11, 0x84, 0x81, 0x90, 0x4b },
	}, {
		.pixclk = 180000000,
		.pll_div_regs = { 0x4b, 0x10, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 185625000,
		.pll_div_regs = { 0x4e, 0x12, 0x9a, 0x95, 0x80, 0x40 },
	}, {
		.pixclk = 188000000,
		.pll_div_regs = { 0x4e, 0x12, 0xa7, 0x10, 0x80, 0x40 },
	}, {
		.pixclk = 198000000,
		.pll_div_regs = { 0x52, 0x12, 0x82, 0x01, 0x88, 0x47 },
	}, {
		.pixclk = 205000000,
		.pll_div_regs = { 0x55, 0x12, 0x8c, 0x05, 0x90, 0x4b },
	}, {
		.pixclk = 209500000,
		.pll_div_regs = { 0x57, 0x12, 0x98, 0x07, 0x90, 0x49 },
	}, {
		.pixclk = 213000000,
		.pll_div_regs = { 0x58, 0x12, 0x84, 0x03, 0x82, 0x41 },
	}, {
		.pixclk = 216000000,
		.pll_div_regs = { 0x5a, 0x10, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 216216000,
		.pll_div_regs = { 0x5a, 0x12, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 237600000,
		.pll_div_regs = { 0x63, 0x10, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 254000000,
		.pll_div_regs = { 0x69, 0x14, 0x89, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 277500000,
		.pll_div_regs = { 0x73, 0x15, 0x88, 0x05, 0x90, 0x4d },
	}, {
		.pixclk = 288000000,
		.pll_div_regs = { 0x78, 0x10, 0x00, 0x00, 0x80, 0x00 },
	}, {
		.pixclk = 297000000,
		.pll_div_regs = { 0x7b, 0x15, 0x84, 0x03, 0x90, 0x45 },
	},
};

struct reg_settings {
	u8 reg;
	u8 val;
};

static const struct reg_settings common_phy_cfg[] = {
	{ PHY_REG_00, 0x00 }, { PHY_REG_01, 0xd1 },
	{ PHY_REG_08, 0x4f }, { PHY_REG_09, 0x30 },
	{ PHY_REG_10, 0x33 }, { PHY_REG_11, 0x65 },
	/* REG12 pixclk specific */
	/* REG13 pixclk specific */
	/* REG14 pixclk specific */
	{ PHY_REG_15, 0x80 }, { PHY_REG_16, 0x6c },
	{ PHY_REG_17, 0xf2 }, { PHY_REG_18, 0x67 },
	{ PHY_REG_19, 0x00 }, { PHY_REG_20, 0x10 },
	/* REG21 pixclk specific */
	{ PHY_REG_22, 0x30 }, { PHY_REG_23, 0x32 },
	{ PHY_REG_24, 0x60 }, { PHY_REG_25, 0x8f },
	{ PHY_REG_26, 0x00 }, { PHY_REG_27, 0x00 },
	{ PHY_REG_28, 0x08 }, { PHY_REG_29, 0x00 },
	{ PHY_REG_30, 0x00 }, { PHY_REG_31, 0x00 },
	{ PHY_REG_32, 0x00 }, { PHY_REG_33, 0x80 },
	{ PHY_REG_34, 0x00 }, { PHY_REG_35, 0x00 },
	{ PHY_REG_36, 0x00 }, { PHY_REG_37, 0x00 },
	{ PHY_REG_38, 0x00 }, { PHY_REG_39, 0x00 },
	{ PHY_REG_40, 0x00 }, { PHY_REG_41, 0xe0 },
	{ PHY_REG_42, 0x83 }, { PHY_REG_43, 0x0f },
	{ PHY_REG_44, 0x3E }, { PHY_REG_45, 0xf8 },
	{ PHY_REG_46, 0x00 }, { PHY_REG_47, 0x00 }
};

struct fsl_samsung_hdmi_phy {
	struct device *dev;
	void __iomem *regs;
	struct clk *apbclk;
	struct clk *refclk;

	/* clk provider */
	struct clk_hw hw;
	const struct phy_config *cur_cfg;
};

static inline struct fsl_samsung_hdmi_phy *
to_fsl_samsung_hdmi_phy(struct clk_hw *hw)
{
	return container_of(hw, struct fsl_samsung_hdmi_phy, hw);
}

static void
fsl_samsung_hdmi_phy_configure_pixclk(struct fsl_samsung_hdmi_phy *phy,
				      const struct phy_config *cfg)
{
	u8 div = 0x1;

	switch (cfg->pixclk) {
	case  22250000 ...  33750000:
		div = 0xf;
		break;
	case  35000000 ...  40000000:
		div = 0xb;
		break;
	case  43200000 ...  47500000:
		div = 0x9;
		break;
	case  50349650 ...  63500000:
		div = 0x7;
		break;
	case  67500000 ...  90000000:
		div = 0x5;
		break;
	case  94000000 ... 148500000:
		div = 0x3;
		break;
	case 154000000 ... 297000000:
		div = 0x1;
		break;
	}

	writeb(REG21_SEL_TX_CK_INV | FIELD_PREP(REG21_PMS_S_MASK, div),
	       phy->regs + PHY_REG_21);
}

static void
fsl_samsung_hdmi_phy_configure_pll_lock_det(struct fsl_samsung_hdmi_phy *phy,
					    const struct phy_config *cfg)
{
	u32 pclk = cfg->pixclk;
	u32 fld_tg_code;
	u32 pclk_khz;
	u8 div = 1;

	switch (cfg->pixclk) {
	case  22250000 ...  47500000:
		div = 1;
		break;
	case  50349650 ...  99000000:
		div = 2;
		break;
	case 100699300 ... 198000000:
		div = 4;
		break;
	case 205000000 ... 297000000:
		div = 8;
		break;
	}

	writeb(FIELD_PREP(REG12_CK_DIV_MASK, ilog2(div)), phy->regs + PHY_REG_12);

	/*
	 * Calculation for the frequency lock detector target code (fld_tg_code)
	 * is based on reference manual register description of PHY_REG13
	 * (13.10.3.1.14.2):
	 *   1st) Calculate int_pllclk which is determinded by FLD_CK_DIV
	 *   2nd) Increase resolution to avoid rounding issues
	 *   3th) Do the div (256 / Freq. of int_pllclk) * 24
	 *   4th) Reduce the resolution and always round up since the NXP
	 *        settings rounding up always too. TODO: Check if that is
	 *        correct.
	 */
	pclk /= div;
	pclk_khz = pclk / 1000;
	fld_tg_code = 256 * 1000 * 1000 / pclk_khz * 24;
	fld_tg_code = DIV_ROUND_UP(fld_tg_code, 1000);

	/* FLD_TOL and FLD_RP_CODE taken from downstream driver */
	writeb(FIELD_PREP(REG13_TG_CODE_LOW_MASK, fld_tg_code),
	       phy->regs + PHY_REG_13);
	writeb(FIELD_PREP(REG14_TOL_MASK, 2) |
	       FIELD_PREP(REG14_RP_CODE_MASK, 2) |
	       FIELD_PREP(REG14_TG_CODE_HIGH_MASK, fld_tg_code >> 8),
	       phy->regs + PHY_REG_14);
}

static int fsl_samsung_hdmi_phy_configure(struct fsl_samsung_hdmi_phy *phy,
					  const struct phy_config *cfg)
{
	int i, ret;
	u8 val;

	/* HDMI PHY init */
	writeb(REG33_FIX_DA, phy->regs + PHY_REG_33);

	/* common PHY registers */
	for (i = 0; i < ARRAY_SIZE(common_phy_cfg); i++)
		writeb(common_phy_cfg[i].val, phy->regs + common_phy_cfg[i].reg);

	/* set individual PLL registers PHY_REG2 ... PHY_REG7 */
	for (i = 0; i < PHY_PLL_DIV_REGS_NUM; i++)
		writeb(cfg->pll_div_regs[i], phy->regs + PHY_REG_02 + i * 4);

	fsl_samsung_hdmi_phy_configure_pixclk(phy, cfg);
	fsl_samsung_hdmi_phy_configure_pll_lock_det(phy, cfg);

	writeb(REG33_FIX_DA | REG33_MODE_SET_DONE, phy->regs + PHY_REG_33);

	ret = readb_poll_timeout(phy->regs + PHY_REG_34, val,
				 val & REG34_PLL_LOCK, 50, 20000);
	if (ret)
		dev_err(phy->dev, "PLL failed to lock\n");

	return ret;
}

static unsigned long phy_clk_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct fsl_samsung_hdmi_phy *phy = to_fsl_samsung_hdmi_phy(hw);

	if (!phy->cur_cfg)
		return 74250000;

	return phy->cur_cfg->pixclk;
}

static long phy_clk_round_rate(struct clk_hw *hw,
			       unsigned long rate, unsigned long *parent_rate)
{
	int i;

	for (i = ARRAY_SIZE(phy_pll_cfg) - 1; i >= 0; i--)
		if (phy_pll_cfg[i].pixclk <= rate)
			return phy_pll_cfg[i].pixclk;

	return -EINVAL;
}

static int phy_clk_set_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long parent_rate)
{
	struct fsl_samsung_hdmi_phy *phy = to_fsl_samsung_hdmi_phy(hw);
	int i;

	for (i = ARRAY_SIZE(phy_pll_cfg) - 1; i >= 0; i--)
		if (phy_pll_cfg[i].pixclk <= rate)
			break;

	if (i < 0)
		return -EINVAL;

	phy->cur_cfg = &phy_pll_cfg[i];

	return fsl_samsung_hdmi_phy_configure(phy, phy->cur_cfg);
}

static const struct clk_ops phy_clk_ops = {
	.recalc_rate = phy_clk_recalc_rate,
	.round_rate = phy_clk_round_rate,
	.set_rate = phy_clk_set_rate,
};

static int phy_clk_register(struct fsl_samsung_hdmi_phy *phy)
{
	struct device *dev = phy->dev;
	struct device_node *np = dev->of_node;
	struct clk_init_data init;
	const char *parent_name;
	struct clk *phyclk;
	int ret;

	parent_name = __clk_get_name(phy->refclk);

	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;
	init.name = "hdmi_pclk";
	init.ops = &phy_clk_ops;

	phy->hw.init = &init;

	phyclk = devm_clk_register(dev, &phy->hw);
	if (IS_ERR(phyclk))
		return dev_err_probe(dev, PTR_ERR(phyclk),
				     "failed to register clock\n");

	ret = of_clk_add_hw_provider(np, of_clk_hw_simple_get, phyclk);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register clock provider\n");

	return 0;
}

static int fsl_samsung_hdmi_phy_probe(struct platform_device *pdev)
{
	struct fsl_samsung_hdmi_phy *phy;
	int ret;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	platform_set_drvdata(pdev, phy);
	phy->dev = &pdev->dev;

	phy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->regs))
		return PTR_ERR(phy->regs);

	phy->apbclk = devm_clk_get(phy->dev, "apb");
	if (IS_ERR(phy->apbclk))
		return dev_err_probe(phy->dev, PTR_ERR(phy->apbclk),
				     "failed to get apb clk\n");

	phy->refclk = devm_clk_get(phy->dev, "ref");
	if (IS_ERR(phy->refclk))
		return dev_err_probe(phy->dev, PTR_ERR(phy->refclk),
				     "failed to get ref clk\n");

	ret = clk_prepare_enable(phy->apbclk);
	if (ret) {
		dev_err(phy->dev, "failed to enable apbclk\n");
		return ret;
	}

	pm_runtime_get_noresume(phy->dev);
	pm_runtime_set_active(phy->dev);
	pm_runtime_enable(phy->dev);

	ret = phy_clk_register(phy);
	if (ret) {
		dev_err(&pdev->dev, "register clk failed\n");
		goto register_clk_failed;
	}

	pm_runtime_put(phy->dev);

	return 0;

register_clk_failed:
	clk_disable_unprepare(phy->apbclk);

	return ret;
}

static void fsl_samsung_hdmi_phy_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
}

static int __maybe_unused fsl_samsung_hdmi_phy_suspend(struct device *dev)
{
	struct fsl_samsung_hdmi_phy *phy = dev_get_drvdata(dev);

	clk_disable_unprepare(phy->apbclk);

	return 0;
}

static int __maybe_unused fsl_samsung_hdmi_phy_resume(struct device *dev)
{
	struct fsl_samsung_hdmi_phy *phy = dev_get_drvdata(dev);
	int ret = 0;

	ret = clk_prepare_enable(phy->apbclk);
	if (ret) {
		dev_err(phy->dev, "failed to enable apbclk\n");
		return ret;
	}

	if (phy->cur_cfg)
		ret = fsl_samsung_hdmi_phy_configure(phy, phy->cur_cfg);

	return ret;

}

static DEFINE_RUNTIME_DEV_PM_OPS(fsl_samsung_hdmi_phy_pm_ops,
				 fsl_samsung_hdmi_phy_suspend,
				 fsl_samsung_hdmi_phy_resume, NULL);

static const struct of_device_id fsl_samsung_hdmi_phy_of_match[] = {
	{
		.compatible = "fsl,imx8mp-hdmi-phy",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, fsl_samsung_hdmi_phy_of_match);

static struct platform_driver fsl_samsung_hdmi_phy_driver = {
	.probe  = fsl_samsung_hdmi_phy_probe,
	.remove_new = fsl_samsung_hdmi_phy_remove,
	.driver = {
		.name = "fsl-samsung-hdmi-phy",
		.of_match_table = fsl_samsung_hdmi_phy_of_match,
		.pm = pm_ptr(&fsl_samsung_hdmi_phy_pm_ops),
	},
};
module_platform_driver(fsl_samsung_hdmi_phy_driver);

MODULE_AUTHOR("Sandor Yu <Sandor.yu@nxp.com>");
MODULE_DESCRIPTION("SAMSUNG HDMI 2.0 Transmitter PHY Driver");
MODULE_LICENSE("GPL");
