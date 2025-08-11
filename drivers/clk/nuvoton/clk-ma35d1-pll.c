// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Nuvoton Technology Corp.
 * Author: Chi-Fang Li <cfli0@nuvoton.com>
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/units.h>
#include <dt-bindings/clock/nuvoton,ma35d1-clk.h>

#include "clk-ma35d1.h"

/* PLL frequency limits */
#define PLL_FREF_MAX_FREQ	(200 * HZ_PER_MHZ)
#define PLL_FREF_MIN_FREQ	(1 * HZ_PER_MHZ)
#define PLL_FREF_M_MAX_FREQ	(40 * HZ_PER_MHZ)
#define PLL_FREF_M_MIN_FREQ	(10 * HZ_PER_MHZ)
#define PLL_FCLK_MAX_FREQ	(2400 * HZ_PER_MHZ)
#define PLL_FCLK_MIN_FREQ	(600 * HZ_PER_MHZ)
#define PLL_FCLKO_MAX_FREQ	(2400 * HZ_PER_MHZ)
#define PLL_FCLKO_MIN_FREQ	(85700 * HZ_PER_KHZ)
#define PLL_SS_RATE		0x77
#define PLL_SLOPE		0x58CFA

#define REG_PLL_CTL0_OFFSET	0x0
#define REG_PLL_CTL1_OFFSET	0x4
#define REG_PLL_CTL2_OFFSET	0x8

/* bit fields for REG_CLK_PLL0CTL0, which is SMIC PLL design */
#define SPLL0_CTL0_FBDIV	GENMASK(7, 0)
#define SPLL0_CTL0_INDIV	GENMASK(11, 8)
#define SPLL0_CTL0_OUTDIV	GENMASK(13, 12)
#define SPLL0_CTL0_PD		BIT(16)
#define SPLL0_CTL0_BP		BIT(17)

/* bit fields for REG_CLK_PLLxCTL0 ~ REG_CLK_PLLxCTL2, where x = 2 ~ 5 */
#define PLL_CTL0_FBDIV		GENMASK(10, 0)
#define PLL_CTL0_INDIV		GENMASK(17, 12)
#define PLL_CTL0_MODE		GENMASK(19, 18)
#define PLL_CTL0_SSRATE		GENMASK(30, 20)
#define PLL_CTL1_PD		BIT(0)
#define PLL_CTL1_BP		BIT(1)
#define PLL_CTL1_OUTDIV		GENMASK(6, 4)
#define PLL_CTL1_FRAC		GENMASK(31, 24)
#define PLL_CTL2_SLOPE		GENMASK(23, 0)

#define INDIV_MIN		1
#define INDIV_MAX		63
#define FBDIV_MIN		16
#define FBDIV_MAX		2047
#define FBDIV_FRAC_MIN		1600
#define FBDIV_FRAC_MAX		204700
#define OUTDIV_MIN		1
#define OUTDIV_MAX		7

#define PLL_MODE_INT            0
#define PLL_MODE_FRAC           1
#define PLL_MODE_SS             2

struct ma35d1_clk_pll {
	struct clk_hw hw;
	u32 id;
	u8 mode;
	void __iomem *ctl0_base;
	void __iomem *ctl1_base;
	void __iomem *ctl2_base;
};

static inline struct ma35d1_clk_pll *to_ma35d1_clk_pll(struct clk_hw *_hw)
{
	return container_of(_hw, struct ma35d1_clk_pll, hw);
}

static unsigned long ma35d1_calc_smic_pll_freq(u32 pll0_ctl0,
					       unsigned long parent_rate)
{
	u32 m, n, p, outdiv;
	u64 pll_freq;

	if (pll0_ctl0 & SPLL0_CTL0_BP)
		return parent_rate;

	n = FIELD_GET(SPLL0_CTL0_FBDIV, pll0_ctl0);
	m = FIELD_GET(SPLL0_CTL0_INDIV, pll0_ctl0);
	p = FIELD_GET(SPLL0_CTL0_OUTDIV, pll0_ctl0);
	outdiv = 1 << p;
	pll_freq = (u64)parent_rate * n;
	div_u64(pll_freq, m * outdiv);
	return pll_freq;
}

static unsigned long ma35d1_calc_pll_freq(u8 mode, u32 *reg_ctl, unsigned long parent_rate)
{
	unsigned long pll_freq, x;
	u32 m, n, p;

	if (reg_ctl[1] & PLL_CTL1_BP)
		return parent_rate;

	n = FIELD_GET(PLL_CTL0_FBDIV, reg_ctl[0]);
	m = FIELD_GET(PLL_CTL0_INDIV, reg_ctl[0]);
	p = FIELD_GET(PLL_CTL1_OUTDIV, reg_ctl[1]);

	if (mode == PLL_MODE_INT) {
		pll_freq = (u64)parent_rate * n;
		div_u64(pll_freq, m * p);
	} else {
		x = FIELD_GET(PLL_CTL1_FRAC, reg_ctl[1]);
		/* 2 decimal places floating to integer (ex. 1.23 to 123) */
		n = n * 100 + ((x * 100) / FIELD_MAX(PLL_CTL1_FRAC));
		pll_freq = div_u64(parent_rate * n, 100 * m * p);
	}
	return pll_freq;
}

