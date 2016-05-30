/*
 * LED MTD trigger
 *
 * Copyright 2016 Ezequiel Garcia <ezequiel@vanguardiasur.com.ar>
 *
 * Based on LED IDE-Disk Activity Trigger
 *
 * Copyright 2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>

#define BLINK_DELAY 30

DEFINE_LED_TRIGGER(ledtrig_mtd);
DEFINE_LED_TRIGGER(ledtrig_nand);

void ledtrig_mtd_activity(void)
{
	unsigned long blink_delay = BLINK_DELAY;

	led_trigger_blink_oneshot(ledtrig_mtd,
				  &blink_delay, &blink_delay, 0);
	led_trigger_blink_oneshot(ledtrig_nand,
				  &blink_delay, &blink_delay, 0);
}
EXPORT_SYMBOL(ledtrig_mtd_activity);

static int __init ledtrig_mtd_init(void)
{
	led_trigger_register_simple("mtd", &ledtrig_mtd);
	led_trigger_register_simple("nand-disk", &ledtrig_nand);

	return 0;
}
device_initcall(ledtrig_mtd_init);
