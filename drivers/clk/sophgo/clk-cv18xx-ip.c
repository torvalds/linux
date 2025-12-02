// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Inochi Amaoto <inochiama@outlook.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/gcd.h>
#include <linux/spinlock.h>

#include "clk-cv18xx-ip.h"

/* GATE */
static inline struct cv1800_clk_gate *hw_to_cv1800_clk_gate(struct clk_hw *hw)
{
	struct cv1800_clk_common *common = hw_to_cv1800_clk_common(hw);

	return container_of(common, struct cv1800_clk_gate, common);
}

static int gate_enable(struct clk_hw *hw)
{
	struct cv1800_clk_gate *gate = hw_to_cv1800_clk_gate(hw);

	return cv1800_clk_setbit(&gate->common, &gate->gate);
}

static void gate_disable(struct clk_hw *hw)
{
	struct cv1800_clk_gate *gate = hw_to_cv1800_clk_gate(hw);

	cv1800_clk_clearbit(&gate->common, &gate->gate);
}

static int gate_is_enabled(struct clk_hw *hw)
{
	struct cv1800_clk_gate *gate = hw_to_cv1800_clk_gate(hw);

	return cv1800_clk_checkbit(&gate->common, &gate->gate);
}

static unsigned long gate_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	return parent_rate;
}

static int gate_determine_rate(struct clk_hw *hw,
			       struct clk_rate_request *req)
{
	req->rate = req->best_parent_rate;

	return 0;
}

static int gate_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	return 0;
}

const struct clk_ops cv1800_clk_gate_ops = {
	.disable = gate_disable,
	.enable = gate_enable,
	.is_enabled = gate_is_enabled,

	.recalc_rate = gate_recalc_rate,
	.determine_rate = gate_determine_rate,
	.set_rate = gate_set_rate,
};

/* DIV */
#define _DIV_EN_CLK_DIV_FACTOR_FIELD		BIT(3)

#define DIV_GET_EN_CLK_DIV_FACTOR(_reg) \
	FIELD_GET(_DIV_EN_CLK_DIV_FACTOR_FIELD, _reg)

#define DIV_SET_EN_DIV_FACTOR(_reg) \
	_CV1800_SET_FIELD(_reg, 1, _DIV_EN_CLK_DIV_FACTOR_FIELD)

static inline struct cv1800_clk_div *hw_to_cv1800_clk_div(struct clk_hw *hw)
{
	struct cv1800_clk_common *common = hw_to_cv1800_clk_common(hw);

	return container_of(common, struct cv1800_clk_div, common);
}

static int div_enable(struct clk_hw *hw)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);

	return cv1800_clk_setbit(&div->common, &div->gate);
}

static void div_disable(struct clk_hw *hw)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);

	cv1800_clk_clearbit(&div->common, &div->gate);
}

static int div_is_enabled(struct clk_hw *hw)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);

	return cv1800_clk_checkbit(&div->common, &div->gate);
}

