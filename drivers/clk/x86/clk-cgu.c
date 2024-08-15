// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 MaxLinear, Inc.
 * Copyright (C) 2020 Intel Corporation.
 * Zhu Yixin <yzhu@maxlinear.com>
 * Rahul Tanwar <rtanwar@maxlinear.com>
 */
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/of.h>

#include "clk-cgu.h"

#define GATE_HW_REG_STAT(reg)	((reg) + 0x0)
#define GATE_HW_REG_EN(reg)	((reg) + 0x4)
#define GATE_HW_REG_DIS(reg)	((reg) + 0x8)
#define MAX_DDIV_REG	8
#define MAX_DIVIDER_VAL 64

#define to_lgm_clk_mux(_hw) container_of(_hw, struct lgm_clk_mux, hw)
#define to_lgm_clk_divider(_hw) container_of(_hw, struct lgm_clk_divider, hw)
#define to_lgm_clk_gate(_hw) container_of(_hw, struct lgm_clk_gate, hw)
#define to_lgm_clk_ddiv(_hw) container_of(_hw, struct lgm_clk_ddiv, hw)

static struct clk_hw *lgm_clk_register_fixed(struct lgm_clk_provider *ctx,
					     const struct lgm_clk_branch *list)
{

	if (list->div_flags & CLOCK_FLAG_VAL_INIT)
		lgm_set_clk_val(ctx->membase, list->div_off, list->div_shift,
				list->div_width, list->div_val);

	return clk_hw_register_fixed_rate(NULL, list->name,
					  list->parent_data[0].name,
					  list->flags, list->mux_flags);
}

static u8 lgm_clk_mux_get_parent(struct clk_hw *hw)
{
	struct lgm_clk_mux *mux = to_lgm_clk_mux(hw);
	u32 val;

	if (mux->flags & MUX_CLK_SW)
		val = mux->reg;
	else
		val = lgm_get_clk_val(mux->membase, mux->reg, mux->shift,
				      mux->width);
	return clk_mux_val_to_index(hw, NULL, mux->flags, val);
}

static int lgm_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct lgm_clk_mux *mux = to_lgm_clk_mux(hw);
	u32 val;

	val = clk_mux_index_to_val(NULL, mux->flags, index);
	if (mux->flags & MUX_CLK_SW)
		mux->reg = val;
	else
		lgm_set_clk_val(mux->membase, mux->reg, mux->shift,
				mux->width, val);

	return 0;
}

static int lgm_clk_mux_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	struct lgm_clk_mux *mux = to_lgm_clk_mux(hw);

	return clk_mux_determine_rate_flags(hw, req, mux->flags);
}

static const struct clk_ops lgm_clk_mux_ops = {
	.get_parent = lgm_clk_mux_get_parent,
	.set_parent = lgm_clk_mux_set_parent,
	.determine_rate = lgm_clk_mux_determine_rate,
};

static struct clk_hw *
lgm_clk_register_mux(struct lgm_clk_provider *ctx,
		     const struct lgm_clk_branch *list)
{
	unsigned long cflags = list->mux_flags;
	struct device *dev = ctx->dev;
	u8 shift = list->mux_shift;
	u8 width = list->mux_width;
	struct clk_init_data init = {};
	struct lgm_clk_mux *mux;
	u32 reg = list->mux_off;
	struct clk_hw *hw;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = list->name;
	init.ops = &lgm_clk_mux_ops;
	init.flags = list->flags;
	init.parent_data = list->parent_data;
	init.num_parents = list->num_parents;

	mux->membase = ctx->membase;
	mux->reg = reg;
	mux->shift = shift;
	mux->width = width;
	mux->flags = cflags;
	mux->hw.init = &init;

	hw = &mux->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	if (cflags & CLOCK_FLAG_VAL_INIT)
		lgm_set_clk_val(mux->membase, reg, shift, width, list->mux_val);

	return hw;
}

static unsigned long
lgm_clk_divider_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct lgm_clk_divider *divider = to_lgm_clk_divider(hw);
	unsigned int val;

	val = lgm_get_clk_val(divider->membase, divider->reg,
			      divider->shift, divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags, divider->width);
}

static long
lgm_clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long *prate)
{
	struct lgm_clk_divider *divider = to_lgm_clk_divider(hw);

	return divider_round_rate(hw, rate, prate, divider->table,
				  divider->width, divider->flags);
}

