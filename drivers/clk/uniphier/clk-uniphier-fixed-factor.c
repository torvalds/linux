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

#include "clk-uniphier.h"

struct clk_hw *uniphier_clk_register_fixed_factor(struct device *dev,
						  const char *name,
			const struct uniphier_clk_fixed_factor_data *data)
{
	struct clk_fixed_factor *fix;
	struct clk_init_data init;
	int ret;

	fix = devm_kzalloc(dev, sizeof(*fix), GFP_KERNEL);
	if (!fix)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_fixed_factor_ops;
	init.flags = data->parent_name ? CLK_SET_RATE_PARENT : 0;
	init.parent_names = data->parent_name ? &data->parent_name : NULL;
	init.num_parents = data->parent_name ? 1 : 0;

	fix->mult = data->mult;
	fix->div = data->div;
	fix->hw.init = &init;

	ret = devm_clk_hw_register(dev, &fix->hw);
	if (ret)
		return ERR_PTR(ret);

	return &fix->hw;
}
