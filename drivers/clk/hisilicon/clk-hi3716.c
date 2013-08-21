/*
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2013 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

static DEFINE_SPINLOCK(_lock);

static void __iomem *clk_base;

struct hi3716_clk {
	struct		clk_gate gate;
	void __iomem	*reg;
	u8		reset_bit;
};

#define MAX_NUMS	10

static struct hi3716_clk *to_clk_hi3716(struct clk_hw *hw)
{
	return container_of(hw, struct hi3716_clk, gate.hw);
}

static void __init hi3716_map_io(void)
{
	struct device_node *node;

	if (clk_base)
		return;

	node = of_find_compatible_node(NULL, NULL, "hisilicon,clkbase");
	if (node)
		clk_base = of_iomap(node, 0);
	WARN_ON(!clk_base);
}

static int hi3716_clkgate_prepare(struct clk_hw *hw)
{
	struct hi3716_clk *clk = to_clk_hi3716(hw);
	unsigned long flags = 0;
	u32 reg;

	spin_lock_irqsave(&_lock, flags);

	reg = readl_relaxed(clk->reg);
	reg &= ~BIT(clk->reset_bit);
	writel_relaxed(reg, clk->reg);

	spin_unlock_irqrestore(&_lock, flags);

	return 0;
}

static void hi3716_clkgate_unprepare(struct clk_hw *hw)
{
	struct hi3716_clk *clk = to_clk_hi3716(hw);
	unsigned long flags = 0;
	u32 reg;

	spin_lock_irqsave(&_lock, flags);

	reg = readl_relaxed(clk->reg);
	reg |= BIT(clk->reset_bit);
	writel_relaxed(reg, clk->reg);

	spin_unlock_irqrestore(&_lock, flags);
}

static struct clk_ops hi3716_clkgate_ops = {
	.prepare	= hi3716_clkgate_prepare,
	.unprepare	= hi3716_clkgate_unprepare,
};

void __init hi3716_clkgate_setup(struct device_node *node)
{
	struct clk *clk;
	struct hi3716_clk *p_clk;
	struct clk_init_data init;
	const char *parent_name;
	u32 array[3];	/* reg, enable_bit, reset_bit */
	int err;

	hi3716_map_io();
	err = of_property_read_u32_array(node, "gate-reg", &array[0], 3);
	if (WARN_ON(err))
		return;

	err = of_property_read_string(node, "clock-output-names", &init.name);
	if (WARN_ON(err))
		return;

	p_clk = kzalloc(sizeof(*p_clk), GFP_KERNEL);
	if (WARN_ON(!p_clk))
		return;

	hi3716_clkgate_ops.enable = clk_gate_ops.enable;
	hi3716_clkgate_ops.disable = clk_gate_ops.disable;
	hi3716_clkgate_ops.is_enabled = clk_gate_ops.is_enabled;

	init.ops = &hi3716_clkgate_ops;
	init.flags = CLK_SET_RATE_PARENT;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	p_clk->reg = p_clk->gate.reg = clk_base + array[0];
	p_clk->gate.bit_idx = array[1];
	p_clk->gate.flags = 0;
	p_clk->gate.lock = &_lock;
	p_clk->gate.hw.init = &init;
	p_clk->reset_bit = array[2];

	clk = clk_register(NULL, &p_clk->gate.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(p_clk);
		return;
	}

	of_clk_add_provider(node, of_clk_src_simple_get, clk);
}

