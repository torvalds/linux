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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define MASTER_PRES_MASK	0x7
#define MASTER_PRES_MAX		MASTER_PRES_MASK
#define MASTER_DIV_SHIFT	8
#define MASTER_DIV_MASK		0x3

#define to_clk_master(hw) container_of(hw, struct clk_master, hw)

struct clk_master {
	struct clk_hw hw;
	struct regmap *regmap;
	const struct clk_master_layout *layout;
	const struct clk_master_characteristics *characteristics;
};

static inline bool clk_master_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_MCKRDY ? 1 : 0;
}

static int clk_master_prepare(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);

	while (!clk_master_ready(master->regmap))
		cpu_relax();

	return 0;
}

static int clk_master_is_prepared(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);

	return clk_master_ready(master->regmap);
}

static unsigned long clk_master_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	u8 pres;
	u8 div;
	unsigned long rate = parent_rate;
	struct clk_master *master = to_clk_master(hw);
	const struct clk_master_layout *layout = master->layout;
	const struct clk_master_characteristics *characteristics =
						master->characteristics;
	unsigned int mckr;

	regmap_read(master->regmap, AT91_PMC_MCKR, &mckr);
	mckr &= layout->mask;

	pres = (mckr >> layout->pres_shift) & MASTER_PRES_MASK;
	div = (mckr >> MASTER_DIV_SHIFT) & MASTER_DIV_MASK;

	if (characteristics->have_div3_pres && pres == MASTER_PRES_MAX)
		rate /= 3;
	else
		rate >>= pres;

	rate /= characteristics->divisors[div];

	if (rate < characteristics->output.min)
		pr_warn("master clk is underclocked");
	else if (rate > characteristics->output.max)
		pr_warn("master clk is overclocked");

	return rate;
}

static u8 clk_master_get_parent(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned int mckr;

	regmap_read(master->regmap, AT91_PMC_MCKR, &mckr);

	return mckr & AT91_PMC_CSS;
}

static const struct clk_ops master_ops = {
	.prepare = clk_master_prepare,
	.is_prepared = clk_master_is_prepared,
	.recalc_rate = clk_master_recalc_rate,
	.get_parent = clk_master_get_parent,
};

struct clk_hw * __init
at91_clk_register_master(struct regmap *regmap,
		const char *name, int num_parents,
		const char **parent_names,
		const struct clk_master_layout *layout,
		const struct clk_master_characteristics *characteristics)
{
	struct clk_master *master;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	if (!name || !num_parents || !parent_names)
		return ERR_PTR(-EINVAL);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &master_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;

	master->hw.init = &init;
	master->layout = layout;
	master->characteristics = characteristics;
	master->regmap = regmap;

	hw = &master->hw;
	ret = clk_hw_register(NULL, &master->hw);
	if (ret) {
		kfree(master);
		hw = ERR_PTR(ret);
	}

	return hw;
}


const struct clk_master_layout at91rm9200_master_layout = {
	.mask = 0x31F,
	.pres_shift = 2,
};

const struct clk_master_layout at91sam9x5_master_layout = {
	.mask = 0x373,
	.pres_shift = 4,
};
