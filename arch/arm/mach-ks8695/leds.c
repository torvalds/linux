/*
 * LED driver for KS8695-based boards.
 *
 * Copyright (C) Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/mach-types.h>
#include <asm/leds.h>
#include <asm/arch/devices.h>
#include <asm/arch/gpio.h>


static inline void ks8695_led_on(unsigned int led)
{
	gpio_set_value(led, 0);
}

static inline void ks8695_led_off(unsigned int led)
{
	gpio_set_value(led, 1);
}

static inline void ks8695_led_toggle(unsigned int led)
{
	unsigned long is_off = gpio_get_value(led);
	if (is_off)
		ks8695_led_on(led);
	else
		ks8695_led_off(led);
}


/*
 * Handle LED events.
 */
static void ks8695_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch(evt) {
	case led_start:		/* System startup */
		ks8695_led_on(ks8695_leds_cpu);
		break;

	case led_stop:		/* System stop / suspend */
		ks8695_led_off(ks8695_leds_cpu);
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:		/* Every 50 timer ticks */
		ks8695_led_toggle(ks8695_leds_timer);
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:	/* Entering idle state */
		ks8695_led_off(ks8695_leds_cpu);
		break;

	case led_idle_end:	/* Exit idle state */
		ks8695_led_on(ks8695_leds_cpu);
		break;
#endif

	default:
		break;
	}

	local_irq_restore(flags);
}


static int __init leds_init(void)
{
	if ((ks8695_leds_timer == -1) || (ks8695_leds_cpu == -1))
		return -ENODEV;

	leds_event = ks8695_leds_event;

	leds_event(led_start);
	return 0;
}

__initcall(leds_init);
