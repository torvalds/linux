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
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "clk.h"

/**
 * struct clk_frac - mxs fractional divider clock
 * @hw: clk_hw for the fractional divider clock
 * @reg: register address
 * @shift: the divider bit shift
 * @width: the divider bit width
 * @busy: busy bit shift
 *
 * The clock is an adjustable fractional divider with a busy bit to wait
 * when the divider is adjusted.
 */
struct clk_frac {
	struct clk_hw hw;
	void __iomem *reg;
	u8 shift;
	u8 width;
	u8 busy;
};

#define to_clk_frac(_hw) container_of(_hw, struct clk_frac, hw)

static unsigned long clk_frac_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_frac *frac = to_clk_frac(hw);
	u32 div;
	u64 tmp_rate;

	div = readl_relaxed(frac->reg) >> frac->shift;
	div &= (1 << frac->width) - 1;

	tmp_rate = (u64)parent_rate * div;
	return tmp_rate >> frac->width;
}

static long clk_frac_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_frac *frac = to_clk_frac(hw);
	unsigned long parent_rate = *prate;
	u32 div;
	u64 tmp, tmp_rate, result;

	if (rate > parent_rate)
		return -EINVAL;

	tmp = rate;
	tmp <<= frac->width;
	do_div(tmp, parent_rate);
	div = tmp;

	if (!div)
		return -EINVAL;

	tmp_rate = (u64)parent_rate * div;
	result = tmp_rate >> frac->width;
	if ((result << frac->width) < tmp_rate)
		result += 1;
	return result;
}

static int clk_frac_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct clk_frac *frac = to_clk_frac(hw);
	unsigned long flags;
	u32 div, val;
	u64 tmp;

	if (rate > parent_rate)
		return -EINVAL;

	tmp = rate;
	tmp <<= frac->width;
	do_div(tmp, parent_rate);
	div = tmp;

	if (!div)
		return -EINVAL;

	spin_lock_irqsave(&mxs_lock, flags);

	val = readl_relaxed(frac->reg);
	val &= ~(((1 << frac->width) - 1) << frac->shift);
	val |= div << frac->shift;
	writel_relaxed(val, frac->reg);

	spin_unlock_irqrestore(&mxs_lock, flags);

	return mxs_clk_wait(frac->reg, frac->busy);
}

static const struct clk_ops clk_frac_ops = {
	.recalc_rate = clk_frac_recalc_rate,
	.round_rate = clk_frac_round_rate,
	.set_rate = clk_frac_set_rate,
};

struct clk *mxs_clk_frac(const char *name, const char *parent_name,
			 void __iomem *reg, u8 shift, u8 width, u8 busy)
{
	struct clk_frac *frac;
	struct clk *clk;
	struct clk_init_data init = {};

	frac = kzalloc(sizeof(*frac), GFP_KERNEL);
	if (!frac)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_frac_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	frac->reg = reg;
	frac->shift = shift;
	frac->width = width;
	frac->busy = busy;
	frac->hw.init = &init;

	clk = clk_register(NULL, &frac->hw);
	if (IS_ERR(clk))
		kfree(frac);

	return clk;
}
