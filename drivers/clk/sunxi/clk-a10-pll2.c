/*
 * Copyright 2013 Emilio López
 * Emilio López <emilio@elopez.com.ar>
 *
 * Copyright 2015 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
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

#include <dt-bindings/clock/sun4i-a10-pll2.h>

#define SUN4I_PLL2_ENABLE		31

#define SUN4I_PLL2_PRE_DIV_SHIFT	0
#define SUN4I_PLL2_PRE_DIV_WIDTH	5
#define SUN4I_PLL2_PRE_DIV_MASK		GENMASK(SUN4I_PLL2_PRE_DIV_WIDTH - 1, 0)

#define SUN4I_PLL2_N_SHIFT		8
#define SUN4I_PLL2_N_WIDTH		7
#define SUN4I_PLL2_N_MASK		GENMASK(SUN4I_PLL2_N_WIDTH - 1, 0)

#define SUN4I_PLL2_POST_DIV_SHIFT	26
#define SUN4I_PLL2_POST_DIV_WIDTH	4
#define SUN4I_PLL2_POST_DIV_MASK	GENMASK(SUN4I_PLL2_POST_DIV_WIDTH - 1, 0)

#define SUN4I_PLL2_POST_DIV_VALUE	4

#define SUN4I_PLL2_OUTPUTS		4

static DEFINE_SPINLOCK(sun4i_a10_pll2_lock);

static void __init sun4i_pll2_setup(struct device_node *node,
				    int post_div_offset)
{
	const char *clk_name = node->name, *parent;
	struct clk **clks, *base_clk, *prediv_clk;
	struct clk_onecell_data *clk_data;
	struct clk_multiplier *mult;
	struct clk_gate *gate;
	void __iomem *reg;
	u32 val;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg))
		return;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err_unmap;

	clks = kcalloc(SUN4I_PLL2_OUTPUTS, sizeof(struct clk *), GFP_KERNEL);
	if (!clks)
		goto err_free_data;

	parent = of_clk_get_parent_name(node, 0);
	prediv_clk = clk_register_divider(NULL, "pll2-prediv",
					  parent, 0, reg,
					  SUN4I_PLL2_PRE_DIV_SHIFT,
					  SUN4I_PLL2_PRE_DIV_WIDTH,
					  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
					  &sun4i_a10_pll2_lock);
	if (IS_ERR(prediv_clk)) {
		pr_err("Couldn't register the prediv clock\n");
		goto err_free_array;
	}

	/* Setup the gate part of the PLL2 */
	gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
	if (!gate)
		goto err_unregister_prediv;

	gate->reg = reg;
	gate->bit_idx = SUN4I_PLL2_ENABLE;
	gate->lock = &sun4i_a10_pll2_lock;

	/* Setup the multiplier part of the PLL2 */
	mult = kzalloc(sizeof(struct clk_multiplier), GFP_KERNEL);
	if (!mult)
		goto err_free_gate;

	mult->reg = reg;
	mult->shift = SUN4I_PLL2_N_SHIFT;
	mult->width = 7;
	mult->flags = CLK_MULTIPLIER_ZERO_BYPASS |
			CLK_MULTIPLIER_ROUND_CLOSEST;
	mult->lock = &sun4i_a10_pll2_lock;

	parent = __clk_get_name(prediv_clk);
	base_clk = clk_register_composite(NULL, "pll2-base",
					  &parent, 1,
					  NULL, NULL,
					  &mult->hw, &clk_multiplier_ops,
					  &gate->hw, &clk_gate_ops,
					  CLK_SET_RATE_PARENT);
	if (IS_ERR(base_clk)) {
		pr_err("Couldn't register the base multiplier clock\n");
		goto err_free_multiplier;
	}

	parent = __clk_get_name(base_clk);

	/*
	 * PLL2-1x
	 *
	 * This is supposed to have a post divider, but we won't need
	 * to use it, we just need to initialise it to 4, and use a
	 * fixed divider.
	 */
	val = readl(reg);
	val &= ~(SUN4I_PLL2_POST_DIV_MASK << SUN4I_PLL2_POST_DIV_SHIFT);
	val |= (SUN4I_PLL2_POST_DIV_VALUE - post_div_offset) << SUN4I_PLL2_POST_DIV_SHIFT;
	writel(val, reg);

	of_property_read_string_index(node, "clock-output-names",
				      SUN4I_A10_PLL2_1X, &clk_name);
	clks[SUN4I_A10_PLL2_1X] = clk_register_fixed_factor(NULL, clk_name,
							    parent,
							    CLK_SET_RATE_PARENT,
							    1,
							    SUN4I_PLL2_POST_DIV_VALUE);
	WARN_ON(IS_ERR(clks[SUN4I_A10_PLL2_1X]));

	/*
	 * PLL2-2x
	 *
	 * This clock doesn't use the post divider, and really is just
	 * a fixed divider from the PLL2 base clock.
	 */
	of_property_read_string_index(node, "clock-output-names",
				      SUN4I_A10_PLL2_2X, &clk_name);
	clks[SUN4I_A10_PLL2_2X] = clk_register_fixed_factor(NULL, clk_name,
							    parent,
							    CLK_SET_RATE_PARENT,
							    1, 2);
	WARN_ON(IS_ERR(clks[SUN4I_A10_PLL2_2X]));

	/* PLL2-4x */
	of_property_read_string_index(node, "clock-output-names",
				      SUN4I_A10_PLL2_4X, &clk_name);
	clks[SUN4I_A10_PLL2_4X] = clk_register_fixed_factor(NULL, clk_name,
							    parent,
							    CLK_SET_RATE_PARENT,
							    1, 1);
	WARN_ON(IS_ERR(clks[SUN4I_A10_PLL2_4X]));

	/* PLL2-8x */
	of_property_read_string_index(node, "clock-output-names",
				      SUN4I_A10_PLL2_8X, &clk_name);
	clks[SUN4I_A10_PLL2_8X] = clk_register_fixed_factor(NULL, clk_name,
							    parent,
							    CLK_SET_RATE_PARENT,
							    2, 1);
	WARN_ON(IS_ERR(clks[SUN4I_A10_PLL2_8X]));

	clk_data->clks = clks;
	clk_data->clk_num = SUN4I_PLL2_OUTPUTS;
	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	return;

err_free_multiplier:
	kfree(mult);
err_free_gate:
	kfree(gate);
err_unregister_prediv:
	clk_unregister_divider(prediv_clk);
err_free_array:
	kfree(clks);
err_free_data:
	kfree(clk_data);
err_unmap:
	iounmap(reg);
}

static void __init sun4i_a10_pll2_setup(struct device_node *node)
{
	sun4i_pll2_setup(node, 0);
}

CLK_OF_DECLARE(sun4i_a10_pll2, "allwinner,sun4i-a10-pll2-clk",
	       sun4i_a10_pll2_setup);

static void __init sun5i_a13_pll2_setup(struct device_node *node)
{
	sun4i_pll2_setup(node, 1);
}

CLK_OF_DECLARE(sun5i_a13_pll2, "allwinner,sun5i-a13-pll2-clk",
	       sun5i_a13_pll2_setup);
