/*
 * linux/arch/arm/mach-imx/leds-mx1ads.c
 *
 * Copyright (c) 2004 Sascha Hauer <sascha@saschahauer.de>
 *
 * Original (leds-footbridge.c) by Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include "leds.h"

/*
 * The MX1ADS Board has only one usable LED,
 * so select only the timer led or the
 * cpu usage led
 */
void
mx1ads_leds_event(led_event_t ledevt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (ledevt) {
#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		DR(0) &= ~(1<<2);
		break;

	case led_idle_end:
		DR(0) |= 1<<2;
		break;
#endif

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		DR(0) ^= 1<<2;
#endif
	default:
		break;
	}
	local_irq_restore(flags);
}
