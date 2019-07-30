// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include "clk-uniphier.h"

struct uniphier_clk_gate {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned int reg;
	unsigned int bit;
};

#define to_uniphier_clk_gate(_hw) \
				container_of(_hw, struct uniphier_clk_gate, hw)

static int uniphier_clk_gate_endisable(struct clk_hw *hw, int enable)
{
	struct uniphier_clk_gate *gate = to_uniphier_clk_gate(hw);

	return regmap_write_bits(gate->regmap, gate->reg, BIT(gate->bit),
				 enable ? BIT(gate->bit) : 0);
}

static int uniphier_clk_gate_enable(struct clk_hw *hw)
{
	return uniphier_clk_gate_endisable(hw, 1);
}

static void uniphier_clk_gate_disable(struct clk_hw *hw)
{
	if (uniphier_clk_gate_endisable(hw, 0) < 0)
		pr_warn("failed to disable clk\n");
}

static int uniphier_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct uniphier_clk_gate *gate = to_uniphier_clk_gate(hw);
	unsigned int val;

	if (regmap_read(gate->regmap, gate->reg, &val) < 0)
		pr_warn("is_enabled() may return wrong result\n");

	return !!(val & BIT(gate->bit));
}

static const struct clk_ops uniphier_clk_gate_ops = {
	.enable = uniphier_clk_gate_enable,
	.disable = uniphier_clk_gate_disable,
	.is_enabled = uniphier_clk_gate_is_enabled,
};

struct clk_hw *uniphier_clk_register_gate(struct device *dev,
					  struct regmap *regmap,
					  const char *name,
				const struct uniphier_clk_gate_data *data)
{
	struct uniphier_clk_gate *gate;
	struct clk_init_data init;
	int ret;

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &uniphier_clk_gate_ops;
	init.flags = data->parent_name ? CLK_SET_RATE_PARENT : 0;
	init.parent_names = data->parent_name ? &data->parent_name : NULL;
	init.num_parents = data->parent_name ? 1 : 0;

	gate->regmap = regmap;
	gate->reg = data->reg;
	gate->bit = data->bit;
	gate->hw.init = &init;

	ret = devm_clk_hw_register(dev, &gate->hw);
	if (ret)
		return ERR_PTR(ret);

	return &gate->hw;
}
