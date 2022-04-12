// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/export.h>

#include "clk-regmap-mux.h"

static inline struct clk_regmap_mux *to_clk_regmap_mux(struct clk_hw *hw)
{
	return container_of(to_clk_regmap(hw), struct clk_regmap_mux, clkr);
}

static u8 mux_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	struct clk_regmap *clkr = to_clk_regmap(hw);
	unsigned int mask = GENMASK(mux->width - 1, 0);
	unsigned int val;

	regmap_read(clkr->regmap, mux->reg, &val);

	val >>= mux->shift;
	val &= mask;

	if (mux->parent_map)
		return qcom_find_cfg_index(hw, mux->parent_map, val);

	return val;
}

static int mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	struct clk_regmap *clkr = to_clk_regmap(hw);
	unsigned int mask = GENMASK(mux->width + mux->shift - 1, mux->shift);
	unsigned int val;

	if (mux->parent_map)
		index = mux->parent_map[index].cfg;

	val = index;
	val <<= mux->shift;

	return regmap_update_bits(clkr->regmap, mux->reg, mask, val);
}

static u8 mux_safe_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	unsigned int val;

	if (clk_hw_is_enabled(hw))
		return mux_get_parent(hw);

	val = mux->stored_parent_cfg;

	if (mux->parent_map)
		return qcom_find_cfg_index(hw, mux->parent_map, val);

	return val;
}

static int mux_safe_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);

	if (clk_hw_is_enabled(hw))
		return mux_set_parent(hw, index);

	if (mux->parent_map)
		index = mux->parent_map[index].cfg;

	mux->stored_parent_cfg = index;

	return 0;
}

static void mux_safe_disable(struct clk_hw *hw)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	struct clk_regmap *clkr = to_clk_regmap(hw);
	unsigned int mask = GENMASK(mux->width + mux->shift - 1, mux->shift);
	unsigned int val;

	regmap_read(clkr->regmap, mux->reg, &val);

	mux->stored_parent_cfg = (val & mask) >> mux->shift;

	val = mux->safe_src_parent;
	if (mux->parent_map) {
		int index = qcom_find_src_index(hw, mux->parent_map, val);

		if (WARN_ON(index < 0))
			return;

		val = mux->parent_map[index].cfg;
	}
	val <<= mux->shift;

	regmap_update_bits(clkr->regmap, mux->reg, mask, val);
}

static int mux_safe_enable(struct clk_hw *hw)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	struct clk_regmap *clkr = to_clk_regmap(hw);
	unsigned int mask = GENMASK(mux->width + mux->shift - 1, mux->shift);
	unsigned int val;

	val = mux->stored_parent_cfg;
	val <<= mux->shift;

	return regmap_update_bits(clkr->regmap, mux->reg, mask, val);
}

const struct clk_ops clk_regmap_mux_closest_ops = {
	.get_parent = mux_get_parent,
	.set_parent = mux_set_parent,
	.determine_rate = __clk_mux_determine_rate_closest,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_closest_ops);

const struct clk_ops clk_regmap_mux_safe_ops = {
	.enable = mux_safe_enable,
	.disable = mux_safe_disable,
	.get_parent = mux_safe_get_parent,
	.set_parent = mux_safe_set_parent,
	.determine_rate = __clk_mux_determine_rate_closest,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_safe_ops);
