// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/slab.h>
#include <linux/clk-provider.h>
#include "clk.h"

#define div_mask(width)	((1 << (width)) - 1)

static bool _is_best_half_div(unsigned long rate, unsigned long now,
			      unsigned long best, unsigned long flags)
{
	if (flags & CLK_DIVIDER_ROUND_CLOSEST)
		return abs(rate - now) < abs(rate - best);

	return now <= rate && now > best;
}

static unsigned long clk_half_divider_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int val;

	val = readl(divider->reg) >> divider->shift;
	val &= div_mask(divider->width);
	val = val * 2 + 3;

	return DIV_ROUND_UP_ULL(((u64)parent_rate * 2), val);
}

static int clk_half_divider_bestdiv(struct clk_hw *hw, unsigned long rate,
				    unsigned long *best_parent_rate, u8 width,
				    unsigned long flags)
{
	unsigned int i, bestdiv = 0;
	unsigned long parent_rate, best = 0, now, maxdiv;
	unsigned long parent_rate_saved = *best_parent_rate;

	if (!rate)
		rate = 1;

	maxdiv = div_mask(width);

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		parent_rate = *best_parent_rate;
		bestdiv = DIV_ROUND_UP_ULL(((u64)parent_rate * 2), rate);
		if (bestdiv < 3)
			bestdiv = 0;
		else
			bestdiv = (bestdiv - 3) / 2;
		bestdiv = bestdiv > maxdiv ? maxdiv : bestdiv;
		return bestdiv;
	}

	/*
	 * The maximum divider we can use without overflowing
	 * unsigned long in rate * i below
	 */
	maxdiv = min(ULONG_MAX / rate, maxdiv);

	for (i = 0; i <= maxdiv; i++) {
		if (((u64)rate * (i * 2 + 3)) == ((u64)parent_rate_saved * 2)) {
			/*
			 * It's the most ideal case if the requested rate can be
			 * divided from parent clock without needing to change
			 * parent rate, so return the divider immediately.
			 */
			*best_parent_rate = parent_rate_saved;
			return i;
		}
		parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
						((u64)rate * (i * 2 + 3)) / 2);
		now = DIV_ROUND_UP_ULL(((u64)parent_rate * 2),
				       (i * 2 + 3));

		if (_is_best_half_div(rate, now, best, flags)) {
			bestdiv = i;
			best = now;
			*best_parent_rate = parent_rate;
		}
	}

	if (!bestdiv) {
		bestdiv = div_mask(width);
		*best_parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw), 1);
	}

	return bestdiv;
}

static long clk_half_divider_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int div;

	div = clk_half_divider_bestdiv(hw, rate, prate,
				       divider->width,
				       divider->flags);

	return DIV_ROUND_UP_ULL(((u64)*prate * 2), div * 2 + 3);
}

static int clk_half_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int value;
	unsigned long flags = 0;
	u32 val;

	value = DIV_ROUND_UP_ULL(((u64)parent_rate * 2), rate);
	value = (value - 3) / 2;
	value =  min_t(unsigned int, value, div_mask(divider->width));

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	if (divider->flags & CLK_DIVIDER_HIWORD_MASK) {
		val = div_mask(divider->width) << (divider->shift + 16);
	} else {
		val = readl(divider->reg);
		val &= ~(div_mask(divider->width) << divider->shift);
	}
	val |= value << divider->shift;
	writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	return 0;
}

const struct clk_ops clk_half_divider_ops = {
	.recalc_rate = clk_half_divider_recalc_rate,
	.round_rate = clk_half_divider_round_rate,
	.set_rate = clk_half_divider_set_rate,
};
EXPORT_SYMBOL_GPL(clk_half_divider_ops);

/**
 * Register a clock branch.
 * Most clock branches have a form like
 *
 * src1 --|--\
 *        |M |--[GATE]-[DIV]-
 * src2 --|--/
 *
 * sometimes without one of those components.
 */
struct clk *rockchip_clk_register_halfdiv(const char *name,
					  const char *const *parent_names,
					  u8 num_parents, void __iomem *base,
					  int muxdiv_offset, u8 mux_shift,
					  u8 mux_width, u8 mux_flags,
					  u8 div_shift, u8 div_width,
					  u8 div_flags, int gate_offset,
					  u8 gate_shift, u8 gate_flags,
					  unsigned long flags,
					  spinlock_t *lock)
{
	struct clk *clk;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;
	const struct clk_ops *mux_ops = NULL, *div_ops = NULL,
			     *gate_ops = NULL;

	if (num_parents > 1) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = base + muxdiv_offset;
		mux->shift = mux_shift;
		mux->mask = BIT(mux_width) - 1;
		mux->flags = mux_flags;
		mux->lock = lock;
		mux_ops = (mux_flags & CLK_MUX_READ_ONLY) ? &clk_mux_ro_ops
							: &clk_mux_ops;
	}

	if (gate_offset >= 0) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate)
			goto err_gate;

		gate->flags = gate_flags;
		gate->reg = base + gate_offset;
		gate->bit_idx = gate_shift;
		gate->lock = lock;
		gate_ops = &clk_gate_ops;
	}

	if (div_width > 0) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div)
			goto err_div;

		div->flags = div_flags;
		div->reg = base + muxdiv_offset;
		div->shift = div_shift;
		div->width = div_width;
		div->lock = lock;
		div_ops = &clk_half_divider_ops;
	}

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
				     mux ? &mux->hw : NULL, mux_ops,
				     div ? &div->hw : NULL, div_ops,
				     gate ? &gate->hw : NULL, gate_ops,
				     flags);

	return clk;
err_div:
	kfree(gate);
err_gate:
	kfree(mux);
	return ERR_PTR(-ENOMEM);
}
