/*
 * arch/metag/kernel/clock.c
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>

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

/**
 * setup_meta_clocks() - Set up the Meta clock.
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

