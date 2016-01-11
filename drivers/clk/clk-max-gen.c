/*
 * clk-max-gen.c - Generic clock driver for Maxim PMICs clocks
 *
 * Copyright (C) 2014 Google, Inc
 *
 * Copyright (C) 2012 Samsung Electornics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver is based on clk-max77686.c
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/export.h>

#include "clk-max-gen.h"

struct max_gen_clk {
	struct regmap *regmap;
	u32 mask;
	u32 reg;
	struct clk_hw hw;
};

static struct max_gen_clk *to_max_gen_clk(struct clk_hw *hw)
{
	return container_of(hw, struct max_gen_clk, hw);
}

static int max_gen_clk_prepare(struct clk_hw *hw)
{
	struct max_gen_clk *max_gen = to_max_gen_clk(hw);

	return regmap_update_bits(max_gen->regmap, max_gen->reg,
				  max_gen->mask, max_gen->mask);
}

static void max_gen_clk_unprepare(struct clk_hw *hw)
{
	struct max_gen_clk *max_gen = to_max_gen_clk(hw);

	regmap_update_bits(max_gen->regmap, max_gen->reg,
			   max_gen->mask, ~max_gen->mask);
}

static int max_gen_clk_is_prepared(struct clk_hw *hw)
{
	struct max_gen_clk *max_gen = to_max_gen_clk(hw);
	int ret;
	u32 val;

	ret = regmap_read(max_gen->regmap, max_gen->reg, &val);

	if (ret < 0)
		return -EINVAL;

	return val & max_gen->mask;
}

static unsigned long max_gen_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	return 32768;
}

struct clk_ops max_gen_clk_ops = {
	.prepare	= max_gen_clk_prepare,
	.unprepare	= max_gen_clk_unprepare,
	.is_prepared	= max_gen_clk_is_prepared,
	.recalc_rate	= max_gen_recalc_rate,
};
EXPORT_SYMBOL_GPL(max_gen_clk_ops);

static struct clk *max_gen_clk_register(struct device *dev,
					struct max_gen_clk *max_gen)
{
	struct clk *clk;
	struct clk_hw *hw = &max_gen->hw;
	int ret;

	clk = devm_clk_register(dev, hw);
	if (IS_ERR(clk))
		return clk;

	ret = clk_register_clkdev(clk, hw->init->name, NULL);

	if (ret)
		return ERR_PTR(ret);

	return clk;
}

int max_gen_clk_probe(struct platform_device *pdev, struct regmap *regmap,
		      u32 reg, struct clk_init_data *clks_init, int num_init)
{
	int i, ret;
	struct max_gen_clk *max_gen_clks;
	struct clk **clocks;
	struct device *dev = pdev->dev.parent;
	const char *clk_name;
	struct clk_init_data *init;

	clocks = devm_kzalloc(dev, sizeof(struct clk *) * num_init, GFP_KERNEL);
	if (!clocks)
		return -ENOMEM;

	max_gen_clks = devm_kzalloc(dev, sizeof(struct max_gen_clk)
				    * num_init, GFP_KERNEL);
	if (!max_gen_clks)
		return -ENOMEM;

	for (i = 0; i < num_init; i++) {
		max_gen_clks[i].regmap = regmap;
		max_gen_clks[i].mask = 1 << i;
		max_gen_clks[i].reg = reg;

		init = devm_kzalloc(dev, sizeof(*init), GFP_KERNEL);
		if (!init)
			return -ENOMEM;

		if (dev->of_node &&
		    !of_property_read_string_index(dev->of_node,
						   "clock-output-names",
						   i, &clk_name))
			init->name = clk_name;
		else
			init->name = clks_init[i].name;

		init->ops = clks_init[i].ops;
		init->flags = clks_init[i].flags;

		max_gen_clks[i].hw.init = init;

		clocks[i] = max_gen_clk_register(dev, &max_gen_clks[i]);
		if (IS_ERR(clocks[i])) {
			ret = PTR_ERR(clocks[i]);
			dev_err(dev, "failed to register %s\n",
				max_gen_clks[i].hw.init->name);
			return ret;
		}
	}

	platform_set_drvdata(pdev, clocks);

	if (dev->of_node) {
		struct clk_onecell_data *of_data;

		of_data = devm_kzalloc(dev, sizeof(*of_data), GFP_KERNEL);
		if (!of_data)
			return -ENOMEM;

		of_data->clks = clocks;
		of_data->clk_num = num_init;
		ret = of_clk_add_provider(dev->of_node, of_clk_src_onecell_get,
					  of_data);

		if (ret) {
			dev_err(dev, "failed to register OF clock provider\n");
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(max_gen_clk_probe);

int max_gen_clk_remove(struct platform_device *pdev, int num_init)
{
	struct device *dev = pdev->dev.parent;

	if (dev->of_node)
		of_clk_del_provider(dev->of_node);

	return 0;
}
EXPORT_SYMBOL_GPL(max_gen_clk_remove);
