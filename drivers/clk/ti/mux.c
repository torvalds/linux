/*
 * TI Multiplexer Clock
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

#define to_clk_mux(_hw) container_of(_hw, struct clk_mux, hw)

static u8 ti_clk_mux_get_parent(struct clk_hw *hw)
{
	struct clk_mux *mux = to_clk_mux(hw);
	int num_parents = __clk_get_num_parents(hw->clk);
	u32 val;

	/*
	 * FIXME need a mux-specific flag to determine if val is bitwise or
	 * numeric. e.g. sys_clkin_ck's clksel field is 3 bits wide, but ranges
	 * from 0x1 to 0x7 (index starts at one)
	 * OTOH, pmd_trace_clk_mux_ck uses a separate bit for each clock, so
	 * val = 0x4 really means "bit 2, index starts at bit 0"
	 */
	val = ti_clk_ll_ops->clk_readl(mux->reg) >> mux->shift;
	val &= mux->mask;

	if (mux->table) {
		int i;

		for (i = 0; i < num_parents; i++)
			if (mux->table[i] == val)
				return i;
		return -EINVAL;
	}

	if (val && (mux->flags & CLK_MUX_INDEX_BIT))
		val = ffs(val) - 1;

	if (val && (mux->flags & CLK_MUX_INDEX_ONE))
		val--;

	if (val >= num_parents)
		return -EINVAL;

	return val;
}

static int ti_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val;
	unsigned long flags = 0;

	if (mux->table) {
		index = mux->table[index];
	} else {
		if (mux->flags & CLK_MUX_INDEX_BIT)
			index = (1 << ffs(index));

		if (mux->flags & CLK_MUX_INDEX_ONE)
			index++;
	}

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);

	if (mux->flags & CLK_MUX_HIWORD_MASK) {
		val = mux->mask << (mux->shift + 16);
	} else {
		val = ti_clk_ll_ops->clk_readl(mux->reg);
		val &= ~(mux->mask << mux->shift);
	}
	val |= index << mux->shift;
	ti_clk_ll_ops->clk_writel(val, mux->reg);

	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);

	return 0;
}