static int div_helper_set_rate(struct cv1800_clk_common *common,
			       struct cv1800_clk_regfield *div,
			       unsigned long val)
{
	unsigned long flags;
	u32 reg;

	if (div->width == 0)
		return 0;

	spin_lock_irqsave(common->lock, flags);

	reg = readl(common->base + div->reg);
	reg = cv1800_clk_regfield_set(reg, val, div);
	if (div->initval > 0)
		reg = DIV_SET_EN_DIV_FACTOR(reg);

	writel(reg, common->base + div->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}

static u32 div_helper_get_clockdiv(struct cv1800_clk_common *common,
				   struct cv1800_clk_regfield *div)
{
	u32 clockdiv = 1;
	u32 reg;

	if (!div || div->initval < 0 || (div->width == 0 && div->initval <= 0))
		return 1;

	if (div->width == 0 && div->initval > 0)
		return div->initval;

	reg = readl(common->base + div->reg);

	if (div->initval == 0 || DIV_GET_EN_CLK_DIV_FACTOR(reg))
		clockdiv = cv1800_clk_regfield_get(reg, div);
	else if (div->initval > 0)
		clockdiv = div->initval;

	return clockdiv;
}

static u32 div_helper_round_rate(struct cv1800_clk_regfield *div,
				 struct clk_hw *hw, struct clk_hw *parent,
				 unsigned long rate, unsigned long *prate)
{
	if (div->width == 0) {
		if (div->initval <= 0)
			return DIV_ROUND_UP_ULL(*prate, 1);
		else
			return DIV_ROUND_UP_ULL(*prate, div->initval);
	}

	return divider_round_rate_parent(hw, parent, rate, prate, NULL,
					 div->width, div->flags);
}

static long div_round_rate(struct clk_hw *parent, unsigned long *parent_rate,
			   unsigned long rate, int id, void *data)
{
	struct cv1800_clk_div *div = data;

	return div_helper_round_rate(&div->div, &div->common.hw, parent,
				     rate, parent_rate);
}

static bool div_is_better_rate(struct cv1800_clk_common *common,
			       unsigned long target, unsigned long now,
			       unsigned long best)
{
	if (common->features & CLK_DIVIDER_ROUND_CLOSEST)
		return abs_diff(target, now) < abs_diff(target, best);

	return now <= target && now > best;
}

static int mux_helper_determine_rate(struct cv1800_clk_common *common,
				     struct clk_rate_request *req,
				     long (*round)(struct clk_hw *,
						   unsigned long *,
						   unsigned long,
						   int,
						   void *),
				     void *data)
{
	unsigned long best_parent_rate = 0, best_rate = 0;
	struct clk_hw *best_parent, *hw = &common->hw;
	unsigned int i;

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_NO_REPARENT) {
		unsigned long adj_parent_rate;

		best_parent = clk_hw_get_parent(hw);
		best_parent_rate = clk_hw_get_rate(best_parent);

		best_rate = round(best_parent, &adj_parent_rate,
				  req->rate, -1, data);

		goto find;
	}

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		unsigned long tmp_rate, parent_rate;
		struct clk_hw *parent;

		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);

		tmp_rate = round(parent, &parent_rate, req->rate, i, data);

		if (tmp_rate == req->rate) {
			best_parent = parent;
			best_parent_rate = parent_rate;
			best_rate = tmp_rate;
			goto find;
		}

		if (div_is_better_rate(common, req->rate,
				       tmp_rate, best_rate)) {
			best_parent = parent;
			best_parent_rate = parent_rate;
			best_rate = tmp_rate;
		}
	}

	if (best_rate == 0)
		return -EINVAL;

find:
	req->best_parent_hw = best_parent;
	req->best_parent_rate = best_parent_rate;
	req->rate = best_rate;
	return 0;
}

static int div_determine_rate(struct clk_hw *hw,
			      struct clk_rate_request *req)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);

	return mux_helper_determine_rate(&div->common, req,
					 div_round_rate, div);
}

static unsigned long div_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);
	unsigned long val;

	val = div_helper_get_clockdiv(&div->common, &div->div);
	if (val == 0)
		return 0;

	return divider_recalc_rate(hw, parent_rate, val, NULL,
				   div->div.flags, div->div.width);
}

static int div_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);
	unsigned long val;

	val = divider_get_val(rate, parent_rate, NULL,
			      div->div.width, div->div.flags);

	return div_helper_set_rate(&div->common, &div->div, val);
}

const struct clk_ops cv1800_clk_div_ops = {
	.disable = div_disable,
	.enable = div_enable,
	.is_enabled = div_is_enabled,

	.determine_rate = div_determine_rate,
	.recalc_rate	= div_recalc_rate,
	.set_rate = div_set_rate,
};

static inline struct cv1800_clk_bypass_div *
hw_to_cv1800_clk_bypass_div(struct clk_hw *hw)
{
	struct cv1800_clk_div *div = hw_to_cv1800_clk_div(hw);

	return container_of(div, struct cv1800_clk_bypass_div, div);
}

