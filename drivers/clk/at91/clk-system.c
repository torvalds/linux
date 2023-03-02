// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
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
	struct at91_clk_pms pms;
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

	return !!(status & (1 << id));
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

	return !!(status & (1 << sys->id));
}

static int clk_system_save_context(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);

	sys->pms.status = clk_system_is_prepared(hw);

	return 0;
}

static void clk_system_restore_context(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);

	if (sys->pms.status)
		clk_system_prepare(&sys->hw);
}

static const struct clk_ops system_ops = {
	.prepare = clk_system_prepare,
	.unprepare = clk_system_unprepare,
	.is_prepared = clk_system_is_prepared,
	.save_context = clk_system_save_context,
	.restore_context = clk_system_restore_context,
};

struct clk_hw * __init
at91_clk_register_system(struct regmap *regmap, const char *name,
			 const char *parent_name, u8 id, unsigned long flags)
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
	init.flags = CLK_SET_RATE_PARENT | flags;

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
