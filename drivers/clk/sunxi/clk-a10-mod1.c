/*
 * Copyright 2013 Emilio López
 *
 * Emilio López <emilio@elopez.com.ar>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

static DEFINE_SPINLOCK(mod1_lock);

#define SUN4I_MOD1_ENABLE	31
#define SUN4I_MOD1_MUX		16
#define SUN4I_MOD1_MUX_WIDTH	2
#define SUN4I_MOD1_MAX_PARENTS	4

static void __init sun4i_mod1_clk_setup(struct device_node *node)
{
	struct clk *clk;
	struct clk_mux *mux;
	struct clk_gate *gate;
	const char *parents[4];
	const char *clk_name = node->name;
	void __iomem *reg;
	int i;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg))
		return;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		goto err_unmap;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		goto err_free_mux;

	of_property_read_string(node, "clock-output-names", &clk_name);
	i = of_clk_parent_fill(node, parents, SUN4I_MOD1_MAX_PARENTS);

	gate->reg = reg;
	gate->bit_idx = SUN4I_MOD1_ENABLE;
	gate->lock = &mod1_lock;
	mux->reg = reg;
	mux->shift = SUN4I_MOD1_MUX;
	mux->mask = BIT(SUN4I_MOD1_MUX_WIDTH) - 1;
	mux->lock = &mod1_lock;

	clk = clk_register_composite(NULL, clk_name, parents, i,
				     &mux->hw, &clk_mux_ops,
				     NULL, NULL,
				     &gate->hw, &clk_gate_ops, CLK_SET_RATE_PARENT);
	if (IS_ERR(clk))
		goto err_free_gate;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

err_free_gate:
	kfree(gate);
err_free_mux:
	kfree(mux);
err_unmap:
	iounmap(reg);
}
CLK_OF_DECLARE(sun4i_mod1, "allwinner,sun4i-a10-mod1-clk",
	       sun4i_mod1_clk_setup);
