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

#define	I2S_BUS_NR	2

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

static struct clk_hw * __init
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

static void __init of_sama5d2_clk_i2s_mux_setup(struct device_node *np)
{
	struct regmap *regmap_sfr;
	u8 bus_id;
	const char *parent_names[2];
	struct device_node *i2s_mux_np;
	struct clk_hw *hw;
	int ret;

	regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d2-sfr");
	if (IS_ERR(regmap_sfr))
		return;

	for_each_child_of_node(np, i2s_mux_np) {
		if (of_property_read_u8(i2s_mux_np, "reg", &bus_id))
			continue;

		if (bus_id > I2S_BUS_NR)
			continue;

		ret = of_clk_parent_fill(i2s_mux_np, parent_names, 2);
		if (ret != 2)
			continue;

		hw = at91_clk_i2s_mux_register(regmap_sfr, i2s_mux_np->name,
					       parent_names, 2, bus_id);
		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(i2s_mux_np, of_clk_hw_simple_get, hw);
	}
}

CLK_OF_DECLARE(sama5d2_clk_i2s_mux, "atmel,sama5d2-clk-i2s-mux",
	       of_sama5d2_clk_i2s_mux_setup);
