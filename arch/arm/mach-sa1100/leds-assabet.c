/*
 * linux/arch/arm/mach-sa1100/leds-assabet.c
 *
 * Copyright (C) 2000 John Dorsey <john+@cs.cmu.edu>
 *
 * Original (leds-footbridge.c) by Russell King
 *
 * Assabet uses the LEDs as follows:
 *   - Green - toggles state every 50 timer interrupts
 *   - Red   - on if system is not idle
 */
#include <linux/config.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>
#include <asm/arch/assabet.h>

#include "leds.h"


#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

#define ASSABET_BCR_LED_MASK	(ASSABET_BCR_LED_GREEN | ASSABET_BCR_LED_RED)

void assabet_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = ASSABET_BCR_LED_RED | ASSABET_BCR_LED_GREEN;
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		hw_led_state = ASSABET_BCR_LED_RED | ASSABET_BCR_LED_GREEN;
		ASSABET_BCR_frob(ASSABET_BCR_LED_MASK, hw_led_state);
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = ASSABET_BCR_LED_RED | ASSABET_BCR_LED_GREEN;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = ASSABET_BCR_LED_RED | ASSABET_BCR_LED_GREEN;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= ASSABET_BCR_LED_GREEN;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= ASSABET_BCR_LED_RED;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~ASSABET_BCR_LED_RED;
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~ASSABET_BCR_LED_GREEN;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= ASSABET_BCR_LED_GREEN;
		break;

	case led_amber_on:
		break;

	case led_amber_off:
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~ASSABET_BCR_LED_RED;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= ASSABET_BCR_LED_RED;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED)
		ASSABET_BCR_frob(ASSABET_BCR_LED_MASK, hw_led_state);

	local_irq_restore(flags);
}
