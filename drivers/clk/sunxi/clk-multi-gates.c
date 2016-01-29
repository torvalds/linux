/*
 * Copyright (C) 2016 ARM Ltd.
 *
 * Based on clk-sun8i-bus-gates.c, which is:
 *  Copyright (C) 2015 Jens Kuske <jenskuske@gmail.com>
 * Based on clk-simple-gates.c, which is:
 *  Copyright 2015 Maxime Ripard <maxime.ripard@free-electrons.com>
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

static void __init sunxi_parse_parent(struct device_node *node,
				      struct clk_onecell_data *clk_data,
				      void __iomem *reg)
{
	const char *parent = of_clk_get_parent_name(node, 0);
	const char *clk_name;
	struct property *prop;
	struct clk *clk;
	const __be32 *p;
	int index, i = 0;

	of_property_for_each_u32(node, "clock-indices", prop, p, index) {
		of_property_read_string_index(node, "clock-output-names",
					      i, &clk_name);

		clk = clk_register_gate(NULL, clk_name, parent, 0,
					reg + 4 * (index / 32), index % 32,
					0, &gates_lock);
		i++;
		if (IS_ERR(clk)) {
			pr_warn("could not register gate clock \"%s\"\n",
				clk_name);
			continue;
		}
		if (clk_data->clks[index])
			pr_warn("bus-gate clock %s: index #%d already registered as %s\n",
				clk_name, index, "?");
		else
			clk_data->clks[index] = clk;
	}
}

static void __init sunxi_multi_bus_gates_init(struct device_node *node)
{
	struct clk_onecell_data *clk_data;
	struct device_node *child;
	struct property *prop;
	struct resource res;
	void __iomem *reg;
	const __be32 *p;
	int number = 0;
	int index;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg))
		return;

	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		goto err_unmap;

	for_each_child_of_node(node, child)
		of_property_for_each_u32(child, "clock-indices", prop, p, index)
			number = max(number, index);

	clk_data->clks = kcalloc(number + 1, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks)
		goto err_free_data;

	for_each_child_of_node(node, child)
		sunxi_parse_parent(child, clk_data, reg);

	clk_data->clk_num = number + 1;
	if (of_clk_add_provider(node, of_clk_src_onecell_get, clk_data))
		pr_err("registering bus-gate clock %s failed\n", node->name);

	return;

err_free_data:
	kfree(clk_data);
err_unmap:
	iounmap(reg);
	of_address_to_resource(node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
}

CLK_OF_DECLARE(sunxi_multi_bus_gates, "allwinner,sunxi-multi-bus-gates-clk",
	       sunxi_multi_bus_gates_init);