static int
lgm_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long prate)
{
	struct lgm_clk_divider *divider = to_lgm_clk_divider(hw);
	int value;

	value = divider_get_val(rate, prate, divider->table,
				divider->width, divider->flags);
	if (value < 0)
		return value;

	lgm_set_clk_val(divider->membase, divider->reg,
			divider->shift, divider->width, value);

	return 0;
}

static int lgm_clk_divider_enable_disable(struct clk_hw *hw, int enable)
{
	struct lgm_clk_divider *div = to_lgm_clk_divider(hw);

	if (div->flags != DIV_CLK_NO_MASK)
		lgm_set_clk_val(div->membase, div->reg, div->shift_gate,
				div->width_gate, enable);
	return 0;
}

static int lgm_clk_divider_enable(struct clk_hw *hw)
{
	return lgm_clk_divider_enable_disable(hw, 1);
}

static void lgm_clk_divider_disable(struct clk_hw *hw)
{
	lgm_clk_divider_enable_disable(hw, 0);
}

static const struct clk_ops lgm_clk_divider_ops = {
	.recalc_rate = lgm_clk_divider_recalc_rate,
	.round_rate = lgm_clk_divider_round_rate,
	.set_rate = lgm_clk_divider_set_rate,
	.enable = lgm_clk_divider_enable,
	.disable = lgm_clk_divider_disable,
};

static struct clk_hw *
lgm_clk_register_divider(struct lgm_clk_provider *ctx,
			 const struct lgm_clk_branch *list)
{
	unsigned long cflags = list->div_flags;
	struct device *dev = ctx->dev;
	struct lgm_clk_divider *div;
	struct clk_init_data init = {};
	u8 shift = list->div_shift;
	u8 width = list->div_width;
	u8 shift_gate = list->div_shift_gate;
	u8 width_gate = list->div_width_gate;
	u32 reg = list->div_off;
	struct clk_hw *hw;
	int ret;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = list->name;
	init.ops = &lgm_clk_divider_ops;
	init.flags = list->flags;
	init.parent_data = list->parent_data;
	init.num_parents = 1;

	div->membase = ctx->membase;
	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->shift_gate	= shift_gate;
	div->width_gate	= width_gate;
	div->flags = cflags;
	div->table = list->div_table;
	div->hw.init = &init;

	hw = &div->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	if (cflags & CLOCK_FLAG_VAL_INIT)
		lgm_set_clk_val(div->membase, reg, shift, width, list->div_val);

	return hw;
}

static struct clk_hw *
lgm_clk_register_fixed_factor(struct lgm_clk_provider *ctx,
			      const struct lgm_clk_branch *list)
{
	struct clk_hw *hw;

	hw = clk_hw_register_fixed_factor(ctx->dev, list->name,
					  list->parent_data[0].name, list->flags,
					  list->mult, list->div);
	if (IS_ERR(hw))
		return ERR_CAST(hw);

	if (list->div_flags & CLOCK_FLAG_VAL_INIT)
		lgm_set_clk_val(ctx->membase, list->div_off, list->div_shift,
				list->div_width, list->div_val);

	return hw;
}

static int lgm_clk_gate_enable(struct clk_hw *hw)
{
	struct lgm_clk_gate *gate = to_lgm_clk_gate(hw);
	unsigned int reg;

	reg = GATE_HW_REG_EN(gate->reg);
	lgm_set_clk_val(gate->membase, reg, gate->shift, 1, 1);

	return 0;
}

static void lgm_clk_gate_disable(struct clk_hw *hw)
{
	struct lgm_clk_gate *gate = to_lgm_clk_gate(hw);
	unsigned int reg;

	reg = GATE_HW_REG_DIS(gate->reg);
	lgm_set_clk_val(gate->membase, reg, gate->shift, 1, 1);
}

static int lgm_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct lgm_clk_gate *gate = to_lgm_clk_gate(hw);
	unsigned int reg, ret;

	reg = GATE_HW_REG_STAT(gate->reg);
	ret = lgm_get_clk_val(gate->membase, reg, gate->shift, 1);

	return ret;
}

static const struct clk_ops lgm_clk_gate_ops = {
	.enable = lgm_clk_gate_enable,
	.disable = lgm_clk_gate_disable,
	.is_enabled = lgm_clk_gate_is_enabled,
};

