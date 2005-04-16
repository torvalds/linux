/*
 *  linux/arch/arm/mach-footbridge/ebsa285-leds.c
 *
 *  Copyright (C) 1998-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * EBSA-285 control routines.
 *
 * The EBSA-285 uses the leds as follows:
 *  - Green - toggles state every 50 timer interrupts
 *  - Amber - On if system is not idle
 *  - Red   - currently unused
 *
 * Changelog:
 *   02-05-1999	RMK	Various cleanups
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/system.h>

#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2
static char led_state;
static char hw_led_state;

static DEFINE_SPINLOCK(leds_lock);

static void ebsa285_leds_event(led_event_t evt)
{
	unsigned long flags;

	spin_lock_irqsave(&leds_lock, flags);

	switch (evt) {
	case led_start:
		hw_led_state = XBUS_LED_RED | XBUS_LED_GREEN;
#ifndef CONFIG_LEDS_CPU
		hw_led_state |= XBUS_LED_AMBER;
#endif
		led_state |= LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = XBUS_LED_RED | XBUS_LED_GREEN | XBUS_LED_AMBER;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = XBUS_LED_RED | XBUS_LED_GREEN | XBUS_LED_AMBER;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= XBUS_LED_GREEN;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= XBUS_LED_AMBER;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~XBUS_LED_AMBER;
		break;
#endif

	case led_halted:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~XBUS_LED_RED;
		break;

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~XBUS_LED_GREEN;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= XBUS_LED_GREEN;
		break;

	case led_amber_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~XBUS_LED_AMBER;
		break;

	case led_amber_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= XBUS_LED_AMBER;
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~XBUS_LED_RED;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= XBUS_LED_RED;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED)
		*XBUS_LEDS = hw_led_state;

	spin_unlock_irqrestore(&leds_lock, flags);
}

static int __init leds_init(void)
{
	if (machine_is_ebsa285() || machine_is_co285())
		leds_event = ebsa285_leds_event;

	leds_event(led_start);

	return 0;
}

__initcall(leds_init);
