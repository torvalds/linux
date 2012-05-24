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

/**
 * DOC: basic gatable clock which can gate and ungate it's ouput
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static void clk_gate_set_bit(struct clk_gate *gate)
{
	u32 reg;
	unsigned long flags = 0;

	if (gate->lock)
		spin_lock_irqsave(gate->lock, flags);

	reg = readl(gate->reg);
	reg |= BIT(gate->bit_idx);
	writel(reg, gate->reg);

	if (gate->lock)
		spin_unlock_irqrestore(gate->lock, flags);
}

static void clk_gate_clear_bit(struct clk_gate *gate)
{
	u32 reg;
	unsigned long flags = 0;

	if (gate->lock)
		spin_lock_irqsave(gate->lock, flags);

	reg = readl(gate->reg);
	reg &= ~BIT(gate->bit_idx);
	writel(reg, gate->reg);

	if (gate->lock)
		spin_unlock_irqrestore(gate->lock, flags);
}

static int clk_gate_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);

	if (gate->flags & CLK_GATE_SET_TO_DISABLE)
		clk_gate_clear_bit(gate);
	else
		clk_gate_set_bit(gate);

	return 0;
}
EXPORT_SYMBOL_GPL(clk_gate_enable);

static void clk_gate_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);

	if (gate->flags & CLK_GATE_SET_TO_DISABLE)
		clk_gate_set_bit(gate);
	else
		clk_gate_clear_bit(gate);
}
EXPORT_SYMBOL_GPL(clk_gate_disable);

static int clk_gate_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	struct clk_gate *gate = to_clk_gate(hw);

	reg = readl(gate->reg);

	/* if a set bit disables this clk, flip it before masking */
	if (gate->flags & CLK_GATE_SET_TO_DISABLE)
		reg ^= BIT(gate->bit_idx);

	reg &= BIT(gate->bit_idx);

	return reg ? 1 : 0;
}
EXPORT_SYMBOL_GPL(clk_gate_is_enabled);

struct clk_ops clk_gate_ops = {
	.enable = clk_gate_enable,
	.disable = clk_gate_disable,
	.is_enabled = clk_gate_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_gate_ops);

struct clk *clk_register_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate_flags, spinlock_t *lock)
{
	struct clk_gate *gate;
	struct clk *clk;

	gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);

	if (!gate) {
		pr_err("%s: could not allocate gated clk\n", __func__);
		return NULL;
	}

	/* struct clk_gate assignments */
	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->flags = clk_gate_flags;
	gate->lock = lock;

	if (parent_name) {
		gate->parent[0] = kstrdup(parent_name, GFP_KERNEL);
		if (!gate->parent[0])
			goto out;
	}

	clk = clk_register(dev, name,
			&clk_gate_ops, &gate->hw,
			gate->parent,
			(parent_name ? 1 : 0),
			flags);
	if (clk)
		return clk;
out:
	kfree(gate->parent[0]);
	kfree(gate);

	return NULL;
}
