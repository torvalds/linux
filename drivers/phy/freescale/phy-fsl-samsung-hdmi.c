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

#define PHY_REG(reg)		(reg * 4)

#define REG01_PMS_P_MASK	GENMASK(3, 0)
#define REG03_PMS_S_MASK	GENMASK(7, 4)
#define REG12_CK_DIV_MASK	GENMASK(5, 4)

#define REG13_TG_CODE_LOW_MASK	GENMASK(7, 0)

#define REG14_TOL_MASK		GENMASK(7, 4)
#define REG14_RP_CODE_MASK	GENMASK(3, 1)
#define REG14_TG_CODE_HIGH_MASK	GENMASK(0, 0)

#define REG21_SEL_TX_CK_INV	BIT(7)
#define REG21_PMS_S_MASK	GENMASK(3, 0)
/*
 * REG33 does not match the ref manual. According to Sandor Yu from NXP,
 * "There is a doc issue on the i.MX8MP latest RM"
 * REG33 is being used per guidance from Sandor
 */
#define REG33_MODE_SET_DONE	BIT(7)
#define REG33_FIX_DA		BIT(1)

#define REG34_PHY_READY	BIT(7)
#define REG34_PLL_LOCK		BIT(6)
#define REG34_PHY_CLK_READY	BIT(5)

#ifndef MHZ
#define MHZ	(1000UL * 1000UL)
#endif

#define PHY_PLL_DIV_REGS_NUM 7

struct phy_config {
	u32	pixclk;
	u8	pll_div_regs[PHY_PLL_DIV_REGS_NUM];
};

/*
 * The calculated_phy_pll_cfg only handles integer divider for PMS,
 * meaning the last four entries will be fixed, but the first three will
 * be calculated by the PMS calculator.
 */
static struct phy_config calculated_phy_pll_cfg = {
	.pixclk = 0,
	.pll_div_regs = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 },
};

