/*
 * Copyright (c) 2017 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/clk-provider.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include "gxbb-aoclk.h"

static int aoclk_gate_regmap_enable(struct clk_hw *hw)
{
	struct aoclk_gate_regmap *gate = to_aoclk_gate_regmap(hw);

	return regmap_update_bits(gate->regmap, AO_RTI_GEN_CNTL_REG0,
				  BIT(gate->bit_idx), BIT(gate->bit_idx));
}

static void aoclk_gate_regmap_disable(struct clk_hw *hw)
{
	struct aoclk_gate_regmap *gate = to_aoclk_gate_regmap(hw);

	regmap_update_bits(gate->regmap, AO_RTI_GEN_CNTL_REG0,
			   BIT(gate->bit_idx), 0);
}

static int aoclk_gate_regmap_is_enabled(struct clk_hw *hw)
{
	struct aoclk_gate_regmap *gate = to_aoclk_gate_regmap(hw);
	unsigned int val;
	int ret;

	ret = regmap_read(gate->regmap, AO_RTI_GEN_CNTL_REG0, &val);
	if (ret)
		return ret;

	return (val & BIT(gate->bit_idx)) != 0;
}

const struct clk_ops meson_aoclk_gate_regmap_ops = {
	.enable = aoclk_gate_regmap_enable,
	.disable = aoclk_gate_regmap_disable,
	.is_enabled = aoclk_gate_regmap_is_enabled,
};
