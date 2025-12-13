// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/export.h>

#include "clk-regmap-divider.h"

static inline struct clk_regmap_div *to_clk_regmap_div(struct clk_hw *hw)
{
	return container_of(to_clk_regmap(hw), struct clk_regmap_div, clkr);
}

static int div_ro_determine_rate(struct clk_hw *hw,
				 struct clk_rate_request *req)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_regmap *clkr = &divider->clkr;
	u32 val;

	regmap_read(clkr->regmap, divider->reg, &val);
	val >>= divider->shift;
	val &= BIT(divider->width) - 1;

	req->rate = divider_ro_round_rate(hw, req->rate,
					  &req->best_parent_rate, NULL,
					  divider->width,
					  CLK_DIVIDER_ROUND_CLOSEST, val);

	return 0;
}

static int div_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);

	req->rate = divider_round_rate(hw, req->rate, &req->best_parent_rate,
				       NULL,
				       divider->width,
				       CLK_DIVIDER_ROUND_CLOSEST);

	return 0;
}

static int div_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_regmap *clkr = &divider->clkr;
	u32 div;

	div = divider_get_val(rate, parent_rate, NULL, divider->width,
			      CLK_DIVIDER_ROUND_CLOSEST);

	return regmap_update_bits(clkr->regmap, divider->reg,
				  (BIT(divider->width) - 1) << divider->shift,
				  div << divider->shift);
}

static unsigned long div_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_regmap *clkr = &divider->clkr;
	u32 div;

	regmap_read(clkr->regmap, divider->reg, &div);
	div >>= divider->shift;
	div &= BIT(divider->width) - 1;

	return divider_recalc_rate(hw, parent_rate, div, NULL,
				   CLK_DIVIDER_ROUND_CLOSEST, divider->width);
}

const struct clk_ops clk_regmap_div_ops = {
	.determine_rate = div_determine_rate,
	.set_rate = div_set_rate,
	.recalc_rate = div_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_div_ops);

const struct clk_ops clk_regmap_div_ro_ops = {
	.determine_rate = div_ro_determine_rate,
	.recalc_rate = div_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_div_ro_ops);
