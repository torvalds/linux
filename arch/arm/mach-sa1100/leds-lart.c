/*
 * linux/arch/arm/mach-sa1100/leds-lart.c
 *
 * (C) Erik Mouw (J.A.K.Mouw@its.tudelft.nl), April 21, 2000
 *
 * LART uses the LED as follows:
 *   - GPIO23 is the LED, on if system is not idle
 *  You can use both CONFIG_LEDS_CPU and CONFIG_LEDS_TIMER at the same
 *  time, but in that case the timer events will still dictate the
 *  pace of the LED.
 */
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

#include "leds.h"


#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

#define LED_23    GPIO_GPIO23
#define LED_MASK  (LED_23)

void lart_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch(evt) {
	case led_start:
		/* pin 23 is output pin */
		GPDR |= LED_23;
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
			hw_led_state ^= LED_23;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		/* The LART people like the LED to be off when the
                   system is idle... */
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~LED_23;
		break;

	case led_idle_end:
		/* ... and on if the system is not idle */
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= LED_23;
		break;
#endif

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~LED_23;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= LED_23;
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
