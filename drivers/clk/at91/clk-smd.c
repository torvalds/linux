/*
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
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define SMD_SOURCE_MAX		2

#define SMD_DIV_SHIFT		8
#define SMD_MAX_DIV		0xf

struct at91sam9x5_clk_smd {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_at91sam9x5_clk_smd(hw) \
	container_of(hw, struct at91sam9x5_clk_smd, hw)

static unsigned long at91sam9x5_clk_smd_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct at91sam9x5_clk_smd *smd = to_at91sam9x5_clk_smd(hw);
	unsigned int smdr;
	u8 smddiv;

	regmap_read(smd->regmap, AT91_PMC_SMD, &smdr);
	smddiv = (smdr & AT91_PMC_SMD_DIV) >> SMD_DIV_SHIFT;

	return parent_rate / (smddiv + 1);
}

static long at91sam9x5_clk_smd_round_rate(struct clk_hw *hw, unsigned long rate,
					  unsigned long *parent_rate)
{
	unsigned long div;
	unsigned long bestrate;
	unsigned long tmp;

	if (rate >= *parent_rate)
		return *parent_rate;

	div = *parent_rate / rate;
	if (div > SMD_MAX_DIV)
		return *parent_rate / (SMD_MAX_DIV + 1);

	bestrate = *parent_rate / div;
	tmp = *parent_rate / (div + 1);
	if (bestrate - rate > rate - tmp)
		bestrate = tmp;

	return bestrate;
}

static int at91sam9x5_clk_smd_set_parent(struct clk_hw *hw, u8 index)
{
	struct at91sam9x5_clk_smd *smd = to_at91sam9x5_clk_smd(hw);

	if (index > 1)
		return -EINVAL;

	regmap_update_bits(smd->regmap, AT91_PMC_SMD, AT91_PMC_SMDS,
			   index ? AT91_PMC_SMDS : 0);

	return 0;
}

static u8 at91sam9x5_clk_smd_get_parent(struct clk_hw *hw)
{
	struct at91sam9x5_clk_smd *smd = to_at91sam9x5_clk_smd(hw);
	unsigned int smdr;

	regmap_read(smd->regmap, AT91_PMC_SMD, &smdr);

	return smdr & AT91_PMC_SMDS;
}

static int at91sam9x5_clk_smd_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	struct at91sam9x5_clk_smd *smd = to_at91sam9x5_clk_smd(hw);
	unsigned long div = parent_rate / rate;

	if (parent_rate % rate || div < 1 || div > (SMD_MAX_DIV + 1))
		return -EINVAL;

	regmap_update_bits(smd->regmap, AT91_PMC_SMD, AT91_PMC_SMD_DIV,
			   (div - 1) << SMD_DIV_SHIFT);

	return 0;
}

static const struct clk_ops at91sam9x5_smd_ops = {
	.recalc_rate = at91sam9x5_clk_smd_recalc_rate,
	.round_rate = at91sam9x5_clk_smd_round_rate,
	.get_parent = at91sam9x5_clk_smd_get_parent,
	.set_parent = at91sam9x5_clk_smd_set_parent,
	.set_rate = at91sam9x5_clk_smd_set_rate,
};

static struct clk * __init
at91sam9x5_clk_register_smd(struct regmap *regmap, const char *name,
			    const char **parent_names, u8 num_parents)
{
	struct at91sam9x5_clk_smd *smd;
	struct clk *clk = NULL;
	struct clk_init_data init;

	smd = kzalloc(sizeof(*smd), GFP_KERNEL);
	if (!smd)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &at91sam9x5_smd_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;

	smd->hw.init = &init;
	smd->regmap = regmap;

	clk = clk_register(NULL, &smd->hw);
	if (IS_ERR(clk))
		kfree(smd);

	return clk;
}

static void __init of_at91sam9x5_clk_smd_setup(struct device_node *np)
{
	struct clk *clk;
	int num_parents;
	const char *parent_names[SMD_SOURCE_MAX];
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents <= 0 || num_parents > SMD_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	clk = at91sam9x5_clk_register_smd(regmap, name, parent_names,
					  num_parents);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(at91sam9x5_clk_smd, "atmel,at91sam9x5-clk-smd",
	       of_at91sam9x5_clk_smd_setup);
