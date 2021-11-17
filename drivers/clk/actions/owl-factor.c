// SPDX-License-Identifier: GPL-2.0+
//
// OWL factor clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "owl-factor.h"

static unsigned int _get_table_maxval(const struct clk_factor_table *table)
{
	unsigned int maxval = 0;
	const struct clk_factor_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->val > maxval)
			maxval = clkt->val;
	return maxval;
}

static int _get_table_div_mul(const struct clk_factor_table *table,
			unsigned int val, unsigned int *mul, unsigned int *div)
{
	const struct clk_factor_table *clkt;

	for (clkt = table; clkt->div; clkt++) {
		if (clkt->val == val) {
			*mul = clkt->mul;
			*div = clkt->div;
			return 1;
		}
	}

	return 0;
}

static unsigned int _get_table_val(const struct clk_factor_table *table,
			unsigned long rate, unsigned long parent_rate)
{
	const struct clk_factor_table *clkt;
	int val = -1;
	u64 calc_rate;

	for (clkt = table; clkt->div; clkt++) {
		calc_rate = parent_rate * clkt->mul;
		do_div(calc_rate, clkt->div);

		if ((unsigned long)calc_rate <= rate) {
			val = clkt->val;
			break;
		}
	}

	if (val == -1)
		val = _get_table_maxval(table);

	return val;
}

static int owl_clk_val_best(const struct owl_factor_hw *factor_hw,
			struct clk_hw *hw, unsigned long rate,
			unsigned long *best_parent_rate)
{
	const struct clk_factor_table *clkt = factor_hw->table;
	unsigned long parent_rate, try_parent_rate, best = 0, cur_rate;
	unsigned long parent_rate_saved = *best_parent_rate;
	int bestval = 0;

	if (!rate)
		rate = 1;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		parent_rate = *best_parent_rate;
		bestval = _get_table_val(clkt, rate, parent_rate);
		return bestval;
	}

	for (clkt = factor_hw->table; clkt->div; clkt++) {
		try_parent_rate = rate * clkt->div / clkt->mul;

		if (try_parent_rate == parent_rate_saved) {
			pr_debug("%s: [%d %d %d] found try_parent_rate %ld\n",
				__func__, clkt->val, clkt->mul, clkt->div,
				try_parent_rate);
			/*
			 * It's the most ideal case if the requested rate can be
			 * divided from parent clock without any need to change
			 * parent rate, so return the divider immediately.
			 */
			*best_parent_rate = parent_rate_saved;
			return clkt->val;
		}

		parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
				try_parent_rate);
		cur_rate = DIV_ROUND_UP(parent_rate, clkt->div) * clkt->mul;
		if (cur_rate <= rate && cur_rate > best) {
			bestval = clkt->val;
			best = cur_rate;
			*best_parent_rate = parent_rate;
		}
	}

	if (!bestval) {
		bestval = _get_table_maxval(clkt);
		*best_parent_rate = clk_hw_round_rate(
				clk_hw_get_parent(hw), 1);
	}

	return bestval;
}

long owl_factor_helper_round_rate(struct owl_clk_common *common,
				const struct owl_factor_hw *factor_hw,
				unsigned long rate,
				unsigned long *parent_rate)
{
	const struct clk_factor_table *clkt = factor_hw->table;
	unsigned int val, mul = 0, div = 1;

	val = owl_clk_val_best(factor_hw, &common->hw, rate, parent_rate);
	_get_table_div_mul(clkt, val, &mul, &div);

	return *parent_rate * mul / div;
}

static long owl_factor_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	struct owl_factor *factor = hw_to_owl_factor(hw);
	struct owl_factor_hw *factor_hw = &factor->factor_hw;

	return owl_factor_helper_round_rate(&factor->common, factor_hw,
					rate, parent_rate);
}

unsigned long owl_factor_helper_recalc_rate(struct owl_clk_common *common,
					 const struct owl_factor_hw *factor_hw,
					 unsigned long parent_rate)
{
	const struct clk_factor_table *clkt = factor_hw->table;
	unsigned long long int rate;
	u32 reg, val, mul, div;

	div = 0;
	mul = 0;

	regmap_read(common->regmap, factor_hw->reg, &reg);

	val = reg >> factor_hw->shift;
	val &= div_mask(factor_hw);

	_get_table_div_mul(clkt, val, &mul, &div);
	if (!div) {
		WARN(!(factor_hw->fct_flags & CLK_DIVIDER_ALLOW_ZERO),
			"%s: Zero divisor and CLK_DIVIDER_ALLOW_ZERO not set\n",
			__clk_get_name(common->hw.clk));
		return parent_rate;
	}

	rate = (unsigned long long int)parent_rate * mul;
	do_div(rate, div);

	return rate;
}

static unsigned long owl_factor_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct owl_factor *factor = hw_to_owl_factor(hw);
	struct owl_factor_hw *factor_hw = &factor->factor_hw;
	struct owl_clk_common *common = &factor->common;

	return owl_factor_helper_recalc_rate(common, factor_hw, parent_rate);
}

int owl_factor_helper_set_rate(const struct owl_clk_common *common,
				const struct owl_factor_hw *factor_hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	u32 val, reg;

	val = _get_table_val(factor_hw->table, rate, parent_rate);

	if (val > div_mask(factor_hw))
		val = div_mask(factor_hw);

	regmap_read(common->regmap, factor_hw->reg, &reg);

	reg &= ~(div_mask(factor_hw) << factor_hw->shift);
	reg |= val << factor_hw->shift;

	regmap_write(common->regmap, factor_hw->reg, reg);

	return 0;
}

static int owl_factor_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct owl_factor *factor = hw_to_owl_factor(hw);
	struct owl_factor_hw *factor_hw = &factor->factor_hw;
	struct owl_clk_common *common = &factor->common;

	return owl_factor_helper_set_rate(common, factor_hw,
					rate, parent_rate);
}

const struct clk_ops owl_factor_ops = {
	.round_rate	= owl_factor_round_rate,
	.recalc_rate	= owl_factor_recalc_rate,
	.set_rate	= owl_factor_set_rate,
};
