/*
 * Kernel Panic LED Trigger
 *
 * Copyright 2016 Ezequiel Garcia <ezequiel@vanguardiasur.com.ar>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>

static struct led_trigger *trigger;

static long led_panic_blink(int state)
{
	led_trigger_event(trigger, state ? LED_FULL : LED_OFF);
	return 0;
}

static int __init ledtrig_panic_init(void)
{
	led_trigger_register_simple("panic", &trigger);
	panic_blink = led_panic_blink;
	return 0;
}
device_initcall(ledtrig_panic_init);
