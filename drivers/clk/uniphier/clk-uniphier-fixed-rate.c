// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/clk-provider.h>
#include <linux/device.h>

#include "clk-uniphier.h"

struct clk_hw *uniphier_clk_register_fixed_rate(struct device *dev,
						const char *name,
				const struct uniphier_clk_fixed_rate_data *data)
{
	struct clk_fixed_rate *fixed;
	struct clk_init_data init;
	int ret;

	/* allocate fixed-rate clock */
	fixed = devm_kzalloc(dev, sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_fixed_rate_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	fixed->fixed_rate = data->fixed_rate;
	fixed->hw.init = &init;

	ret = devm_clk_hw_register(dev, &fixed->hw);
	if (ret)
		return ERR_PTR(ret);

	return &fixed->hw;
}
