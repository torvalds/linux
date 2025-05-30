// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 Collabora Ltd.
 * Author: Nicolas Frattaroli <nicolas.frattaroli@collabora.com>
 *
 * Certain clocks on Rockchip are "gated" behind an additional register bit
 * write in a GRF register, such as the SAI MCLKs on RK3576. This code
 * implements a clock driver for these types of gates, based on regmaps.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include "clk.h"

struct rockchip_gate_grf {
	struct clk_hw		hw;
	struct regmap		*regmap;
	unsigned int		reg;
	unsigned int		shift;
	u8			flags;
};

#define to_gate_grf(_hw) container_of(_hw, struct rockchip_gate_grf, hw)

static int rockchip_gate_grf_enable(struct clk_hw *hw)
{
	struct rockchip_gate_grf *gate = to_gate_grf(hw);
	u32 val = !(gate->flags & CLK_GATE_SET_TO_DISABLE) ? BIT(gate->shift) : 0;
	u32 hiword = ((gate->flags & CLK_GATE_HIWORD_MASK) ? 1 : 0) << (gate->shift + 16);
	int ret;

	ret = regmap_update_bits(gate->regmap, gate->reg,
				 hiword | BIT(gate->shift), hiword | val);

	return ret;
}

static void rockchip_gate_grf_disable(struct clk_hw *hw)
{
	struct rockchip_gate_grf *gate = to_gate_grf(hw);
	u32 val = !(gate->flags & CLK_GATE_SET_TO_DISABLE) ? 0 : BIT(gate->shift);
	u32 hiword = ((gate->flags & CLK_GATE_HIWORD_MASK) ? 1 : 0) << (gate->shift + 16);

	regmap_update_bits(gate->regmap, gate->reg,
			   hiword | BIT(gate->shift), hiword | val);
}

static int rockchip_gate_grf_is_enabled(struct clk_hw *hw)
{
	struct rockchip_gate_grf *gate = to_gate_grf(hw);
	bool invert = !!(gate->flags & CLK_GATE_SET_TO_DISABLE);
	int ret;

	ret = regmap_test_bits(gate->regmap, gate->reg, BIT(gate->shift));
	if (ret < 0)
		ret = 0;

	return invert ? 1 - ret : ret;

}

static const struct clk_ops rockchip_gate_grf_ops = {
	.enable = rockchip_gate_grf_enable,
	.disable = rockchip_gate_grf_disable,
	.is_enabled = rockchip_gate_grf_is_enabled,
};

struct clk *rockchip_clk_register_gate_grf(const char *name,
		const char *parent_name, unsigned long flags,
		struct regmap *regmap, unsigned int reg, unsigned int shift,
		u8 gate_flags)
{
	struct rockchip_gate_grf *gate;
	struct clk_init_data init;
	struct clk *clk;

	if (IS_ERR(regmap)) {
		pr_err("%s: regmap not available\n", __func__);
		return ERR_PTR(-EOPNOTSUPP);
	}

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags;
	init.num_parents = parent_name ? 1 : 0;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.ops = &rockchip_gate_grf_ops;

	gate->hw.init = &init;
	gate->regmap = regmap;
	gate->reg = reg;
	gate->shift = shift;
	gate->flags = gate_flags;

	clk = clk_register(NULL, &gate->hw);
	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}
