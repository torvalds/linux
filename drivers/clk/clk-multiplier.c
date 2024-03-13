// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>

static inline u32 clk_mult_readl(struct clk_multiplier *mult)
{
	if (mult->flags & CLK_MULTIPLIER_BIG_ENDIAN)
		return ioread32be(mult->reg);

	return readl(mult->reg);
}

static inline void clk_mult_writel(struct clk_multiplier *mult, u32 val)
{
	if (mult->flags & CLK_MULTIPLIER_BIG_ENDIAN)
		iowrite32be(val, mult->reg);
	else
		writel(val, mult->reg);
}

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

	val = clk_mult_readl(mult) >> mult->shift;
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
	struct clk_multiplier *mult = to_clk_multiplier(hw);
	unsigned long orig_parent_rate = *best_parent_rate;
	unsigned long parent_rate, current_rate, best_rate = ~0;
	unsigned int i, bestmult = 0;
	unsigned int maxmult = (1 << width) - 1;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		bestmult = rate / orig_parent_rate;

		/* Make sure we don't end up with a 0 multiplier */
		if ((bestmult == 0) &&
		    !(mult->flags & CLK_MULTIPLIER_ZERO_BYPASS))
			bestmult = 1;

		/* Make sure we don't overflow the multiplier */
		if (bestmult > maxmult)
			bestmult = maxmult;

		return bestmult;
	}

	for (i = 1; i < maxmult; i++) {
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

	val = clk_mult_readl(mult);
	val &= ~GENMASK(mult->width + mult->shift - 1, mult->shift);
	val |= factor << mult->shift;
	clk_mult_writel(mult, val);

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
