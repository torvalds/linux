// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/export.h>

#include "clk-regmap-mux.h"
#include "clk-debug.h"

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

static void clk_regmap_mux_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	int val;

	regmap_read(mux->clkr.regmap, mux->reg, &val);
	clock_debug_output(f, "%20s: 0x%.8x\n", "MUXR", val);
}

static struct clk_regmap_ops clk_regmap_mux_regmap_ops = {
	.list_registers = clk_regmap_mux_list_registers,
};

static int clk_regmap_mux_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_regmap_mux_regmap_ops;

	return 0;
}

const struct clk_ops clk_regmap_mux_closest_ops = {
	.get_parent = mux_get_parent,
	.set_parent = mux_set_parent,
	.determine_rate = __clk_mux_determine_rate_closest,
	.init = clk_regmap_mux_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_closest_ops);
