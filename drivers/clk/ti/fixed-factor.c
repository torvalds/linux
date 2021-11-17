/*
 * TI Fixed Factor Clock
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

/**
 * of_ti_fixed_factor_clk_setup - Setup function for TI fixed factor clock
 * @node: device node for this clock
 *
 * Sets up a simple fixed factor clock based on device tree info.
 */
static void __init of_ti_fixed_factor_clk_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *parent_name;
	u32 div, mult;
	u32 flags = 0;

	if (of_property_read_u32(node, "ti,clock-div", &div)) {
		pr_err("%pOFn must have a clock-div property\n", node);
		return;
	}

	if (of_property_read_u32(node, "ti,clock-mult", &mult)) {
		pr_err("%pOFn must have a clock-mult property\n", node);
		return;
	}

	if (of_property_read_bool(node, "ti,set-rate-parent"))
		flags |= CLK_SET_RATE_PARENT;

	parent_name = of_clk_get_parent_name(node, 0);

	clk = clk_register_fixed_factor(NULL, clk_name, parent_name, flags,
					mult, div);

	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		of_ti_clk_autoidle_setup(node);
		ti_clk_add_alias(NULL, clk, clk_name);
	}
}
CLK_OF_DECLARE(ti_fixed_factor_clk, "ti,fixed-factor-clock",
	       of_ti_fixed_factor_clk_setup);
