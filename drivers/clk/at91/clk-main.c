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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define SLOW_CLOCK_FREQ		32768
#define MAINF_DIV		16
#define MAINFRDY_TIMEOUT	(((MAINF_DIV + 1) * USEC_PER_SEC) / \
				 SLOW_CLOCK_FREQ)
#define MAINF_LOOP_MIN_WAIT	(USEC_PER_SEC / SLOW_CLOCK_FREQ)
#define MAINF_LOOP_MAX_WAIT	MAINFRDY_TIMEOUT

#define MOR_KEY_MASK		(0xff << 16)

struct clk_main_osc {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_clk_main_osc(hw) container_of(hw, struct clk_main_osc, hw)

struct clk_main_rc_osc {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned long frequency;
	unsigned long accuracy;
};

#define to_clk_main_rc_osc(hw) container_of(hw, struct clk_main_rc_osc, hw)

struct clk_rm9200_main {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_clk_rm9200_main(hw) container_of(hw, struct clk_rm9200_main, hw)

struct clk_sam9x5_main {
	struct clk_hw hw;
	struct regmap *regmap;
	u8 parent;
};

#define to_clk_sam9x5_main(hw) container_of(hw, struct clk_sam9x5_main, hw)

static inline bool clk_main_osc_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_MOSCS;
}

static int clk_main_osc_prepare(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct regmap *regmap = osc->regmap;
	u32 tmp;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	tmp &= ~MOR_KEY_MASK;

	if (tmp & AT91_PMC_OSCBYPASS)
		return 0;

	if (!(tmp & AT91_PMC_MOSCEN)) {
		tmp |= AT91_PMC_MOSCEN | AT91_PMC_KEY;
		regmap_write(regmap, AT91_CKGR_MOR, tmp);
	}

	while (!clk_main_osc_ready(regmap))
		cpu_relax();

	return 0;
}

static void clk_main_osc_unprepare(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct regmap *regmap = osc->regmap;
	u32 tmp;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	if (tmp & AT91_PMC_OSCBYPASS)
		return;

	if (!(tmp & AT91_PMC_MOSCEN))
		return;

	tmp &= ~(AT91_PMC_KEY | AT91_PMC_MOSCEN);
	regmap_write(regmap, AT91_CKGR_MOR, tmp | AT91_PMC_KEY);
}

static int clk_main_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct regmap *regmap = osc->regmap;
	u32 tmp, status;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	if (tmp & AT91_PMC_OSCBYPASS)
		return 1;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return (status & AT91_PMC_MOSCS) && (tmp & AT91_PMC_MOSCEN);
}

static const struct clk_ops main_osc_ops = {
	.prepare = clk_main_osc_prepare,
	.unprepare = clk_main_osc_unprepare,
	.is_prepared = clk_main_osc_is_prepared,
};

static struct clk * __init
at91_clk_register_main_osc(struct regmap *regmap,
			   const char *name,
			   const char *parent_name,
			   bool bypass)
{
	struct clk_main_osc *osc;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!name || !parent_name)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_osc_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->regmap = regmap;

	if (bypass)
		regmap_update_bits(regmap,
				   AT91_CKGR_MOR, MOR_KEY_MASK |
				   AT91_PMC_MOSCEN,
				   AT91_PMC_OSCBYPASS | AT91_PMC_KEY);

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk))
		kfree(osc);

	return clk;
}

static void __init of_at91rm9200_clk_main_osc_setup(struct device_node *np)
{
	struct clk *clk;
	const char *name = np->name;
	const char *parent_name;
	struct regmap *regmap;
	bool bypass;

	of_property_read_string(np, "clock-output-names", &name);
	bypass = of_property_read_bool(np, "atmel,osc-bypass");
	parent_name = of_clk_get_parent_name(np, 0);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	clk = at91_clk_register_main_osc(regmap, name, parent_name, bypass);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(at91rm9200_clk_main_osc, "atmel,at91rm9200-clk-main-osc",
	       of_at91rm9200_clk_main_osc_setup);

static bool clk_main_rc_osc_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_MOSCRCS;
}

static int clk_main_rc_osc_prepare(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct regmap *regmap = osc->regmap;
	unsigned int mor;

	regmap_read(regmap, AT91_CKGR_MOR, &mor);

	if (!(mor & AT91_PMC_MOSCRCEN))
		regmap_update_bits(regmap, AT91_CKGR_MOR,
				   MOR_KEY_MASK | AT91_PMC_MOSCRCEN,
				   AT91_PMC_MOSCRCEN | AT91_PMC_KEY);

	while (!clk_main_rc_osc_ready(regmap))
		cpu_relax();

	return 0;
}

