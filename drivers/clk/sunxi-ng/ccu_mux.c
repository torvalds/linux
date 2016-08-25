/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk-provider.h>

#include "ccu_gate.h"
#include "ccu_mux.h"

void ccu_mux_helper_adjust_parent_for_prediv(struct ccu_common *common,
					     struct ccu_mux_internal *cm,
					     int parent_index,
					     unsigned long *parent_rate)
{
	u16 prediv = 1;
	u32 reg;
	int i;

	if (!((common->features & CCU_FEATURE_FIXED_PREDIV) ||
	      (common->features & CCU_FEATURE_VARIABLE_PREDIV)))
		return;

	reg = readl(common->base + common->reg);
	if (parent_index < 0) {
		parent_index = reg >> cm->shift;
		parent_index &= (1 << cm->width) - 1;
	}

	if (common->features & CCU_FEATURE_FIXED_PREDIV)
		for (i = 0; i < cm->n_predivs; i++)
			if (parent_index == cm->fixed_predivs[i].index)
				prediv = cm->fixed_predivs[i].div;

	if (common->features & CCU_FEATURE_VARIABLE_PREDIV)
		if (parent_index == cm->variable_prediv.index) {
			u8 div;

			div = reg >> cm->variable_prediv.shift;
			div &= (1 << cm->variable_prediv.width) - 1;
			prediv = div + 1;
		}

	*parent_rate = *parent_rate / prediv;
}

int ccu_mux_helper_determine_rate(struct ccu_common *common,
				  struct ccu_mux_internal *cm,
				  struct clk_rate_request *req,
				  unsigned long (*round)(struct ccu_mux_internal *,
							 unsigned long,
							 unsigned long,
							 void *),
				  void *data)
{
	unsigned long best_parent_rate = 0, best_rate = 0;
	struct clk_hw *best_parent, *hw = &common->hw;
	unsigned int i;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		unsigned long tmp_rate, parent_rate;
		struct clk_hw *parent;

		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);
		ccu_mux_helper_adjust_parent_for_prediv(common, cm, i,
							&parent_rate);

		tmp_rate = round(cm, clk_hw_get_rate(parent), req->rate, data);
		if (tmp_rate == req->rate) {
			best_parent = parent;
			best_parent_rate = parent_rate;
			best_rate = tmp_rate;
			goto out;
		}

		if ((req->rate - tmp_rate) < (req->rate - best_rate)) {
			best_rate = tmp_rate;
			best_parent_rate = parent_rate;
			best_parent = parent;
		}
	}

	if (best_rate == 0)
		return -EINVAL;

out:
	req->best_parent_hw = best_parent;
	req->best_parent_rate = best_parent_rate;
	req->rate = best_rate;
	return 0;
}

u8 ccu_mux_helper_get_parent(struct ccu_common *common,
			     struct ccu_mux_internal *cm)
{
	u32 reg;
	u8 parent;

	reg = readl(common->base + common->reg);
	parent = reg >> cm->shift;
	parent &= (1 << cm->width) - 1;

	if (cm->table) {
		int num_parents = clk_hw_get_num_parents(&common->hw);
		int i;

		for (i = 0; i < num_parents; i++)
			if (cm->table[i] == parent)
				return i;
	}

	return parent;
}

int ccu_mux_helper_set_parent(struct ccu_common *common,
			      struct ccu_mux_internal *cm,
			      u8 index)
{
	unsigned long flags;
	u32 reg;

	if (cm->table)
		index = cm->table[index];

	spin_lock_irqsave(common->lock, flags);

	reg = readl(common->base + common->reg);
	reg &= ~GENMASK(cm->width + cm->shift - 1, cm->shift);
	writel(reg | (index << cm->shift), common->base + common->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}

static void ccu_mux_disable(struct clk_hw *hw)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	return ccu_gate_helper_disable(&cm->common, cm->enable);
}

static int ccu_mux_enable(struct clk_hw *hw)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	return ccu_gate_helper_enable(&cm->common, cm->enable);
}

static int ccu_mux_is_enabled(struct clk_hw *hw)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	return ccu_gate_helper_is_enabled(&cm->common, cm->enable);
}

static u8 ccu_mux_get_parent(struct clk_hw *hw)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	return ccu_mux_helper_get_parent(&cm->common, &cm->mux);
}

static int ccu_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	return ccu_mux_helper_set_parent(&cm->common, &cm->mux, index);
}

static unsigned long ccu_mux_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	ccu_mux_helper_adjust_parent_for_prediv(&cm->common, &cm->mux, -1,
						&parent_rate);

	return parent_rate;
}

const struct clk_ops ccu_mux_ops = {
	.disable	= ccu_mux_disable,
	.enable		= ccu_mux_enable,
	.is_enabled	= ccu_mux_is_enabled,

	.get_parent	= ccu_mux_get_parent,
	.set_parent	= ccu_mux_set_parent,

	.determine_rate	= __clk_mux_determine_rate,
	.recalc_rate	= ccu_mux_recalc_rate,
};
