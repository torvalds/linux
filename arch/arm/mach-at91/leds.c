/*
 * LED driver for Atmel AT91-based boards.
 *
 *  Copyright (C) SAN People (Pty) Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/mach-types.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>


/* ------------------------------------------------------------------------- */

#if defined(CONFIG_NEW_LEDS)

#include <linux/platform_device.h>

/*
 * New cross-platform LED support.
 */

static struct gpio_led_platform_data led_data;

static struct platform_device at91_leds = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &led_data,
};

void __init at91_gpio_leds(struct gpio_led *leds, int nr)
{
	int i;

	if (!nr)
		return;

	for (i = 0; i < nr; i++)
		at91_set_gpio_output(leds[i].gpio, leds[i].active_low);

	led_data.leds = leds;
	led_data.num_leds = nr;
	platform_device_register(&at91_leds);
}

#else
void __init at91_gpio_leds(struct gpio_led *leds, int nr) {}
#endif


/* ------------------------------------------------------------------------- */

#if defined(CONFIG_LEDS)

#include <asm/leds.h>

/*
 * Old ARM-specific LED framework; not fully functional when generic time is
 * in use.
 */

static u8 at91_leds_cpu;
static u8 at91_leds_timer;

static inline void at91_led_on(unsigned int led)
{
	at91_set_gpio_value(led, 0);
}

static inline void at91_led_off(unsigned int led)
{
	at91_set_gpio_value(led, 1);
}

static inline void at91_led_toggle(unsigned int led)
{
	unsigned long is_off = at91_get_gpio_value(led);
	if (is_off)
		at91_led_on(led);
	else
		at91_led_off(led);
}


/*
 * Handle LED events.
 */
static void at91_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch(evt) {
	case led_start:		/* System startup */
		at91_led_on(at91_leds_cpu);
		break;

	case led_stop:		/* System stop / suspend */
		at91_led_off(at91_leds_cpu);
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:		/* Every 50 timer ticks */
		at91_led_toggle(at91_leds_timer);
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:	/* Entering idle state */
		at91_led_off(at91_leds_cpu);
		break;

	case led_idle_end:	/* Exit idle state */
		at91_led_on(at91_leds_cpu);
		break;
#endif

	default:
		break;
	}

	local_irq_restore(flags);
}


static int __init leds_init(void)
{
	if (!at91_leds_timer || !at91_leds_cpu)
		return -ENODEV;

	leds_event = at91_leds_event;

	leds_event(led_start);
	return 0;
}

__initcall(leds_init);


void __init at91_init_leds(u8 cpu_led, u8 timer_led)
{
	/* Enable GPIO to access the LEDs */
	at91_set_gpio_output(cpu_led, 1);
	at91_set_gpio_output(timer_led, 1);

	at91_leds_cpu	= cpu_led;
	at91_leds_timer	= timer_led;
}

#else
void __init at91_init_leds(u8 cpu_led, u8 timer_led) {}
#endif