/* The lookup table contains values for which the fractional divder is used */
static const struct phy_config phy_pll_cfg[] = {
	{
		.pixclk = 22250000,
		.pll_div_regs = { 0xd1, 0x4b, 0xf1, 0x89, 0x88, 0x80, 0x40 },
	}, {
		.pixclk = 23750000,
		.pll_div_regs = { 0xd1, 0x50, 0xf1, 0x86, 0x85, 0x80, 0x40 },
	}, {
		.pixclk = 24024000,
		.pll_div_regs = { 0xd1, 0x50, 0xf1, 0x99, 0x02, 0x80, 0x40 },
	}, {
		.pixclk = 25175000,
		.pll_div_regs = { 0xd1, 0x54, 0xfc, 0xcc, 0x91, 0x80, 0x40 },
	},  {
		.pixclk = 26750000,
		.pll_div_regs = { 0xd1, 0x5a, 0xf2, 0x89, 0x88, 0x80, 0x40 },
	},  {
		.pixclk = 27027000,
		.pll_div_regs = { 0xd1, 0x5a, 0xf2, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 29500000,
		.pll_div_regs = { 0xd1, 0x62, 0xf4, 0x95, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 30750000,
		.pll_div_regs = { 0xd1, 0x66, 0xf4, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 30888000,
		.pll_div_regs = { 0xd1, 0x66, 0xf4, 0x99, 0x18, 0x88, 0x45 },
	}, {
		.pixclk = 33750000,
		.pll_div_regs = { 0xd1, 0x70, 0xf4, 0x82, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 35000000,
		.pll_div_regs = { 0xd1, 0x58, 0xb8, 0x8b, 0x88, 0x80, 0x40 },
	},  {
		.pixclk = 36036000,
		.pll_div_regs = { 0xd1, 0x5a, 0xb2, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 43243200,
		.pll_div_regs = { 0xd1, 0x5a, 0x92, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 44500000,
		.pll_div_regs = { 0xd1, 0x5c, 0x92, 0x98, 0x11, 0x84, 0x41 },
	}, {
		.pixclk = 47000000,
		.pll_div_regs = { 0xd1, 0x62, 0x94, 0x95, 0x82, 0x80, 0x40 },
	}, {
		.pixclk = 47500000,
		.pll_div_regs = { 0xd1, 0x63, 0x96, 0xa1, 0x82, 0x80, 0x40 },
	}, {
		.pixclk = 50349650,
		.pll_div_regs = { 0xd1, 0x54, 0x7c, 0xc3, 0x8f, 0x80, 0x40 },
	}, {
		.pixclk = 53250000,
		.pll_div_regs = { 0xd1, 0x58, 0x72, 0x84, 0x03, 0x82, 0x41 },
	}, {
		.pixclk = 53500000,
		.pll_div_regs = { 0xd1, 0x5a, 0x72, 0x89, 0x88, 0x80, 0x40 },
	},  {
		.pixclk = 54054000,
		.pll_div_regs = { 0xd1, 0x5a, 0x72, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 59000000,
		.pll_div_regs = { 0xd1, 0x62, 0x74, 0x95, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 59340659,
		.pll_div_regs = { 0xd1, 0x62, 0x74, 0xdb, 0x52, 0x88, 0x47 },
	},  {
		.pixclk = 61500000,
		.pll_div_regs = { 0xd1, 0x66, 0x74, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 63500000,
		.pll_div_regs = { 0xd1, 0x69, 0x74, 0x89, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 67500000,
		.pll_div_regs = { 0xd1, 0x54, 0x52, 0x87, 0x03, 0x80, 0x40 },
	}, {
		.pixclk = 70000000,
		.pll_div_regs = { 0xd1, 0x58, 0x58, 0x8b, 0x88, 0x80, 0x40 },
	},  {
		.pixclk = 72072000,
		.pll_div_regs = { 0xd1, 0x5a, 0x52, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 74176000,
		.pll_div_regs = { 0xd1, 0x5d, 0x58, 0xdb, 0xA2, 0x88, 0x41 },
	}, {
		.pixclk = 74250000,
		.pll_div_regs = { 0xd1, 0x5c, 0x52, 0x90, 0x0d, 0x84, 0x41 },
	}, {
		.pixclk = 78500000,
		.pll_div_regs = { 0xd1, 0x62, 0x54, 0x87, 0x01, 0x80, 0x40 },
	},  {
		.pixclk = 82000000,
		.pll_div_regs = { 0xd1, 0x66, 0x54, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 82500000,
		.pll_div_regs = { 0xd1, 0x67, 0x54, 0x88, 0x01, 0x90, 0x49 },
	}, {
		.pixclk = 89000000,
		.pll_div_regs = { 0xd1, 0x70, 0x54, 0x84, 0x83, 0x80, 0x40 },
	}, {
		.pixclk = 90000000,
		.pll_div_regs = { 0xd1, 0x70, 0x54, 0x82, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 94000000,
		.pll_div_regs = { 0xd1, 0x4e, 0x32, 0xa7, 0x10, 0x80, 0x40 },
	}, {
		.pixclk = 95000000,
		.pll_div_regs = { 0xd1, 0x50, 0x31, 0x86, 0x85, 0x80, 0x40 },
	}, {
		.pixclk = 98901099,
		.pll_div_regs = { 0xd1, 0x52, 0x3a, 0xdb, 0x4c, 0x88, 0x47 },
	}, {
		.pixclk = 99000000,
		.pll_div_regs = { 0xd1, 0x52, 0x32, 0x82, 0x01, 0x88, 0x47 },
	}, {
		.pixclk = 100699300,
		.pll_div_regs = { 0xd1, 0x54, 0x3c, 0xc3, 0x8f, 0x80, 0x40 },
	},  {
		.pixclk = 102500000,
		.pll_div_regs = { 0xd1, 0x55, 0x32, 0x8c, 0x05, 0x90, 0x4b },
	}, {
		.pixclk = 104750000,
		.pll_div_regs = { 0xd1, 0x57, 0x32, 0x98, 0x07, 0x90, 0x49 },
	}, {
		.pixclk = 106500000,
		.pll_div_regs = { 0xd1, 0x58, 0x32, 0x84, 0x03, 0x82, 0x41 },
	}, {
		.pixclk = 107000000,
		.pll_div_regs = { 0xd1, 0x5a, 0x32, 0x89, 0x88, 0x80, 0x40 },
	},  {
		.pixclk = 108108000,
		.pll_div_regs = { 0xd1, 0x5a, 0x32, 0xfd, 0x0c, 0x80, 0x40 },
	}, {
		.pixclk = 118000000,
		.pll_div_regs = { 0xd1, 0x62, 0x34, 0x95, 0x08, 0x80, 0x40 },
	},  {
		.pixclk = 123000000,
		.pll_div_regs = { 0xd1, 0x66, 0x34, 0x82, 0x01, 0x88, 0x45 },
	}, {
		.pixclk = 127000000,
		.pll_div_regs = { 0xd1, 0x69, 0x34, 0x89, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 135000000,
		.pll_div_regs = { 0xd1, 0x70, 0x34, 0x82, 0x01, 0x80, 0x40 },
	}, {
		.pixclk = 135580000,
		.pll_div_regs = { 0xd1, 0x71, 0x39, 0xe9, 0x82, 0x9c, 0x5b },
	}, {
		.pixclk = 137520000,
		.pll_div_regs = { 0xd1, 0x72, 0x38, 0x99, 0x10, 0x85, 0x41 },
	}, {
		.pixclk = 138750000,
		.pll_div_regs = { 0xd1, 0x73, 0x35, 0x88, 0x05, 0x90, 0x4d },
	}, {
		.pixclk = 140000000,
		.pll_div_regs = { 0xd1, 0x75, 0x36, 0xa7, 0x90, 0x80, 0x40 },
	},  {
		.pixclk = 148352000,
		.pll_div_regs = { 0xd1, 0x7b, 0x35, 0xdb, 0x39, 0x90, 0x45 },
	}, {
		.pixclk = 148500000,
		.pll_div_regs = { 0xd1, 0x7b, 0x35, 0x84, 0x03, 0x90, 0x45 },
	}, {
		.pixclk = 154000000,
		.pll_div_regs = { 0xd1, 0x40, 0x18, 0x83, 0x01, 0x00, 0x40 },
	}, {
		.pixclk = 157000000,
		.pll_div_regs = { 0xd1, 0x41, 0x11, 0xa7, 0x14, 0x80, 0x40 },
	}, {
		.pixclk = 160000000,
		.pll_div_regs = { 0xd1, 0x42, 0x12, 0xa1, 0x20, 0x80, 0x40 },
	}, {
		.pixclk = 162000000,
		.pll_div_regs = { 0xd1, 0x43, 0x18, 0x8b, 0x08, 0x96, 0x55 },
	}, {
		.pixclk = 164000000,
		.pll_div_regs = { 0xd1, 0x45, 0x11, 0x83, 0x82, 0x90, 0x4b },
	}, {
		.pixclk = 165000000,
		.pll_div_regs = { 0xd1, 0x45, 0x11, 0x84, 0x81, 0x90, 0x4b },
	}, {
		.pixclk = 185625000,
		.pll_div_regs = { 0xd1, 0x4e, 0x12, 0x9a, 0x95, 0x80, 0x40 },
	}, {
		.pixclk = 188000000,
		.pll_div_regs = { 0xd1, 0x4e, 0x12, 0xa7, 0x10, 0x80, 0x40 },
	}, {
		.pixclk = 198000000,
		.pll_div_regs = { 0xd1, 0x52, 0x12, 0x82, 0x01, 0x88, 0x47 },
	}, {
		.pixclk = 205000000,
		.pll_div_regs = { 0xd1, 0x55, 0x12, 0x8c, 0x05, 0x90, 0x4b },
	}, {
		.pixclk = 209500000,
		.pll_div_regs = { 0xd1, 0x57, 0x12, 0x98, 0x07, 0x90, 0x49 },
	}, {
		.pixclk = 213000000,
		.pll_div_regs = { 0xd1, 0x58, 0x12, 0x84, 0x03, 0x82, 0x41 },
	}, {
		.pixclk = 216216000,
		.pll_div_regs = { 0xd1, 0x5a, 0x12, 0xfd, 0x0c, 0x80, 0x40 },
	},  {
		.pixclk = 254000000,
		.pll_div_regs = { 0xd1, 0x69, 0x14, 0x89, 0x08, 0x80, 0x40 },
	}, {
		.pixclk = 277500000,
		.pll_div_regs = { 0xd1, 0x73, 0x15, 0x88, 0x05, 0x90, 0x4d },
	},  {
		.pixclk = 297000000,
		.pll_div_regs = { 0xd1, 0x7b, 0x15, 0x84, 0x03, 0x90, 0x45 },
	},
};

struct reg_settings {
	u8 reg;
	u8 val;
};

static const struct reg_settings common_phy_cfg[] = {
	{ PHY_REG(0), 0x00 },
	/* PHY_REG(1-7) pix clk specific */
	{ PHY_REG(8), 0x4f }, { PHY_REG(9), 0x30 },
	{ PHY_REG(10), 0x33 }, { PHY_REG(11), 0x65 },
	/* REG12 pixclk specific */
	/* REG13 pixclk specific */
	/* REG14 pixclk specific */
	{ PHY_REG(15), 0x80 }, { PHY_REG(16), 0x6c },
	{ PHY_REG(17), 0xf2 }, { PHY_REG(18), 0x67 },
	{ PHY_REG(19), 0x00 }, { PHY_REG(20), 0x10 },
	/* REG21 pixclk specific */
	{ PHY_REG(22), 0x30 }, { PHY_REG(23), 0x32 },
	{ PHY_REG(24), 0x60 }, { PHY_REG(25), 0x8f },
	{ PHY_REG(26), 0x00 }, { PHY_REG(27), 0x00 },
	{ PHY_REG(28), 0x08 }, { PHY_REG(29), 0x00 },
	{ PHY_REG(30), 0x00 }, { PHY_REG(31), 0x00 },
	{ PHY_REG(32), 0x00 }, { PHY_REG(33), 0x80 },
	{ PHY_REG(34), 0x00 }, { PHY_REG(35), 0x00 },
	{ PHY_REG(36), 0x00 }, { PHY_REG(37), 0x00 },
	{ PHY_REG(38), 0x00 }, { PHY_REG(39), 0x00 },
	{ PHY_REG(40), 0x00 }, { PHY_REG(41), 0xe0 },
	{ PHY_REG(42), 0x83 }, { PHY_REG(43), 0x0f },
	{ PHY_REG(44), 0x3E }, { PHY_REG(45), 0xf8 },
	{ PHY_REG(46), 0x00 }, { PHY_REG(47), 0x00 }
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

static int
fsl_samsung_hdmi_phy_configure_pll_lock_det(struct fsl_samsung_hdmi_phy *phy,
					    const struct phy_config *cfg)
{
	u32 pclk = cfg->pixclk;
	u32 fld_tg_code;
	u32 int_pllclk;
	u8 div;

	/* Find int_pllclk speed */
	for (div = 0; div < 4; div++) {
		int_pllclk = pclk / (1 << div);
		if (int_pllclk < (50 * MHZ))
			break;
	}

	if (unlikely(div == 4))
		return -EINVAL;

	writeb(FIELD_PREP(REG12_CK_DIV_MASK, div), phy->regs + PHY_REG(12));

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

	fld_tg_code =  DIV_ROUND_UP(24 * MHZ * 256, int_pllclk);

	/* FLD_TOL and FLD_RP_CODE taken from downstream driver */
	writeb(FIELD_PREP(REG13_TG_CODE_LOW_MASK, fld_tg_code),
	       phy->regs + PHY_REG(13));
	writeb(FIELD_PREP(REG14_TOL_MASK, 2) |
	       FIELD_PREP(REG14_RP_CODE_MASK, 2) |
	       FIELD_PREP(REG14_TG_CODE_HIGH_MASK, fld_tg_code >> 8),
	       phy->regs + PHY_REG(14));

	return 0;
}

static unsigned long fsl_samsung_hdmi_phy_find_pms(unsigned long fout, u8 *p, u16 *m, u8 *s)
{
	unsigned long best_freq = 0;
	u32 min_delta = 0xffffffff;
	u8 _p, best_p;
	u16 _m, best_m;
	u8 _s, best_s;

	/*
	 * Figure 13-78 of the reference manual states the PLL should be TMDS x 5
	 * while the TMDS_CLKO should be the PLL / 5.  So to calculate the PLL,
	 * take the pix clock x 5, then return the value of the PLL / 5.
	 */
	fout *= 5;

	/* The ref manual states the values of 'P' range from 1 to 11 */
	for (_p = 1; _p <= 11; ++_p) {
		for (_s = 1; _s <= 16; ++_s) {
			u64 tmp;
			u32 delta;

			/* s must be one or even */
			if (_s > 1 && (_s & 0x01) == 1)
				_s++;

			/* _s cannot be 14 per the TRM */
			if (_s == 14)
				continue;

			/*
			 * The Ref manual doesn't explicitly state the range of M,
			 * but it does show it as an 8-bit value, so reject
			 * any value above 255.
			 */
			tmp = (u64)fout * (_p * _s);
			do_div(tmp, 24 * MHZ);
			if (tmp > 255)
				continue;
			_m = tmp;

			/*
			 * Rev 2 of the Ref Manual states the
			 * VCO can range between 750MHz and
			 * 3GHz. The VCO is assumed to be
			 * Fvco = (M * f_ref) / P,
			 * where f_ref is 24MHz.
			 */
			tmp = div64_ul((u64)_m * 24 * MHZ, _p);
			if (tmp < 750 * MHZ ||
			    tmp > 3000 * MHZ)
				continue;

			/* Final frequency after post-divider */
			do_div(tmp, _s);

			delta = abs(fout - tmp);
			if (delta < min_delta) {
				best_p = _p;
				best_s = _s;
				best_m = _m;
				min_delta = delta;
				best_freq = tmp;
			}

			/* If we have an exact match, stop looking for a better value */
			if (!delta)
				goto done;
		}
	}
done:
	if (best_freq) {
		*p = best_p;
		*m = best_m;
		*s = best_s;
	}

	return best_freq / 5;
}

static int fsl_samsung_hdmi_phy_configure(struct fsl_samsung_hdmi_phy *phy,
					  const struct phy_config *cfg)
{
	int i, ret;
	u8 val;

	/* HDMI PHY init */
	writeb(REG33_FIX_DA, phy->regs + PHY_REG(33));

	/* common PHY registers */
	for (i = 0; i < ARRAY_SIZE(common_phy_cfg); i++)
		writeb(common_phy_cfg[i].val, phy->regs + common_phy_cfg[i].reg);

	/* set individual PLL registers PHY_REG1 ... PHY_REG7 */
	for (i = 0; i < PHY_PLL_DIV_REGS_NUM; i++)
		writeb(cfg->pll_div_regs[i], phy->regs + PHY_REG(1) + i * 4);

	/* High nibble of PHY_REG3 and low nibble of PHY_REG21 both contain 'S' */
	writeb(REG21_SEL_TX_CK_INV | FIELD_PREP(REG21_PMS_S_MASK,
	       cfg->pll_div_regs[2] >> 4), phy->regs + PHY_REG(21));

	ret = fsl_samsung_hdmi_phy_configure_pll_lock_det(phy, cfg);
	if (ret) {
		dev_err(phy->dev, "pixclock too large\n");
		return ret;
	}

	writeb(REG33_FIX_DA | REG33_MODE_SET_DONE, phy->regs + PHY_REG(33));

	ret = readb_poll_timeout(phy->regs + PHY_REG(34), val,
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

/* Helper function to lookup the available fractional-divider rate */
static const struct phy_config *fsl_samsung_hdmi_phy_lookup_rate(unsigned long rate)
{
	int i;

	/* Search the lookup table */
	for (i = ARRAY_SIZE(phy_pll_cfg) - 1; i >= 0; i--)
		if (phy_pll_cfg[i].pixclk <= rate)
			break;

	return &phy_pll_cfg[i];
}

static void fsl_samsung_hdmi_calculate_phy(struct phy_config *cal_phy, unsigned long rate,
				    u8 p, u16 m, u8 s)
{
	cal_phy->pixclk = rate;
	cal_phy->pll_div_regs[0] = FIELD_PREP(REG01_PMS_P_MASK, p);
	cal_phy->pll_div_regs[1] = m;
	cal_phy->pll_div_regs[2] = FIELD_PREP(REG03_PMS_S_MASK, s-1);
	/* pll_div_regs 3-6 are fixed and pre-defined already */
}

static u32 fsl_samsung_hdmi_phy_get_closest_rate(unsigned long rate,
						 u32 int_div_clk, u32 frac_div_clk)
{
	/* Calculate the absolute value of the differences and return whichever is closest */
	if (abs((long)rate - (long)int_div_clk) < abs((long)(rate - (long)frac_div_clk)))
		return int_div_clk;

	return frac_div_clk;
}

static long phy_clk_round_rate(struct clk_hw *hw,
			       unsigned long rate, unsigned long *parent_rate)
{
	const struct phy_config *fract_div_phy;
	u32 int_div_clk;
	u16 m;
	u8 p, s;

	/* If the clock is out of range return error instead of searching */
	if (rate > 297000000 || rate < 22250000)
		return -EINVAL;

	/* Search the fractional divider lookup table */
	fract_div_phy = fsl_samsung_hdmi_phy_lookup_rate(rate);

	/* If the rate is an exact match, return that value */
	if (rate == fract_div_phy->pixclk)
		return fract_div_phy->pixclk;

	/* If the exact match isn't found, calculate the integer divider */
	int_div_clk = fsl_samsung_hdmi_phy_find_pms(rate, &p, &m, &s);

	/* If the int_div_clk rate is an exact match, return that value */
	if (int_div_clk == rate)
		return int_div_clk;

	/* If neither rate is an exact match, use the value from the LUT */
	return fract_div_phy->pixclk;
}

static int phy_use_fract_div(struct fsl_samsung_hdmi_phy *phy, const struct phy_config *fract_div_phy)
{
	phy->cur_cfg = fract_div_phy;
	dev_dbg(phy->dev, "fsl_samsung_hdmi_phy: using fractional divider rate = %u\n",
		phy->cur_cfg->pixclk);
	return fsl_samsung_hdmi_phy_configure(phy, phy->cur_cfg);
}

static int phy_use_integer_div(struct fsl_samsung_hdmi_phy *phy,
			       const struct phy_config *int_div_clk)
{
	phy->cur_cfg  = &calculated_phy_pll_cfg;
	dev_dbg(phy->dev, "fsl_samsung_hdmi_phy: integer divider rate = %u\n",
		phy->cur_cfg->pixclk);
	return fsl_samsung_hdmi_phy_configure(phy, phy->cur_cfg);
}

static int phy_clk_set_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long parent_rate)
{
	struct fsl_samsung_hdmi_phy *phy = to_fsl_samsung_hdmi_phy(hw);
	const struct phy_config *fract_div_phy;
	u32 int_div_clk;
	u16 m;
	u8 p, s;

	/* Search the fractional divider lookup table */
	fract_div_phy = fsl_samsung_hdmi_phy_lookup_rate(rate);

	/* If the rate is an exact match, use that value */
	if (fract_div_phy->pixclk == rate)
		return phy_use_fract_div(phy, fract_div_phy);

	/*
	 * If the rate from the fractional divider is not exact, check the integer divider,
	 * and use it if that value is an exact match.
	 */
	int_div_clk = fsl_samsung_hdmi_phy_find_pms(rate, &p, &m, &s);
	fsl_samsung_hdmi_calculate_phy(&calculated_phy_pll_cfg, int_div_clk, p, m, s);
	if (int_div_clk == rate)
		return phy_use_integer_div(phy, &calculated_phy_pll_cfg);

	/*
	 * Compare the difference between the integer clock and the fractional clock against
	 * the desired clock and which whichever is closest.
	 */
	if (fsl_samsung_hdmi_phy_get_closest_rate(rate, int_div_clk,
						  fract_div_phy->pixclk) == fract_div_phy->pixclk)
		return phy_use_fract_div(phy, fract_div_phy);
	else
		return phy_use_integer_div(phy, &calculated_phy_pll_cfg);
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

	phy->apbclk = devm_clk_get_enabled(phy->dev, "apb");
	if (IS_ERR(phy->apbclk))
		return dev_err_probe(phy->dev, PTR_ERR(phy->apbclk),
				     "failed to get apb clk\n");

	phy->refclk = devm_clk_get(phy->dev, "ref");
	if (IS_ERR(phy->refclk))
		return dev_err_probe(phy->dev, PTR_ERR(phy->refclk),
				     "failed to get ref clk\n");

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
	.probe = fsl_samsung_hdmi_phy_probe,
	.remove = fsl_samsung_hdmi_phy_remove,
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