static long bypass_div_round_rate(struct clk_hw *parent,
				  unsigned long *parent_rate,
				  unsigned long rate, int id, void *data)
{
	struct cv1800_clk_bypass_div *div = data;

	if (id == -1) {
		if (cv1800_clk_checkbit(&div->div.common, &div->bypass))
			return *parent_rate;
		else
			return div_round_rate(parent, parent_rate, rate,
					      -1, &div->div);
	}

	if (id == 0)
		return *parent_rate;

	return div_round_rate(parent, parent_rate, rate, id - 1, &div->div);
}

static int bypass_div_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct cv1800_clk_bypass_div *div = hw_to_cv1800_clk_bypass_div(hw);

	return mux_helper_determine_rate(&div->div.common, req,
					 bypass_div_round_rate, div);
}

static unsigned long bypass_div_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct cv1800_clk_bypass_div *div = hw_to_cv1800_clk_bypass_div(hw);

	if (cv1800_clk_checkbit(&div->div.common, &div->bypass))
		return parent_rate;

	return div_recalc_rate(hw, parent_rate);
}

static int bypass_div_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct cv1800_clk_bypass_div *div = hw_to_cv1800_clk_bypass_div(hw);

	if (cv1800_clk_checkbit(&div->div.common, &div->bypass))
		return 0;

	return div_set_rate(hw, rate, parent_rate);
}

static u8 bypass_div_get_parent(struct clk_hw *hw)
{
	struct cv1800_clk_bypass_div *div = hw_to_cv1800_clk_bypass_div(hw);

	if (cv1800_clk_checkbit(&div->div.common, &div->bypass))
		return 0;

	return 1;
}

static int bypass_div_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv1800_clk_bypass_div *div = hw_to_cv1800_clk_bypass_div(hw);

	if (index)
		return cv1800_clk_clearbit(&div->div.common, &div->bypass);

	return cv1800_clk_setbit(&div->div.common, &div->bypass);
}

const struct clk_ops cv1800_clk_bypass_div_ops = {
	.disable = div_disable,
	.enable = div_enable,
	.is_enabled = div_is_enabled,

	.determine_rate = bypass_div_determine_rate,
	.recalc_rate = bypass_div_recalc_rate,
	.set_rate = bypass_div_set_rate,

	.set_parent = bypass_div_set_parent,
	.get_parent = bypass_div_get_parent,
};

/* MUX */
static inline struct cv1800_clk_mux *hw_to_cv1800_clk_mux(struct clk_hw *hw)
{
	struct cv1800_clk_common *common = hw_to_cv1800_clk_common(hw);

	return container_of(common, struct cv1800_clk_mux, common);
}

static int mux_enable(struct clk_hw *hw)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);

	return cv1800_clk_setbit(&mux->common, &mux->gate);
}

static void mux_disable(struct clk_hw *hw)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);

	cv1800_clk_clearbit(&mux->common, &mux->gate);
}

static int mux_is_enabled(struct clk_hw *hw)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);

	return cv1800_clk_checkbit(&mux->common, &mux->gate);
}

static long mux_round_rate(struct clk_hw *parent, unsigned long *parent_rate,
			   unsigned long rate, int id, void *data)
{
	struct cv1800_clk_mux *mux = data;

	return div_helper_round_rate(&mux->div, &mux->common.hw, parent,
				     rate, parent_rate);
}

static int mux_determine_rate(struct clk_hw *hw,
			      struct clk_rate_request *req)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);

	return mux_helper_determine_rate(&mux->common, req,
					 mux_round_rate, mux);
}

static unsigned long mux_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);
	unsigned long val;

	val = div_helper_get_clockdiv(&mux->common, &mux->div);
	if (val == 0)
		return 0;

	return divider_recalc_rate(hw, parent_rate, val, NULL,
				   mux->div.flags, mux->div.width);
}

static int mux_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);
	unsigned long val;

	val = divider_get_val(rate, parent_rate, NULL,
			      mux->div.width, mux->div.flags);

	return div_helper_set_rate(&mux->common, &mux->div, val);
}

static u8 mux_get_parent(struct clk_hw *hw)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);
	u32 reg = readl(mux->common.base + mux->mux.reg);

	return cv1800_clk_regfield_get(reg, &mux->mux);
}

