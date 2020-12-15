// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
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
	u8		cgr_val;
	u8		flags;
	spinlock_t	*lock;
	unsigned int	*share_count;
};

#define to_clk_gate2(_hw) container_of(_hw, struct clk_gate2, hw)

static int clk_gate2_enable(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	u32 reg;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count && (*gate->share_count)++ > 0)
		goto out;

	if (gate->flags & IMX_CLK_GATE2_SINGLE_BIT) {
		ret = clk_gate_ops.enable(hw);
	} else {
		reg = readl(gate->reg);
		reg &= ~(3 << gate->bit_idx);
		reg |= gate->cgr_val << gate->bit_idx;
		writel(reg, gate->reg);
	}

out:
	spin_unlock_irqrestore(gate->lock, flags);

	return ret;
}

static void clk_gate2_disable(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	u32 reg;
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);

	if (gate->share_count) {
		if (WARN_ON(*gate->share_count == 0))
			goto out;
		else if (--(*gate->share_count) > 0)
			goto out;
	}

	if (gate->flags & IMX_CLK_GATE2_SINGLE_BIT) {
		clk_gate_ops.disable(hw);
	} else {
		reg = readl(gate->reg);
		reg &= ~(3 << gate->bit_idx);
		writel(reg, gate->reg);
	}

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

	if (gate->flags & IMX_CLK_GATE2_SINGLE_BIT)
		return clk_gate_ops.is_enabled(hw);

	return clk_gate2_reg_is_enabled(gate->reg, gate->bit_idx);
}

static void clk_gate2_disable_unused(struct clk_hw *hw)
{
	struct clk_gate2 *gate = to_clk_gate2(hw);
	unsigned long flags;
	u32 reg;

	if (gate->flags & IMX_CLK_GATE2_SINGLE_BIT)
		return;

	spin_lock_irqsave(gate->lock, flags);

	if (!gate->share_count || *gate->share_count == 0) {
		reg = readl(gate->reg);
		reg &= ~(3 << gate->bit_idx);
		writel(reg, gate->reg);
	}

	spin_unlock_irqrestore(gate->lock, flags);
}

static const struct clk_ops clk_gate2_ops = {
	.enable = clk_gate2_enable,
	.disable = clk_gate2_disable,
	.disable_unused = clk_gate2_disable_unused,
	.is_enabled = clk_gate2_is_enabled,
};

struct clk_hw *clk_hw_register_gate2(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, u8 cgr_val,
		u8 clk_gate2_flags, spinlock_t *lock,
		unsigned int *share_count)
{
	struct clk_gate2 *gate;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	gate = kzalloc(sizeof(struct clk_gate2), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	/* struct clk_gate2 assignments */
	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->cgr_val = cgr_val;
	gate->flags = clk_gate2_flags;
	gate->lock = lock;
	gate->share_count = share_count;

	init.name = name;
	init.ops = &clk_gate2_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->hw.init = &init;
	hw = &gate->hw;

	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(gate);
		return ERR_PTR(ret);
	}

	return hw;
}
