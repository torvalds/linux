/*
 * arch/metag/include/asm/clock.h
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _METAG_CLOCK_H_
#define _METAG_CLOCK_H_

#include <asm/mach/arch.h>

/**
 * struct meta_clock_desc - Meta Core clock callbacks.
 * @get_core_freq:	Get the frequency of the Meta core. If this is NULL, the
 *			core frequency will be determined like this:
 *			Meta 1: based on loops_per_jiffy.
 *			Meta 2: (EXPAND_TIMER_DIV + 1) MHz.
 *			If a "core" clock is provided by the device tree, it
 *			will override this function.
 */
struct meta_clock_desc {
	unsigned long		(*get_core_freq)(void);
};

extern struct meta_clock_desc _meta_clock;

/*
 * Perform platform clock initialisation, reading clocks from device tree etc.
 * Only accessible during boot.
 */
void init_metag_clocks(void);

/*
 * Set up the default clock, ensuring all callbacks are valid - only accessible
 * during boot.
 */
void setup_meta_clocks(struct meta_clock_desc *desc);

/**
 * get_coreclock() - Get the frequency of the Meta core clock.
 *
 * Returns:	The Meta core clock frequency in Hz.
 */
static inline unsigned long get_coreclock(void)
{
	/*
	 * Use the current clock callback. If set correctly this will provide
	 * the most accurate frequency as it can be calculated directly from the
	 * PLL configuration. otherwise a default callback will have been set
	 * instead.
	 */
	return _meta_clock.get_core_freq();
}

#endif /* _METAG_CLOCK_H_ */
