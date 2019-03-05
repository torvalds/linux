// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright 2018 NXP.
 *
 * This driver supports the SCCG plls found in the imx8m SOCs
 *
 * Documentation for this SCCG pll can be found at:
 *   https://www.nxp.com/docs/en/reference-manual/IMX8MDQLQRM.pdf#page=834
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/bitfield.h>

#include "clk.h"

/* PLL CFGs */
#define PLL_CFG0		0x0
#define PLL_CFG1		0x4
#define PLL_CFG2		0x8

#define PLL_DIVF1_MASK		GENMASK(18, 13)
#define PLL_DIVF2_MASK		GENMASK(12, 7)
#define PLL_DIVR1_MASK		GENMASK(27, 25)
#define PLL_DIVR2_MASK		GENMASK(24, 19)
#define PLL_REF_MASK		GENMASK(2, 0)

#define PLL_LOCK_MASK		BIT(31)
#define PLL_PD_MASK		BIT(7)

#define OSC_25M			25000000
#define OSC_27M			27000000

#define PLL_SCCG_LOCK_TIMEOUT	70

struct clk_sccg_pll {
	struct clk_hw	hw;
	void __iomem	*base;
};

#define to_clk_sccg_pll(_hw) container_of(_hw, struct clk_sccg_pll, hw)

static int clk_pll_wait_lock(struct clk_sccg_pll *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base, val, val & PLL_LOCK_MASK, 0,
					PLL_SCCG_LOCK_TIMEOUT);
}

static int clk_pll1_is_prepared(struct clk_hw *hw)
{
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + PLL_CFG0);
	return (val & PLL_PD_MASK) ? 0 : 1;
}

static unsigned long clk_pll1_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);
	u32 val, divf;

	val = readl_relaxed(pll->base + PLL_CFG2);
	divf = FIELD_GET(PLL_DIVF1_MASK, val);

	return parent_rate * 2 * (divf + 1);
}

static long clk_pll1_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	u32 div;

	if (!parent_rate)
		return 0;

	div = rate / (parent_rate * 2);

	return parent_rate * div * 2;
}

static int clk_pll1_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);
	u32 val;
	u32 divf;

	if (!parent_rate)
		return -EINVAL;

	divf = rate / (parent_rate * 2);

	val = readl_relaxed(pll->base + PLL_CFG2);
	val &= ~PLL_DIVF1_MASK;
	val |= FIELD_PREP(PLL_DIVF1_MASK, divf - 1);
	writel_relaxed(val, pll->base + PLL_CFG2);

	return clk_pll_wait_lock(pll);
}

static int clk_pll1_prepare(struct clk_hw *hw)
{
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + PLL_CFG0);
	val &= ~PLL_PD_MASK;
	writel_relaxed(val, pll->base + PLL_CFG0);

	return clk_pll_wait_lock(pll);
}

static void clk_pll1_unprepare(struct clk_hw *hw)
{
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + PLL_CFG0);
	val |= PLL_PD_MASK;
	writel_relaxed(val, pll->base + PLL_CFG0);

}

static unsigned long clk_pll2_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);
	u32 val, ref, divr1, divf1, divr2, divf2;
	u64 temp64;

	val = readl_relaxed(pll->base + PLL_CFG0);
	switch (FIELD_GET(PLL_REF_MASK, val)) {
	case 0:
		ref = OSC_25M;
		break;
	case 1:
		ref = OSC_27M;
		break;
	default:
		ref = OSC_25M;
		break;
	}

	val = readl_relaxed(pll->base + PLL_CFG2);
	divr1 = FIELD_GET(PLL_DIVR1_MASK, val);
	divr2 = FIELD_GET(PLL_DIVR2_MASK, val);
	divf1 = FIELD_GET(PLL_DIVF1_MASK, val);
	divf2 = FIELD_GET(PLL_DIVF2_MASK, val);

	temp64 = ref * 2;
	temp64 *= (divf1 + 1) * (divf2 + 1);

	do_div(temp64, (divr1 + 1) * (divr2 + 1));

	return temp64;
}

static long clk_pll2_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	u32 div;
	unsigned long parent_rate = *prate;

	if (!parent_rate)
		return 0;

	div = rate / parent_rate;

	return parent_rate * div;
}

static int clk_pll2_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	u32 val;
	u32 divf;
	struct clk_sccg_pll *pll = to_clk_sccg_pll(hw);

	if (!parent_rate)
		return -EINVAL;

	divf = rate / parent_rate;

	val = readl_relaxed(pll->base + PLL_CFG2);
	val &= ~PLL_DIVF2_MASK;
	val |= FIELD_PREP(PLL_DIVF2_MASK, divf - 1);
	writel_relaxed(val, pll->base + PLL_CFG2);

	return clk_pll_wait_lock(pll);
}

static const struct clk_ops clk_sccg_pll1_ops = {
	.is_prepared	= clk_pll1_is_prepared,
	.recalc_rate	= clk_pll1_recalc_rate,
	.round_rate	= clk_pll1_round_rate,
	.set_rate	= clk_pll1_set_rate,
};

static const struct clk_ops clk_sccg_pll2_ops = {
	.prepare	= clk_pll1_prepare,
	.unprepare	= clk_pll1_unprepare,
	.recalc_rate	= clk_pll2_recalc_rate,
	.round_rate	= clk_pll2_round_rate,
	.set_rate	= clk_pll2_set_rate,
};

struct clk *imx_clk_sccg_pll(const char *name,
				const char *parent_name,
				void __iomem *base,
				enum imx_sccg_pll_type pll_type)
{
	struct clk_sccg_pll *pll;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	switch (pll_type) {
	case SCCG_PLL1:
		init.ops = &clk_sccg_pll1_ops;
		break;
	case SCCG_PLL2:
		init.ops = &clk_sccg_pll2_ops;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll->base = base;
	pll->hw.init = &init;

	hw = &pll->hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}

	return hw->clk;
}
