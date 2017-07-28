/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define SYSTEM_MAX_ID		31

#define SYSTEM_MAX_NAME_SZ	32

#define to_clk_system(hw) container_of(hw, struct clk_system, hw)
struct clk_system {
	struct clk_hw hw;
	struct regmap *regmap;
	u8 id;
};

static inline int is_pck(int id)
{
	return (id >= 8) && (id <= 15);
}

static inline bool clk_system_ready(struct regmap *regmap, int id)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & (1 << id) ? 1 : 0;
}

static int clk_system_prepare(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);

	regmap_write(sys->regmap, AT91_PMC_SCER, 1 << sys->id);

	if (!is_pck(sys->id))
		return 0;

	while (!clk_system_ready(sys->regmap, sys->id))
		cpu_relax();

	return 0;
}

static void clk_system_unprepare(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);

	regmap_write(sys->regmap, AT91_PMC_SCDR, 1 << sys->id);
}

static int clk_system_is_prepared(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);
	unsigned int status;

	regmap_read(sys->regmap, AT91_PMC_SCSR, &status);

	if (!(status & (1 << sys->id)))
		return 0;

	if (!is_pck(sys->id))
		return 1;

	regmap_read(sys->regmap, AT91_PMC_SR, &status);

	return status & (1 << sys->id) ? 1 : 0;
}

static const struct clk_ops system_ops = {
	.prepare = clk_system_prepare,
	.unprepare = clk_system_unprepare,
	.is_prepared = clk_system_is_prepared,
};

static struct clk_hw * __init
at91_clk_register_system(struct regmap *regmap, const char *name,
			 const char *parent_name, u8 id)
{
	struct clk_system *sys;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	if (!parent_name || id > SYSTEM_MAX_ID)
		return ERR_PTR(-EINVAL);

	sys = kzalloc(sizeof(*sys), GFP_KERNEL);
	if (!sys)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &system_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_PARENT;

	sys->id = id;
	sys->hw.init = &init;
	sys->regmap = regmap;

	hw = &sys->hw;
	ret = clk_hw_register(NULL, &sys->hw);
	if (ret) {
		kfree(sys);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void __init of_at91rm9200_clk_sys_setup(struct device_node *np)
{
	int num;
	u32 id;
	struct clk_hw *hw;
	const char *name;
	struct device_node *sysclknp;
	const char *parent_name;
	struct regmap *regmap;

	num = of_get_child_count(np);
	if (num > (SYSTEM_MAX_ID + 1))
		return;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	for_each_child_of_node(np, sysclknp) {
		if (of_property_read_u32(sysclknp, "reg", &id))
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = sysclknp->name;

		parent_name = of_clk_get_parent_name(sysclknp, 0);

		hw = at91_clk_register_system(regmap, name, parent_name, id);
		if (IS_ERR(hw))
			continue;

		of_clk_add_hw_provider(sysclknp, of_clk_hw_simple_get, hw);
	}
}
CLK_OF_DECLARE(at91rm9200_clk_sys, "atmel,at91rm9200-clk-system",
	       of_at91rm9200_clk_sys_setup);
