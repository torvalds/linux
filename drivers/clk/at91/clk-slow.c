// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/clk/at91/clk-slow.c
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
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

struct clk_hw * __init
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
