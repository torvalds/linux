/*
 * Copyright (C) 2015 Jens Kuske <jenskuske@gmail.com>
 *
 * Based on clk-simple-gates.c, which is:
 * Copyright 2015 Maxime Ripard
 *
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
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(gates_lock);

static void __init sun8i_h3_bus_gates_init(struct device_node *node)
{
	static const char * const names[] = { "ahb1", "ahb2", "apb1", "apb2" };
	enum { AHB1, AHB2, APB1, APB2, PARENT_MAX } clk_parent;
	const char *parents[PARENT_MAX];
	struct clk_onecell_data *clk_data;
	const char *clk_name;
	struct property *prop;
	struct resource res;
	void __iomem *clk_reg;
	void __iomem *reg;
	const __be32 *p;
	int number, i;
	u8 clk_bit;
	int index;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg))
		return;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		int idx = of_property_match_string(node, "clock-names",
						   names[i]);
		if (idx < 0)
			return;

		parents[i] = of_clk_get_parent_name(node, idx);
	}

	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		goto err_unmap;

	number = of_property_count_u32_elems(node, "clock-indices");
	of_property_read_u32_index(node, "clock-indices", number - 1, &number);

	clk_data->clks = kcalloc(number + 1, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks)
		goto err_free_data;

	i = 0;
	of_property_for_each_u32(node, "clock-indices", prop, p, index) {
		of_property_read_string_index(node, "clock-output-names",
					      i, &clk_name);

		if (index == 17 || (index >= 29 && index <= 31))
			clk_parent = AHB2;
		else if (index <= 63 || index >= 128)
			clk_parent = AHB1;
		else if (index >= 64 && index <= 95)
			clk_parent = APB1;
		else if (index >= 96 && index <= 127)
			clk_parent = APB2;

		clk_reg = reg + 4 * (index / 32);
		clk_bit = index % 32;

		clk_data->clks[index] = clk_register_gate(NULL, clk_name,
							  parents[clk_parent],
							  0, clk_reg, clk_bit,
							  0, &gates_lock);
		i++;

		if (IS_ERR(clk_data->clks[index])) {
			WARN_ON(true);
			continue;
		}
	}

	clk_data->clk_num = number + 1;
	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	return;

err_free_data:
	kfree(clk_data);
err_unmap:
	iounmap(reg);
	of_address_to_resource(node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
}

CLK_OF_DECLARE(sun8i_h3_bus_gates, "allwinner,sun8i-h3-bus-gates-clk",
	       sun8i_h3_bus_gates_init);
CLK_OF_DECLARE(sun8i_a83t_bus_gates, "allwinner,sun8i-a83t-bus-gates-clk",
	       sun8i_h3_bus_gates_init);
