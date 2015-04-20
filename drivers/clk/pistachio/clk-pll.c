/*
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "clk.h"

#define PLL_STATUS			0x0
#define PLL_STATUS_LOCK			BIT(0)

#define PLL_CTRL1			0x4
#define PLL_CTRL1_REFDIV_SHIFT		0
#define PLL_CTRL1_REFDIV_MASK		0x3f
#define PLL_CTRL1_FBDIV_SHIFT		6
#define PLL_CTRL1_FBDIV_MASK		0xfff
#define PLL_INT_CTRL1_POSTDIV1_SHIFT	18
#define PLL_INT_CTRL1_POSTDIV1_MASK	0x7
#define PLL_INT_CTRL1_POSTDIV2_SHIFT	21
#define PLL_INT_CTRL1_POSTDIV2_MASK	0x7
#define PLL_INT_CTRL1_PD		BIT(24)
#define PLL_INT_CTRL1_DSMPD		BIT(25)
#define PLL_INT_CTRL1_FOUTPOSTDIVPD	BIT(26)
#define PLL_INT_CTRL1_FOUTVCOPD		BIT(27)

#define PLL_CTRL2			0x8
#define PLL_FRAC_CTRL2_FRAC_SHIFT	0
#define PLL_FRAC_CTRL2_FRAC_MASK	0xffffff
#define PLL_FRAC_CTRL2_POSTDIV1_SHIFT	24
#define PLL_FRAC_CTRL2_POSTDIV1_MASK	0x7
#define PLL_FRAC_CTRL2_POSTDIV2_SHIFT	27
#define PLL_FRAC_CTRL2_POSTDIV2_MASK	0x7
#define PLL_INT_CTRL2_BYPASS		BIT(28)

#define PLL_CTRL3			0xc
#define PLL_FRAC_CTRL3_PD		BIT(0)
#define PLL_FRAC_CTRL3_DACPD		BIT(1)
#define PLL_FRAC_CTRL3_DSMPD		BIT(2)
#define PLL_FRAC_CTRL3_FOUTPOSTDIVPD	BIT(3)
#define PLL_FRAC_CTRL3_FOUT4PHASEPD	BIT(4)
#define PLL_FRAC_CTRL3_FOUTVCOPD	BIT(5)

#define PLL_CTRL4			0x10
#define PLL_FRAC_CTRL4_BYPASS		BIT(28)

struct pistachio_clk_pll {
	struct clk_hw hw;
	void __iomem *base;
	struct pistachio_pll_rate_table *rates;
	unsigned int nr_rates;
};

static inline u32 pll_readl(struct pistachio_clk_pll *pll, u32 reg)
{
	return readl(pll->base + reg);
}

static inline void pll_writel(struct pistachio_clk_pll *pll, u32 val, u32 reg)
{
	writel(val, pll->base + reg);
}

static inline u32 do_div_round_closest(u64 dividend, u32 divisor)
{
	dividend += divisor / 2;
	do_div(dividend, divisor);

	return dividend;
}

static inline struct pistachio_clk_pll *to_pistachio_pll(struct clk_hw *hw)
{
	return container_of(hw, struct pistachio_clk_pll, hw);
}

static struct pistachio_pll_rate_table *
pll_get_params(struct pistachio_clk_pll *pll, unsigned long fref,
	       unsigned long fout)
{
	unsigned int i;

	for (i = 0; i < pll->nr_rates; i++) {
		if (pll->rates[i].fref == fref && pll->rates[i].fout == fout)
			return &pll->rates[i];
	}

	return NULL;
}

static long pll_round_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long *parent_rate)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	unsigned int i;

	for (i = 0; i < pll->nr_rates; i++) {
		if (i > 0 && pll->rates[i].fref == *parent_rate &&
		    pll->rates[i].fout <= rate)
			return pll->rates[i - 1].fout;
	}

	return pll->rates[0].fout;
}

static int pll_gf40lp_frac_enable(struct clk_hw *hw)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	u32 val;

	val = pll_readl(pll, PLL_CTRL3);
	val &= ~(PLL_FRAC_CTRL3_PD | PLL_FRAC_CTRL3_DACPD |
		 PLL_FRAC_CTRL3_DSMPD | PLL_FRAC_CTRL3_FOUTPOSTDIVPD |
		 PLL_FRAC_CTRL3_FOUT4PHASEPD | PLL_FRAC_CTRL3_FOUTVCOPD);
	pll_writel(pll, val, PLL_CTRL3);

	val = pll_readl(pll, PLL_CTRL4);
	val &= ~PLL_FRAC_CTRL4_BYPASS;
	pll_writel(pll, val, PLL_CTRL4);

	return 0;
}

static void pll_gf40lp_frac_disable(struct clk_hw *hw)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	u32 val;

	val = pll_readl(pll, PLL_CTRL3);
	val |= PLL_FRAC_CTRL3_PD;
	pll_writel(pll, val, PLL_CTRL3);
}

static int pll_gf40lp_frac_is_enabled(struct clk_hw *hw)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);

	return !(pll_readl(pll, PLL_CTRL3) & PLL_FRAC_CTRL3_PD);
}

static int pll_gf40lp_frac_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	struct pistachio_pll_rate_table *params;
	bool was_enabled;
	u32 val;

	params = pll_get_params(pll, parent_rate, rate);
	if (!params)
		return -EINVAL;

	was_enabled = pll_gf40lp_frac_is_enabled(hw);
	if (!was_enabled)
		pll_gf40lp_frac_enable(hw);

	val = pll_readl(pll, PLL_CTRL1);
	val &= ~((PLL_CTRL1_REFDIV_MASK << PLL_CTRL1_REFDIV_SHIFT) |
		 (PLL_CTRL1_FBDIV_MASK << PLL_CTRL1_FBDIV_SHIFT));
	val |= (params->refdiv << PLL_CTRL1_REFDIV_SHIFT) |
		(params->fbdiv << PLL_CTRL1_FBDIV_SHIFT);
	pll_writel(pll, val, PLL_CTRL1);

	val = pll_readl(pll, PLL_CTRL2);
	val &= ~((PLL_FRAC_CTRL2_FRAC_MASK << PLL_FRAC_CTRL2_FRAC_SHIFT) |
		 (PLL_FRAC_CTRL2_POSTDIV1_MASK <<
		  PLL_FRAC_CTRL2_POSTDIV1_SHIFT) |
		 (PLL_FRAC_CTRL2_POSTDIV2_MASK <<
		  PLL_FRAC_CTRL2_POSTDIV2_SHIFT));
	val |= (params->frac << PLL_FRAC_CTRL2_FRAC_SHIFT) |
		(params->postdiv1 << PLL_FRAC_CTRL2_POSTDIV1_SHIFT) |
		(params->postdiv2 << PLL_FRAC_CTRL2_POSTDIV2_SHIFT);
	pll_writel(pll, val, PLL_CTRL2);

	while (!(pll_readl(pll, PLL_STATUS) & PLL_STATUS_LOCK))
		cpu_relax();

	if (!was_enabled)
		pll_gf40lp_frac_disable(hw);

	return 0;
}

static unsigned long pll_gf40lp_frac_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	u32 val, prediv, fbdiv, frac, postdiv1, postdiv2;
	u64 rate = parent_rate;

	val = pll_readl(pll, PLL_CTRL1);
	prediv = (val >> PLL_CTRL1_REFDIV_SHIFT) & PLL_CTRL1_REFDIV_MASK;
	fbdiv = (val >> PLL_CTRL1_FBDIV_SHIFT) & PLL_CTRL1_FBDIV_MASK;

	val = pll_readl(pll, PLL_CTRL2);
	postdiv1 = (val >> PLL_FRAC_CTRL2_POSTDIV1_SHIFT) &
		PLL_FRAC_CTRL2_POSTDIV1_MASK;
	postdiv2 = (val >> PLL_FRAC_CTRL2_POSTDIV2_SHIFT) &
		PLL_FRAC_CTRL2_POSTDIV2_MASK;
	frac = (val >> PLL_FRAC_CTRL2_FRAC_SHIFT) & PLL_FRAC_CTRL2_FRAC_MASK;

	rate *= (fbdiv << 24) + frac;
	rate = do_div_round_closest(rate, (prediv * postdiv1 * postdiv2) << 24);

	return rate;
}

static struct clk_ops pll_gf40lp_frac_ops = {
	.enable = pll_gf40lp_frac_enable,
	.disable = pll_gf40lp_frac_disable,
	.is_enabled = pll_gf40lp_frac_is_enabled,
	.recalc_rate = pll_gf40lp_frac_recalc_rate,
	.round_rate = pll_round_rate,
	.set_rate = pll_gf40lp_frac_set_rate,
};

static struct clk_ops pll_gf40lp_frac_fixed_ops = {
	.enable = pll_gf40lp_frac_enable,
	.disable = pll_gf40lp_frac_disable,
	.is_enabled = pll_gf40lp_frac_is_enabled,
	.recalc_rate = pll_gf40lp_frac_recalc_rate,
};

static int pll_gf40lp_laint_enable(struct clk_hw *hw)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	u32 val;

	val = pll_readl(pll, PLL_CTRL1);
	val &= ~(PLL_INT_CTRL1_PD | PLL_INT_CTRL1_DSMPD |
		 PLL_INT_CTRL1_FOUTPOSTDIVPD | PLL_INT_CTRL1_FOUTVCOPD);
	pll_writel(pll, val, PLL_CTRL1);

	val = pll_readl(pll, PLL_CTRL2);
	val &= ~PLL_INT_CTRL2_BYPASS;
	pll_writel(pll, val, PLL_CTRL2);

	return 0;
}

static void pll_gf40lp_laint_disable(struct clk_hw *hw)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	u32 val;

	val = pll_readl(pll, PLL_CTRL1);
	val |= PLL_INT_CTRL1_PD;
	pll_writel(pll, val, PLL_CTRL1);
}

static int pll_gf40lp_laint_is_enabled(struct clk_hw *hw)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);

	return !(pll_readl(pll, PLL_CTRL1) & PLL_INT_CTRL1_PD);
}

static int pll_gf40lp_laint_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	struct pistachio_pll_rate_table *params;
	bool was_enabled;
	u32 val;

	params = pll_get_params(pll, parent_rate, rate);
	if (!params)
		return -EINVAL;

	was_enabled = pll_gf40lp_laint_is_enabled(hw);
	if (!was_enabled)
		pll_gf40lp_laint_enable(hw);

	val = pll_readl(pll, PLL_CTRL1);
	val &= ~((PLL_CTRL1_REFDIV_MASK << PLL_CTRL1_REFDIV_SHIFT) |
		 (PLL_CTRL1_FBDIV_MASK << PLL_CTRL1_FBDIV_SHIFT) |
		 (PLL_INT_CTRL1_POSTDIV1_MASK << PLL_INT_CTRL1_POSTDIV1_SHIFT) |
		 (PLL_INT_CTRL1_POSTDIV2_MASK << PLL_INT_CTRL1_POSTDIV2_SHIFT));
	val |= (params->refdiv << PLL_CTRL1_REFDIV_SHIFT) |
		(params->fbdiv << PLL_CTRL1_FBDIV_SHIFT) |
		(params->postdiv1 << PLL_INT_CTRL1_POSTDIV1_SHIFT) |
		(params->postdiv2 << PLL_INT_CTRL1_POSTDIV2_SHIFT);
	pll_writel(pll, val, PLL_CTRL1);

	while (!(pll_readl(pll, PLL_STATUS) & PLL_STATUS_LOCK))
		cpu_relax();

	if (!was_enabled)
		pll_gf40lp_laint_disable(hw);

	return 0;
}

static unsigned long pll_gf40lp_laint_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct pistachio_clk_pll *pll = to_pistachio_pll(hw);
	u32 val, prediv, fbdiv, postdiv1, postdiv2;
	u64 rate = parent_rate;

	val = pll_readl(pll, PLL_CTRL1);
	prediv = (val >> PLL_CTRL1_REFDIV_SHIFT) & PLL_CTRL1_REFDIV_MASK;
	fbdiv = (val >> PLL_CTRL1_FBDIV_SHIFT) & PLL_CTRL1_FBDIV_MASK;
	postdiv1 = (val >> PLL_INT_CTRL1_POSTDIV1_SHIFT) &
		PLL_INT_CTRL1_POSTDIV1_MASK;
	postdiv2 = (val >> PLL_INT_CTRL1_POSTDIV2_SHIFT) &
		PLL_INT_CTRL1_POSTDIV2_MASK;

	rate *= fbdiv;
	rate = do_div_round_closest(rate, prediv * postdiv1 * postdiv2);

	return rate;
}

static struct clk_ops pll_gf40lp_laint_ops = {
	.enable = pll_gf40lp_laint_enable,
	.disable = pll_gf40lp_laint_disable,
	.is_enabled = pll_gf40lp_laint_is_enabled,
	.recalc_rate = pll_gf40lp_laint_recalc_rate,
	.round_rate = pll_round_rate,
	.set_rate = pll_gf40lp_laint_set_rate,
};

static struct clk_ops pll_gf40lp_laint_fixed_ops = {
	.enable = pll_gf40lp_laint_enable,
	.disable = pll_gf40lp_laint_disable,
	.is_enabled = pll_gf40lp_laint_is_enabled,
	.recalc_rate = pll_gf40lp_laint_recalc_rate,
};

static struct clk *pll_register(const char *name, const char *parent_name,
				unsigned long flags, void __iomem *base,
				enum pistachio_pll_type type,
				struct pistachio_pll_rate_table *rates,
				unsigned int nr_rates)
{
	struct pistachio_clk_pll *pll;
	struct clk_init_data init;
	struct clk *clk;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags | CLK_GET_RATE_NOCACHE;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	switch (type) {
	case PLL_GF40LP_FRAC:
		if (rates)
			init.ops = &pll_gf40lp_frac_ops;
		else
			init.ops = &pll_gf40lp_frac_fixed_ops;
		break;
	case PLL_GF40LP_LAINT:
		if (rates)
			init.ops = &pll_gf40lp_laint_ops;
		else
			init.ops = &pll_gf40lp_laint_fixed_ops;
		break;
	default:
		pr_err("Unrecognized PLL type %u\n", type);
		kfree(pll);
		return ERR_PTR(-EINVAL);
	}

	pll->hw.init = &init;
	pll->base = base;
	pll->rates = rates;
	pll->nr_rates = nr_rates;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

void pistachio_clk_register_pll(struct pistachio_clk_provider *p,
				struct pistachio_pll *pll,
				unsigned int num)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < num; i++) {
		clk = pll_register(pll[i].name, pll[i].parent,
				   0, p->base + pll[i].reg_base,
				   pll[i].type, pll[i].rates,
				   pll[i].nr_rates);
		p->clk_data.clks[pll[i].id] = clk;
	}
}
