/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gated clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include "clk.h"

/**
 * DOC: basic gatable clock which can gate and ungate it's ouput
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_gate2 {
	struct clk_hw hw;
	void __iomem	*reg;
	u8		bit_idx;
	u8		flags;
	spinlock_t	*lock;
	unsigned int	*share_count;
};

#define to_clk_gate2(_hw) container_of(_hw, struct clk_gate2, hw)

static int clk_gate2_enable(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	u32 reg;
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count && (*gate->share_count)++ > 0)
		goto out;

	reg = readl(gate->reg);
	reg |= 3 << gate->bit_idx;
	writel(reg, gate->reg);

out:
	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static void clk_gate2_disable(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	u32 reg;
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count) {
		if (WARN_ON(*gate->share_count == 0))
			goto out;
		else if (--(*gate->share_count) > 0)
			goto out;
	}

	reg = readl(gate->reg);
	reg &= ~(3 << gate->bit_idx);
	writel(reg, gate->reg);

out:
	spin_unlock_irqrestore(gate->lock, flags);
}

static int clk_gate2_reg_is_enabled(void __iomem *reg, u8 bit_idx)
{
	u32 val = readl(reg);

	if (((val >> bit_idx) & 1) == 1)
		return 1;

	return 0;
}

static int clk_gate2_is_enabled(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);

	return clk_gate2_reg_is_enabled(gate->reg, gate->bit_idx);
}

static void clk_gate2_disable_unused(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	unsigned long flags = 0;
	u32 reg;

	spin_lock_irqsave(gate->lock, flags);

	if (!gate->share_count || *gate->share_count == 0) {
		reg = readl(gate->reg);
		reg &= ~(3 << gate->bit_idx);
		writel(reg, gate->reg);
	}

	spin_unlock_irqrestore(gate->lock, flags);
}

static struct clk_ops clk_gate2_ops = {
	.enable = clk_gate2_enable,
	.disable = clk_gate2_disable,
	.disable_unused = clk_gate2_disable_unused,
	.is_enabled = clk_gate2_is_enabled,
};

struct clk *clk_register_gate2(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate2_flags, spinlock_t *lock,
		unsigned int *share_count)
{
	struct clk_gate2 *gate;
	struct clk *clk;
	struct clk_init_data init;

	gate = kzalloc(sizeof(struct clk_gate2), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	/* struct clk_gate2 assignments */
	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->flags = clk_gate2_flags;
	gate->lock = lock;
	gate->share_count = share_count;

	init.name = name;
	init.ops = &clk_gate2_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->hw.init = &init;

	clk = clk_register(dev, &gate->hw);
	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}
