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
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "clk-mtk.h"
#include "clk-gate.h"
#include "clk-mux.h"

const struct mtk_gate_regs cg_regs_dummy = { 0, 0, 0 };
EXPORT_SYMBOL_GPL(cg_regs_dummy);

static int mtk_clk_dummy_enable(struct clk_hw *hw)
{
	return 0;
}

static void mtk_clk_dummy_disable(struct clk_hw *hw) { }

const struct clk_ops mtk_clk_dummy_ops = {
	.enable		= mtk_clk_dummy_enable,
	.disable	= mtk_clk_dummy_disable,
};
EXPORT_SYMBOL_GPL(mtk_clk_dummy_ops);

static void mtk_init_clk_data(struct clk_hw_onecell_data *clk_data,
			      unsigned int clk_num)
{
	int i;

	clk_data->num = clk_num;

	for (i = 0; i < clk_num; i++)
		clk_data->hws[i] = ERR_PTR(-ENOENT);
}

struct clk_hw_onecell_data *mtk_devm_alloc_clk_data(struct device *dev,
						    unsigned int clk_num)
{
	struct clk_hw_onecell_data *clk_data;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, clk_num),
				GFP_KERNEL);
	if (!clk_data)
		return NULL;

	mtk_init_clk_data(clk_data, clk_num);

	return clk_data;
}
EXPORT_SYMBOL_GPL(mtk_devm_alloc_clk_data);

struct clk_hw_onecell_data *mtk_alloc_clk_data(unsigned int clk_num)
{
	struct clk_hw_onecell_data *clk_data;

	clk_data = kzalloc(struct_size(clk_data, hws, clk_num), GFP_KERNEL);
	if (!clk_data)
		return NULL;

	mtk_init_clk_data(clk_data, clk_num);

	return clk_data;
}
EXPORT_SYMBOL_GPL(mtk_alloc_clk_data);

void mtk_free_clk_data(struct clk_hw_onecell_data *clk_data)
{
	kfree(clk_data);
}
EXPORT_SYMBOL_GPL(mtk_free_clk_data);

int mtk_clk_register_fixed_clks(const struct mtk_fixed_clk *clks, int num,
				struct clk_hw_onecell_data *clk_data)
{
	int i;
	struct clk_hw *hw;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_clk *rc = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[rc->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n", rc->id);
			continue;
		}

		hw = clk_hw_register_fixed_rate(NULL, rc->name, rc->parent, 0,
					      rc->rate);

		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", rc->name,
			       hw);
			goto err;
		}

		clk_data->hws[rc->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_fixed_clk *rc = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->hws[rc->id]))
			continue;

		clk_hw_unregister_fixed_rate(clk_data->hws[rc->id]);
		clk_data->hws[rc->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_fixed_clks);

void mtk_clk_unregister_fixed_clks(const struct mtk_fixed_clk *clks, int num,
				   struct clk_hw_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_fixed_clk *rc = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[rc->id]))
			continue;

		clk_hw_unregister_fixed_rate(clk_data->hws[rc->id]);
		clk_data->hws[rc->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_fixed_clks);

int mtk_clk_register_factors(const struct mtk_fixed_factor *clks, int num,
			     struct clk_hw_onecell_data *clk_data)
{
	int i;
	struct clk_hw *hw;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_factor *ff = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[ff->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n", ff->id);
			continue;
		}

		hw = clk_hw_register_fixed_factor(NULL, ff->name, ff->parent_name,
				ff->flags, ff->mult, ff->div);

		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", ff->name,
			       hw);
			goto err;
		}

		clk_data->hws[ff->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_fixed_factor *ff = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->hws[ff->id]))
			continue;

		clk_hw_unregister_fixed_factor(clk_data->hws[ff->id]);
		clk_data->hws[ff->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_factors);

void mtk_clk_unregister_factors(const struct mtk_fixed_factor *clks, int num,
				struct clk_hw_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_fixed_factor *ff = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[ff->id]))
			continue;

		clk_hw_unregister_fixed_factor(clk_data->hws[ff->id]);
		clk_data->hws[ff->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_factors);

static struct clk_hw *mtk_clk_register_composite(struct device *dev,
		const struct mtk_composite *mc, void __iomem *base, spinlock_t *lock)
{
	struct clk_hw *hw;
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

	hw = clk_hw_register_composite(dev, mc->name, parent_names, num_parents,
		mux_hw, mux_ops,
		div_hw, div_ops,
		gate_hw, gate_ops,
		mc->flags);

	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		goto err_out;
	}

	return hw;
err_out:
	kfree(div);
	kfree(gate);
	kfree(mux);

	return ERR_PTR(ret);
}

