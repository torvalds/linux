/*
 * TI clock autoidle support
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
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>

#include "clock.h"

struct clk_ti_autoidle {
	void __iomem		*reg;
	u8			shift;
	u8			flags;
	const char		*name;
	struct list_head	node;
};

#define AUTOIDLE_LOW		0x1

static LIST_HEAD(autoidle_clks);
static LIST_HEAD(clk_hw_omap_clocks);

/**
 * omap2_clk_deny_idle - disable autoidle on an OMAP clock
 * @clk: struct clk * to disable autoidle for
 *
 * Disable autoidle on an OMAP clock.
 */
int omap2_clk_deny_idle(struct clk *clk)
{
	struct clk_hw_omap *c;

	c = to_clk_hw_omap(__clk_get_hw(clk));
	if (c->ops && c->ops->deny_idle)
		c->ops->deny_idle(c);
	return 0;
}

/**
 * omap2_clk_allow_idle - enable autoidle on an OMAP clock
 * @clk: struct clk * to enable autoidle for
 *
 * Enable autoidle on an OMAP clock.
 */
int omap2_clk_allow_idle(struct clk *clk)
{
	struct clk_hw_omap *c;

	c = to_clk_hw_omap(__clk_get_hw(clk));
	if (c->ops && c->ops->allow_idle)
		c->ops->allow_idle(c);
	return 0;
}

static void _allow_autoidle(struct clk_ti_autoidle *clk)
{
	u32 val;

	val = ti_clk_ll_ops->clk_readl(clk->reg);

	if (clk->flags & AUTOIDLE_LOW)
		val &= ~(1 << clk->shift);
	else
		val |= (1 << clk->shift);

	ti_clk_ll_ops->clk_writel(val, clk->reg);
}

static void _deny_autoidle(struct clk_ti_autoidle *clk)
{
	u32 val;

	val = ti_clk_ll_ops->clk_readl(clk->reg);

	if (clk->flags & AUTOIDLE_LOW)
		val |= (1 << clk->shift);
	else
		val &= ~(1 << clk->shift);

	ti_clk_ll_ops->clk_writel(val, clk->reg);
}

/**
 * _clk_generic_allow_autoidle_all - enable autoidle for all clocks
 *
 * Enables hardware autoidle for all registered DT clocks, which have
 * the feature.
 */
static void _clk_generic_allow_autoidle_all(void)
{
	struct clk_ti_autoidle *c;

	list_for_each_entry(c, &autoidle_clks, node)
		_allow_autoidle(c);
}

/**
 * _clk_generic_deny_autoidle_all - disable autoidle for all clocks
 *
 * Disables hardware autoidle for all registered DT clocks, which have
 * the feature.
 */
static void _clk_generic_deny_autoidle_all(void)
{
	struct clk_ti_autoidle *c;

	list_for_each_entry(c, &autoidle_clks, node)
		_deny_autoidle(c);
}

/**
 * of_ti_clk_autoidle_setup - sets up hardware autoidle for a clock
 * @node: pointer to the clock device node
 *
 * Checks if a clock has hardware autoidle support or not (check
 * for presence of 'ti,autoidle-shift' property in the device tree
 * node) and sets up the hardware autoidle feature for the clock
 * if available. If autoidle is available, the clock is also added
 * to the autoidle list for later processing. Returns 0 on success,
 * negative error value on failure.
 */
int __init of_ti_clk_autoidle_setup(struct device_node *node)
{
	u32 shift;
	struct clk_ti_autoidle *clk;

	/* Check if this clock has autoidle support or not */
	if (of_property_read_u32(node, "ti,autoidle-shift", &shift))
		return 0;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);

	if (!clk)
		return -ENOMEM;

	clk->shift = shift;
	clk->name = node->name;
	clk->reg = ti_clk_get_reg_addr(node, 0);

	if (IS_ERR(clk->reg)) {
		kfree(clk);
		return -EINVAL;
	}

	if (of_property_read_bool(node, "ti,invert-autoidle-bit"))
		clk->flags |= AUTOIDLE_LOW;

	list_add(&clk->node, &autoidle_clks);

	return 0;
}

/**
 * omap2_init_clk_hw_omap_clocks - initialize an OMAP clock
 * @hw: struct clk_hw * to initialize
 *
 * Add an OMAP clock @clk to the internal list of OMAP clocks.  Used
 * temporarily for autoidle handling, until this support can be
 * integrated into the common clock framework code in some way.  No
 * return value.
 */
void omap2_init_clk_hw_omap_clocks(struct clk_hw *hw)
{
	struct clk_hw_omap *c;

	if (clk_hw_get_flags(hw) & CLK_IS_BASIC)
		return;

	c = to_clk_hw_omap(hw);
	list_add(&c->node, &clk_hw_omap_clocks);
}

/**
 * omap2_clk_enable_autoidle_all - enable autoidle on all OMAP clocks that
 * support it
 *
 * Enable clock autoidle on all OMAP clocks that have allow_idle
 * function pointers associated with them.  This function is intended
 * to be temporary until support for this is added to the common clock
 * code.  Returns 0.
 */
int omap2_clk_enable_autoidle_all(void)
{
	struct clk_hw_omap *c;

	list_for_each_entry(c, &clk_hw_omap_clocks, node)
		if (c->ops && c->ops->allow_idle)
			c->ops->allow_idle(c);

	_clk_generic_allow_autoidle_all();

	return 0;
}

/**
 * omap2_clk_disable_autoidle_all - disable autoidle on all OMAP clocks that
 * support it
 *
 * Disable clock autoidle on all OMAP clocks that have allow_idle
 * function pointers associated with them.  This function is intended
 * to be temporary until support for this is added to the common clock
 * code.  Returns 0.
 */
int omap2_clk_disable_autoidle_all(void)
{
	struct clk_hw_omap *c;

	list_for_each_entry(c, &clk_hw_omap_clocks, node)
		if (c->ops && c->ops->deny_idle)
			c->ops->deny_idle(c);

	_clk_generic_deny_autoidle_all();

	return 0;
}
