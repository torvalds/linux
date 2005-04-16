/*
 * linux/arch/arm/mach-sa1100/leds-badge4.c
 *
 * Author: Christopher Hoover <ch@hpl.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

#include "leds.h"

#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

#define LED_RED		GPIO_GPIO(7)
#define LED_GREEN       GPIO_GPIO(9)
#define LED_MASK	(LED_RED|LED_GREEN)

#define LED_IDLE	LED_GREEN
#define LED_TIMER	LED_RED

void badge4_leds_event(led_event_t evt)
{
        unsigned long flags;

	local_irq_save(flags);

        switch (evt) {
        case led_start:
		GPDR |= LED_MASK;
                hw_led_state = LED_MASK;
                led_state = LED_STATE_ENABLED;
                break;

        case led_stop:
                led_state &= ~LED_STATE_ENABLED;
                break;

        case led_claim:
                led_state |= LED_STATE_CLAIMED;
                hw_led_state = LED_MASK;
                break;

        case led_release:
                led_state &= ~LED_STATE_CLAIMED;
                hw_led_state = LED_MASK;
                break;

#ifdef CONFIG_LEDS_TIMER
        case led_timer:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state ^= LED_TIMER;
                break;
#endif

#ifdef CONFIG_LEDS_CPU
        case led_idle_start:
		/* LED off when system is idle */
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_IDLE;
                break;

        case led_idle_end:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_IDLE;
                break;
#endif

        case led_red_on:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_RED;
                break;

        case led_red_off:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_RED;
                break;

        case led_green_on:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state &= ~LED_GREEN;
                break;

        case led_green_off:
                if (!(led_state & LED_STATE_CLAIMED))
                        hw_led_state |= LED_GREEN;
                break;

	default:
		break;
        }

        if  (led_state & LED_STATE_ENABLED) {
                GPSR = hw_led_state;
                GPCR = hw_led_state ^ LED_MASK;
        }

	local_irq_restore(flags);
}