static void mtk_clk_unregister_composite(struct clk_hw *hw)
{
	struct clk_composite *composite;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;

	if (!hw)
		return;

	composite = to_clk_composite(hw);
	if (composite->mux_hw)
		mux = to_clk_mux(composite->mux_hw);
	if (composite->gate_hw)
		gate = to_clk_gate(composite->gate_hw);
	if (composite->rate_hw)
		div = to_clk_divider(composite->rate_hw);

	clk_hw_unregister_composite(hw);
	kfree(div);
	kfree(gate);
	kfree(mux);
}

int mtk_clk_register_composites(struct device *dev,
				const struct mtk_composite *mcs, int num,
				void __iomem *base, spinlock_t *lock,
				struct clk_hw_onecell_data *clk_data)
{
	struct clk_hw *hw;
	int i;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		const struct mtk_composite *mc = &mcs[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[mc->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n",
				mc->id);
			continue;
		}

		hw = mtk_clk_register_composite(dev, mc, base, lock);

		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", mc->name,
			       hw);
			goto err;
		}

		clk_data->hws[mc->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_composite *mc = &mcs[i];

		if (IS_ERR_OR_NULL(clk_data->hws[mcs->id]))
			continue;

		mtk_clk_unregister_composite(clk_data->hws[mc->id]);
		clk_data->hws[mc->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_composites);

void mtk_clk_unregister_composites(const struct mtk_composite *mcs, int num,
				   struct clk_hw_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_composite *mc = &mcs[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[mc->id]))
			continue;

		mtk_clk_unregister_composite(clk_data->hws[mc->id]);
		clk_data->hws[mc->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_composites);

int mtk_clk_register_dividers(struct device *dev,
			      const struct mtk_clk_divider *mcds, int num,
			      void __iomem *base, spinlock_t *lock,
			      struct clk_hw_onecell_data *clk_data)
{
	struct clk_hw *hw;
	int i;

	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i <  num; i++) {
		const struct mtk_clk_divider *mcd = &mcds[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[mcd->id])) {
			pr_warn("Trying to register duplicate clock ID: %d\n",
				mcd->id);
			continue;
		}

		hw = clk_hw_register_divider(dev, mcd->name, mcd->parent_name,
			mcd->flags, base +  mcd->div_reg, mcd->div_shift,
			mcd->div_width, mcd->clk_divider_flags, lock);

		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", mcd->name,
			       hw);
			goto err;
		}

		clk_data->hws[mcd->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_clk_divider *mcd = &mcds[i];

		if (IS_ERR_OR_NULL(clk_data->hws[mcd->id]))
			continue;

		clk_hw_unregister_divider(clk_data->hws[mcd->id]);
		clk_data->hws[mcd->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_dividers);

void mtk_clk_unregister_dividers(const struct mtk_clk_divider *mcds, int num,
				 struct clk_hw_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_clk_divider *mcd = &mcds[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[mcd->id]))
			continue;

		clk_hw_unregister_divider(clk_data->hws[mcd->id]);
		clk_data->hws[mcd->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_dividers);

static int __mtk_clk_simple_probe(struct platform_device *pdev,
				  struct device_node *node)
{
	const struct platform_device_id *id;
	const struct mtk_clk_desc *mcd;
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base = NULL;
	int num_clks, r;

	mcd = device_get_match_data(&pdev->dev);
	if (!mcd) {
		/* Clock driver wasn't registered from devicetree */
		id = platform_get_device_id(pdev);
		if (id)
			mcd = (const struct mtk_clk_desc *)id->driver_data;

		if (!mcd)
			return -EINVAL;
	}

	/* Composite and divider clocks needs us to pass iomem pointer */
	if (mcd->composite_clks || mcd->divider_clks) {
		if (!mcd->shared_io)
			base = devm_platform_ioremap_resource(pdev, 0);
		else
			base = of_iomap(node, 0);

		if (IS_ERR_OR_NULL(base))
			return IS_ERR(base) ? PTR_ERR(base) : -ENOMEM;
	}


	devm_pm_runtime_enable(&pdev->dev);
	/*
	 * Do a pm_runtime_resume_and_get() to workaround a possible
	 * deadlock between clk_register() and the genpd framework.
	 */
	r = pm_runtime_resume_and_get(&pdev->dev);
	if (r)
		return r;

	/* Calculate how many clk_hw_onecell_data entries to allocate */
	num_clks = mcd->num_clks + mcd->num_composite_clks;
	num_clks += mcd->num_fixed_clks + mcd->num_factor_clks;
	num_clks += mcd->num_mux_clks + mcd->num_divider_clks;

	clk_data = mtk_alloc_clk_data(num_clks);
	if (!clk_data) {
		r = -ENOMEM;
		goto free_base;
	}

	if (mcd->fixed_clks) {
		r = mtk_clk_register_fixed_clks(mcd->fixed_clks,
						mcd->num_fixed_clks, clk_data);
		if (r)
			goto free_data;
	}

	if (mcd->factor_clks) {
		r = mtk_clk_register_factors(mcd->factor_clks,
					     mcd->num_factor_clks, clk_data);
		if (r)
			goto unregister_fixed_clks;
	}

	if (mcd->mux_clks) {
		r = mtk_clk_register_muxes(&pdev->dev, mcd->mux_clks,
					   mcd->num_mux_clks, node,
					   mcd->clk_lock, clk_data);
		if (r)
			goto unregister_factors;
	}

	if (mcd->composite_clks) {
		/* We don't check composite_lock because it's optional */
		r = mtk_clk_register_composites(&pdev->dev,
						mcd->composite_clks,
						mcd->num_composite_clks,
						base, mcd->clk_lock, clk_data);
		if (r)
			goto unregister_muxes;
	}

	if (mcd->divider_clks) {
		r = mtk_clk_register_dividers(&pdev->dev,
					      mcd->divider_clks,
					      mcd->num_divider_clks,
					      base, mcd->clk_lock, clk_data);
		if (r)
			goto unregister_composites;
	}

	if (mcd->clks) {
		r = mtk_clk_register_gates(&pdev->dev, node, mcd->clks,
					   mcd->num_clks, clk_data);
		if (r)
			goto unregister_dividers;
	}

	if (mcd->clk_notifier_func) {
		struct clk *mfg_mux = clk_data->hws[mcd->mfg_clk_idx]->clk;

		r = mcd->clk_notifier_func(&pdev->dev, mfg_mux);
		if (r)
			goto unregister_clks;
	}

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		goto unregister_clks;

	platform_set_drvdata(pdev, clk_data);

	if (mcd->rst_desc) {
		r = mtk_register_reset_controller_with_dev(&pdev->dev,
							   mcd->rst_desc);
		if (r)
			goto unregister_clks;
	}

	pm_runtime_put(&pdev->dev);

	return r;

unregister_clks:
	if (mcd->clks)
		mtk_clk_unregister_gates(mcd->clks, mcd->num_clks, clk_data);
unregister_dividers:
	if (mcd->divider_clks)
		mtk_clk_unregister_dividers(mcd->divider_clks,
					    mcd->num_divider_clks, clk_data);
unregister_composites:
	if (mcd->composite_clks)
		mtk_clk_unregister_composites(mcd->composite_clks,
					      mcd->num_composite_clks, clk_data);
unregister_muxes:
	if (mcd->mux_clks)
		mtk_clk_unregister_muxes(mcd->mux_clks,
					 mcd->num_mux_clks, clk_data);
unregister_factors:
	if (mcd->factor_clks)
		mtk_clk_unregister_factors(mcd->factor_clks,
					   mcd->num_factor_clks, clk_data);
unregister_fixed_clks:
	if (mcd->fixed_clks)
		mtk_clk_unregister_fixed_clks(mcd->fixed_clks,
					      mcd->num_fixed_clks, clk_data);
free_data:
	mtk_free_clk_data(clk_data);
free_base:
	if (mcd->shared_io && base)
		iounmap(base);

	pm_runtime_put(&pdev->dev);
	return r;
}

static void __mtk_clk_simple_remove(struct platform_device *pdev,
				   struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);
	const struct mtk_clk_desc *mcd = device_get_match_data(&pdev->dev);

	of_clk_del_provider(node);
	if (mcd->clks)
		mtk_clk_unregister_gates(mcd->clks, mcd->num_clks, clk_data);
	if (mcd->divider_clks)
		mtk_clk_unregister_dividers(mcd->divider_clks,
					    mcd->num_divider_clks, clk_data);
	if (mcd->composite_clks)
		mtk_clk_unregister_composites(mcd->composite_clks,
					      mcd->num_composite_clks, clk_data);
	if (mcd->mux_clks)
		mtk_clk_unregister_muxes(mcd->mux_clks,
					 mcd->num_mux_clks, clk_data);
	if (mcd->factor_clks)
		mtk_clk_unregister_factors(mcd->factor_clks,
					   mcd->num_factor_clks, clk_data);
	if (mcd->fixed_clks)
		mtk_clk_unregister_fixed_clks(mcd->fixed_clks,
					      mcd->num_fixed_clks, clk_data);
	mtk_free_clk_data(clk_data);
}

int mtk_clk_pdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->parent->of_node;

	return __mtk_clk_simple_probe(pdev, node);
}
EXPORT_SYMBOL_GPL(mtk_clk_pdev_probe);

int mtk_clk_simple_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;

	return __mtk_clk_simple_probe(pdev, node);
}
EXPORT_SYMBOL_GPL(mtk_clk_simple_probe);

void mtk_clk_pdev_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->parent->of_node;

	__mtk_clk_simple_remove(pdev, node);
}
EXPORT_SYMBOL_GPL(mtk_clk_pdev_remove);

void mtk_clk_simple_remove(struct platform_device *pdev)
{
	__mtk_clk_simple_remove(pdev, pdev->dev.of_node);
}
EXPORT_SYMBOL_GPL(mtk_clk_simple_remove);

MODULE_LICENSE("GPL");
