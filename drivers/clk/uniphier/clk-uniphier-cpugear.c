/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
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

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include "clk-uniphier.h"

#define UNIPHIER_CLK_CPUGEAR_STAT	0	/* status */
#define UNIPHIER_CLK_CPUGEAR_SET	4	/* set */
#define UNIPHIER_CLK_CPUGEAR_UPD	8	/* update */
#define   UNIPHIER_CLK_CPUGEAR_UPD_BIT	BIT(0)

struct uniphier_clk_cpugear {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned int regbase;
	unsigned int mask;
};

#define to_uniphier_clk_cpugear(_hw) \
			container_of(_hw, struct uniphier_clk_cpugear, hw)

static int uniphier_clk_cpugear_set_parent(struct clk_hw *hw, u8 index)
{
	struct uniphier_clk_cpugear *gear = to_uniphier_clk_cpugear(hw);
	int ret;
	unsigned int val;

	ret = regmap_write_bits(gear->regmap,
				gear->regbase + UNIPHIER_CLK_CPUGEAR_SET,
				gear->mask, index);
	if (ret)
		return ret;

	ret = regmap_write_bits(gear->regmap,
				gear->regbase + UNIPHIER_CLK_CPUGEAR_SET,
				UNIPHIER_CLK_CPUGEAR_UPD_BIT,
				UNIPHIER_CLK_CPUGEAR_UPD_BIT);
	if (ret)
		return ret;

	return regmap_read_poll_timeout(gear->regmap,
				gear->regbase + UNIPHIER_CLK_CPUGEAR_UPD,
				val, !(val & UNIPHIER_CLK_CPUGEAR_UPD_BIT),
				0, 1);
}

static u8 uniphier_clk_cpugear_get_parent(struct clk_hw *hw)
{
	struct uniphier_clk_cpugear *gear = to_uniphier_clk_cpugear(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	int ret;
	unsigned int val;

	ret = regmap_read(gear->regmap,
			  gear->regbase + UNIPHIER_CLK_CPUGEAR_STAT, &val);
	if (ret)
		return ret;

	val &= gear->mask;

	return val < num_parents ? val : -EINVAL;
}

static const struct clk_ops uniphier_clk_cpugear_ops = {
	.determine_rate = __clk_mux_determine_rate,
	.set_parent = uniphier_clk_cpugear_set_parent,
	.get_parent = uniphier_clk_cpugear_get_parent,
};

struct clk_hw *uniphier_clk_register_cpugear(struct device *dev,
					 struct regmap *regmap,
					 const char *name,
				const struct uniphier_clk_cpugear_data *data)
{
	struct uniphier_clk_cpugear *gear;
	struct clk_init_data init;
	int ret;

	gear = devm_kzalloc(dev, sizeof(*gear), GFP_KERNEL);
	if (!gear)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &uniphier_clk_cpugear_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = data->parent_names;
	init.num_parents = data->num_parents,

	gear->regmap = regmap;
	gear->regbase = data->regbase;
	gear->mask = data->mask;
	gear->hw.init = &init;

	ret = devm_clk_hw_register(dev, &gear->hw);
	if (ret)
		return ERR_PTR(ret);

	return &gear->hw;
}
