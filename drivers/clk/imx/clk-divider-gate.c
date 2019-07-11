// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP.
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "clk.h"

struct clk_divider_gate {
	struct clk_divider divider;
	u32 cached_val;
};

static inline struct clk_divider_gate *to_clk_divider_gate(struct clk_hw *hw)
{
	struct clk_divider *div = to_clk_divider(hw);

	return container_of(div, struct clk_divider_gate, divider);
}

static unsigned long clk_divider_gate_recalc_rate_ro(struct clk_hw *hw,
						     unsigned long parent_rate)
{
	struct clk_divider *div = to_clk_divider(hw);
	unsigned int val;

	val = clk_readl(div->reg) >> div->shift;
	val &= clk_div_mask(div->width);
	if (!val)
		return 0;

	return divider_recalc_rate(hw, parent_rate, val, div->table,
				   div->flags, div->width);
}

static unsigned long clk_divider_gate_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_divider_gate *div_gate = to_clk_divider_gate(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned long flags = 0;
	unsigned int val;

	spin_lock_irqsave(div->lock, flags);

	if (!clk_hw_is_enabled(hw)) {
		val = div_gate->cached_val;
	} else {
		val = clk_readl(div->reg) >> div->shift;
		val &= clk_div_mask(div->width);
	}

	spin_unlock_irqrestore(div->lock, flags);

	if (!val)
		return 0;

	return divider_recalc_rate(hw, parent_rate, val, div->table,
				   div->flags, div->width);
}

static long clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	return clk_divider_ops.round_rate(hw, rate, prate);
}

static int clk_divider_gate_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_divider_gate *div_gate = to_clk_divider_gate(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned long flags = 0;
	int value;
	u32 val;

	value = divider_get_val(rate, parent_rate, div->table,
				div->width, div->flags);
	if (value < 0)
		return value;

	spin_lock_irqsave(div->lock, flags);

	if (clk_hw_is_enabled(hw)) {
		val = clk_readl(div->reg);
		val &= ~(clk_div_mask(div->width) << div->shift);
		val |= (u32)value << div->shift;
		clk_writel(val, div->reg);
	} else {
		div_gate->cached_val = value;
	}

	spin_unlock_irqrestore(div->lock, flags);

	return 0;
}

static int clk_divider_enable(struct clk_hw *hw)
{
	struct clk_divider_gate *div_gate = to_clk_divider_gate(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned long flags = 0;
	u32 val;

	if (!div_gate->cached_val) {
		pr_err("%s: no valid preset rate\n", clk_hw_get_name(hw));
		return -EINVAL;
	}

	spin_lock_irqsave(div->lock, flags);
	/* restore div val */
	val = clk_readl(div->reg);
	val |= div_gate->cached_val << div->shift;
	clk_writel(val, div->reg);

	spin_unlock_irqrestore(div->lock, flags);

	return 0;
}

static void clk_divider_disable(struct clk_hw *hw)
{
	struct clk_divider_gate *div_gate = to_clk_divider_gate(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned long flags = 0;
	u32 val;

	spin_lock_irqsave(div->lock, flags);

	/* store the current div val */
	val = clk_readl(div->reg) >> div->shift;
	val &= clk_div_mask(div->width);
	div_gate->cached_val = val;
	clk_writel(0, div->reg);

	spin_unlock_irqrestore(div->lock, flags);
}

static int clk_divider_is_enabled(struct clk_hw *hw)
{
	struct clk_divider *div = to_clk_divider(hw);
	u32 val;

	val = clk_readl(div->reg) >> div->shift;
	val &= clk_div_mask(div->width);

	return val ? 1 : 0;
}

static const struct clk_ops clk_divider_gate_ro_ops = {
	.recalc_rate = clk_divider_gate_recalc_rate_ro,
	.round_rate = clk_divider_round_rate,
};

static const struct clk_ops clk_divider_gate_ops = {
	.recalc_rate = clk_divider_gate_recalc_rate,
	.round_rate = clk_divider_round_rate,
	.set_rate = clk_divider_gate_set_rate,
	.enable = clk_divider_enable,
	.disable = clk_divider_disable,
	.is_enabled = clk_divider_is_enabled,
};

/*
 * NOTE: In order to resue the most code from the common divider,
 * we also design our divider following the way that provids an extra
 * clk_divider_flags, however it's fixed to CLK_DIVIDER_ONE_BASED by
 * default as our HW is. Besides that it supports only CLK_DIVIDER_READ_ONLY
 * flag which can be specified by user flexibly.
 */
struct clk_hw *imx_clk_divider_gate(const char *name, const char *parent_name,
				    unsigned long flags, void __iomem *reg,
				    u8 shift, u8 width, u8 clk_divider_flags,
				    const struct clk_div_table *table,
				    spinlock_t *lock)
{
	struct clk_init_data init;
	struct clk_divider_gate *div_gate;
	struct clk_hw *hw;
	u32 val;
	int ret;

	div_gate  = kzalloc(sizeof(*div_gate), GFP_KERNEL);
	if (!div_gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	if (clk_divider_flags & CLK_DIVIDER_READ_ONLY)
		init.ops = &clk_divider_gate_ro_ops;
	else
		init.ops = &clk_divider_gate_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	div_gate->divider.reg = reg;
	div_gate->divider.shift = shift;
	div_gate->divider.width = width;
	div_gate->divider.lock = lock;
	div_gate->divider.table = table;
	div_gate->divider.hw.init = &init;
	div_gate->divider.flags = CLK_DIVIDER_ONE_BASED | clk_divider_flags;
	/* cache gate status */
	val = clk_readl(reg) >> shift;
	val &= clk_div_mask(width);
	div_gate->cached_val = val;

	hw = &div_gate->divider.hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(div_gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}
