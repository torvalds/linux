// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include "clk-input.h"

static const struct clk_ops meson_clk_no_ops = {};

struct clk_hw *meson_clk_hw_register_input(struct device *dev,
					   const char *of_name,
					   const char *clk_name,
					   unsigned long flags)
{
	struct clk *parent_clk = devm_clk_get(dev, of_name);
	struct clk_init_data init;
	const char *parent_name;
	struct clk_hw *hw;
	int ret;

	if (IS_ERR(parent_clk))
		return (struct clk_hw *)parent_clk;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return ERR_PTR(-ENOMEM);

	parent_name = __clk_get_name(parent_clk);
	init.name = clk_name;
	init.ops = &meson_clk_no_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	hw->init = &init;

	ret = devm_clk_hw_register(dev, hw);

	return ret ? ERR_PTR(ret) : hw;
}
EXPORT_SYMBOL_GPL(meson_clk_hw_register_input);

MODULE_DESCRIPTION("Amlogic clock input helper");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
