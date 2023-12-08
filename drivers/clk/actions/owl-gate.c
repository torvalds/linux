// SPDX-License-Identifier: GPL-2.0+
//
// OWL gate clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include "owl-gate.h"

void owl_gate_set(const struct owl_clk_common *common,
		 const struct owl_gate_hw *gate_hw, bool enable)
{
	int set = gate_hw->gate_flags & CLK_GATE_SET_TO_DISABLE ? 1 : 0;
	u32 reg;

	set ^= enable;

	regmap_read(common->regmap, gate_hw->reg, &reg);

	if (set)
		reg |= BIT(gate_hw->bit_idx);
	else
		reg &= ~BIT(gate_hw->bit_idx);

	regmap_write(common->regmap, gate_hw->reg, reg);
}

static void owl_gate_disable(struct clk_hw *hw)
{
	struct owl_gate *gate = hw_to_owl_gate(hw);
	struct owl_clk_common *common = &gate->common;

	owl_gate_set(common, &gate->gate_hw, false);
}

static int owl_gate_enable(struct clk_hw *hw)
{
	struct owl_gate *gate = hw_to_owl_gate(hw);
	struct owl_clk_common *common = &gate->common;

	owl_gate_set(common, &gate->gate_hw, true);

	return 0;
}

int owl_gate_clk_is_enabled(const struct owl_clk_common *common,
		   const struct owl_gate_hw *gate_hw)
{
	u32 reg;

	regmap_read(common->regmap, gate_hw->reg, &reg);

	if (gate_hw->gate_flags & CLK_GATE_SET_TO_DISABLE)
		reg ^= BIT(gate_hw->bit_idx);

	return !!(reg & BIT(gate_hw->bit_idx));
}

static int owl_gate_is_enabled(struct clk_hw *hw)
{
	struct owl_gate *gate = hw_to_owl_gate(hw);
	struct owl_clk_common *common = &gate->common;

	return owl_gate_clk_is_enabled(common, &gate->gate_hw);
}

const struct clk_ops owl_gate_ops = {
	.disable	= owl_gate_disable,
	.enable		= owl_gate_enable,
	.is_enabled	= owl_gate_is_enabled,
};
