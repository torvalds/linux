// SPDX-License-Identifier: GPL-2.0
//
// Spreadtrum divider clock driver
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#include <linux/clk-provider.h>

#include "div.h"

long sprd_div_helper_round_rate(struct sprd_clk_common *common,
				const struct sprd_div_internal *div,
				unsigned long rate,
				unsigned long *parent_rate)
{
	return divider_round_rate(&common->hw, rate, parent_rate,
				  NULL, div->width, 0);
}
EXPORT_SYMBOL_GPL(sprd_div_helper_round_rate);

static long sprd_div_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	struct sprd_div *cd = hw_to_sprd_div(hw);

	return sprd_div_helper_round_rate(&cd->common, &cd->div,
					  rate, parent_rate);
}

unsigned long sprd_div_helper_recalc_rate(struct sprd_clk_common *common,
					  const struct sprd_div_internal *div,
					  unsigned long parent_rate)
{
	unsigned long val;
	unsigned int reg;

	regmap_read(common->regmap, common->reg, &reg);
	val = reg >> div->shift;
	val &= (1 << div->width) - 1;

	return divider_recalc_rate(&common->hw, parent_rate, val, NULL, 0,
				   div->width);
}
EXPORT_SYMBOL_GPL(sprd_div_helper_recalc_rate);

static unsigned long sprd_div_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct sprd_div *cd = hw_to_sprd_div(hw);

	return sprd_div_helper_recalc_rate(&cd->common, &cd->div, parent_rate);
}

int sprd_div_helper_set_rate(const struct sprd_clk_common *common,
			     const struct sprd_div_internal *div,
			     unsigned long rate,
			     unsigned long parent_rate)
{
	unsigned long val;
	unsigned int reg;

	val = divider_get_val(rate, parent_rate, NULL,
			      div->width, 0);

	regmap_read(common->regmap, common->reg, &reg);
	reg &= ~GENMASK(div->width + div->shift - 1, div->shift);

	regmap_write(common->regmap, common->reg,
			  reg | (val << div->shift));

	return 0;

}
EXPORT_SYMBOL_GPL(sprd_div_helper_set_rate);

static int sprd_div_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct sprd_div *cd = hw_to_sprd_div(hw);

	return sprd_div_helper_set_rate(&cd->common, &cd->div,
					rate, parent_rate);
}

const struct clk_ops sprd_div_ops = {
	.recalc_rate = sprd_div_recalc_rate,
	.round_rate = sprd_div_round_rate,
	.set_rate = sprd_div_set_rate,
};
EXPORT_SYMBOL_GPL(sprd_div_ops);
