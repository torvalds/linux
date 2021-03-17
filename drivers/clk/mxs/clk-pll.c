/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "clk.h"

/**
 * struct clk_pll - mxs pll clock
 * @hw: clk_hw for the pll
 * @base: base address of the pll
 * @power: the shift of power bit
 * @rate: the clock rate of the pll
 *
 * The mxs pll is a fixed rate clock with power and gate control,
 * and the shift of gate bit is always 31.
 */
struct clk_pll {
	struct clk_hw hw;
	void __iomem *base;
	u8 power;
	unsigned long rate;
};

#define to_clk_pll(_hw) container_of(_hw, struct clk_pll, hw)

static int clk_pll_prepare(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);

	writel_relaxed(1 << pll->power, pll->base + SET);

	udelay(10);

	return 0;
}

static void clk_pll_unprepare(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);

	writel_relaxed(1 << pll->power, pll->base + CLR);
}

static int clk_pll_enable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);

	writel_relaxed(1 << 31, pll->base + CLR);

	return 0;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);

	writel_relaxed(1 << 31, pll->base + SET);
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);

	return pll->rate;
}

static const struct clk_ops clk_pll_ops = {
	.prepare = clk_pll_prepare,
	.unprepare = clk_pll_unprepare,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
	.recalc_rate = clk_pll_recalc_rate,
};

struct clk *mxs_clk_pll(const char *name, const char *parent_name,
			void __iomem *base, u8 power, unsigned long rate)
{
	struct clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_pll_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	pll->base = base;
	pll->rate = rate;
	pll->power = power;
	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
