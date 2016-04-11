/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clkdev.h>
#include <linux/mfd/syscon.h>

#include "clk-mtk.h"
#include "clk-gate.h"

struct clk_onecell_data * __init mtk_alloc_clk_data(unsigned int clk_num)
{
	int i;
	struct clk_onecell_data *clk_data;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return NULL;

	clk_data->clks = kcalloc(clk_num, sizeof(*clk_data->clks), GFP_KERNEL);
	if (!clk_data->clks)
		goto err_out;

	clk_data->clk_num = clk_num;

	for (i = 0; i < clk_num; i++)
		clk_data->clks[i] = ERR_PTR(-ENOENT);

	return clk_data;
err_out:
	kfree(clk_data);

	return NULL;
}

void __init mtk_clk_register_fixed_clks(const struct mtk_fixed_clk *clks,
		int num, struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_clk *rc = &clks[i];

		clk = clk_register_fixed_rate(NULL, rc->name, rc->parent, 0,
					      rc->rate);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					rc->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[rc->id] = clk;
	}
}

void __init mtk_clk_register_factors(const struct mtk_fixed_factor *clks,
		int num, struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_factor *ff = &clks[i];

		clk = clk_register_fixed_factor(NULL, ff->name, ff->parent_name,
				CLK_SET_RATE_PARENT, ff->mult, ff->div);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					ff->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[ff->id] = clk;
	}
}

int __init mtk_clk_register_gates(struct device_node *node,
		const struct mtk_gate *clks,
		int num, struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;
	struct regmap *regmap;

	if (!clk_data)
		return -ENOMEM;

	regmap = syscon_node_to_regmap(node);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %s: %ld\n", node->full_name,
				PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	for (i = 0; i < num; i++) {
		const struct mtk_gate *gate = &clks[i];

		clk = mtk_clk_register_gate(gate->name, gate->parent_name,
				regmap,
				gate->regs->set_ofs,
				gate->regs->clr_ofs,
				gate->regs->sta_ofs,
				gate->shift, gate->ops);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					gate->name, PTR_ERR(clk));
			continue;
		}

		clk_data->clks[gate->id] = clk;
	}

	return 0;
}

struct clk * __init mtk_clk_register_composite(const struct mtk_composite *mc,
		void __iomem *base, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL, *div_hw = NULL;
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL, *div_ops = NULL;
	const char * const *parent_names;
	const char *parent;
	int num_parents;
	int ret;

	if (mc->mux_shift >= 0) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = base + mc->mux_reg;
		mux->mask = BIT(mc->mux_width) - 1;
		mux->shift = mc->mux_shift;
		mux->lock = lock;

		mux_hw = &mux->hw;
		mux_ops = &clk_mux_ops;

		parent_names = mc->parent_names;
		num_parents = mc->num_parents;
	} else {
		parent = mc->parent;
		parent_names = &parent;
		num_parents = 1;
	}

	if (mc->gate_shift >= 0) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			ret = -ENOMEM;
			goto err_out;
		}

		gate->reg = base + mc->gate_reg;
		gate->bit_idx = mc->gate_shift;
		gate->flags = CLK_GATE_SET_TO_DISABLE;
		gate->lock = lock;

		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	if (mc->divider_shift >= 0) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div) {
			ret = -ENOMEM;
			goto err_out;
		}

		div->reg = base + mc->divider_reg;
		div->shift = mc->divider_shift;
		div->width = mc->divider_width;
		div->lock = lock;

		div_hw = &div->hw;
		div_ops = &clk_divider_ops;
	}

	clk = clk_register_composite(NULL, mc->name, parent_names, num_parents,
		mux_hw, mux_ops,
		div_hw, div_ops,
		gate_hw, gate_ops,
		mc->flags);

	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_out;
	}

	return clk;
err_out:
	kfree(div);
	kfree(gate);
	kfree(mux);

	return ERR_PTR(ret);
}

void __init mtk_clk_register_composites(const struct mtk_composite *mcs,
		int num, void __iomem *base, spinlock_t *lock,
		struct clk_onecell_data *clk_data)
{
	struct clk *clk;
	int i;

	for (i = 0; i < num; i++) {
		const struct mtk_composite *mc = &mcs[i];

		clk = mtk_clk_register_composite(mc, base, lock);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					mc->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[mc->id] = clk;
	}
}
