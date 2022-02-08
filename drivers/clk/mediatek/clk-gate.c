// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "clk-gate.h"

struct mtk_clk_gate {
	struct clk_hw	hw;
	struct regmap	*regmap;
	int		set_ofs;
	int		clr_ofs;
	int		sta_ofs;
	u8		bit;
};

static inline struct mtk_clk_gate *to_mtk_clk_gate(struct clk_hw *hw)
{
	return container_of(hw, struct mtk_clk_gate, hw);
}

static u32 mtk_get_clockgating(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);
	u32 val;

	regmap_read(cg->regmap, cg->sta_ofs, &val);

	return val & BIT(cg->bit);
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

	regmap_write(cg->regmap, cg->set_ofs, BIT(cg->bit));
}

static void mtk_cg_clr_bit(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_write(cg->regmap, cg->clr_ofs, BIT(cg->bit));
}

static void mtk_cg_set_bit_no_setclr(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_set_bits(cg->regmap, cg->sta_ofs, BIT(cg->bit));
}

static void mtk_cg_clr_bit_no_setclr(struct clk_hw *hw)
{
	struct mtk_clk_gate *cg = to_mtk_clk_gate(hw);

	regmap_clear_bits(cg->regmap, cg->sta_ofs, BIT(cg->bit));
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

static struct clk *mtk_clk_register_gate(const char *name,
					 const char *parent_name,
					 struct regmap *regmap, int set_ofs,
					 int clr_ofs, int sta_ofs, u8 bit,
					 const struct clk_ops *ops,
					 unsigned long flags, struct device *dev)
{
	struct mtk_clk_gate *cg;
	struct clk *clk;
	struct clk_init_data init = {};

	cg = kzalloc(sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags | CLK_SET_RATE_PARENT;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.ops = ops;

	cg->regmap = regmap;
	cg->set_ofs = set_ofs;
	cg->clr_ofs = clr_ofs;
	cg->sta_ofs = sta_ofs;
	cg->bit = bit;

	cg->hw.init = &init;

	clk = clk_register(dev, &cg->hw);
	if (IS_ERR(clk))
		kfree(cg);

	return clk;
}

static void mtk_clk_unregister_gate(struct clk *clk)
{
	struct mtk_clk_gate *cg;
	struct clk_hw *hw;

	hw = __clk_get_hw(clk);
	if (!hw)
		return;

	cg = to_mtk_clk_gate(hw);

	clk_unregister(clk);
	kfree(cg);
}

int mtk_clk_register_gates_with_dev(struct device_node *node,
				    const struct mtk_gate *clks, int num,
				    struct clk_onecell_data *clk_data,
				    struct device *dev)
{
	int i;
	struct clk *clk;
	struct regmap *regmap;

	if (!clk_data)
		return -ENOMEM;

	regmap = device_node_to_regmap(node);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %pe\n", node, regmap);
		return PTR_ERR(regmap);
	}

	for (i = 0; i < num; i++) {
		const struct mtk_gate *gate = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->clks[gate->id]))
			continue;

		clk = mtk_clk_register_gate(gate->name, gate->parent_name,
					    regmap,
					    gate->regs->set_ofs,
					    gate->regs->clr_ofs,
					    gate->regs->sta_ofs,
					    gate->shift, gate->ops,
					    gate->flags, dev);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %pe\n", gate->name, clk);
			goto err;
		}

		clk_data->clks[gate->id] = clk;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_gate *gate = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->clks[gate->id]))
			continue;

		mtk_clk_unregister_gate(clk_data->clks[gate->id]);
		clk_data->clks[gate->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(clk);
}

int mtk_clk_register_gates(struct device_node *node,
			   const struct mtk_gate *clks, int num,
			   struct clk_onecell_data *clk_data)
{
	return mtk_clk_register_gates_with_dev(node, clks, num, clk_data, NULL);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_gates);

void mtk_clk_unregister_gates(const struct mtk_gate *clks, int num,
			      struct clk_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_gate *gate = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->clks[gate->id]))
			continue;

		mtk_clk_unregister_gate(clk_data->clks[gate->id]);
		clk_data->clks[gate->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_gates);

MODULE_LICENSE("GPL");