static int ma35d1_pll_find_closest(struct ma35d1_clk_pll *pll, unsigned long rate,
				   unsigned long parent_rate, u32 *reg_ctl,
				   unsigned long *freq)
{
	unsigned long min_diff = ULONG_MAX;
	int fbdiv_min, fbdiv_max;
	int p, m, n;

	*freq = 0;
	if (rate < PLL_FCLKO_MIN_FREQ || rate > PLL_FCLKO_MAX_FREQ)
		return -EINVAL;

	if (pll->mode == PLL_MODE_INT) {
		fbdiv_min = FBDIV_MIN;
		fbdiv_max = FBDIV_MAX;
	} else {
		fbdiv_min = FBDIV_FRAC_MIN;
		fbdiv_max = FBDIV_FRAC_MAX;
	}

	for (m = INDIV_MIN; m <= INDIV_MAX; m++) {
		for (n = fbdiv_min; n <= fbdiv_max; n++) {
			for (p = OUTDIV_MIN; p <= OUTDIV_MAX; p++) {
				unsigned long tmp, fout, fclk, diff;

				tmp = div_u64(parent_rate, m);
				if (tmp < PLL_FREF_M_MIN_FREQ ||
				    tmp > PLL_FREF_M_MAX_FREQ)
					continue; /* constrain */

				fclk = div_u64(parent_rate * n, m);
				/* for 2 decimal places */
				if (pll->mode != PLL_MODE_INT)
					fclk = div_u64(fclk, 100);

				if (fclk < PLL_FCLK_MIN_FREQ ||
				    fclk > PLL_FCLK_MAX_FREQ)
					continue; /* constrain */

				fout = div_u64(fclk, p);
				if (fout < PLL_FCLKO_MIN_FREQ ||
				    fout > PLL_FCLKO_MAX_FREQ)
					continue; /* constrain */

				diff = abs(rate - fout);
				if (diff < min_diff) {
					reg_ctl[0] = FIELD_PREP(PLL_CTL0_INDIV, m) |
						     FIELD_PREP(PLL_CTL0_FBDIV, n);
					reg_ctl[1] = FIELD_PREP(PLL_CTL1_OUTDIV, p);
					*freq = fout;
					min_diff = diff;
					if (min_diff == 0)
						break;
				}
			}
		}
	}
	if (*freq == 0)
		return -EINVAL; /* cannot find even one valid setting */
	return 0;
}

static int ma35d1_clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct ma35d1_clk_pll *pll = to_ma35d1_clk_pll(hw);
	u32 reg_ctl[3] = { 0 };
	unsigned long pll_freq;
	int ret;

	if (parent_rate < PLL_FREF_MIN_FREQ || parent_rate > PLL_FREF_MAX_FREQ)
		return -EINVAL;

	ret = ma35d1_pll_find_closest(pll, rate, parent_rate, reg_ctl, &pll_freq);
	if (ret != 0)
		return ret;

	switch (pll->mode) {
	case PLL_MODE_INT:
		reg_ctl[0] |= FIELD_PREP(PLL_CTL0_MODE, PLL_MODE_INT);
		break;
	case PLL_MODE_FRAC:
		reg_ctl[0] |= FIELD_PREP(PLL_CTL0_MODE, PLL_MODE_FRAC);
		break;
	case PLL_MODE_SS:
		reg_ctl[0] |= FIELD_PREP(PLL_CTL0_MODE, PLL_MODE_SS) |
			      FIELD_PREP(PLL_CTL0_SSRATE, PLL_SS_RATE);
		reg_ctl[2] = FIELD_PREP(PLL_CTL2_SLOPE, PLL_SLOPE);
		break;
	}
	reg_ctl[1] |= PLL_CTL1_PD;

	writel_relaxed(reg_ctl[0], pll->ctl0_base);
	writel_relaxed(reg_ctl[1], pll->ctl1_base);
	writel_relaxed(reg_ctl[2], pll->ctl2_base);
	return 0;
}

