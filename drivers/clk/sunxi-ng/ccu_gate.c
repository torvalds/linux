// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>

#include "ccu_gate.h"

void ccu_gate_helper_disable(struct ccu_common *common, u32 gate)
{
	unsigned long flags;
	u32 reg;

	if (!gate)
		return;

	spin_lock_irqsave(common->lock, flags);

	reg = readl(common->base + common->reg);
	if (common->features & CCU_FEATURE_UPDATE_BIT)
		reg |= CCU_SUNXI_UPDATE_BIT;
	writel(reg & ~gate, common->base + common->reg);

	spin_unlock_irqrestore(common->lock, flags);
}
EXPORT_SYMBOL_NS_GPL(ccu_gate_helper_disable, "SUNXI_CCU");

static void ccu_gate_disable(struct clk_hw *hw)
{
	struct ccu_gate *cg = hw_to_ccu_gate(hw);

	return ccu_gate_helper_disable(&cg->common, cg->enable);
}

int ccu_gate_helper_enable(struct ccu_common *common, u32 gate)
{
	unsigned long flags;
	u32 reg;

	if (!gate)
		return 0;

	spin_lock_irqsave(common->lock, flags);

	reg = readl(common->base + common->reg);
	if (common->features & CCU_FEATURE_UPDATE_BIT)
		reg |= CCU_SUNXI_UPDATE_BIT;
	writel(reg | gate, common->base + common->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ccu_gate_helper_enable, "SUNXI_CCU");

static int ccu_gate_enable(struct clk_hw *hw)
{
	struct ccu_gate *cg = hw_to_ccu_gate(hw);

	return ccu_gate_helper_enable(&cg->common, cg->enable);
}

int ccu_gate_helper_is_enabled(struct ccu_common *common, u32 gate)
{
	if (!gate)
		return 1;

	return readl(common->base + common->reg) & gate;
}
EXPORT_SYMBOL_NS_GPL(ccu_gate_helper_is_enabled, "SUNXI_CCU");

static int ccu_gate_is_enabled(struct clk_hw *hw)
{
	struct ccu_gate *cg = hw_to_ccu_gate(hw);

	return ccu_gate_helper_is_enabled(&cg->common, cg->enable);
}

static unsigned long ccu_gate_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct ccu_gate *cg = hw_to_ccu_gate(hw);
	unsigned long rate = parent_rate;

	if (cg->common.features & CCU_FEATURE_ALL_PREDIV)
		rate /= cg->common.prediv;

	return rate;
}

static int ccu_gate_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct ccu_gate *cg = hw_to_ccu_gate(hw);
	int div = 1;

	if (cg->common.features & CCU_FEATURE_ALL_PREDIV)
		div = cg->common.prediv;

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		unsigned long best_parent = req->rate;

		if (cg->common.features & CCU_FEATURE_ALL_PREDIV)
			best_parent *= div;
		req->best_parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw), best_parent);
	}

	req->rate = req->best_parent_rate / div;

	return 0;
}

static int ccu_gate_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * clk_factor_round_rate returns values that ensure this call is a
	 * nop.
	 */

	return 0;
}

const struct clk_ops ccu_gate_ops = {
	.disable	= ccu_gate_disable,
	.enable		= ccu_gate_enable,
	.is_enabled	= ccu_gate_is_enabled,
	.determine_rate = ccu_gate_determine_rate,
	.set_rate	= ccu_gate_set_rate,
	.recalc_rate	= ccu_gate_recalc_rate,
};
EXPORT_SYMBOL_NS_GPL(ccu_gate_ops, "SUNXI_CCU");
