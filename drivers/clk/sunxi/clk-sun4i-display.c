/*
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
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct sun4i_a10_display_clk_data {
	bool	has_div;
	u8	num_rst;
	u8	parents;

	u8	offset_en;
	u8	offset_div;
	u8	offset_mux;
	u8	offset_rst;

	u8	width_div;
	u8	width_mux;
};

struct reset_data {
	void __iomem			*reg;
	spinlock_t			*lock;
	struct reset_controller_dev	rcdev;
	u8				offset;
};

static DEFINE_SPINLOCK(sun4i_a10_display_lock);

static inline struct reset_data *rcdev_to_reset_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct reset_data, rcdev);
};

static int sun4i_a10_display_assert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct reset_data *data = rcdev_to_reset_data(rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg & ~BIT(data->offset + id), data->reg);

	spin_unlock_irqrestore(data->lock, flags);

	return 0;
}

static int sun4i_a10_display_deassert(struct reset_controller_dev *rcdev,
				      unsigned long id)
{
	struct reset_data *data = rcdev_to_reset_data(rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg | BIT(data->offset + id), data->reg);

	spin_unlock_irqrestore(data->lock, flags);

	return 0;
}

static int sun4i_a10_display_status(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct reset_data *data = rcdev_to_reset_data(rcdev);

	return !(readl(data->reg) & BIT(data->offset + id));
}

static const struct reset_control_ops sun4i_a10_display_reset_ops = {
	.assert		= sun4i_a10_display_assert,
	.deassert	= sun4i_a10_display_deassert,
	.status		= sun4i_a10_display_status,
};

static int sun4i_a10_display_reset_xlate(struct reset_controller_dev *rcdev,
					 const struct of_phandle_args *spec)
{
	/* We only have a single reset signal */
	return 0;
}

static void __init sun4i_a10_display_init(struct device_node *node,
					  const struct sun4i_a10_display_clk_data *data)
{
	const char *parents[4];
	const char *clk_name = node->name;
	struct reset_data *reset_data;
	struct clk_divider *div = NULL;
	struct clk_gate *gate;
	struct resource res;
	struct clk_mux *mux;
	void __iomem *reg;
	struct clk *clk;
	int ret;

	of_property_read_string(node, "clock-output-names", &clk_name);

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%s: Could not map the clock registers\n", clk_name);
		return;
	}

	ret = of_clk_parent_fill(node, parents, data->parents);
	if (ret != data->parents) {
		pr_err("%s: Could not retrieve the parents\n", clk_name);
		goto unmap;
	}

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		goto unmap;

	mux->reg = reg;
	mux->shift = data->offset_mux;
	mux->mask = (1 << data->width_mux) - 1;
	mux->lock = &sun4i_a10_display_lock;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		goto free_mux;

	gate->reg = reg;
	gate->bit_idx = data->offset_en;
	gate->lock = &sun4i_a10_display_lock;

	if (data->has_div) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div)
			goto free_gate;

		div->reg = reg;
		div->shift = data->offset_div;
		div->width = data->width_div;
		div->lock = &sun4i_a10_display_lock;
	}

	clk = clk_register_composite(NULL, clk_name,
				     parents, data->parents,
				     &mux->hw, &clk_mux_ops,
				     data->has_div ? &div->hw : NULL,
				     data->has_div ? &clk_divider_ops : NULL,
				     &gate->hw, &clk_gate_ops,
				     0);
	if (IS_ERR(clk)) {
		pr_err("%s: Couldn't register the clock\n", clk_name);
		goto free_div;
	}

	ret = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (ret) {
		pr_err("%s: Couldn't register DT provider\n", clk_name);
		goto free_clk;
	}

	if (!data->num_rst)
		return;

	reset_data = kzalloc(sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		goto free_of_clk;

	reset_data->reg = reg;
	reset_data->offset = data->offset_rst;
	reset_data->lock = &sun4i_a10_display_lock;
	reset_data->rcdev.nr_resets = data->num_rst;
	reset_data->rcdev.ops = &sun4i_a10_display_reset_ops;
	reset_data->rcdev.of_node = node;

	if (data->num_rst == 1) {
		reset_data->rcdev.of_reset_n_cells = 0;
		reset_data->rcdev.of_xlate = &sun4i_a10_display_reset_xlate;
	} else {
		reset_data->rcdev.of_reset_n_cells = 1;
	}

	if (reset_controller_register(&reset_data->rcdev)) {
		pr_err("%s: Couldn't register the reset controller\n",
		       clk_name);
		goto free_reset;
	}

	return;

free_reset:
	kfree(reset_data);
free_of_clk:
	of_clk_del_provider(node);
free_clk:
	clk_unregister_composite(clk);
free_div:
	kfree(div);
free_gate:
	kfree(gate);
free_mux:
	kfree(mux);
unmap:
	iounmap(reg);
	of_address_to_resource(node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
}

static const struct sun4i_a10_display_clk_data sun4i_a10_tcon_ch0_data __initconst = {
	.num_rst	= 2,
	.parents	= 4,
	.offset_en	= 31,
	.offset_rst	= 29,
	.offset_mux	= 24,
	.width_mux	= 2,
};

static void __init sun4i_a10_tcon_ch0_setup(struct device_node *node)
{
	sun4i_a10_display_init(node, &sun4i_a10_tcon_ch0_data);
}
CLK_OF_DECLARE(sun4i_a10_tcon_ch0, "allwinner,sun4i-a10-tcon-ch0-clk",
	       sun4i_a10_tcon_ch0_setup);

static const struct sun4i_a10_display_clk_data sun4i_a10_display_data __initconst = {
	.has_div	= true,
	.num_rst	= 1,
	.parents	= 3,
	.offset_en	= 31,
	.offset_rst	= 30,
	.offset_mux	= 24,
	.offset_div	= 0,
	.width_mux	= 2,
	.width_div	= 4,
};

static void __init sun4i_a10_display_setup(struct device_node *node)
{
	sun4i_a10_display_init(node, &sun4i_a10_display_data);
}
CLK_OF_DECLARE(sun4i_a10_display, "allwinner,sun4i-a10-display-clk",
	       sun4i_a10_display_setup);
