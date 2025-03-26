// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "ccu_gate.h"
#include "ccu_mux.h"

#define CCU_MUX_KEY_VALUE		0x16aa0000

static u16 ccu_mux_get_prediv(struct ccu_common *common,
			      struct ccu_mux_internal *cm,
			      int parent_index)
{
	u16 prediv = 1;
	u32 reg;

	if (!((common->features & CCU_FEATURE_FIXED_PREDIV) ||
	      (common->features & CCU_FEATURE_VARIABLE_PREDIV) ||
	      (common->features & CCU_FEATURE_ALL_PREDIV)))
		return 1;

	if (common->features & CCU_FEATURE_ALL_PREDIV)
		return common->prediv;

	reg = readl(common->base + common->reg);
	if (parent_index < 0) {
		parent_index = reg >> cm->shift;
		parent_index &= (1 << cm->width) - 1;
	}

	if (common->features & CCU_FEATURE_FIXED_PREDIV) {
		int i;

		for (i = 0; i < cm->n_predivs; i++)
			if (parent_index == cm->fixed_predivs[i].index)
				prediv = cm->fixed_predivs[i].div;
	}

	if (common->features & CCU_FEATURE_VARIABLE_PREDIV) {
		int i;

		for (i = 0; i < cm->n_var_predivs; i++)
			if (parent_index == cm->var_predivs[i].index) {
				u8 div;

				div = reg >> cm->var_predivs[i].shift;
				div &= (1 << cm->var_predivs[i].width) - 1;
				prediv = div + 1;
			}
	}

	return prediv;
}

unsigned long ccu_mux_helper_apply_prediv(struct ccu_common *common,
					  struct ccu_mux_internal *cm,
					  int parent_index,
					  unsigned long parent_rate)
{
	return parent_rate / ccu_mux_get_prediv(common, cm, parent_index);
}
EXPORT_SYMBOL_NS_GPL(ccu_mux_helper_apply_prediv, "SUNXI_CCU");

static unsigned long ccu_mux_helper_unapply_prediv(struct ccu_common *common,
					    struct ccu_mux_internal *cm,
					    int parent_index,
					    unsigned long parent_rate)
{
	return parent_rate * ccu_mux_get_prediv(common, cm, parent_index);
}

int ccu_mux_helper_determine_rate(struct ccu_common *common,
				  struct ccu_mux_internal *cm,
				  struct clk_rate_request *req,
				  unsigned long (*round)(struct ccu_mux_internal *,
							 struct clk_hw *,
							 unsigned long *,
							 unsigned long,
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
		adj_parent_rate = ccu_mux_helper_apply_prediv(common, cm, -1,
							      best_parent_rate);

		best_rate = round(cm, best_parent, &adj_parent_rate,
				  req->rate, data);

		/*
		 * adj_parent_rate might have been modified by our clock.
		 * Unapply the pre-divider if there's one, and give
		 * the actual frequency the parent needs to run at.
		 */
		best_parent_rate = ccu_mux_helper_unapply_prediv(common, cm, -1,
								 adj_parent_rate);

		goto out;
	}

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		unsigned long tmp_rate, parent_rate;
		struct clk_hw *parent;

		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		parent_rate = ccu_mux_helper_apply_prediv(common, cm, i,
							  clk_hw_get_rate(parent));

		tmp_rate = round(cm, parent, &parent_rate, req->rate, data);

		/*
		 * parent_rate might have been modified by our clock.
		 * Unapply the pre-divider if there's one, and give
		 * the actual frequency the parent needs to run at.
		 */
		parent_rate = ccu_mux_helper_unapply_prediv(common, cm, i,
							    parent_rate);
		if (tmp_rate == req->rate) {
			best_parent = parent;
			best_parent_rate = parent_rate;
			best_rate = tmp_rate;
			goto out;
		}

		if (ccu_is_better_rate(common, req->rate, tmp_rate, best_rate)) {
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
EXPORT_SYMBOL_NS_GPL(ccu_mux_helper_determine_rate, "SUNXI_CCU");

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
EXPORT_SYMBOL_NS_GPL(ccu_mux_helper_get_parent, "SUNXI_CCU");

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

	/* The key field always reads as zero. */
	if (common->features & CCU_FEATURE_KEY_FIELD)
		reg |= CCU_MUX_KEY_VALUE;
	if (common->features & CCU_FEATURE_UPDATE_BIT)
		reg |= CCU_SUNXI_UPDATE_BIT;

	reg &= ~GENMASK(cm->width + cm->shift - 1, cm->shift);
	writel(reg | (index << cm->shift), common->base + common->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ccu_mux_helper_set_parent, "SUNXI_CCU");

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

static int ccu_mux_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	if (cm->common.features & CCU_FEATURE_CLOSEST_RATE)
		return clk_mux_determine_rate_flags(hw, req, CLK_MUX_ROUND_CLOSEST);

	return clk_mux_determine_rate_flags(hw, req, 0);
}

static unsigned long ccu_mux_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct ccu_mux *cm = hw_to_ccu_mux(hw);

	return ccu_mux_helper_apply_prediv(&cm->common, &cm->mux, -1,
					   parent_rate);
}

const struct clk_ops ccu_mux_ops = {
	.disable	= ccu_mux_disable,
	.enable		= ccu_mux_enable,
	.is_enabled	= ccu_mux_is_enabled,

	.get_parent	= ccu_mux_get_parent,
	.set_parent	= ccu_mux_set_parent,

	.determine_rate	= ccu_mux_determine_rate,
	.recalc_rate	= ccu_mux_recalc_rate,
};
EXPORT_SYMBOL_NS_GPL(ccu_mux_ops, "SUNXI_CCU");

/*
 * This clock notifier is called when the frequency of the of the parent
 * PLL clock is to be changed. The idea is to switch the parent to a
 * stable clock, such as the main oscillator, while the PLL frequency
 * stabilizes.
 */
static int ccu_mux_notifier_cb(struct notifier_block *nb,
			       unsigned long event, void *data)
{
	struct ccu_mux_nb *mux = to_ccu_mux_nb(nb);
	int ret = 0;

	if (event == PRE_RATE_CHANGE) {
		mux->original_index = ccu_mux_helper_get_parent(mux->common,
								mux->cm);
		ret = ccu_mux_helper_set_parent(mux->common, mux->cm,
						mux->bypass_index);
	} else if (event == POST_RATE_CHANGE) {
		ret = ccu_mux_helper_set_parent(mux->common, mux->cm,
						mux->original_index);
	}

	udelay(mux->delay_us);

	return notifier_from_errno(ret);
}

int ccu_mux_notifier_register(struct clk *clk, struct ccu_mux_nb *mux_nb)
{
	mux_nb->clk_nb.notifier_call = ccu_mux_notifier_cb;

	return clk_notifier_register(clk, &mux_nb->clk_nb);
}
EXPORT_SYMBOL_NS_GPL(ccu_mux_notifier_register, "SUNXI_CCU");