static struct clk_hw *
lgm_clk_register_gate(struct lgm_clk_provider *ctx,
		      const struct lgm_clk_branch *list)
{
	unsigned long cflags = list->gate_flags;
	const char *pname = list->parent_data[0].name;
	struct device *dev = ctx->dev;
	u8 shift = list->gate_shift;
	struct clk_init_data init = {};
	struct lgm_clk_gate *gate;
	u32 reg = list->gate_off;
	struct clk_hw *hw;
	int ret;

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = list->name;
	init.ops = &lgm_clk_gate_ops;
	init.flags = list->flags;
	init.parent_names = pname ? &pname : NULL;
	init.num_parents = pname ? 1 : 0;

	gate->membase = ctx->membase;
	gate->reg = reg;
	gate->shift = shift;
	gate->flags = cflags;
	gate->hw.init = &init;

	hw = &gate->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	if (cflags & CLOCK_FLAG_VAL_INIT) {
		lgm_set_clk_val(gate->membase, reg, shift, 1, list->gate_val);
	}

	return hw;
}

int lgm_clk_register_branches(struct lgm_clk_provider *ctx,
			      const struct lgm_clk_branch *list,
			      unsigned int nr_clk)
{
	struct clk_hw *hw;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		switch (list->type) {
		case CLK_TYPE_FIXED:
			hw = lgm_clk_register_fixed(ctx, list);
			break;
		case CLK_TYPE_MUX:
			hw = lgm_clk_register_mux(ctx, list);
			break;
		case CLK_TYPE_DIVIDER:
			hw = lgm_clk_register_divider(ctx, list);
			break;
		case CLK_TYPE_FIXED_FACTOR:
			hw = lgm_clk_register_fixed_factor(ctx, list);
			break;
		case CLK_TYPE_GATE:
			if (list->gate_flags & GATE_CLK_HW) {
				hw = lgm_clk_register_gate(ctx, list);
			} else {
				/*
				 * GATE_CLKs can be controlled either from
				 * CGU clk driver i.e. this driver or directly
				 * from power management driver/daemon. It is
				 * dependent on the power policy/profile requirements
				 * of the end product. To override control of gate
				 * clks from this driver, provide NULL for this index
				 * of gate clk provider.
				 */
				hw = NULL;
			}
			break;

		default:
			dev_err(ctx->dev, "invalid clk type\n");
			return -EINVAL;
		}

		if (IS_ERR(hw)) {
			dev_err(ctx->dev,
				"register clk: %s, type: %u failed!\n",
				list->name, list->type);
			return -EIO;
		}
		ctx->clk_data.hws[list->id] = hw;
	}

	return 0;
}

static unsigned long
lgm_clk_ddiv_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct lgm_clk_ddiv *ddiv = to_lgm_clk_ddiv(hw);
	unsigned int div0, div1, exdiv;
	u64 prate;

	div0 = lgm_get_clk_val(ddiv->membase, ddiv->reg,
			       ddiv->shift0, ddiv->width0) + 1;
	div1 = lgm_get_clk_val(ddiv->membase, ddiv->reg,
			       ddiv->shift1, ddiv->width1) + 1;
	exdiv = lgm_get_clk_val(ddiv->membase, ddiv->reg,
				ddiv->shift2, ddiv->width2);
	prate = (u64)parent_rate;
	do_div(prate, div0);
	do_div(prate, div1);

	if (exdiv) {
		do_div(prate, ddiv->div);
		prate *= ddiv->mult;
	}

	return prate;
}

static int lgm_clk_ddiv_enable(struct clk_hw *hw)
{
	struct lgm_clk_ddiv *ddiv = to_lgm_clk_ddiv(hw);

	lgm_set_clk_val(ddiv->membase, ddiv->reg, ddiv->shift_gate,
			ddiv->width_gate, 1);
	return 0;
}

static void lgm_clk_ddiv_disable(struct clk_hw *hw)
{
	struct lgm_clk_ddiv *ddiv = to_lgm_clk_ddiv(hw);

	lgm_set_clk_val(ddiv->membase, ddiv->reg, ddiv->shift_gate,
			ddiv->width_gate, 0);
}

static int
lgm_clk_get_ddiv_val(u32 div, u32 *ddiv1, u32 *ddiv2)
{
	u32 idx, temp;

	*ddiv1 = 1;
	*ddiv2 = 1;

	if (div > MAX_DIVIDER_VAL)
		div = MAX_DIVIDER_VAL;

	if (div > 1) {
		for (idx = 2; idx <= MAX_DDIV_REG; idx++) {
			temp = DIV_ROUND_UP_ULL((u64)div, idx);
			if (div % idx == 0 && temp <= MAX_DDIV_REG)
				break;
		}

		if (idx > MAX_DDIV_REG)
			return -EINVAL;

		*ddiv1 = temp;
		*ddiv2 = idx;
	}

	return 0;
}

