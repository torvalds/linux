// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "clk.h"

#define div_mask(width)	((1 << (width)) - 1)

static unsigned long clk_dclk_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int val;

	val = clk_readl(divider->reg) >> divider->shift;
	val &= div_mask(divider->width);

	return DIV_ROUND_UP_ULL(((u64)parent_rate), val + 1);
}

static long clk_dclk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int div, maxdiv =  div_mask(divider->width) + 1;

	div = DIV_ROUND_UP_ULL(divider->max_prate, rate);
	if (div % 2)
		div = __rounddown_pow_of_two(div);
	div = div > maxdiv ? maxdiv : div;
	*prate = div  * rate;
	return rate;
}

static int clk_dclk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int value;
	unsigned long flags = 0;
	u32 val;

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	if (divider->flags & CLK_DIVIDER_HIWORD_MASK) {
		val = div_mask(divider->width) << (divider->shift + 16);
	} else {
		val = clk_readl(divider->reg);
		val &= ~(div_mask(divider->width) << divider->shift);
	}
	val |= value << divider->shift;
	clk_writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	return 0;
}

const struct clk_ops clk_dclk_divider_ops = {
	.recalc_rate = clk_dclk_recalc_rate,
	.round_rate = clk_dclk_round_rate,
	.set_rate = clk_dclk_set_rate,
};
EXPORT_SYMBOL_GPL(clk_dclk_divider_ops);

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
struct clk *rockchip_clk_register_dclk_branch(const char *name,
					      const char *const *parent_names,
					      u8 num_parents,
					      void __iomem *base,
					      int muxdiv_offset, u8 mux_shift,
					      u8 mux_width, u8 mux_flags,
					      int div_offset, u8 div_shift,
					      u8 div_width, u8 div_flags,
					      struct clk_div_table *div_table,
					      int gate_offset,
					      u8 gate_shift, u8 gate_flags,
					      unsigned long flags,
					      unsigned long max_prate,
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
		if (div_offset)
			div->reg = base + div_offset;
		else
			div->reg = base + muxdiv_offset;
		div->shift = div_shift;
		div->width = div_width;
		div->lock = lock;
		div->max_prate = max_prate;
		div_ops = &clk_dclk_divider_ops;
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
