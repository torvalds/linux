/*
 * Copyright 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "clk.h"

/**
 * struct clk_gate_exclusive - i.MX specific gate clock which is mutually
 * exclusive with other gate clocks
 *
 * @gate: the parent class
 * @exclusive_mask: mask of gate bits which are mutually exclusive to this
 *	gate clock
 *
 * The imx exclusive gate clock is a subclass of basic clk_gate
 * with an addtional mask to indicate which other gate bits in the same
 * register is mutually exclusive to this gate clock.
 */
struct clk_gate_exclusive {
	struct clk_gate gate;
	u32 exclusive_mask;
};

static int clk_gate_exclusive_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct clk_gate_exclusive *exgate = container_of(gate,
					struct clk_gate_exclusive, gate);
	u32 val = readl(gate->reg);

	if (val & exgate->exclusive_mask)
		return -EBUSY;

	return clk_gate_ops.enable(hw);
}

static void clk_gate_exclusive_disable(struct clk_hw *hw)
{
	clk_gate_ops.disable(hw);
}

static int clk_gate_exclusive_is_enabled(struct clk_hw *hw)
{
	return clk_gate_ops.is_enabled(hw);
}

static const struct clk_ops clk_gate_exclusive_ops = {
	.enable = clk_gate_exclusive_enable,
	.disable = clk_gate_exclusive_disable,
	.is_enabled = clk_gate_exclusive_is_enabled,
};

struct clk *imx_clk_gate_exclusive(const char *name, const char *parent,
	 void __iomem *reg, u8 shift, u32 exclusive_mask)
{
	struct clk_gate_exclusive *exgate;
	struct clk_gate *gate;
	struct clk *clk;
	struct clk_init_data init;

	if (exclusive_mask == 0)
		return ERR_PTR(-EINVAL);

	exgate = kzalloc(sizeof(*exgate), GFP_KERNEL);
	if (!exgate)
		return ERR_PTR(-ENOMEM);
	gate = &exgate->gate;

	init.name = name;
	init.ops = &clk_gate_exclusive_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent ? &parent : NULL;
	init.num_parents = parent ? 1 : 0;

	gate->reg = reg;
	gate->bit_idx = shift;
	gate->lock = &imx_ccm_lock;
	gate->hw.init = &init;
	exgate->exclusive_mask = exclusive_mask;

	clk = clk_register(NULL, &gate->hw);
	if (IS_ERR(clk))
		kfree(exgate);

	return clk;
}
