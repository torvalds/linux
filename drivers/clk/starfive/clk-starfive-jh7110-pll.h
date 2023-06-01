/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * StarFive JH7110 PLL Clock Generator Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#ifndef _CLK_STARFIVE_JH7110_PLL_H_
#define _CLK_STARFIVE_JH7110_PLL_H_

/*
 * If set PLL2_DEFAULT_FREQ NULL of 0 , then PLL2 frequency is original.
 * If set PLL2_DEFAULT_FREQ one of 'starfive_pll2_freq_value', then PLL2
 * frequency will be set the new rate during clock tree registering.
 */
#define PLL0_DEFAULT_FREQ	PLL0_FREQ_1500_VALUE
#define PLL2_DEFAULT_FREQ	PLL2_FREQ_1188_VALUE

#define PLL0_INDEX		0
#define PLL1_INDEX		1
#define PLL2_INDEX		2

#define PLL_INDEX_MAX	3

#define PLL0_DACPD_SHIFT	24
#define PLL0_DACPD_MASK		0x1000000
#define PLL0_DSMPD_SHIFT	25
#define PLL0_DSMPD_MASK		0x2000000
#define PLL0_FBDIV_SHIFT	0
#define PLL0_FBDIV_MASK		0xFFF
#define PLL0_FRAC_SHIFT		0
#define PLL0_FRAC_MASK		0xFFFFFF
#define PLL0_POSTDIV1_SHIFT	28
#define PLL0_POSTDIV1_MASK	0x30000000
#define PLL0_PREDIV_SHIFT	0
#define PLL0_PREDIV_MASK	0x3F

#define PLL1_DACPD_SHIFT	15
#define PLL1_DACPD_MASK		0x8000
#define PLL1_DSMPD_SHIFT	16
#define PLL1_DSMPD_MASK		0x10000
#define PLL1_FBDIV_SHIFT	17
#define PLL1_FBDIV_MASK		0x1FFE0000
#define PLL1_FRAC_SHIFT		0
#define PLL1_FRAC_MASK		0xFFFFFF
#define PLL1_POSTDIV1_SHIFT	28
#define PLL1_POSTDIV1_MASK	0x30000000
#define PLL1_PREDIV_SHIFT	0
#define PLL1_PREDIV_MASK	0x3F

#define PLL2_DACPD_SHIFT	15
#define PLL2_DACPD_MASK		0x8000
#define PLL2_DSMPD_SHIFT	16
#define PLL2_DSMPD_MASK		0x10000
#define PLL2_FBDIV_SHIFT	17
#define PLL2_FBDIV_MASK		0x1FFE0000
#define PLL2_FRAC_SHIFT		0
#define PLL2_FRAC_MASK		0xFFFFFF
#define PLL2_POSTDIV1_SHIFT	28
#define PLL2_POSTDIV1_MASK	0x30000000
#define PLL2_PREDIV_SHIFT	0
#define PLL2_PREDIV_MASK	0x3F

#define FRAC_PATR_SIZE		1000

struct pll_syscon_offset {
	u32 dacpd_offset;
	u32 dsmpd_offset;
	u32 fbdiv_offset;
	u32 frac_offset;
	u32 prediv_offset;
	u32 postdiv1_offset;
};

struct pll_syscon_mask {
	u32 dacpd_mask;
	u32 dsmpd_mask;
	u32 fbdiv_mask;
	u32 frac_mask;
	u32 prediv_mask;
	u32 postdiv1_mask;
};

struct pll_syscon_shift {
	u32 dacpd_shift;
	u32 dsmpd_shift;
	u32 fbdiv_shift;
	u32 frac_shift;
	u32 prediv_shift;
	u32 postdiv1_shift;
};

struct jh7110_clk_pll_data {
	struct device *dev;
	struct clk_hw hw;
	unsigned long refclk_freq;
	unsigned int idx;
	unsigned int freq_select_idx;

	struct regmap *sys_syscon_regmap;
	struct pll_syscon_offset offset;
	struct pll_syscon_mask mask;
	struct pll_syscon_shift shift;
};

