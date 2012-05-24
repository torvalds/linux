/*
 *  linux/arch/arm/mach-ebsa110/leds.c
 *
 *  Copyright (C) 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  EBSA-110 LED control routines.  We use the led as follows:
 *
 *   - Red - toggles state every 50 timer interrupts
 */
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

#include "core.h"

static spinlock_t leds_lock;

static void ebsa110_leds_event(led_event_t ledevt)
{
	unsigned long flags;

	spin_lock_irqsave(&leds_lock, flags);

	switch(ledevt) {
	case led_timer:
		*(volatile unsigned char *)SOFT_BASE ^= 128;
		break;

	default:
		break;
	}

	spin_unlock_irqrestore(&leds_lock, flags);
}

static int __init leds_init(void)
{
	if (machine_is_ebsa110())
		leds_event = ebsa110_leds_event;

	return 0;
}

__initcall(leds_init);
