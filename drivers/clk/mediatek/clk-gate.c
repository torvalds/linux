// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/dev_printk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "clk-mtk.h"
#include "clk-gate.h"

struct mtk_clk_gate {
	struct clk_hw	hw;
	struct regmap	*regmap;
	struct regmap	*regmap_hwv;
	const struct mtk_gate *gate;
};

static inline struct mtk_clk_gate *to_mtk_clk_gate(struct clk_hw *hw)
{
	return container_of(hw, struct mtk_clk_gate, hw);
}

static u32 mtk_get_clockgating(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);
	u32 val;

	regmap_read(cg->regmap, cg->gate->regs->sta_ofs, &val);

	return val & BIT(cg->gate->shift);
}

static int mtk_cg_bit_is_cleared(struct clk_hw *hw)
{
	return mtk_get_clockgating(hw) == 0;
}

static int mtk_cg_bit_is_set(struct clk_hw *hw)
{
	return mtk_get_clockgating(hw) != 0;
}

static void mtk_cg_set_bit(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_write(cg->regmap, cg->gate->regs->set_ofs, BIT(cg->gate->shift));
}

static void mtk_cg_clr_bit(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_write(cg->regmap, cg->gate->regs->clr_ofs, BIT(cg->gate->shift));
}

static void mtk_cg_set_bit_no_setclr(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_set_bits(cg->regmap, cg->gate->regs->sta_ofs,
			BIT(cg->gate->shift));
}

static void mtk_cg_clr_bit_no_setclr(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_clear_bits(cg->regmap, cg->gate->regs->sta_ofs,
			  BIT(cg->gate->shift));
}

static int mtk_cg_enable(struct clk_hw *hw)
{
	mtk_cg_clr_bit(hw);

	return 0;
}

static void mtk_cg_disable(struct clk_hw *hw)
{
	mtk_cg_set_bit(hw);
}

static int mtk_cg_enable_inv(struct clk_hw *hw)
{
	mtk_cg_set_bit(hw);

	return 0;
}

static void mtk_cg_disable_inv(struct clk_hw *hw)
{
	mtk_cg_clr_bit(hw);
}

static int mtk_cg_hwv_set_en(struct clk_hw *hw, bool enable)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);
	u32 val;

	regmap_write(cg->regmap_hwv,
		     enable ? cg->gate->hwv_regs->set_ofs :
			      cg->gate->hwv_regs->clr_ofs,
		     BIT(cg->gate->shift));

	return regmap_read_poll_timeout_atomic(cg->regmap_hwv,
					       cg->gate->hwv_regs->sta_ofs, val,
					       val & BIT(cg->gate->shift), 0,
					       MTK_WAIT_HWV_DONE_US);
}

static int mtk_cg_hwv_enable(struct clk_hw *hw)
{
	return mtk_cg_hwv_set_en(hw, true);
}

static void mtk_cg_hwv_disable(struct clk_hw *hw)
{
	mtk_cg_hwv_set_en(hw, false);
}

static int mtk_cg_enable_no_setclr(struct clk_hw *hw)
{
	mtk_cg_clr_bit_no_setclr(hw);

	return 0;
}

static void mtk_cg_disable_no_setclr(struct clk_hw *hw)
{
	mtk_cg_set_bit_no_setclr(hw);
}

static int mtk_cg_enable_inv_no_setclr(struct clk_hw *hw)
{
	mtk_cg_set_bit_no_setclr(hw);

	return 0;
}

static void mtk_cg_disable_inv_no_setclr(struct clk_hw *hw)
{
	mtk_cg_clr_bit_no_setclr(hw);
}

static bool mtk_cg_uses_hwv(const struct clk_ops *ops)
{
	if (ops == &mtk_clk_gate_hwv_ops_setclr ||
	    ops == &mtk_clk_gate_hwv_ops_setclr_inv)
		return true;

	return false;
}

const struct clk_ops mtk_clk_gate_ops_setclr = {
	.is_enabled	= mtk_cg_bit_is_cleared,
	.enable		= mtk_cg_enable,
	.disable	= mtk_cg_disable,
};
EXPORT_SYMBOL_GPL(mtk_clk_gate_ops_setclr);

const struct clk_ops mtk_clk_gate_ops_setclr_inv = {
	.is_enabled	= mtk_cg_bit_is_set,
	.enable		= mtk_cg_enable_inv,
	.disable	= mtk_cg_disable_inv,
};
EXPORT_SYMBOL_GPL(mtk_clk_gate_ops_setclr_inv);

