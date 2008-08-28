/*
 * GPIO switch definitions
 *
 * Copyright (C) 2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_OMAP_GPIO_SWITCH_H
#define __ASM_ARCH_OMAP_GPIO_SWITCH_H

#include <linux/types.h>

/* Cover:
 *	high -> closed
 *	low  -> open
 * Connection:
 *	high -> connected
 *	low  -> disconnected
 * Activity:
 *	high -> active
 *	low  -> inactive
 *
 */
#define OMAP_GPIO_SWITCH_TYPE_COVER		0x0000
#define OMAP_GPIO_SWITCH_TYPE_CONNECTION	0x0001
#define OMAP_GPIO_SWITCH_TYPE_ACTIVITY		0x0002
#define OMAP_GPIO_SWITCH_FLAG_INVERTED		0x0001
#define OMAP_GPIO_SWITCH_FLAG_OUTPUT		0x0002

struct omap_gpio_switch {
	const char *name;
	s16 gpio;
	unsigned flags:4;
	unsigned type:4;

	/* Time in ms to debounce when transitioning from
	 * inactive state to active state. */
	u16 debounce_rising;
	/* Same for transition from active to inactive state. */
	u16 debounce_falling;

	/* notify board-specific code about state changes */
	void (* notify)(void *data, int state);
	void *notify_data;
};

/* Call at init time only */
extern void omap_register_gpio_switches(const struct omap_gpio_switch *tbl,
					int count);

#endif