static int _mux_set_parent(struct cv1800_clk_mux *mux, u8 index)
{
	u32 reg;

	reg = readl(mux->common.base + mux->mux.reg);
	reg = cv1800_clk_regfield_set(reg, index, &mux->mux);
	writel(reg, mux->common.base + mux->mux.reg);

	return 0;
}

static int mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);
	unsigned long flags;

	spin_lock_irqsave(mux->common.lock, flags);

	_mux_set_parent(mux, index);

	spin_unlock_irqrestore(mux->common.lock, flags);

	return 0;
}

const struct clk_ops cv1800_clk_mux_ops = {
	.disable = mux_disable,
	.enable = mux_enable,
	.is_enabled = mux_is_enabled,

	.determine_rate = mux_determine_rate,
	.recalc_rate = mux_recalc_rate,
	.set_rate = mux_set_rate,

	.set_parent = mux_set_parent,
	.get_parent = mux_get_parent,
};

static inline struct cv1800_clk_bypass_mux *
hw_to_cv1800_clk_bypass_mux(struct clk_hw *hw)
{
	struct cv1800_clk_mux *mux = hw_to_cv1800_clk_mux(hw);

	return container_of(mux, struct cv1800_clk_bypass_mux, mux);
}

static long bypass_mux_round_rate(struct clk_hw *parent,
				  unsigned long *parent_rate,
				  unsigned long rate, int id, void *data)
{
	struct cv1800_clk_bypass_mux *mux = data;

	if (id == -1) {
		if (cv1800_clk_checkbit(&mux->mux.common, &mux->bypass))
			return *parent_rate;
		else
			return mux_round_rate(parent, parent_rate, rate,
					      -1, &mux->mux);
	}

	if (id == 0)
		return *parent_rate;

	return mux_round_rate(parent, parent_rate, rate, id - 1, &mux->mux);
}

static int bypass_mux_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct cv1800_clk_bypass_mux *mux = hw_to_cv1800_clk_bypass_mux(hw);

	return mux_helper_determine_rate(&mux->mux.common, req,
					 bypass_mux_round_rate, mux);
}

static unsigned long bypass_mux_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct cv1800_clk_bypass_mux *mux = hw_to_cv1800_clk_bypass_mux(hw);

	if (cv1800_clk_checkbit(&mux->mux.common, &mux->bypass))
		return parent_rate;

	return mux_recalc_rate(hw, parent_rate);
}

static int bypass_mux_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct cv1800_clk_bypass_mux *mux = hw_to_cv1800_clk_bypass_mux(hw);

	if (cv1800_clk_checkbit(&mux->mux.common, &mux->bypass))
		return 0;

	return mux_set_rate(hw, rate, parent_rate);
}

static u8 bypass_mux_get_parent(struct clk_hw *hw)
{
	struct cv1800_clk_bypass_mux *mux = hw_to_cv1800_clk_bypass_mux(hw);

	if (cv1800_clk_checkbit(&mux->mux.common, &mux->bypass))
		return 0;

	return mux_get_parent(hw) + 1;
}

static int bypass_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv1800_clk_bypass_mux *mux = hw_to_cv1800_clk_bypass_mux(hw);

	if (index == 0)
		return cv1800_clk_setbit(&mux->mux.common, &mux->bypass);

	return cv1800_clk_clearbit(&mux->mux.common, &mux->bypass);
}

const struct clk_ops cv1800_clk_bypass_mux_ops = {
	.disable = mux_disable,
	.enable = mux_enable,
	.is_enabled = mux_is_enabled,

	.determine_rate = bypass_mux_determine_rate,
	.recalc_rate = bypass_mux_recalc_rate,
	.set_rate = bypass_mux_set_rate,

	.set_parent = bypass_mux_set_parent,
	.get_parent = bypass_mux_get_parent,
};

/* MMUX */
static inline struct cv1800_clk_mmux *hw_to_cv1800_clk_mmux(struct clk_hw *hw)
{
	struct cv1800_clk_common *common = hw_to_cv1800_clk_common(hw);

	return container_of(common, struct cv1800_clk_mmux, common);
}

