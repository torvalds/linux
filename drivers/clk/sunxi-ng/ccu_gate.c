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

void ccu_gate_helper_disable(struct ccu_common *common, u32 gate)
{
	unsigned long flags;
	u32 reg;

	if (!gate)
		return;

	spin_lock_irqsave(common->lock, flags);

	reg = readl(common->base + common->reg);
	writel(reg & ~gate, common->base + common->reg);

	spin_unlock_irqrestore(common->lock, flags);
}

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
	writel(reg | gate, common->base + common->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}

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

static int ccu_gate_is_enabled(struct clk_hw *hw)
{
	struct ccu_gate *cg = hw_to_ccu_gate(hw);

	return ccu_gate_helper_is_enabled(&cg->common, cg->enable);
}

const struct clk_ops ccu_gate_ops = {
	.disable	= ccu_gate_disable,
	.enable		= ccu_gate_enable,
	.is_enabled	= ccu_gate_is_enabled,
};
