/*
 * linux/arch/arm/mach-pxa/leds-mainstone.c
 *
 * Author:     Nicolas Pitre
 * Created:    Nov 05, 2002
 * Copyright:  MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/mainstone.h>

#include "leds.h"


/* 8 discrete leds available for general use: */
#define D28			(1 << 0)
#define D27			(1 << 1)
#define D26			(1 << 2)
#define D25			(1 << 3)
#define D24			(1 << 4)
#define D23			(1 << 5)
#define D22			(1 << 6)
#define D21			(1 << 7)

#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

void mainstone_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = 0;
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = 0;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = 0;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		hw_led_state ^= D26;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		hw_led_state &= ~D27;
		break;

	case led_idle_end:
		hw_led_state |= D27;
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		hw_led_state |= D21;;
		break;

	case led_green_off:
		hw_led_state &= ~D21;
		break;

	case led_amber_on:
		hw_led_state |= D22;;
		break;

	case led_amber_off:
		hw_led_state &= ~D22;
		break;

	case led_red_on:
		hw_led_state |= D23;;
		break;

	case led_red_off:
		hw_led_state &= ~D23;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED)
		MST_LEDCTRL = (MST_LEDCTRL | 0xff) & ~hw_led_state;
	else
		MST_LEDCTRL |= 0xff;

	local_irq_restore(flags);
}
