/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Base on code in drivers/clk/clk-gate.c.
 * See clk-gate.c for further copyright information.
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

#define to_clk_regmap_gate(_hw)	container_of(_hw, struct clk_regmap_gate, hw)

static int clk_regmap_gate_prepare(struct clk_hw *hw)
{
	struct clk_regmap_gate *gate = to_clk_regmap_gate(hw);

	return regmap_write(gate->regmap, gate->reg,
			    0 | BIT(gate->shift + 16));
}

static void clk_regmap_gate_unprepare(struct clk_hw *hw)
{
	struct clk_regmap_gate *gate = to_clk_regmap_gate(hw);

	regmap_write(gate->regmap, gate->reg,
		     BIT(gate->shift) | BIT(gate->shift + 16));
}

static int clk_regmap_gate_is_prepared(struct clk_hw *hw)
{
	struct clk_regmap_gate *gate = to_clk_regmap_gate(hw);
	u32 val;

	regmap_read(gate->regmap, gate->reg, &val);

	return !(val & BIT(gate->shift));
}

const struct clk_ops clk_regmap_gate_ops = {
	.prepare = clk_regmap_gate_prepare,
	.unprepare = clk_regmap_gate_unprepare,
	.is_prepared = clk_regmap_gate_is_prepared,
};
EXPORT_SYMBOL_GPL(clk_regmap_gate_ops);

struct clk *
devm_clk_regmap_register_gate(struct device *dev, const char *name,
			      const char *parent_name,
			      struct regmap *regmap, u32 reg, u8 shift,
			      unsigned long flags)
{
	struct clk_regmap_gate *gate;
	struct clk_init_data init;

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_regmap_gate_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	gate->dev = dev;
	gate->regmap = regmap;
	gate->reg = reg;
	gate->shift = shift;
	gate->hw.init = &init;

	return devm_clk_register(dev, &gate->hw);
}
EXPORT_SYMBOL_GPL(devm_clk_regmap_register_gate);
