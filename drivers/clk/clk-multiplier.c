/*
 * Copyright (C) 2015 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>

#define to_clk_multiplier(_hw) container_of(_hw, struct clk_multiplier, hw)

static unsigned long __get_mult(struct clk_multiplier *mult,
				unsigned long rate,
				unsigned long parent_rate)
{
	if (mult->flags & CLK_MULTIPLIER_ROUND_CLOSEST)
		return DIV_ROUND_CLOSEST(rate, parent_rate);

	return rate / parent_rate;
}

static unsigned long clk_multiplier_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_multiplier *mult = to_clk_multiplier(hw);
	unsigned long val;

	val = clk_readl(mult->reg) >> mult->shift;
	val &= GENMASK(mult->width - 1, 0);

	if (!val && mult->flags & CLK_MULTIPLIER_ZERO_BYPASS)
		val = 1;

	return parent_rate * val;
}

static bool __is_best_rate(unsigned long rate, unsigned long new,
			   unsigned long best, unsigned long flags)
{
	if (flags & CLK_MULTIPLIER_ROUND_CLOSEST)
		return abs(rate - new) < abs(rate - best);

	return new >= rate && new < best;
}

static unsigned long __bestmult(struct clk_hw *hw, unsigned long rate,
				unsigned long *best_parent_rate,
				u8 width, unsigned long flags)
{
	unsigned long orig_parent_rate = *best_parent_rate;
	unsigned long parent_rate, current_rate, best_rate = ~0;
	unsigned int i, bestmult = 0;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT))
		return rate / *best_parent_rate;

	for (i = 1; i < ((1 << width) - 1); i++) {
		if (rate == orig_parent_rate * i) {
			/*
			 * This is the best case for us if we have a
			 * perfect match without changing the parent
			 * rate.
			 */
			*best_parent_rate = orig_parent_rate;
			return i;
		}

		parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
						rate / i);
		current_rate = parent_rate * i;

		if (__is_best_rate(rate, current_rate, best_rate, flags)) {
			bestmult = i;
			best_rate = current_rate;
			*best_parent_rate = parent_rate;
		}
	}

	return bestmult;
}

static long clk_multiplier_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate)
{
	struct clk_multiplier *mult = to_clk_multiplier(hw);
	unsigned long factor = __bestmult(hw, rate, parent_rate,
					  mult->width, mult->flags);

	return *parent_rate * factor;
}

static int clk_multiplier_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_multiplier *mult = to_clk_multiplier(hw);
	unsigned long factor = __get_mult(mult, rate, parent_rate);
	unsigned long flags = 0;
	unsigned long val;

	if (mult->lock)
		spin_lock_irqsave(mult->lock, flags);
	else
		__acquire(mult->lock);

	val = clk_readl(mult->reg);
	val &= ~GENMASK(mult->width + mult->shift - 1, mult->shift);
	val |= factor << mult->shift;
	clk_writel(val, mult->reg);

	if (mult->lock)
		spin_unlock_irqrestore(mult->lock, flags);
	else
		__release(mult->lock);

	return 0;
}

const struct clk_ops clk_multiplier_ops = {
	.recalc_rate	= clk_multiplier_recalc_rate,
	.round_rate	= clk_multiplier_round_rate,
	.set_rate	= clk_multiplier_set_rate,
};
EXPORT_SYMBOL_GPL(clk_multiplier_ops);
