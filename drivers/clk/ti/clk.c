/*
 * TI clock support
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
#include <linux/clkdev.h>
#include <linux/clk/ti.h>
#include <linux/of.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/**
 * ti_dt_clocks_register - register DT alias clocks during boot
 * @oclks: list of clocks to register
 *
 * Register alias or non-standard DT clock entries during boot. By
 * default, DT clocks are found based on their node name. If any
 * additional con-id / dev-id -> clock mapping is required, use this
 * function to list these.
 */
void __init ti_dt_clocks_register(struct ti_dt_clk oclks[])
{
	struct ti_dt_clk *c;
	struct device_node *node;
	struct clk *clk;
	struct of_phandle_args clkspec;

	for (c = oclks; c->node_name != NULL; c++) {
		node = of_find_node_by_name(NULL, c->node_name);
		clkspec.np = node;
		clk = of_clk_get_from_provider(&clkspec);

		if (!IS_ERR(clk)) {
			c->lk.clk = clk;
			clkdev_add(&c->lk);
		} else {
			pr_warn("failed to lookup clock node %s\n",
				c->node_name);
		}
	}
}
