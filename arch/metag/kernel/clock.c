/*
 * arch/metag/kernel/clock.c
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/param.h>
#include <asm/clock.h>

struct meta_clock_desc _meta_clock;

/* Default machine get_core_freq callback. */
static unsigned long get_core_freq_default(void)
{
#ifdef CONFIG_METAG_META21
	/*
	 * Meta 2 cores divide down the core clock for the Meta timers, so we
	 * can estimate the core clock from the divider.
	 */
	return (metag_in32(EXPAND_TIMER_DIV) + 1) * 1000000;
#else
	/*
	 * On Meta 1 we don't know the core clock, but assuming the Meta timer
	 * is correct it can be estimated based on loops_per_jiffy.
	 */
	return (loops_per_jiffy * HZ * 5) >> 1;
#endif
}

static struct clk *clk_core;

/* Clk based get_core_freq callback. */
static unsigned long get_core_freq_clk(void)
{
	return clk_get_rate(clk_core);
}

/**
 * init_metag_core_clock() - Set up core clock from devicetree.
 *
 * Checks to see if a "core" clock is provided in the device tree, and overrides
 * the get_core_freq callback to use it.
 */
static void __init init_metag_core_clock(void)
{
	/*
	 * See if a core clock is provided by the devicetree (and
	 * registered by the init callback above).
	 */
	struct device_node *node;
	node = of_find_compatible_node(NULL, NULL, "img,meta");
	if (!node) {
		pr_warn("%s: no compatible img,meta DT node found\n",
			__func__);
		return;
	}

	clk_core = of_clk_get_by_name(node, "core");
	if (IS_ERR(clk_core)) {
		pr_warn("%s: no core clock found in DT\n",
			__func__);
		return;
	}

	/*
	 * Override the core frequency callback to use
	 * this clk.
	 */
	_meta_clock.get_core_freq = get_core_freq_clk;
}

/**
 * init_metag_clocks() - Set up clocks from devicetree.
 *
 * Set up important clocks from device tree. In particular any needed for clock
 * sources.
 */
void __init init_metag_clocks(void)
{
	init_metag_core_clock();

	pr_info("Core clock frequency: %lu Hz\n", get_coreclock());
}

/**
 * setup_meta_clocks() - Early set up of the Meta clock.
 * @desc:	Clock descriptor usually provided by machine description
 *
 * Ensures all callbacks are valid.
 */
void __init setup_meta_clocks(struct meta_clock_desc *desc)
{
	/* copy callbacks */
	if (desc)
		_meta_clock = *desc;

	/* set fallback functions */
	if (!_meta_clock.get_core_freq)
		_meta_clock.get_core_freq = get_core_freq_default;
}