static void clk_main_rc_osc_unprepare(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct regmap *regmap = osc->regmap;
	unsigned int mor;

	regmap_read(regmap, AT91_CKGR_MOR, &mor);

	if (!(mor & AT91_PMC_MOSCRCEN))
		return;

	regmap_update_bits(regmap, AT91_CKGR_MOR,
			   MOR_KEY_MASK | AT91_PMC_MOSCRCEN, AT91_PMC_KEY);
}

static int clk_main_rc_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct regmap *regmap = osc->regmap;
	unsigned int mor, status;

	regmap_read(regmap, AT91_CKGR_MOR, &mor);
	regmap_read(regmap, AT91_PMC_SR, &status);

	return (mor & AT91_PMC_MOSCRCEN) && (status & AT91_PMC_MOSCRCS);
}

static unsigned long clk_main_rc_osc_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	return osc->frequency;
}

static unsigned long clk_main_rc_osc_recalc_accuracy(struct clk_hw *hw,
						     unsigned long parent_acc)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	return osc->accuracy;
}

static const struct clk_ops main_rc_osc_ops = {
	.prepare = clk_main_rc_osc_prepare,
	.unprepare = clk_main_rc_osc_unprepare,
	.is_prepared = clk_main_rc_osc_is_prepared,
	.recalc_rate = clk_main_rc_osc_recalc_rate,
	.recalc_accuracy = clk_main_rc_osc_recalc_accuracy,
};

static struct clk * __init
at91_clk_register_main_rc_osc(struct regmap *regmap,
			      const char *name,
			      u32 frequency, u32 accuracy)
{
	struct clk_main_rc_osc *osc;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!name || !frequency)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_rc_osc_ops;
	init.parent_names = NULL;
	init.num_parents = 0;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->regmap = regmap;
	osc->frequency = frequency;
	osc->accuracy = accuracy;

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk))
		kfree(osc);

	return clk;
}

static void __init of_at91sam9x5_clk_main_rc_osc_setup(struct device_node *np)
{
	struct clk *clk;
	u32 frequency = 0;
	u32 accuracy = 0;
	const char *name = np->name;
	struct regmap *regmap;

	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "clock-frequency", &frequency);
	of_property_read_u32(np, "clock-accuracy", &accuracy);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	clk = at91_clk_register_main_rc_osc(regmap, name, frequency, accuracy);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(at91sam9x5_clk_main_rc_osc, "atmel,at91sam9x5-clk-main-rc-osc",
	       of_at91sam9x5_clk_main_rc_osc_setup);


static int clk_main_probe_frequency(struct regmap *regmap)
{
	unsigned long prep_time, timeout;
	unsigned int mcfr;

	timeout = jiffies + usecs_to_jiffies(MAINFRDY_TIMEOUT);
	do {
		prep_time = jiffies;
		regmap_read(regmap, AT91_CKGR_MCFR, &mcfr);
		if (mcfr & AT91_PMC_MAINRDY)
			return 0;
		usleep_range(MAINF_LOOP_MIN_WAIT, MAINF_LOOP_MAX_WAIT);
	} while (time_before(prep_time, timeout));

	return -ETIMEDOUT;
}

static unsigned long clk_main_recalc_rate(struct regmap *regmap,
					  unsigned long parent_rate)
{
	unsigned int mcfr;

	if (parent_rate)
		return parent_rate;

	pr_warn("Main crystal frequency not set, using approximate value\n");
	regmap_read(regmap, AT91_CKGR_MCFR, &mcfr);
	if (!(mcfr & AT91_PMC_MAINRDY))
		return 0;

	return ((mcfr & AT91_PMC_MAINF) * SLOW_CLOCK_FREQ) / MAINF_DIV;
}

static int clk_rm9200_main_prepare(struct clk_hw *hw)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return clk_main_probe_frequency(clkmain->regmap);
}

static int clk_rm9200_main_is_prepared(struct clk_hw *hw)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);
	unsigned int status;

	regmap_read(clkmain->regmap, AT91_CKGR_MCFR, &status);

	return status & AT91_PMC_MAINRDY ? 1 : 0;
}

static unsigned long clk_rm9200_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return clk_main_recalc_rate(clkmain->regmap, parent_rate);
}