static void __init hi3716_clkmux_setup(struct device_node *node)
{
	int num = 0, err;
	void __iomem *reg;
	unsigned int shift, width;
	u32 array[3];	/* reg, mux_shift, mux_width */
	u32 *table = NULL;
	const char *clk_name = node->name;
	const char *parents[MAX_NUMS];
	struct clk *clk;

	hi3716_map_io();
	err = of_property_read_string(node, "clock-output-names", &clk_name);
	if (WARN_ON(err))
		return;

	err = of_property_read_u32_array(node, "mux-reg", &array[0], 3);
	if (WARN_ON(err))
		return;

	reg = clk_base + array[0];
	shift = array[1];
	width = array[2];

	while ((num < MAX_NUMS) &&
		((parents[num] = of_clk_get_parent_name(node, num)) != NULL))
		num++;
	if (!num)
		return;

	table = kzalloc(sizeof(u32 *) * num, GFP_KERNEL);
	if (WARN_ON(!table))
		return;

	err = of_property_read_u32_array(node, "mux-table", table, num);
	if (WARN_ON(err))
		goto err;

	clk = clk_register_mux_table(NULL, clk_name, parents, num,
			CLK_SET_RATE_PARENT, reg, shift, BIT(width) - 1,
			0, table, &_lock);
	if (WARN_ON(IS_ERR(clk)))
		goto err;

	clk_register_clkdev(clk, clk_name, NULL);
	of_clk_add_provider(node, of_clk_src_simple_get, clk);
	return;

err:
	kfree(table);
	return;
}

static void __init hi3716_fixed_pll_setup(struct device_node *node)
{
	const char *clk_name, *parent_name;
	struct clk *clks[MAX_NUMS];
	u32 rate[MAX_NUMS];
	struct clk_onecell_data *clk_data;
	int i, err, nums = 0;

	nums = of_property_count_strings(node, "clock-output-names");
	if (WARN_ON((nums < 0) || (nums > MAX_NUMS)))
		return;

	err = of_property_read_u32_array(node, "clock-frequency",
						&rate[0], nums);
	WARN_ON(err);

	parent_name = of_clk_get_parent_name(node, 0);

	for (i = 0; i < nums; i++) {
		err = of_property_read_string_index(node, "clock-output-names",
				i, &clk_name);
		WARN_ON(err);

		clks[i] = clk_register_fixed_rate(NULL, clk_name,
					parent_name, 0, rate[i]);
		WARN_ON(IS_ERR(clks[i]));
	}

	clk_data = kzalloc(sizeof(*clk_data) + nums * sizeof(struct clk *),
			GFP_KERNEL);
	if (WARN_ON(!clk_data))
		return;

	memcpy(&clk_data[1], clks, nums * sizeof(struct clk *));
	clk_data->clks = (struct clk **)&clk_data[1];
	clk_data->clk_num = nums;
	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

void __init hi3716_fixed_divider_setup(struct device_node *node)
{
	const char *clk_parent;
	const char *clk_name;
	u32 div[MAX_NUMS];
	struct clk *clks[MAX_NUMS];
	struct clk_onecell_data *clk_data;
	int err, i, nums = 0;

	clk_parent = of_clk_get_parent_name(node, 0);

	nums = of_property_count_strings(node, "clock-output-names");
	if (WARN_ON((nums < 0) || (nums > MAX_NUMS)))
		return;

	err = of_property_read_u32_array(node, "div-table", &div[0], nums);
	WARN_ON(err);

	for (i = 0; i < nums; i++) {
		err = of_property_read_string_index(node,
				"clock-output-names", i, &clk_name);
		WARN_ON(err);

		clks[i] = clk_register_fixed_factor(NULL, clk_name,
				clk_parent, CLK_SET_RATE_PARENT, 1, div[i]);
		WARN_ON(IS_ERR(clks[i]));
	}

	clk_data = kzalloc(sizeof(*clk_data) + nums * sizeof(struct clk *),
			GFP_KERNEL);
	if (WARN_ON(!clk_data))
		return;

	memcpy(&clk_data[1], clks, nums * sizeof(struct clk *));
	clk_data->clks = (struct clk **)&clk_data[1];
	clk_data->clk_num = nums;
	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

CLK_OF_DECLARE(hi3716_fixed_rate, "fixed-clock", of_fixed_clk_setup)
CLK_OF_DECLARE(hi3716_fixed_pll, "hisilicon,hi3716-fixed-pll", hi3716_fixed_pll_setup)
CLK_OF_DECLARE(hi3716_divider, "hisilicon,hi3716-fixed-divider", hi3716_fixed_divider_setup)
CLK_OF_DECLARE(hi3716_mux, "hisilicon,hi3716-clk-mux", hi3716_clkmux_setup)
CLK_OF_DECLARE(hi3716_gate, "hisilicon,hi3716-clk-gate", hi3716_clkgate_setup)
