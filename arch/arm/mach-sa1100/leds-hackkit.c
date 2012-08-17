/*
 * linux/arch/arm/mach-sa1100/leds-hackkit.c
 *
 * based on leds-lart.c
 *
 * (C) Erik Mouw (J.A.K.Mouw@its.tudelft.nl), April 21, 2000
 * (C) Stefan Eletzhofer <stefan.eletzhofer@eletztrick.de>, 2002
 *
 * The HackKit has two leds (GPIO 22/23). The red led (gpio 22) is used
 * as cpu led, the green one is used as timer led.
 */
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/leds.h>

#include "leds.h"


#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

#define LED_GREEN    GPIO_GPIO23
#define LED_RED    GPIO_GPIO22
#define LED_MASK  (LED_RED | LED_GREEN)

void hackkit_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch(evt) {
		case led_start:
			/* pin 22/23 are outputs */
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
				hw_led_state ^= LED_GREEN;
			break;
#endif

#ifdef CONFIG_LEDS_CPU
		case led_idle_start:
			/* The LART people like the LED to be off when the
			   system is idle... */
			if (!(led_state & LED_STATE_CLAIMED))
				hw_led_state &= ~LED_RED;
			break;

		case led_idle_end:
			/* ... and on if the system is not idle */
			if (!(led_state & LED_STATE_CLAIMED))
				hw_led_state |= LED_RED;
			break;
#endif

		case led_red_on:
			if (led_state & LED_STATE_CLAIMED)
				hw_led_state &= ~LED_RED;
			break;

		case led_red_off:
			if (led_state & LED_STATE_CLAIMED)
				hw_led_state |= LED_RED;
			break;

		case led_green_on:
			if (led_state & LED_STATE_CLAIMED)
				hw_led_state &= ~LED_GREEN;
			break;

		case led_green_off:
			if (led_state & LED_STATE_CLAIMED)
				hw_led_state |= LED_GREEN;
			break;

		default:
			break;
	}

	/* Now set the GPIO state, or nothing will happen at all */
	if (led_state & LED_STATE_ENABLED) {
		GPSR = hw_led_state;
		GPCR = hw_led_state ^ LED_MASK;
	}

	local_irq_restore(flags);
}
