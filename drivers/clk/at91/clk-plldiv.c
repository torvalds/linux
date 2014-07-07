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

#include "pmc.h"

#define to_clk_plldiv(hw) container_of(hw, struct clk_plldiv, hw)

struct clk_plldiv {
	struct clk_hw hw;
	struct at91_pmc *pmc;
};

static unsigned long clk_plldiv_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_plldiv *plldiv = to_clk_plldiv(hw);
	struct at91_pmc *pmc = plldiv->pmc;

	if (pmc_read(pmc, AT91_PMC_MCKR) & AT91_PMC_PLLADIV2)
		return parent_rate / 2;

	return parent_rate;
}

static long clk_plldiv_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	unsigned long div;

	if (rate > *parent_rate)
		return *parent_rate;
	div = *parent_rate / 2;
	if (rate < div)
		return div;

	if (rate - div < *parent_rate - rate)
		return div;

	return *parent_rate;
}

static int clk_plldiv_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_plldiv *plldiv = to_clk_plldiv(hw);
	struct at91_pmc *pmc = plldiv->pmc;
	u32 tmp;

	if (parent_rate != rate && (parent_rate / 2) != rate)
		return -EINVAL;

	pmc_lock(pmc);
	tmp = pmc_read(pmc, AT91_PMC_MCKR) & ~AT91_PMC_PLLADIV2;
	if ((parent_rate / 2) == rate)
		tmp |= AT91_PMC_PLLADIV2;
	pmc_write(pmc, AT91_PMC_MCKR, tmp);
	pmc_unlock(pmc);

	return 0;
}

static const struct clk_ops plldiv_ops = {
	.recalc_rate = clk_plldiv_recalc_rate,
	.round_rate = clk_plldiv_round_rate,
	.set_rate = clk_plldiv_set_rate,
};

static struct clk * __init
at91_clk_register_plldiv(struct at91_pmc *pmc, const char *name,
			 const char *parent_name)
{
	struct clk_plldiv *plldiv;
	struct clk *clk = NULL;
	struct clk_init_data init;

	plldiv = kzalloc(sizeof(*plldiv), GFP_KERNEL);
	if (!plldiv)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &plldiv_ops;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.flags = CLK_SET_RATE_GATE;

	plldiv->hw.init = &init;
	plldiv->pmc = pmc;

	clk = clk_register(NULL, &plldiv->hw);

	if (IS_ERR(clk))
		kfree(plldiv);

	return clk;
}

static void __init
of_at91_clk_plldiv_setup(struct device_node *np, struct at91_pmc *pmc)
{
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	clk = at91_clk_register_plldiv(pmc, name, parent_name);

	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	return;
}

void __init of_at91sam9x5_clk_plldiv_setup(struct device_node *np,
					   struct at91_pmc *pmc)
{
	of_at91_clk_plldiv_setup(np, pmc);
}
