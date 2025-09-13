// SPDX-License-Identifier: GPL-2.0+
//
// OWL divider clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include "owl-divider.h"

long owl_divider_helper_round_rate(struct owl_clk_common *common,
				const struct owl_divider_hw *div_hw,
				unsigned long rate,
				unsigned long *parent_rate)
{
	return divider_round_rate(&common->hw, rate, parent_rate,
				  div_hw->table, div_hw->width,
				  div_hw->div_flags);
}

static int owl_divider_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	struct owl_divider *div = hw_to_owl_divider(hw);

	req->rate = owl_divider_helper_round_rate(&div->common, &div->div_hw,
						  req->rate,
						  &req->best_parent_rate);

	return 0;
}

unsigned long owl_divider_helper_recalc_rate(struct owl_clk_common *common,
					 const struct owl_divider_hw *div_hw,
					 unsigned long parent_rate)
{
	unsigned long val;
	unsigned int reg;

	regmap_read(common->regmap, div_hw->reg, &reg);
	val = reg >> div_hw->shift;
	val &= (1 << div_hw->width) - 1;

	return divider_recalc_rate(&common->hw, parent_rate,
				   val, div_hw->table,
				   div_hw->div_flags,
				   div_hw->width);
}

static unsigned long owl_divider_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct owl_divider *div = hw_to_owl_divider(hw);

	return owl_divider_helper_recalc_rate(&div->common,
					      &div->div_hw, parent_rate);
}

int owl_divider_helper_set_rate(const struct owl_clk_common *common,
				const struct owl_divider_hw *div_hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	unsigned long val;
	unsigned int reg;

	val = divider_get_val(rate, parent_rate, div_hw->table,
			      div_hw->width, 0);

	regmap_read(common->regmap, div_hw->reg, &reg);
	reg &= ~GENMASK(div_hw->width + div_hw->shift - 1, div_hw->shift);

	regmap_write(common->regmap, div_hw->reg,
			  reg | (val << div_hw->shift));

	return 0;
}

static int owl_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct owl_divider *div = hw_to_owl_divider(hw);

	return owl_divider_helper_set_rate(&div->common, &div->div_hw,
					rate, parent_rate);
}

const struct clk_ops owl_divider_ops = {
	.recalc_rate = owl_divider_recalc_rate,
	.determine_rate = owl_divider_determine_rate,
	.set_rate = owl_divider_set_rate,
};
