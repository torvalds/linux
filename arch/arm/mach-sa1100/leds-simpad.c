/*
 * linux/arch/arm/mach-sa1100/leds-simpad.c
 *
 * Author: Juergen Messerer <juergen.messerer@siemens.ch>
 */
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>
#include <mach/simpad.h>

#include "leds.h"


#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

static unsigned int led_state;
static unsigned int hw_led_state;

#define	LED_GREEN	(1)
#define	LED_MASK	(1)

extern void set_cs3_bit(int value);
extern void clear_cs3_bit(int value);     

void simpad_leds_event(led_event_t evt)
{
	switch (evt)
	{
	case led_start:
	        hw_led_state = LED_GREEN;
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = LED_GREEN;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = LED_GREEN;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= LED_GREEN;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		break;

	case led_idle_end:
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= LED_GREEN;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~LED_GREEN;
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

	if  (led_state & LED_STATE_ENABLED)
		set_cs3_bit(LED2_ON);
	else 
	        clear_cs3_bit(LED2_ON);
}

