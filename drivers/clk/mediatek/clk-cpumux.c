// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Linaro Ltd.
 * Author: Pi-Cheng Chen <pi-cheng.chen@linaro.org>
 */

#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "clk-mtk.h"
#include "clk-cpumux.h"

struct mtk_clk_cpumux {
	struct clk_hw	hw;
	struct regmap	*regmap;
	u32		reg;
	u32		mask;
	u8		shift;
};

static inline struct mtk_clk_cpumux *to_mtk_clk_cpumux(struct clk_hw *_hw)
{
	return container_of(_hw, struct mtk_clk_cpumux, hw);
}

static u8 clk_cpumux_get_parent(struct clk_hw *hw)
{
	struct mtk_clk_cpumux *mux = to_mtk_clk_cpumux(hw);
	unsigned int val;

	regmap_read(mux->regmap, mux->reg, &val);

	val >>= mux->shift;
	val &= mux->mask;

	return val;
}

static int clk_cpumux_set_parent(struct clk_hw *hw, u8 index)
{
	struct mtk_clk_cpumux *mux = to_mtk_clk_cpumux(hw);
	u32 mask, val;

	val = index << mux->shift;
	mask = mux->mask << mux->shift;

	return regmap_update_bits(mux->regmap, mux->reg, mask, val);
}

static const struct clk_ops clk_cpumux_ops = {
	.get_parent = clk_cpumux_get_parent,
	.set_parent = clk_cpumux_set_parent,
};

static struct clk_hw *
mtk_clk_register_cpumux(struct device *dev, const struct mtk_composite *mux,
			struct regmap *regmap)
{
	struct mtk_clk_cpumux *cpumux;
	int ret;
	struct clk_init_data init;

	cpumux = kzalloc(sizeof(*cpumux), GFP_KERNEL);
	if (!cpumux)
		return ERR_PTR(-ENOMEM);

	init.name = mux->name;
	init.ops = &clk_cpumux_ops;
	init.parent_names = mux->parent_names;
	init.num_parents = mux->num_parents;
	init.flags = mux->flags;

	cpumux->reg = mux->mux_reg;
	cpumux->shift = mux->mux_shift;
	cpumux->mask = BIT(mux->mux_width) - 1;
	cpumux->regmap = regmap;
	cpumux->hw.init = &init;

	ret = clk_hw_register(dev, &cpumux->hw);
	if (ret) {
		kfree(cpumux);
		return ERR_PTR(ret);
	}

	return &cpumux->hw;
}

static void mtk_clk_unregister_cpumux(struct clk_hw *hw)
{
	struct mtk_clk_cpumux *cpumux;
	if (!hw)
		return;

	cpumux = to_mtk_clk_cpumux(hw);

	clk_hw_unregister(hw);
	kfree(cpumux);
}

int mtk_clk_register_cpumuxes(struct device *dev, struct device_node *node,
			      const struct mtk_composite *clks, int num,
			      struct clk_hw_onecell_data *clk_data)
{
	int i;
	struct clk_hw *hw;
	struct regmap *regmap;

	regmap = device_node_to_regmap(node);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %pe\n", node, regmap);
		return PTR_ERR(regmap);
	}

	for (i = 0; i < num; i++) {
		const struct mtk_composite *mux = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[mux->id])) {
			pr_warn("%pOF: Trying to register duplicate clock ID: %d\n",
				node, mux->id);
			continue;
		}

		hw = mtk_clk_register_cpumux(dev, mux, regmap);
		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", mux->name,
			       hw);
			goto err;
		}

		clk_data->hws[mux->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_composite *mux = &clks[i];

		if (IS_ERR_OR_NULL(clk_data->hws[mux->id]))
			continue;

		mtk_clk_unregister_cpumux(clk_data->hws[mux->id]);
		clk_data->hws[mux->id] = ERR_PTR(-ENOENT);
	}

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_cpumuxes);

void mtk_clk_unregister_cpumuxes(const struct mtk_composite *clks, int num,
				 struct clk_hw_onecell_data *clk_data)
{
	int i;

	for (i = num; i > 0; i--) {
		const struct mtk_composite *mux = &clks[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[mux->id]))
			continue;

		mtk_clk_unregister_cpumux(clk_data->hws[mux->id]);
		clk_data->hws[mux->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_cpumuxes);

MODULE_LICENSE("GPL");
