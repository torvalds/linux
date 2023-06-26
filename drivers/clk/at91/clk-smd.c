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

static int at91sam9x5_clk_smd_determine_rate(struct clk_hw *hw,
					     struct clk_rate_request *req)
{
	unsigned long div;
	unsigned long bestrate;
	unsigned long tmp;

	if (req->rate >= req->best_parent_rate) {
		req->rate = req->best_parent_rate;
		return 0;
	}

	div = req->best_parent_rate / req->rate;
	if (div > SMD_MAX_DIV) {
		req->rate = req->best_parent_rate / (SMD_MAX_DIV + 1);
		return 0;
	}

	bestrate = req->best_parent_rate / div;
	tmp = req->best_parent_rate / (div + 1);
	if (bestrate - req->rate > req->rate - tmp)
		bestrate = tmp;

	req->rate = bestrate;
	return 0;
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
	.determine_rate = at91sam9x5_clk_smd_determine_rate,
	.get_parent = at91sam9x5_clk_smd_get_parent,
	.set_parent = at91sam9x5_clk_smd_set_parent,
	.set_rate = at91sam9x5_clk_smd_set_rate,
};

struct clk_hw * __init
at91sam9x5_clk_register_smd(struct regmap *regmap, const char *name,
			    const char **parent_names, u8 num_parents)
{
	struct at91sam9x5_clk_smd *smd;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

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

	hw = &smd->hw;
	ret = clk_hw_register(NULL, &smd->hw);
	if (ret) {
		kfree(smd);
		hw = ERR_PTR(ret);
	}

	return hw;
}
