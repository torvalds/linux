// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>

#include "clk-mtk.h"
#include "clk-mux.h"

struct mtk_clk_mux {
	struct clk_hw hw;
	struct regmap *regmap;
	const struct mtk_mux *data;
	spinlock_t *lock;
	bool reparent;
};

static inline struct mtk_clk_mux *to_mtk_clk_mux(struct clk_hw *hw)
{
	return container_of(hw, struct mtk_clk_mux, hw);
}

static int mtk_clk_mux_enable_setclr(struct clk_hw *hw)
{
	struct mtk_clk_mux *mux = to_mtk_clk_mux(hw);
	unsigned long flags = 0;

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);
	else
		__acquire(mux->lock);

	regmap_write(mux->regmap, mux->data->clr_ofs,
		     BIT(mux->data->gate_shift));

	/*
	 * If the parent has been changed when the clock was disabled, it will
	 * not be effective yet. Set the update bit to ensure the mux gets
	 * updated.
	 */
	if (mux->reparent && mux->data->upd_shift >= 0) {
		regmap_write(mux->regmap, mux->data->upd_ofs,
			     BIT(mux->data->upd_shift));
		mux->reparent = false;
	}

	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);
	else
		__release(mux->lock);

	return 0;
}

static void mtk_clk_mux_disable_setclr(struct clk_hw *hw)
{
	struct mtk_clk_mux *mux = to_mtk_clk_mux(hw);

	regmap_write(mux->regmap, mux->data->set_ofs,
			BIT(mux->data->gate_shift));
}

static int mtk_clk_mux_is_enabled(struct clk_hw *hw)
{
	struct mtk_clk_mux *mux = to_mtk_clk_mux(hw);
	u32 val;

	regmap_read(mux->regmap, mux->data->mux_ofs, &val);

	return (val & BIT(mux->data->gate_shift)) == 0;
}

static u8 mtk_clk_mux_get_parent(struct clk_hw *hw)
{
	struct mtk_clk_mux *mux = to_mtk_clk_mux(hw);
	u32 mask = GENMASK(mux->data->mux_width - 1, 0);
	u32 val;

	regmap_read(mux->regmap, mux->data->mux_ofs, &val);
	val = (val >> mux->data->mux_shift) & mask;

	return val;
}

static int mtk_clk_mux_set_parent_setclr_lock(struct clk_hw *hw, u8 index)
{
	struct mtk_clk_mux *mux = to_mtk_clk_mux(hw);
	u32 mask = GENMASK(mux->data->mux_width - 1, 0);
	u32 val, orig;
	unsigned long flags = 0;

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);
	else
		__acquire(mux->lock);

	regmap_read(mux->regmap, mux->data->mux_ofs, &orig);
	val = (orig & ~(mask << mux->data->mux_shift))
			| (index << mux->data->mux_shift);

	if (val != orig) {
		regmap_write(mux->regmap, mux->data->clr_ofs,
				mask << mux->data->mux_shift);
		regmap_write(mux->regmap, mux->data->set_ofs,
				index << mux->data->mux_shift);

		if (mux->data->upd_shift >= 0) {
			regmap_write(mux->regmap, mux->data->upd_ofs,
					BIT(mux->data->upd_shift));
			mux->reparent = true;
		}
	}

	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);
	else
		__release(mux->lock);

	return 0;
}

const struct clk_ops mtk_mux_clr_set_upd_ops = {
	.get_parent = mtk_clk_mux_get_parent,
	.set_parent = mtk_clk_mux_set_parent_setclr_lock,
};
EXPORT_SYMBOL_GPL(mtk_mux_clr_set_upd_ops);

const struct clk_ops mtk_mux_gate_clr_set_upd_ops  = {
	.enable = mtk_clk_mux_enable_setclr,
	.disable = mtk_clk_mux_disable_setclr,
	.is_enabled = mtk_clk_mux_is_enabled,
	.get_parent = mtk_clk_mux_get_parent,
	.set_parent = mtk_clk_mux_set_parent_setclr_lock,
};
EXPORT_SYMBOL_GPL(mtk_mux_gate_clr_set_upd_ops);

static struct clk *mtk_clk_register_mux(const struct mtk_mux *mux,
				 struct regmap *regmap,
				 spinlock_t *lock)
{
	struct mtk_clk_mux *clk_mux;
	struct clk_init_data init = {};
	struct clk *clk;

	clk_mux = kzalloc(sizeof(*clk_mux), GFP_KERNEL);
	if (!clk_mux)
		return ERR_PTR(-ENOMEM);

	init.name = mux->name;
	init.flags = mux->flags | CLK_SET_RATE_PARENT;
	init.parent_names = mux->parent_names;
	init.num_parents = mux->num_parents;
	init.ops = mux->ops;

	clk_mux->regmap = regmap;
	clk_mux->data = mux;
	clk_mux->lock = lock;
	clk_mux->hw.init = &init;

	clk = clk_register(NULL, &clk_mux->hw);
	if (IS_ERR(clk)) {
		kfree(clk_mux);
		return clk;
	}

	return clk;
}

static void mtk_clk_unregister_mux(struct clk *clk)
{
	struct mtk_clk_mux *mux;
	struct clk_hw *hw;

	hw = __clk_get_hw(clk);
	if (!hw)
		return;

	mux = to_mtk_clk_mux(hw);

	clk_unregister(clk);
	kfree(mux);
}

int mtk_clk_register_muxes(const struct mtk_mux *muxes,
			   int num, struct device_node *node,
			   spinlock_t *lock,
			   struct clk_onecell_data *clk_data)
{
	struct regmap *regmap;
	struct clk *clk;
	int i;

	regmap = device_node_to_regmap(node);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %pe\n", node, regmap);
		return PTR_ERR(regmap);
	}

	for (i = 0; i < num; i++) {
		const struct mtk_mux *mux = &muxes[i];

		if (IS_ERR_OR_NULL(clk_data->clks[mux->id])) {
			clk = mtk_clk_register_mux(mux, regmap, lock);

			if (IS_ERR(clk)) {
				pr_err("Failed to register clk %s: %pe\n", mux->name, clk);
				continue;
			}

			clk_data->clks[mux->id] = clk;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_clk_register_muxes);

void mtk_clk_unregister_muxes(const struct mtk_mux *muxes, int num,
			      struct clk_onecell_data *clk_data)
{
	int i;

	if (!clk_data)
		return;

	for (i = num; i > 0; i--) {
		const struct mtk_mux *mux = &muxes[i - 1];

		if (IS_ERR_OR_NULL(clk_data->clks[mux->id]))
			continue;

		mtk_clk_unregister_mux(clk_data->clks[mux->id]);
		clk_data->clks[mux->id] = ERR_PTR(-ENOENT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_muxes);

MODULE_LICENSE("GPL");
