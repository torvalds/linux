/*
 * Copyright (C) 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright (C) 2011 Richard Zhao, Linaro <richard.zhao@linaro.org>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Adjustable divider clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>

/*
 * DOC: basic adjustable divider clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = parent->rate / divisor
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_divider(_hw) container_of(_hw, struct clk_divider, hw)

#define div_mask(d)	((1 << (d->width)) - 1)

static unsigned long clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int div;

	div = readl(divider->reg) >> divider->shift;
	div &= div_mask(divider);

	if (!(divider->flags & CLK_DIVIDER_ONE_BASED))
		div++;

	return parent_rate / div;
}
EXPORT_SYMBOL_GPL(clk_divider_recalc_rate);

/*
 * The reverse of DIV_ROUND_UP: The maximum number which
 * divided by m is r
 */
#define MULT_ROUND_UP(r, m) ((r) * (m) + (m) - 1)

static int clk_divider_bestdiv(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int i, bestdiv = 0;
	unsigned long parent_rate, best = 0, now, maxdiv;

	if (!rate)
		rate = 1;

	maxdiv = (1 << divider->width);

	if (divider->flags & CLK_DIVIDER_ONE_BASED)
		maxdiv--;

	if (!best_parent_rate) {
		parent_rate = __clk_get_rate(__clk_get_parent(hw->clk));
		bestdiv = DIV_ROUND_UP(parent_rate, rate);
		bestdiv = bestdiv == 0 ? 1 : bestdiv;
		bestdiv = bestdiv > maxdiv ? maxdiv : bestdiv;
		return bestdiv;
	}

	/*
	 * The maximum divider we can use without overflowing
	 * unsigned long in rate * i below
	 */
	maxdiv = min(ULONG_MAX / rate, maxdiv);

	for (i = 1; i <= maxdiv; i++) {
		parent_rate = __clk_round_rate(__clk_get_parent(hw->clk),
				MULT_ROUND_UP(rate, i));
		now = parent_rate / i;
		if (now <= rate && now > best) {
			bestdiv = i;
			best = now;
			*best_parent_rate = parent_rate;
		}
	}

	if (!bestdiv) {
		bestdiv = (1 << divider->width);
		if (divider->flags & CLK_DIVIDER_ONE_BASED)
			bestdiv--;
		*best_parent_rate = __clk_round_rate(__clk_get_parent(hw->clk), 1);
	}

	return bestdiv;
}

static long clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	int div;
	div = clk_divider_bestdiv(hw, rate, prate);

	if (prate)
		return *prate / div;
	else {
		unsigned long r;
		r = __clk_get_rate(__clk_get_parent(hw->clk));
		return r / div;
	}
}
EXPORT_SYMBOL_GPL(clk_divider_round_rate);

static int clk_divider_set_rate(struct clk_hw *hw, unsigned long rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int div;
	unsigned long flags = 0;
	u32 val;

	div = __clk_get_rate(__clk_get_parent(hw->clk)) / rate;

	if (!(divider->flags & CLK_DIVIDER_ONE_BASED))
		div--;

	if (div > div_mask(divider))
		div = div_mask(divider);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);

	val = readl(divider->reg);
	val &= ~(div_mask(divider) << divider->shift);
	val |= div << divider->shift;
	writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(clk_divider_set_rate);

struct clk_ops clk_divider_ops = {
	.recalc_rate = clk_divider_recalc_rate,
	.round_rate = clk_divider_round_rate,
	.set_rate = clk_divider_set_rate,
};
EXPORT_SYMBOL_GPL(clk_divider_ops);

struct clk *clk_register_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, spinlock_t *lock)
{
	struct clk_divider *div;
	struct clk *clk;

	div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);

	if (!div) {
		pr_err("%s: could not allocate divider clk\n", __func__);
		return NULL;
	}

	/* struct clk_divider assignments */
	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;

	if (parent_name) {
		div->parent[0] = kstrdup(parent_name, GFP_KERNEL);
		if (!div->parent[0])
			goto out;
	}

	clk = clk_register(dev, name,
			&clk_divider_ops, &div->hw,
			div->parent,
			(parent_name ? 1 : 0),
			flags);
	if (clk)
		return clk;

out:
	kfree(div->parent[0]);
	kfree(div);

	return NULL;
}
