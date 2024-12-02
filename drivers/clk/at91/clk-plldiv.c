// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define to_clk_plldiv(hw) container_of(hw, struct clk_plldiv, hw)

struct clk_plldiv {
	struct clk_hw hw;
	struct regmap *regmap;
};

static unsigned long clk_plldiv_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_plldiv *plldiv = to_clk_plldiv(hw);
	unsigned int mckr;

	regmap_read(plldiv->regmap, AT91_PMC_MCKR, &mckr);

	if (mckr & AT91_PMC_PLLADIV2)
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

	if ((parent_rate != rate) && (parent_rate / 2 != rate))
		return -EINVAL;

	regmap_update_bits(plldiv->regmap, AT91_PMC_MCKR, AT91_PMC_PLLADIV2,
			   parent_rate != rate ? AT91_PMC_PLLADIV2 : 0);

	return 0;
}

static const struct clk_ops plldiv_ops = {
	.recalc_rate = clk_plldiv_recalc_rate,
	.round_rate = clk_plldiv_round_rate,
	.set_rate = clk_plldiv_set_rate,
};

struct clk_hw * __init
at91_clk_register_plldiv(struct regmap *regmap, const char *name,
			 const char *parent_name)
{
	struct clk_plldiv *plldiv;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	plldiv = kzalloc(sizeof(*plldiv), GFP_KERNEL);
	if (!plldiv)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &plldiv_ops;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.flags = CLK_SET_RATE_GATE;

	plldiv->hw.init = &init;
	plldiv->regmap = regmap;

	hw = &plldiv->hw;
	ret = clk_hw_register(NULL, &plldiv->hw);
	if (ret) {
		kfree(plldiv);
		hw = ERR_PTR(ret);
	}

	return hw;
}
