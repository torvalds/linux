// SPDX-License-Identifier: GPL-2.0+
//
// OWL pll clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "owl-pll.h"

static u32 owl_pll_calculate_mul(struct owl_pll_hw *pll_hw, unsigned long rate)
{
	u32 mul;

	mul = DIV_ROUND_CLOSEST(rate, pll_hw->bfreq);
	if (mul < pll_hw->min_mul)
		mul = pll_hw->min_mul;
	else if (mul > pll_hw->max_mul)
		mul = pll_hw->max_mul;

	return mul &= mul_mask(pll_hw);
}

static unsigned long _get_table_rate(const struct clk_pll_table *table,
		unsigned int val)
{
	const struct clk_pll_table *clkt;

	for (clkt = table; clkt->rate; clkt++)
		if (clkt->val == val)
			return clkt->rate;

	return 0;
}

static const struct clk_pll_table *_get_pll_table(
		const struct clk_pll_table *table, unsigned long rate)
{
	const struct clk_pll_table *clkt;

	for (clkt = table; clkt->rate; clkt++) {
		if (clkt->rate == rate) {
			table = clkt;
			break;
		} else if (clkt->rate < rate)
			table = clkt;
	}

	return table;
}

static long owl_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	struct owl_pll *pll = hw_to_owl_pll(hw);
	struct owl_pll_hw *pll_hw = &pll->pll_hw;
	const struct clk_pll_table *clkt;
	u32 mul;

	if (pll_hw->table) {
		clkt = _get_pll_table(pll_hw->table, rate);
		return clkt->rate;
	}

	/* fixed frequency */
	if (pll_hw->width == 0)
		return pll_hw->bfreq;

	mul = owl_pll_calculate_mul(pll_hw, rate);

	return pll_hw->bfreq * mul;
}

static unsigned long owl_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct owl_pll *pll = hw_to_owl_pll(hw);
	struct owl_pll_hw *pll_hw = &pll->pll_hw;
	const struct owl_clk_common *common = &pll->common;
	u32 val;

	if (pll_hw->table) {
		regmap_read(common->regmap, pll_hw->reg, &val);

		val = val >> pll_hw->shift;
		val &= mul_mask(pll_hw);

		return _get_table_rate(pll_hw->table, val);
	}

	/* fixed frequency */
	if (pll_hw->width == 0)
		return pll_hw->bfreq;

	regmap_read(common->regmap, pll_hw->reg, &val);

	val = val >> pll_hw->shift;
	val &= mul_mask(pll_hw);

	return pll_hw->bfreq * val;
}

static int owl_pll_is_enabled(struct clk_hw *hw)
{
	struct owl_pll *pll = hw_to_owl_pll(hw);
	struct owl_pll_hw *pll_hw = &pll->pll_hw;
	const struct owl_clk_common *common = &pll->common;
	u32 reg;

	regmap_read(common->regmap, pll_hw->reg, &reg);

	return !!(reg & BIT(pll_hw->bit_idx));
}

static void owl_pll_set(const struct owl_clk_common *common,
		       const struct owl_pll_hw *pll_hw, bool enable)
{
	u32 reg;

	regmap_read(common->regmap, pll_hw->reg, &reg);

	if (enable)
		reg |= BIT(pll_hw->bit_idx);
	else
		reg &= ~BIT(pll_hw->bit_idx);

	regmap_write(common->regmap, pll_hw->reg, reg);
}

static int owl_pll_enable(struct clk_hw *hw)
{
	struct owl_pll *pll = hw_to_owl_pll(hw);
	const struct owl_clk_common *common = &pll->common;

	owl_pll_set(common, &pll->pll_hw, true);

	return 0;
}

static void owl_pll_disable(struct clk_hw *hw)
{
	struct owl_pll *pll = hw_to_owl_pll(hw);
	const struct owl_clk_common *common = &pll->common;

	owl_pll_set(common, &pll->pll_hw, false);
}

static int owl_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct owl_pll *pll = hw_to_owl_pll(hw);
	struct owl_pll_hw *pll_hw = &pll->pll_hw;
	const struct owl_clk_common *common = &pll->common;
	const struct clk_pll_table *clkt;
	u32 val, reg;

	/* fixed frequency */
	if (pll_hw->width == 0)
		return 0;

	if (pll_hw->table) {
		clkt = _get_pll_table(pll_hw->table, rate);
		val = clkt->val;
	} else {
		val = owl_pll_calculate_mul(pll_hw, rate);
	}

	regmap_read(common->regmap, pll_hw->reg, &reg);

	reg &= ~mul_mask(pll_hw);
	reg |= val << pll_hw->shift;

	regmap_write(common->regmap, pll_hw->reg, reg);

	udelay(pll_hw->delay);

	return 0;
}

const struct clk_ops owl_pll_ops = {
	.enable = owl_pll_enable,
	.disable = owl_pll_disable,
	.is_enabled = owl_pll_is_enabled,
	.round_rate = owl_pll_round_rate,
	.recalc_rate = owl_pll_recalc_rate,
	.set_rate = owl_pll_set_rate,
};