static const struct clk_ops rm9200_main_ops = {
	.prepare = clk_rm9200_main_prepare,
	.is_prepared = clk_rm9200_main_is_prepared,
	.recalc_rate = clk_rm9200_main_recalc_rate,
};

static struct clk * __init
at91_clk_register_rm9200_main(struct regmap *regmap,
			      const char *name,
			      const char *parent_name)
{
	struct clk_rm9200_main *clkmain;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!name)
		return ERR_PTR(-EINVAL);

	if (!parent_name)
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &rm9200_main_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	clkmain->hw.init = &init;
	clkmain->regmap = regmap;

	clk = clk_register(NULL, &clkmain->hw);
	if (IS_ERR(clk))
		kfree(clkmain);

	return clk;
}

static void __init of_at91rm9200_clk_main_setup(struct device_node *np)
{
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;
	struct regmap *regmap;

	parent_name = of_clk_get_parent_name(np, 0);
	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	clk = at91_clk_register_rm9200_main(regmap, name, parent_name);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(at91rm9200_clk_main, "atmel,at91rm9200-clk-main",
	       of_at91rm9200_clk_main_setup);

static inline bool clk_sam9x5_main_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_MOSCSELS ? 1 : 0;
}

static int clk_sam9x5_main_prepare(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	struct regmap *regmap = clkmain->regmap;

	while (!clk_sam9x5_main_ready(regmap))
		cpu_relax();

	return clk_main_probe_frequency(regmap);
}

static int clk_sam9x5_main_is_prepared(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return clk_sam9x5_main_ready(clkmain->regmap);
}

static unsigned long clk_sam9x5_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return clk_main_recalc_rate(clkmain->regmap, parent_rate);
}

static int clk_sam9x5_main_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	struct regmap *regmap = clkmain->regmap;
	unsigned int tmp;

	if (index > 1)
		return -EINVAL;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	tmp &= ~MOR_KEY_MASK;

	if (index && !(tmp & AT91_PMC_MOSCSEL))
		regmap_write(regmap, AT91_CKGR_MOR, tmp | AT91_PMC_MOSCSEL);
	else if (!index && (tmp & AT91_PMC_MOSCSEL))
		regmap_write(regmap, AT91_CKGR_MOR, tmp & ~AT91_PMC_MOSCSEL);

	while (!clk_sam9x5_main_ready(regmap))
		cpu_relax();

	return 0;
}

static u8 clk_sam9x5_main_get_parent(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	unsigned int status;

	regmap_read(clkmain->regmap, AT91_CKGR_MOR, &status);

	return status & AT91_PMC_MOSCEN ? 1 : 0;
}

static const struct clk_ops sam9x5_main_ops = {
	.prepare = clk_sam9x5_main_prepare,
	.is_prepared = clk_sam9x5_main_is_prepared,
	.recalc_rate = clk_sam9x5_main_recalc_rate,
	.set_parent = clk_sam9x5_main_set_parent,
	.get_parent = clk_sam9x5_main_get_parent,
};

static struct clk * __init
at91_clk_register_sam9x5_main(struct regmap *regmap,
			      const char *name,
			      const char **parent_names,
			      int num_parents)
{
	struct clk_sam9x5_main *clkmain;
	struct clk *clk = NULL;
	struct clk_init_data init;
	unsigned int status;

	if (!name)
		return ERR_PTR(-EINVAL);

	if (!parent_names || !num_parents)
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sam9x5_main_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_PARENT_GATE;

	clkmain->hw.init = &init;
	clkmain->regmap = regmap;
	regmap_read(clkmain->regmap, AT91_CKGR_MOR, &status);
	clkmain->parent = status & AT91_PMC_MOSCEN ? 1 : 0;

	clk = clk_register(NULL, &clkmain->hw);
	if (IS_ERR(clk))
		kfree(clkmain);

	return clk;
}

static void __init of_at91sam9x5_clk_main_setup(struct device_node *np)
{
	struct clk *clk;
	const char *parent_names[2];
	unsigned int num_parents;
	const char *name = np->name;
	struct regmap *regmap;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > 2)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);
	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	of_property_read_string(np, "clock-output-names", &name);

	clk = at91_clk_register_sam9x5_main(regmap, name, parent_names,
					    num_parents);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(at91sam9x5_clk_main, "atmel,at91sam9x5-clk-main",
	       of_at91sam9x5_clk_main_setup);
