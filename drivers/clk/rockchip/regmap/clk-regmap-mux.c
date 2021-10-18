/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Base on code in drivers/clk/clk-mux.c.
 * See clk-mux.c for further copyright information.
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

#include "clk-regmap.h"

#define to_clk_regmap_mux(_hw)	container_of(_hw, struct clk_regmap_mux, hw)

static u8 clk_regmap_mux_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);
	u8 index;
	u32 val;

	regmap_read(mux->regmap, mux->reg, &val);

	index = val >> mux->shift;
	index &= mux->mask;

	return index;
}

static int clk_regmap_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_mux *mux = to_clk_regmap_mux(hw);

	return regmap_write(mux->regmap, mux->reg, (index << mux->shift) |
			    (mux->mask << (mux->shift + 16)));
}

const struct clk_ops clk_regmap_mux_ops = {
	.set_parent = clk_regmap_mux_set_parent,
	.get_parent = clk_regmap_mux_get_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_ops);

struct clk *
devm_clk_regmap_register_mux(struct device *dev, const char *name,
			     const char * const *parent_names, u8 num_parents,
			     struct regmap *regmap, u32 reg, u8 shift, u8 width,
			     unsigned long flags)
{
	struct clk_regmap_mux *mux;
	struct clk_init_data init = {};

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_regmap_mux_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	mux->dev = dev;
	mux->regmap = regmap;
	mux->reg = reg;
	mux->shift = shift;
	mux->mask = BIT(width) - 1;
	mux->hw.init = &init;

	return devm_clk_register(dev, &mux->hw);
}
EXPORT_SYMBOL_GPL(devm_clk_regmap_register_mux);

MODULE_LICENSE("GPL");
