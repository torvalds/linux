/*
 * linux/arch/arm/mach-pxa/leds-trizeps4.c
 *
 *  Author:	Jürgen Schindele
 *  Created:	20 02, 2006
 *  Copyright:	Jürgen Schindele
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/types.h>
#include <asm/leds.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/trizeps4.h>

#include "leds.h"

#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

#define SYS_BUSY		0x01
#define HEARTBEAT		0x02
#define BLINK			0x04

static unsigned int led_state;
static unsigned int hw_led_state;

void trizeps4_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = 0;
		pxa_gpio_mode( GPIO_SYS_BUSY_LED  | GPIO_OUT);		/* LED1 */
		pxa_gpio_mode( GPIO_HEARTBEAT_LED | GPIO_OUT);		/* LED2 */
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
		hw_led_state ^= HEARTBEAT;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		hw_led_state &= ~SYS_BUSY;
		break;

	case led_idle_end:
		hw_led_state |= SYS_BUSY;
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		hw_led_state |= BLINK;
		break;

	case led_green_off:
		hw_led_state &= ~BLINK;
		break;

	case led_amber_on:
		break;

	case led_amber_off:
		break;

	case led_red_on:
		break;

	case led_red_off:
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED) {
		switch (hw_led_state) {
			case 0:
				GPSR(GPIO_SYS_BUSY_LED)  |= GPIO_bit(GPIO_SYS_BUSY_LED);
				GPSR(GPIO_HEARTBEAT_LED) |= GPIO_bit(GPIO_HEARTBEAT_LED);
				break;
			case 1:
				GPCR(GPIO_SYS_BUSY_LED)  |= GPIO_bit(GPIO_SYS_BUSY_LED);
				GPSR(GPIO_HEARTBEAT_LED) |= GPIO_bit(GPIO_HEARTBEAT_LED);
				break;
			case 2:
				GPSR(GPIO_SYS_BUSY_LED)  |= GPIO_bit(GPIO_SYS_BUSY_LED);
				GPCR(GPIO_HEARTBEAT_LED) |= GPIO_bit(GPIO_HEARTBEAT_LED);
				break;
			case 3:
				GPCR(GPIO_SYS_BUSY_LED)  |= GPIO_bit(GPIO_SYS_BUSY_LED);
				GPCR(GPIO_HEARTBEAT_LED) |= GPIO_bit(GPIO_HEARTBEAT_LED);
				break;
		}
	}
	else {
		/* turn all off */
		GPSR(GPIO_SYS_BUSY_LED)  |= GPIO_bit(GPIO_SYS_BUSY_LED);
		GPSR(GPIO_HEARTBEAT_LED) |= GPIO_bit(GPIO_HEARTBEAT_LED);
	}

	local_irq_restore(flags);
}