static u8 mmux_get_parent_id(struct cv1800_clk_mmux *mmux)
{
	struct clk_hw *hw = &mmux->common.hw;
	struct clk_hw *parent = clk_hw_get_parent(hw);
	unsigned int i;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		if (parent == clk_hw_get_parent_by_index(hw, i))
			return i;
	}

	BUG();
}

static int mmux_enable(struct clk_hw *hw)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);

	return cv1800_clk_setbit(&mmux->common, &mmux->gate);
}

static void mmux_disable(struct clk_hw *hw)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);

	cv1800_clk_clearbit(&mmux->common, &mmux->gate);
}

static int mmux_is_enabled(struct clk_hw *hw)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);

	return cv1800_clk_checkbit(&mmux->common, &mmux->gate);
}

static long mmux_round_rate(struct clk_hw *parent, unsigned long *parent_rate,
			    unsigned long rate, int id, void *data)
{
	struct cv1800_clk_mmux *mmux = data;
	s8 div_id;

	if (id == -1) {
		if (cv1800_clk_checkbit(&mmux->common, &mmux->bypass))
			return *parent_rate;

		id = mmux_get_parent_id(mmux);
	}

	div_id = mmux->parent2sel[id];

	if (div_id < 0)
		return *parent_rate;

	return div_helper_round_rate(&mmux->div[div_id],
				     &mmux->common.hw, parent,
				     rate, parent_rate);
}

static int mmux_determine_rate(struct clk_hw *hw,
			       struct clk_rate_request *req)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);

	return mux_helper_determine_rate(&mmux->common, req,
					 mmux_round_rate, mmux);
}

static unsigned long mmux_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);
	unsigned long val;
	struct cv1800_clk_regfield *div;

	if (cv1800_clk_checkbit(&mmux->common, &mmux->bypass))
		return parent_rate;

	if (cv1800_clk_checkbit(&mmux->common, &mmux->clk_sel))
		div = &mmux->div[0];
	else
		div = &mmux->div[1];

	val = div_helper_get_clockdiv(&mmux->common, div);
	if (val == 0)
		return 0;

	return divider_recalc_rate(hw, parent_rate, val, NULL,
				   div->flags, div->width);
}

static int mmux_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);
	struct cv1800_clk_regfield *div;
	unsigned long val;

	if (cv1800_clk_checkbit(&mmux->common, &mmux->bypass))
		return parent_rate;

	if (cv1800_clk_checkbit(&mmux->common, &mmux->clk_sel))
		div = &mmux->div[0];
	else
		div = &mmux->div[1];

	val = divider_get_val(rate, parent_rate, NULL,
			      div->width, div->flags);

	return div_helper_set_rate(&mmux->common, div, val);
}

static u8 mmux_get_parent(struct clk_hw *hw)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);
	struct cv1800_clk_regfield *mux;
	u32 reg;
	s8 clk_sel;

	if (cv1800_clk_checkbit(&mmux->common, &mmux->bypass))
		return 0;

	if (cv1800_clk_checkbit(&mmux->common, &mmux->clk_sel))
		clk_sel = 0;
	else
		clk_sel = 1;
	mux = &mmux->mux[clk_sel];

	reg = readl(mmux->common.base + mux->reg);

	return mmux->sel2parent[clk_sel][cv1800_clk_regfield_get(reg, mux)];
}

static int mmux_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv1800_clk_mmux *mmux = hw_to_cv1800_clk_mmux(hw);
	struct cv1800_clk_regfield *mux;
	unsigned long flags;
	u32 reg;
	s8 clk_sel = mmux->parent2sel[index];

	if (index == 0 || clk_sel == -1) {
		cv1800_clk_setbit(&mmux->common, &mmux->bypass);
		goto release;
	}

	cv1800_clk_clearbit(&mmux->common, &mmux->bypass);

	if (clk_sel)
		cv1800_clk_clearbit(&mmux->common, &mmux->clk_sel);
	else
		cv1800_clk_setbit(&mmux->common, &mmux->clk_sel);

	spin_lock_irqsave(mmux->common.lock, flags);

	mux = &mmux->mux[clk_sel];
	reg = readl(mmux->common.base + mux->reg);
	reg = cv1800_clk_regfield_set(reg, index, mux);

	writel(reg, mmux->common.base + mux->reg);

	spin_unlock_irqrestore(mmux->common.lock, flags);

