// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2018 Microchip Technology Inc,
 *                     Codrin Ciubotariu <codrin.ciubotariu@microchip.com>
 *
 *
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <soc/at91/atmel-sfr.h>

#include "pmc.h"

struct clk_i2s_mux {
	struct clk_hw hw;
	struct regmap *regmap;
	u8 bus_id;
};

#define to_clk_i2s_mux(hw) container_of(hw, struct clk_i2s_mux, hw)

static u8 clk_i2s_mux_get_parent(struct clk_hw *hw)
{
	struct clk_i2s_mux *mux = to_clk_i2s_mux(hw);
	u32 val;

	regmap_read(mux->regmap, AT91_SFR_I2SCLKSEL, &val);

	return (val & BIT(mux->bus_id)) >> mux->bus_id;
}

static int clk_i2s_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_i2s_mux *mux = to_clk_i2s_mux(hw);

	return regmap_update_bits(mux->regmap, AT91_SFR_I2SCLKSEL,
				  BIT(mux->bus_id), index << mux->bus_id);
}

static const struct clk_ops clk_i2s_mux_ops = {
	.get_parent = clk_i2s_mux_get_parent,
	.set_parent = clk_i2s_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

struct clk_hw * __init
at91_clk_i2s_mux_register(struct regmap *regmap, const char *name,
			  const char * const *parent_names,
			  unsigned int num_parents, u8 bus_id)
{
	struct clk_init_data init = {};
	struct clk_i2s_mux *i2s_ck;
	int ret;

	i2s_ck = kzalloc(sizeof(*i2s_ck), GFP_KERNEL);
	if (!i2s_ck)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_i2s_mux_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	i2s_ck->hw.init = &init;
	i2s_ck->bus_id = bus_id;
	i2s_ck->regmap = regmap;

	ret = clk_hw_register(NULL, &i2s_ck->hw);
	if (ret) {
		kfree(i2s_ck);
		return ERR_PTR(ret);
	}

	return &i2s_ck->hw;
}