const struct clk_ops ti_clk_mux_ops = {
	.get_parent = ti_clk_mux_get_parent,
	.set_parent = ti_clk_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk *_register_mux(struct device *dev, const char *name,
				 const char **parent_names, u8 num_parents,
				 unsigned long flags, void __iomem *reg,
				 u8 shift, u32 mask, u8 clk_mux_flags,
				 u32 *table, spinlock_t *lock)
{
	struct clk_mux *mux;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the mux */
	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux) {
		pr_err("%s: could not allocate mux clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &ti_clk_mux_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	/* struct clk_mux assignments */
	mux->reg = reg;
	mux->shift = shift;
	mux->mask = mask;
	mux->flags = clk_mux_flags;
	mux->lock = lock;
	mux->table = table;
	mux->hw.init = &init;

	clk = clk_register(dev, &mux->hw);

	if (IS_ERR(clk))
		kfree(mux);

	return clk;
}

struct clk *ti_clk_register_mux(struct ti_clk *setup)
{
	struct ti_clk_mux *mux;
	u32 flags;
	u8 mux_flags = 0;
	struct clk_omap_reg *reg_setup;
	u32 reg;
	u32 mask;

	reg_setup = (struct clk_omap_reg *)&reg;

	mux = setup->data;
	flags = CLK_SET_RATE_NO_REPARENT;

	mask = mux->num_parents;
	if (!(mux->flags & CLKF_INDEX_STARTS_AT_ONE))
		mask--;

	mask = (1 << fls(mask)) - 1;
	reg_setup->index = mux->module;
	reg_setup->offset = mux->reg;

	if (mux->flags & CLKF_INDEX_STARTS_AT_ONE)
		mux_flags |= CLK_MUX_INDEX_ONE;

	if (mux->flags & CLKF_SET_RATE_PARENT)
		flags |= CLK_SET_RATE_PARENT;

	return _register_mux(NULL, setup->name, mux->parents, mux->num_parents,
			     flags, (void __iomem *)reg, mux->bit_shift, mask,
			     mux_flags, NULL, NULL);
}

/**
 * of_mux_clk_setup - Setup function for simple mux rate clock
 * @node: DT node for the clock
 *
 * Sets up a basic clock multiplexer.
 */
static void of_mux_clk_setup(struct device_node *node)
{
	struct clk *clk;
	void __iomem *reg;
	int num_parents;
	const char **parent_names;
	int i;
	u8 clk_mux_flags = 0;
	u32 mask = 0;
	u32 shift = 0;
	u32 flags = CLK_SET_RATE_NO_REPARENT;

	num_parents = of_clk_get_parent_count(node);
	if (num_parents < 2) {
		pr_err("mux-clock %s must have parents\n", node->name);
		return;
	}
	parent_names = kzalloc((sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names)
		goto cleanup;

	for (i = 0; i < num_parents; i++)
		parent_names[i] = of_clk_get_parent_name(node, i);

	reg = ti_clk_get_reg_addr(node, 0);

	if (IS_ERR(reg))
		goto cleanup;

	of_property_read_u32(node, "ti,bit-shift", &shift);

	if (of_property_read_bool(node, "ti,index-starts-at-one"))
		clk_mux_flags |= CLK_MUX_INDEX_ONE;

	if (of_property_read_bool(node, "ti,set-rate-parent"))
		flags |= CLK_SET_RATE_PARENT;

	/* Generate bit-mask based on parent info */
	mask = num_parents;
	if (!(clk_mux_flags & CLK_MUX_INDEX_ONE))
		mask--;

	mask = (1 << fls(mask)) - 1;

	clk = _register_mux(NULL, node->name, parent_names, num_parents,
			    flags, reg, shift, mask, clk_mux_flags, NULL,
			    NULL);

	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);

cleanup:
	kfree(parent_names);
}
CLK_OF_DECLARE(mux_clk, "ti,mux-clock", of_mux_clk_setup);

struct clk_hw *ti_clk_build_component_mux(struct ti_clk_mux *setup)
{
	struct clk_mux *mux;
	struct clk_omap_reg *reg;
	int num_parents;

	if (!setup)
		return NULL;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	reg = (struct clk_omap_reg *)&mux->reg;

	mux->shift = setup->bit_shift;

	reg->index = setup->module;
	reg->offset = setup->reg;

	if (setup->flags & CLKF_INDEX_STARTS_AT_ONE)
		mux->flags |= CLK_MUX_INDEX_ONE;

	num_parents = setup->num_parents;

	mux->mask = num_parents - 1;
	mux->mask = (1 << fls(mux->mask)) - 1;

	return &mux->hw;
}

static void __init of_ti_composite_mux_clk_setup(struct device_node *node)
{
	struct clk_mux *mux;
	int num_parents;
	u32 val;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return;

	mux->reg = ti_clk_get_reg_addr(node, 0);

	if (IS_ERR(mux->reg))
		goto cleanup;

	if (!of_property_read_u32(node, "ti,bit-shift", &val))
		mux->shift = val;

	if (of_property_read_bool(node, "ti,index-starts-at-one"))
		mux->flags |= CLK_MUX_INDEX_ONE;

	num_parents = of_clk_get_parent_count(node);

	if (num_parents < 2) {
		pr_err("%s must have parents\n", node->name);
		goto cleanup;
	}

	mux->mask = num_parents - 1;
	mux->mask = (1 << fls(mux->mask)) - 1;

	if (!ti_clk_add_component(node, &mux->hw, CLK_COMPONENT_TYPE_MUX))
		return;

cleanup:
	kfree(mux);
}
CLK_OF_DECLARE(ti_composite_mux_clk_setup, "ti,composite-mux-clock",
	       of_ti_composite_mux_clk_setup);
