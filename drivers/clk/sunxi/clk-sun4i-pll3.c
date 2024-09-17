// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define SUN4I_A10_PLL3_GATE_BIT	31
#define SUN4I_A10_PLL3_DIV_WIDTH	7
#define SUN4I_A10_PLL3_DIV_SHIFT	0

static DEFINE_SPINLOCK(sun4i_a10_pll3_lock);

static void __init sun4i_a10_pll3_setup(struct device_node *node)
{
	const char *clk_name = node->name, *parent;
	struct clk_multiplier *mult;
	struct clk_gate *gate;
	struct resource res;
	void __iomem *reg;
	struct clk *clk;
	int ret;

	of_property_read_string(node, "clock-output-names", &clk_name);
	parent = of_clk_get_parent_name(node, 0);

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%s: Could not map the clock registers\n", clk_name);
		return;
	}

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		goto err_unmap;

	gate->reg = reg;
	gate->bit_idx = SUN4I_A10_PLL3_GATE_BIT;
	gate->lock = &sun4i_a10_pll3_lock;

	mult = kzalloc(sizeof(*mult), GFP_KERNEL);
	if (!mult)
		goto err_free_gate;

	mult->reg = reg;
	mult->shift = SUN4I_A10_PLL3_DIV_SHIFT;
	mult->width = SUN4I_A10_PLL3_DIV_WIDTH;
	mult->lock = &sun4i_a10_pll3_lock;

	clk = clk_register_composite(NULL, clk_name,
				     &parent, 1,
				     NULL, NULL,
				     &mult->hw, &clk_multiplier_ops,
				     &gate->hw, &clk_gate_ops,
				     0);
	if (IS_ERR(clk)) {
		pr_err("%s: Couldn't register the clock\n", clk_name);
		goto err_free_mult;
	}

	ret = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (ret) {
		pr_err("%s: Couldn't register DT provider\n",
		       clk_name);
		goto err_clk_unregister;
	}

	return;

err_clk_unregister:
	clk_unregister_composite(clk);
err_free_mult:
	kfree(mult);
err_free_gate:
	kfree(gate);
err_unmap:
	iounmap(reg);
	of_address_to_resource(node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
}

CLK_OF_DECLARE(sun4i_a10_pll3, "allwinner,sun4i-a10-pll3-clk",
	       sun4i_a10_pll3_setup);
