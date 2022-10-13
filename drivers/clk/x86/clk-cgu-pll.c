// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 MaxLinear, Inc.
 * Copyright (C) 2020 Intel Corporation.
 * Zhu Yixin <yzhu@maxlinear.com>
 * Rahul Tanwar <rtanwar@maxlinear.com>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/of.h>

#include "clk-cgu.h"

#define to_lgm_clk_pll(_hw)	container_of(_hw, struct lgm_clk_pll, hw)
#define PLL_REF_DIV(x)		((x) + 0x08)

/*
 * Calculate formula:
 * rate = (prate * mult + (prate * frac) / frac_div) / div
 */
static unsigned long
lgm_pll_calc_rate(unsigned long prate, unsigned int mult,
		  unsigned int div, unsigned int frac, unsigned int frac_div)
{
	u64 crate, frate, rate64;

	rate64 = prate;
	crate = rate64 * mult;
	frate = rate64 * frac;
	do_div(frate, frac_div);
	crate += frate;
	do_div(crate, div);

	return crate;
}

static unsigned long lgm_pll_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct lgm_clk_pll *pll = to_lgm_clk_pll(hw);
	unsigned int div, mult, frac;
	unsigned long flags;

	spin_lock_irqsave(&pll->lock, flags);
	mult = lgm_get_clk_val(pll->membase, PLL_REF_DIV(pll->reg), 0, 12);
	div = lgm_get_clk_val(pll->membase, PLL_REF_DIV(pll->reg), 18, 6);
	frac = lgm_get_clk_val(pll->membase, pll->reg, 2, 24);
	spin_unlock_irqrestore(&pll->lock, flags);

	if (pll->type == TYPE_LJPLL)
		div *= 4;

	return lgm_pll_calc_rate(prate, mult, div, frac, BIT(24));
}

static int lgm_pll_is_enabled(struct clk_hw *hw)
{
	struct lgm_clk_pll *pll = to_lgm_clk_pll(hw);
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&pll->lock, flags);
	ret = lgm_get_clk_val(pll->membase, pll->reg, 0, 1);
	spin_unlock_irqrestore(&pll->lock, flags);

	return ret;
}

static int lgm_pll_enable(struct clk_hw *hw)
{
	struct lgm_clk_pll *pll = to_lgm_clk_pll(hw);
	unsigned long flags;
	u32 val;
	int ret;

	spin_lock_irqsave(&pll->lock, flags);
	lgm_set_clk_val(pll->membase, pll->reg, 0, 1, 1);
	ret = regmap_read_poll_timeout_atomic(pll->membase, pll->reg,
					      val, (val & 0x1), 1, 100);

	spin_unlock_irqrestore(&pll->lock, flags);

	return ret;
}

static void lgm_pll_disable(struct clk_hw *hw)
{
	struct lgm_clk_pll *pll = to_lgm_clk_pll(hw);
	unsigned long flags;

	spin_lock_irqsave(&pll->lock, flags);
	lgm_set_clk_val(pll->membase, pll->reg, 0, 1, 0);
	spin_unlock_irqrestore(&pll->lock, flags);
}

static const struct clk_ops lgm_pll_ops = {
	.recalc_rate = lgm_pll_recalc_rate,
	.is_enabled = lgm_pll_is_enabled,
	.enable = lgm_pll_enable,
	.disable = lgm_pll_disable,
};

static struct clk_hw *
lgm_clk_register_pll(struct lgm_clk_provider *ctx,
		     const struct lgm_pll_clk_data *list)
{
	struct clk_init_data init = {};
	struct lgm_clk_pll *pll;
	struct device *dev = ctx->dev;
	struct clk_hw *hw;
	int ret;

	init.ops = &lgm_pll_ops;
	init.name = list->name;
	init.flags = list->flags;
	init.parent_data = list->parent_data;
	init.num_parents = list->num_parents;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->membase = ctx->membase;
	pll->lock = ctx->lock;
	pll->reg = list->reg;
	pll->flags = list->flags;
	pll->type = list->type;
	pll->hw.init = &init;

	hw = &pll->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	return hw;
}

int lgm_clk_register_plls(struct lgm_clk_provider *ctx,
			  const struct lgm_pll_clk_data *list,
			  unsigned int nr_clk)
{
	struct clk_hw *hw;
	int i;

	for (i = 0; i < nr_clk; i++, list++) {
		hw = lgm_clk_register_pll(ctx, list);
		if (IS_ERR(hw)) {
			dev_err(ctx->dev, "failed to register pll: %s\n",
				list->name);
			return PTR_ERR(hw);
		}
		ctx->clk_data.hws[list->id] = hw;
	}

	return 0;
}