static int
lgm_clk_ddiv_set_rate(struct clk_hw *hw, unsigned long rate,
		      unsigned long prate)
{
	struct lgm_clk_ddiv *ddiv = to_lgm_clk_ddiv(hw);
	u32 div, ddiv1, ddiv2;

	div = DIV_ROUND_CLOSEST_ULL((u64)prate, rate);

	if (lgm_get_clk_val(ddiv->membase, ddiv->reg, ddiv->shift2, 1)) {
		div = DIV_ROUND_CLOSEST_ULL((u64)div, 5);
		div = div * 2;
	}

	if (div <= 0)
		return -EINVAL;

	if (lgm_clk_get_ddiv_val(div, &ddiv1, &ddiv2))
		return -EINVAL;

	lgm_set_clk_val(ddiv->membase, ddiv->reg, ddiv->shift0, ddiv->width0,
			ddiv1 - 1);

	lgm_set_clk_val(ddiv->membase, ddiv->reg,  ddiv->shift1, ddiv->width1,
			ddiv2 - 1);

	return 0;
}

static long
lgm_clk_ddiv_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct lgm_clk_ddiv *ddiv = to_lgm_clk_ddiv(hw);
	u32 div, ddiv1, ddiv2;
	u64 rate64;

	div = DIV_ROUND_CLOSEST_ULL((u64)*prate, rate);

	/* if predivide bit is enabled, modify div by factor of 2.5 */
	if (lgm_get_clk_val(ddiv->membase, ddiv->reg, ddiv->shift2, 1)) {
		div = div * 2;
		div = DIV_ROUND_CLOSEST_ULL((u64)div, 5);
	}

	if (div <= 0)
		return *prate;

	if (lgm_clk_get_ddiv_val(div, &ddiv1, &ddiv2) != 0)
		if (lgm_clk_get_ddiv_val(div + 1, &ddiv1, &ddiv2) != 0)
			return -EINVAL;

	rate64 = *prate;
	do_div(rate64, ddiv1);
	do_div(rate64, ddiv2);

	/* if predivide bit is enabled, modify rounded rate by factor of 2.5 */
	if (lgm_get_clk_val(ddiv->membase, ddiv->reg, ddiv->shift2, 1)) {
		rate64 = rate64 * 2;
		rate64 = DIV_ROUND_CLOSEST_ULL(rate64, 5);
	}

	return rate64;
}

static const struct clk_ops lgm_clk_ddiv_ops = {
	.recalc_rate = lgm_clk_ddiv_recalc_rate,
	.enable	= lgm_clk_ddiv_enable,
	.disable = lgm_clk_ddiv_disable,
	.set_rate = lgm_clk_ddiv_set_rate,
	.round_rate = lgm_clk_ddiv_round_rate,
};

int lgm_clk_register_ddiv(struct lgm_clk_provider *ctx,
			  const struct lgm_clk_ddiv_data *list,
			  unsigned int nr_clk)
{
	struct device *dev = ctx->dev;
	struct clk_hw *hw;
	unsigned int idx;
	int ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		struct clk_init_data init = {};
		struct lgm_clk_ddiv *ddiv;

		ddiv = devm_kzalloc(dev, sizeof(*ddiv), GFP_KERNEL);
		if (!ddiv)
			return -ENOMEM;

		init.name = list->name;
		init.ops = &lgm_clk_ddiv_ops;
		init.flags = list->flags;
		init.parent_data = list->parent_data;
		init.num_parents = 1;

		ddiv->membase = ctx->membase;
		ddiv->reg = list->reg;
		ddiv->shift0 = list->shift0;
		ddiv->width0 = list->width0;
		ddiv->shift1 = list->shift1;
		ddiv->width1 = list->width1;
		ddiv->shift_gate = list->shift_gate;
		ddiv->width_gate = list->width_gate;
		ddiv->shift2 = list->ex_shift;
		ddiv->width2 = list->ex_width;
		ddiv->flags = list->div_flags;
		ddiv->mult = 2;
		ddiv->div = 5;
		ddiv->hw.init = &init;

		hw = &ddiv->hw;
		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "register clk: %s failed!\n", list->name);
			return ret;
		}
		ctx->clk_data.hws[list->id] = hw;
	}

	return 0;
}