struct starfive_pll_syscon_value {
	unsigned long freq;
	u32 prediv;
	u32 fbdiv;
	u32 postdiv1;
/* Both daxpd and dsmpd set 1 while integer multiple mode */
/* Both daxpd and dsmpd set 0 while fraction multiple mode */
	u32 dacpd;
	u32 dsmpd;
/* frac value should be decimals multiplied by 2^24 */
	u32 frac;
};

enum starfive_pll0_freq_value {
	PLL0_FREQ_375_VALUE = 375000000,
	PLL0_FREQ_500_VALUE = 500000000,
	PLL0_FREQ_625_VALUE = 625000000,
	PLL0_FREQ_750_VALUE = 750000000,
	PLL0_FREQ_875_VALUE = 875000000,
	PLL0_FREQ_1000_VALUE = 1000000000,
	PLL0_FREQ_1250_VALUE = 1250000000,
	PLL0_FREQ_1375_VALUE = 1375000000,
	PLL0_FREQ_1500_VALUE = 1500000000
};

enum starfive_pll0_freq {
	PLL0_FREQ_375 = 0,
	PLL0_FREQ_500,
	PLL0_FREQ_625,
	PLL0_FREQ_750,
	PLL0_FREQ_875,
	PLL0_FREQ_1000,
	PLL0_FREQ_1250,
	PLL0_FREQ_1375,
	PLL0_FREQ_1500,
	PLL0_FREQ_MAX = PLL0_FREQ_1500
};

enum starfive_pll1_freq_value {
	PLL1_FREQ_1066_VALUE = 1066000000,
};

enum starfive_pll1_freq {
	PLL1_FREQ_1066 = 0,
};

enum starfive_pll2_freq_value {
	PLL2_FREQ_1188_VALUE = 1188000000,
	PLL2_FREQ_12288_VALUE = 1228800000,
};

enum starfive_pll2_freq {
	PLL2_FREQ_1188 = 0,
	PLL2_FREQ_12288,
};

static const struct starfive_pll_syscon_value
	jh7110_pll0_syscon_freq[] = {
	[PLL0_FREQ_375] = {
		.freq = PLL0_FREQ_375_VALUE,
		.prediv = 8,
		.fbdiv = 125,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_500] = {
		.freq = PLL0_FREQ_500_VALUE,
		.prediv = 6,
		.fbdiv = 125,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_625] = {
		.freq = PLL0_FREQ_625_VALUE,
		.prediv = 24,
		.fbdiv = 625,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_750] = {
		.freq = PLL0_FREQ_750_VALUE,
		.prediv = 4,
		.fbdiv = 125,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_875] = {
		.freq = PLL0_FREQ_875_VALUE,
		.prediv = 24,
		.fbdiv = 875,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_1000] = {
		.freq = PLL0_FREQ_1000_VALUE,
		.prediv = 3,
		.fbdiv = 125,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_1250] = {
		.freq = PLL0_FREQ_1250_VALUE,
		.prediv = 12,
		.fbdiv = 625,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_1375] = {
		.freq = PLL0_FREQ_1375_VALUE,
		.prediv = 24,
		.fbdiv = 1375,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL0_FREQ_1500] = {
		.freq = PLL0_FREQ_1500_VALUE,
		.prediv = 2,
		.fbdiv = 125,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
};

static const struct starfive_pll_syscon_value
	jh7110_pll1_syscon_freq[] = {
	[PLL1_FREQ_1066] = {
		.freq = PLL1_FREQ_1066_VALUE,
		.prediv = 12,
		.fbdiv = 533,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
};

static const struct starfive_pll_syscon_value
	jh7110_pll2_syscon_freq[] = {
	[PLL2_FREQ_1188] = {
		.freq = PLL2_FREQ_1188_VALUE,
		.prediv = 2,
		.fbdiv = 99,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
	[PLL2_FREQ_12288] = {
		.freq = PLL2_FREQ_12288_VALUE,
		.prediv = 5,
		.fbdiv = 256,
		.postdiv1 = 1,
		.dacpd = 1,
		.dsmpd = 1,
	},
};

int __init clk_starfive_jh7110_pll_init(struct platform_device *pdev,
				struct jh7110_clk_pll_data *pll_priv);

#endif
