// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-mtk.h"
#include "clk-gate.h"

struct clk_onecell_data *mtk_alloc_clk_data(unsigned int clk_num)
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
EXPORT_SYMBOL_GPL(mtk_alloc_clk_data);

void mtk_free_clk_data(struct clk_onecell_data *clk_data)
{
	if (!clk_data)
		return;

	kfree(clk_data->clks);
	kfree(clk_data);
}

int mtk_clk_register_fixed_clks(const struct mtk_fixed_clk *clks, int num,
				struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_clk *rc = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->clks[rc->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n", rc->id);
			continue;
		}

		clk = clk_register_fixed_rate(NULL, rc->name, rc->parent, 0,
					      rc->rate);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %pe\n", rc->name, clk);
			goto err;
		}

		clk_data->clks[rc->id] = clk;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_fixed_clk *rc = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->clks[rc->id]))
			continue;

		clk_unregister_fixed_rate(clk_data->clks[rc->id]);
		clk_data->clks[rc->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_fixed_clks);

void mtk_clk_unregister_fixed_clks(const struct mtk_fixed_clk *clks, int num,
				   struct clk_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_fixed_clk *rc = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->clks[rc->id]))
			continue;

		clk_unregister_fixed_rate(clk_data->clks[rc->id]);
		clk_data->clks[rc->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_fixed_clks);

int mtk_clk_register_factors(const struct mtk_fixed_factor *clks, int num,
			     struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_factor *ff = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->clks[ff->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n", ff->id);
			continue;
		}

		clk = clk_register_fixed_factor(NULL, ff->name, ff->parent_name,
				CLK_SET_RATE_PARENT, ff->mult, ff->div);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %pe\n", ff->name, clk);
			goto err;
		}

		clk_data->clks[ff->id] = clk;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_fixed_factor *ff = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->clks[ff->id]))
			continue;

		clk_unregister_fixed_factor(clk_data->clks[ff->id]);
		clk_data->clks[ff->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_factors);

void mtk_clk_unregister_factors(const struct mtk_fixed_factor *clks, int num,
				struct clk_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_fixed_factor *ff = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->clks[ff->id]))
			continue;

		clk_unregister_fixed_factor(clk_data->clks[ff->id]);
		clk_data->clks[ff->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_factors);

struct clk *mtk_clk_register_composite(const struct mtk_composite *mc,
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
		mux->flags = mc->mux_flags;
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

static void mtk_clk_unregister_composite(struct clk *clk)
{
	struct clk_hw *hw;
	struct clk_composite *composite;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;

	hw = __clk_get_hw(clk);
	if (!hw)
		return;

	composite = to_clk_composite(hw);
	if (composite->mux_hw)
		mux = to_clk_mux(composite->mux_hw);
	if (composite->gate_hw)
		gate = to_clk_gate(composite->gate_hw);
	if (composite->rate_hw)
		div = to_clk_divider(composite->rate_hw);

	clk_unregister_composite(clk);
	kfree(div);
	kfree(gate);
	kfree(mux);
}

int mtk_clk_register_composites(const struct mtk_composite *mcs, int num,
				void __iomem *base, spinlock_t *lock,
				struct clk_onecell_data *clk_data)
{
	struct clk *clk;
	int i;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		const struct mtk_composite *mc = &mcs[i];

		if (!IS_ERR_OR_NULL(clk_data->clks[mc->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n",
				mc->id);
			continue;
		}

		clk = mtk_clk_register_composite(mc, base, lock);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %pe\n", mc->name, clk);
			goto err;
		}

		clk_data->clks[mc->id] = clk;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_composite *mc = &mcs[i];

		if (IS_ERR_OR_NULL(clk_data->clks[mcs->id]))
			continue;

		mtk_clk_unregister_composite(clk_data->clks[mc->id]);
		clk_data->clks[mc->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_composites);

void mtk_clk_unregister_composites(const struct mtk_composite *mcs, int num,
				   struct clk_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_composite *mc = &mcs[i - 1];

		if (IS_ERR_OR_NULL(clk_data->clks[mc->id]))
			continue;

		mtk_clk_unregister_composite(clk_data->clks[mc->id]);
		clk_data->clks[mc->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_composites);

int mtk_clk_register_dividers(const struct mtk_clk_divider *mcds, int num,
			      void __iomem *base, spinlock_t *lock,
			      struct clk_onecell_data *clk_data)
{
	struct clk *clk;
	int i;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i <  num; i++) {
		const struct mtk_clk_divider *mcd = &mcds[i];

		if (!IS_ERR_OR_NULL(clk_data->clks[mcd->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n",
				mcd->id);
			continue;
		}

		clk = clk_register_divider(NULL, mcd->name, mcd->parent_name,
			mcd->flags, base +  mcd->div_reg, mcd->div_shift,
			mcd->div_width, mcd->clk_divider_flags, lock);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %pe\n", mcd->name, clk);
			goto err;
		}

		clk_data->clks[mcd->id] = clk;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_clk_divider *mcd = &mcds[i];

		if (IS_ERR_OR_NULL(clk_data->clks[mcd->id]))
			continue;

		mtk_clk_unregister_composite(clk_data->clks[mcd->id]);
		clk_data->clks[mcd->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(clk);
}

void mtk_clk_unregister_dividers(const struct mtk_clk_divider *mcds, int num,
				 struct clk_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_clk_divider *mcd = &mcds[i - 1];

		if (IS_ERR_OR_NULL(clk_data->clks[mcd->id]))
			continue;

		clk_unregister_divider(clk_data->clks[mcd->id]);
		clk_data->clks[mcd->id] = ERR_PTR(-ENOENT);
	}
}

int mtk_clk_simple_probe(struct platform_device *pdev)
{
	const struct mtk_clk_desc *mcd;
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	mcd = of_device_get_match_data(&pdev->dev);
	if (!mcd)
		return -EINVAL;

	clk_data = mtk_alloc_clk_data(mcd->num_clks);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_gates(node, mcd->clks, mcd->num_clks, clk_data);
	if (r)
		goto free_data;

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		goto unregister_clks;

	platform_set_drvdata(pdev, clk_data);

	return r;

unregister_clks:
	mtk_clk_unregister_gates(mcd->clks, mcd->num_clks, clk_data);
free_data:
	mtk_free_clk_data(clk_data);
	return r;
}

int mtk_clk_simple_remove(struct platform_device *pdev)
{
	const struct mtk_clk_desc *mcd = of_device_get_match_data(&pdev->dev);
	struct clk_onecell_data *clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_gates(mcd->clks, mcd->num_clks, clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

MODULE_LICENSE("GPL");