const struct clk_ops mtk_clk_gate_hwv_ops_setclr = {
	.is_enabled	= mtk_cg_bit_is_cleared,
	.enable		= mtk_cg_hwv_enable,
	.disable	= mtk_cg_hwv_disable,
};
EXPORT_SYMBOL_GPL(mtk_clk_gate_hwv_ops_setclr);

const struct clk_ops mtk_clk_gate_hwv_ops_setclr_inv = {
	.is_enabled	= mtk_cg_bit_is_set,
	.enable		= mtk_cg_hwv_enable,
	.disable	= mtk_cg_hwv_disable,
};
EXPORT_SYMBOL_GPL(mtk_clk_gate_hwv_ops_setclr_inv);

const struct clk_ops mtk_clk_gate_ops_no_setclr = {
	.is_enabled	= mtk_cg_bit_is_cleared,
	.enable		= mtk_cg_enable_no_setclr,
	.disable	= mtk_cg_disable_no_setclr,
};
EXPORT_SYMBOL_GPL(mtk_clk_gate_ops_no_setclr);

const struct clk_ops mtk_clk_gate_ops_no_setclr_inv = {
	.is_enabled	= mtk_cg_bit_is_set,
	.enable		= mtk_cg_enable_inv_no_setclr,
	.disable	= mtk_cg_disable_inv_no_setclr,
};
EXPORT_SYMBOL_GPL(mtk_clk_gate_ops_no_setclr_inv);

static struct clk_hw *mtk_clk_register_gate(struct device *dev,
					    const struct mtk_gate *gate,
					    struct regmap *regmap,
					    struct regmap *regmap_hwv)
{
	struct mtk_clk_gate *cg;
	int ret;
	struct clk_init_data init = {};

	cg = kzalloc(sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return ERR_PTR(-ENOMEM);

	init.name = gate->name;
	init.flags = gate->flags | CLK_SET_RATE_PARENT;
	init.parent_names = gate->parent_name ? &gate->parent_name : NULL;
	init.num_parents = gate->parent_name ? 1 : 0;
	init.ops = gate->ops;
	if (mtk_cg_uses_hwv(init.ops) && !regmap_hwv)
		return dev_err_ptr_probe(
			dev, -ENXIO,
			"regmap not found for hardware voter clocks\n");

	cg->regmap = regmap;
	cg->regmap_hwv = regmap_hwv;
	cg->gate = gate;
	cg->hw.init = &init;

	ret = clk_hw_register(dev, &cg->hw);
	if (ret) {
		kfree(cg);
		return ERR_PTR(ret);
	}

	return &cg->hw;
}

static void mtk_clk_unregister_gate(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg;
	if (!hw)
		return;

	cg = to_mtk_clk_gate(hw);

	clk_hw_unregister(hw);
	kfree(cg);
}

int mtk_clk_register_gates(struct device *dev, struct device_node *node,
			   const struct mtk_gate *clks, int num,
			   struct clk_hw_onecell_data *clk_data)
{
	int i;
	struct clk_hw *hw;
	struct regmap *regmap;
	struct regmap *regmap_hwv;

	if (!clk_data)
		return -ENOMEM;

	regmap = device_node_to_regmap(node);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %pe\n", node, regmap);
		return PTR_ERR(regmap);
	}

	regmap_hwv = mtk_clk_get_hwv_regmap(node);
	if (IS_ERR(regmap_hwv))
		return dev_err_probe(
			dev, PTR_ERR(regmap_hwv),
			"Cannot find hardware voter regmap for %pOF\n", node);

	for (i = 0; i < num; i++) {
		const struct mtk_gate *gate = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[gate->id])) {
			pr_warn("%pOF: Trying to register duplicate clock ID: %d\n",
				node, gate->id);
			continue;
		}

		hw = mtk_clk_register_gate(dev, gate, regmap, regmap_hwv);

		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", gate->name,
			       hw);
			goto err;
		}

		clk_data->hws[gate->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_gate *gate = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->hws[gate->id]))
			continue;

		mtk_clk_unregister_gate(clk_data->hws[gate->id]);
		clk_data->hws[gate->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_gates);

void mtk_clk_unregister_gates(const struct mtk_gate *clks, int num,
			      struct clk_hw_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_gate *gate = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[gate->id]))
			continue;

		mtk_clk_unregister_gate(clk_data->hws[gate->id]);
		clk_data->hws[gate->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_gates);

MODULE_LICENSE("GPL");
