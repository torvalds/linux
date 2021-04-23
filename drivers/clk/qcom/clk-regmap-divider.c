// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, 2017, 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/export.h>

#include "clk-regmap-divider.h"
#include "clk-debug.h"

static inline struct clk_regmap_div *to_clk_regmap_div(struct clk_hw *hw)
{
	return container_of(to_clk_regmap(hw), struct clk_regmap_div, clkr);
}

static long div_round_ro_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *prate)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_regmap *clkr = &divider->clkr;
	u32 val;
	int ret;

	ret = clk_runtime_get_regmap(clkr);
	if (ret)
		return ret;

	regmap_read(clkr->regmap, divider->reg, &val);
	val >>= divider->shift;
	val &= BIT(divider->width) - 1;

	clk_runtime_put_regmap(clkr);

	return divider_ro_round_rate(hw, rate, prate, NULL, divider->width,
				     CLK_DIVIDER_ROUND_CLOSEST, val);
}

static long div_round_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long *prate)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);

	return divider_round_rate(hw, rate, prate, divider->table,
				  divider->width,
				  CLK_DIVIDER_ROUND_CLOSEST |
				  divider->flags);
}

static int div_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_regmap *clkr = &divider->clkr;
	u32 div;

	div = divider_get_val(rate, parent_rate, divider->table,
			      divider->width, CLK_DIVIDER_ROUND_CLOSEST |
			      divider->flags);

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

	return divider_recalc_rate(hw, parent_rate, div, divider->table,
				   CLK_DIVIDER_ROUND_CLOSEST | divider->flags,
				   divider->width);
}

static long clk_regmap_div_list_rate(struct clk_hw *hw, unsigned int n,
		unsigned long fmax)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_hw *parent_hw = clk_hw_get_parent(hw);
	struct clk_regmap *clkr = &divider->clkr;
	struct clk_regmap *parent_clkr = to_clk_regmap(parent_hw);
	u32 div;
	int ret;

	ret = clk_runtime_get_regmap(clkr);
	if (ret)
		return ret;

	regmap_read(clkr->regmap, divider->reg, &div);
	div >>= divider->shift;
	div &= BIT(divider->width) - 1;
	div += 1;

	clk_runtime_put_regmap(clkr);

	if (parent_clkr && parent_clkr->ops && parent_clkr->ops->list_rate)
		return (parent_clkr->ops->list_rate(parent_hw, n, fmax) / div);

	return -EINVAL;
}

static struct clk_regmap_ops clk_regmap_div_regmap_ops = {
	.list_rate = clk_regmap_div_list_rate,
};

static int clk_regmap_div_init(struct clk_hw *hw)
{
	struct clk_regmap_div *divider = to_clk_regmap_div(hw);
	struct clk_regmap *clkr = &divider->clkr;

	if (!clkr->ops)
		clkr->ops = &clk_regmap_div_regmap_ops;

	return 0;
}

const struct clk_ops clk_regmap_div_ops = {
	.round_rate = div_round_rate,
	.set_rate = div_set_rate,
	.recalc_rate = div_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_div_ops);

const struct clk_ops clk_regmap_div_ro_ops = {
	.round_rate = div_round_ro_rate,
	.recalc_rate = div_recalc_rate,
	.init = clk_regmap_div_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_regmap_div_ro_ops);