static unsigned long ma35d1_clk_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct ma35d1_clk_pll *pll = to_ma35d1_clk_pll(hw);
	u32 reg_ctl[3];
	unsigned long pll_freq;

	if (parent_rate < PLL_FREF_MIN_FREQ || parent_rate > PLL_FREF_MAX_FREQ)
		return 0;

	switch (pll->id) {
	case CAPLL:
		reg_ctl[0] = readl_relaxed(pll->ctl0_base);
		pll_freq = ma35d1_calc_smic_pll_freq(reg_ctl[0], parent_rate);
		return pll_freq;
	case DDRPLL:
	case APLL:
	case EPLL:
	case VPLL:
		reg_ctl[0] = readl_relaxed(pll->ctl0_base);
		reg_ctl[1] = readl_relaxed(pll->ctl1_base);
		pll_freq = ma35d1_calc_pll_freq(pll->mode, reg_ctl, parent_rate);
		return pll_freq;
	}
	return 0;
}

static int ma35d1_clk_pll_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	struct ma35d1_clk_pll *pll = to_ma35d1_clk_pll(hw);
	u32 reg_ctl[3] = { 0 };
	unsigned long pll_freq;
	long ret;

	if (req->best_parent_rate < PLL_FREF_MIN_FREQ || req->best_parent_rate > PLL_FREF_MAX_FREQ)
		return -EINVAL;

	ret = ma35d1_pll_find_closest(pll, req->rate, req->best_parent_rate,
				      reg_ctl, &pll_freq);
	if (ret < 0)
		return ret;

	switch (pll->id) {
	case CAPLL:
		reg_ctl[0] = readl_relaxed(pll->ctl0_base);
		pll_freq = ma35d1_calc_smic_pll_freq(reg_ctl[0], req->best_parent_rate);
		req->rate = pll_freq;

		return 0;
	case DDRPLL:
	case APLL:
	case EPLL:
	case VPLL:
		reg_ctl[0] = readl_relaxed(pll->ctl0_base);
		reg_ctl[1] = readl_relaxed(pll->ctl1_base);
		pll_freq = ma35d1_calc_pll_freq(pll->mode, reg_ctl, req->best_parent_rate);
		req->rate = pll_freq;

		return 0;
	}

	req->rate = 0;

	return 0;
}

static int ma35d1_clk_pll_is_prepared(struct clk_hw *hw)
{
	struct ma35d1_clk_pll *pll = to_ma35d1_clk_pll(hw);
	u32 val = readl_relaxed(pll->ctl1_base);

	return !(val & PLL_CTL1_PD);
}

static int ma35d1_clk_pll_prepare(struct clk_hw *hw)
{
	struct ma35d1_clk_pll *pll = to_ma35d1_clk_pll(hw);
	u32 val;

	val = readl_relaxed(pll->ctl1_base);
	val &= ~PLL_CTL1_PD;
	writel_relaxed(val, pll->ctl1_base);
	return 0;
}

static void ma35d1_clk_pll_unprepare(struct clk_hw *hw)
{
	struct ma35d1_clk_pll *pll = to_ma35d1_clk_pll(hw);
	u32 val;

	val = readl_relaxed(pll->ctl1_base);
	val |= PLL_CTL1_PD;
	writel_relaxed(val, pll->ctl1_base);
}

static const struct clk_ops ma35d1_clk_pll_ops = {
	.is_prepared = ma35d1_clk_pll_is_prepared,
	.prepare = ma35d1_clk_pll_prepare,
	.unprepare = ma35d1_clk_pll_unprepare,
	.set_rate = ma35d1_clk_pll_set_rate,
	.recalc_rate = ma35d1_clk_pll_recalc_rate,
	.determine_rate = ma35d1_clk_pll_determine_rate,
};

static const struct clk_ops ma35d1_clk_fixed_pll_ops = {
	.recalc_rate = ma35d1_clk_pll_recalc_rate,
	.determine_rate = ma35d1_clk_pll_determine_rate,
};

struct clk_hw *ma35d1_reg_clk_pll(struct device *dev, u32 id, u8 u8mode, const char *name,
				  struct clk_hw *parent_hw, void __iomem *base)
{
	struct clk_parent_data pdata = { .index = 0 };
	struct clk_init_data init = {};
	struct ma35d1_clk_pll *pll;
	struct clk_hw *hw;
	int ret;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->id = id;
	pll->mode = u8mode;
	pll->ctl0_base = base + REG_PLL_CTL0_OFFSET;
	pll->ctl1_base = base + REG_PLL_CTL1_OFFSET;
	pll->ctl2_base = base + REG_PLL_CTL2_OFFSET;

	init.name = name;
	init.flags = 0;
	pdata.hw = parent_hw;
	init.parent_data = &pdata;
	init.num_parents = 1;

	if (id == CAPLL || id == DDRPLL)
		init.ops = &ma35d1_clk_fixed_pll_ops;
	else
		init.ops = &ma35d1_clk_pll_ops;

	pll->hw.init = &init;
	hw = &pll->hw;

	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);
	return hw;
}
EXPORT_SYMBOL_GPL(ma35d1_reg_clk_pll);
