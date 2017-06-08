/*
 * drivers/clk/at91/clk-slow.c
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

struct clk_sam9260_slow {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_clk_sam9260_slow(hw) container_of(hw, struct clk_sam9260_slow, hw)

static u8 clk_sam9260_slow_get_parent(struct clk_hw *hw)
{
	struct clk_sam9260_slow *slowck = to_clk_sam9260_slow(hw);
	unsigned int status;

	regmap_read(slowck->regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_OSCSEL ? 1 : 0;
}

static const struct clk_ops sam9260_slow_ops = {
	.get_parent = clk_sam9260_slow_get_parent,
};

static struct clk_hw * __init
at91_clk_register_sam9260_slow(struct regmap *regmap,
			       const char *name,
			       const char **parent_names,
			       int num_parents)
{
	struct clk_sam9260_slow *slowck;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	if (!name)
		return ERR_PTR(-EINVAL);

	if (!parent_names || !num_parents)
		return ERR_PTR(-EINVAL);

	slowck = kzalloc(sizeof(*slowck), GFP_KERNEL);
	if (!slowck)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sam9260_slow_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;

	slowck->hw.init = &init;
	slowck->regmap = regmap;

	hw = &slowck->hw;
	ret = clk_hw_register(NULL, &slowck->hw);
	if (ret) {
		kfree(slowck);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void __init of_at91sam9260_clk_slow_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_names[2];
	unsigned int num_parents;
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents != 2)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);
	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	of_property_read_string(np, "clock-output-names", &name);

	hw = at91_clk_register_sam9260_slow(regmap, name, parent_names,
					     num_parents);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}

CLK_OF_DECLARE(at91sam9260_clk_slow, "atmel,at91sam9260-clk-slow",
	       of_at91sam9260_clk_slow_setup);