release:
	return 0;
}

const struct clk_ops cv1800_clk_mmux_ops = {
	.disable = mmux_disable,
	.enable = mmux_enable,
	.is_enabled = mmux_is_enabled,

	.determine_rate = mmux_determine_rate,
	.recalc_rate = mmux_recalc_rate,
	.set_rate = mmux_set_rate,

	.set_parent = mmux_set_parent,
	.get_parent = mmux_get_parent,
};

/* AUDIO CLK */
static inline struct cv1800_clk_audio *
hw_to_cv1800_clk_audio(struct clk_hw *hw)
{
	struct cv1800_clk_common *common = hw_to_cv1800_clk_common(hw);

	return container_of(common, struct cv1800_clk_audio, common);
}

static int aclk_enable(struct clk_hw *hw)
{
	struct cv1800_clk_audio *aclk = hw_to_cv1800_clk_audio(hw);

	cv1800_clk_setbit(&aclk->common, &aclk->src_en);
	return cv1800_clk_setbit(&aclk->common, &aclk->output_en);
}

static void aclk_disable(struct clk_hw *hw)
{
	struct cv1800_clk_audio *aclk = hw_to_cv1800_clk_audio(hw);

	cv1800_clk_clearbit(&aclk->common, &aclk->output_en);
	cv1800_clk_clearbit(&aclk->common, &aclk->src_en);
}

static int aclk_is_enabled(struct clk_hw *hw)
{
	struct cv1800_clk_audio *aclk = hw_to_cv1800_clk_audio(hw);

	return cv1800_clk_checkbit(&aclk->common, &aclk->output_en);
}

static int aclk_determine_rate(struct clk_hw *hw,
			       struct clk_rate_request *req)
{
	struct cv1800_clk_audio *aclk = hw_to_cv1800_clk_audio(hw);

	req->rate = aclk->target_rate;

	return 0;
}

static unsigned long aclk_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct cv1800_clk_audio *aclk = hw_to_cv1800_clk_audio(hw);
	u64 rate = parent_rate;
	u64 factor = 2;
	u32 regval;

	if (!cv1800_clk_checkbit(&aclk->common, &aclk->div_en))
		return 0;

	regval = readl(aclk->common.base + aclk->m.reg);
	factor *= cv1800_clk_regfield_get(regval, &aclk->m);

	regval = readl(aclk->common.base + aclk->n.reg);
	rate *= cv1800_clk_regfield_get(regval, &aclk->n);

	return DIV64_U64_ROUND_UP(rate, factor);
}

static void aclk_determine_mn(unsigned long parent_rate, unsigned long rate,
			      u32 *m, u32 *n)
{
	u32 tm = parent_rate / 2;
	u32 tn = rate;
	u32 tcommon = gcd(tm, tn);
	*m = tm / tcommon;
	*n = tn / tcommon;
}

static int aclk_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct cv1800_clk_audio *aclk = hw_to_cv1800_clk_audio(hw);
	unsigned long flags;
	u32 m, n;

	aclk_determine_mn(parent_rate, rate,
			  &m, &n);

	spin_lock_irqsave(aclk->common.lock, flags);

	writel(m, aclk->common.base + aclk->m.reg);
	writel(n, aclk->common.base + aclk->n.reg);

	cv1800_clk_setbit(&aclk->common, &aclk->div_en);
	cv1800_clk_setbit(&aclk->common, &aclk->div_up);

	spin_unlock_irqrestore(aclk->common.lock, flags);

	return 0;
}

const struct clk_ops cv1800_clk_audio_ops = {
	.disable = aclk_disable,
	.enable = aclk_enable,
	.is_enabled = aclk_is_enabled,

	.determine_rate = aclk_determine_rate,
	.recalc_rate = aclk_recalc_rate,
	.set_rate = aclk_set_rate,
};
