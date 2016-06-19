/*
 * Copyright 2015 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
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
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(ve_lock);

#define SUN4I_VE_ENABLE		31
#define SUN4I_VE_DIVIDER_SHIFT	16
#define SUN4I_VE_DIVIDER_WIDTH	3
#define SUN4I_VE_RESET		0

/**
 * sunxi_ve_reset... - reset bit in ve clk registers handling
 */

struct ve_reset_data {
	void __iomem			*reg;
	spinlock_t			*lock;
	struct reset_controller_dev	rcdev;
};

static int sunxi_ve_reset_assert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct ve_reset_data *data = container_of(rcdev,
						  struct ve_reset_data,
						  rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg & ~BIT(SUN4I_VE_RESET), data->reg);

	spin_unlock_irqrestore(data->lock, flags);

	return 0;
}

static int sunxi_ve_reset_deassert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	struct ve_reset_data *data = container_of(rcdev,
						  struct ve_reset_data,
						  rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg | BIT(SUN4I_VE_RESET), data->reg);

	spin_unlock_irqrestore(data->lock, flags);

	return 0;
}

static int sunxi_ve_of_xlate(struct reset_controller_dev *rcdev,
			     const struct of_phandle_args *reset_spec)
{
	if (WARN_ON(reset_spec->args_count != 0))
		return -EINVAL;

	return 0;
}

static const struct reset_control_ops sunxi_ve_reset_ops = {
	.assert		= sunxi_ve_reset_assert,
	.deassert	= sunxi_ve_reset_deassert,
};

static void __init sun4i_ve_clk_setup(struct device_node *node)
{
	struct clk *clk;
	struct clk_divider *div;
	struct clk_gate *gate;
	struct ve_reset_data *reset_data;
	const char *parent;
	const char *clk_name = node->name;
	void __iomem *reg;
	int err;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg))
		return;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		goto err_unmap;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		goto err_free_div;

	of_property_read_string(node, "clock-output-names", &clk_name);
	parent = of_clk_get_parent_name(node, 0);

	gate->reg = reg;
	gate->bit_idx = SUN4I_VE_ENABLE;
	gate->lock = &ve_lock;

	div->reg = reg;
	div->shift = SUN4I_VE_DIVIDER_SHIFT;
	div->width = SUN4I_VE_DIVIDER_WIDTH;
	div->lock = &ve_lock;

	clk = clk_register_composite(NULL, clk_name, &parent, 1,
				     NULL, NULL,
				     &div->hw, &clk_divider_ops,
				     &gate->hw, &clk_gate_ops,
				     CLK_SET_RATE_PARENT);
	if (IS_ERR(clk))
		goto err_free_gate;

	err = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (err)
		goto err_unregister_clk;

	reset_data = kzalloc(sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		goto err_del_provider;

	reset_data->reg = reg;
	reset_data->lock = &ve_lock;
	reset_data->rcdev.nr_resets = 1;
	reset_data->rcdev.ops = &sunxi_ve_reset_ops;
	reset_data->rcdev.of_node = node;
	reset_data->rcdev.of_xlate = sunxi_ve_of_xlate;
	reset_data->rcdev.of_reset_n_cells = 0;
	err = reset_controller_register(&reset_data->rcdev);
	if (err)
		goto err_free_reset;

	return;

err_free_reset:
	kfree(reset_data);
err_del_provider:
	of_clk_del_provider(node);
err_unregister_clk:
	clk_unregister(clk);
err_free_gate:
	kfree(gate);
err_free_div:
	kfree(div);
err_unmap:
	iounmap(reg);
}
CLK_OF_DECLARE(sun4i_ve, "allwinner,sun4i-a10-ve-clk",
	       sun4i_ve_clk_setup);
