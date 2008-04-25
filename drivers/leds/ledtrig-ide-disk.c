/*
 * LED IDE-Disk Activity Trigger
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

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/leds.h>

static void ledtrig_ide_timerfunc(unsigned long data);

DEFINE_LED_TRIGGER(ledtrig_ide);
static DEFINE_TIMER(ledtrig_ide_timer, ledtrig_ide_timerfunc, 0, 0);
static int ide_activity;
static int ide_lastactivity;

void ledtrig_ide_activity(void)
{
	ide_activity++;
	if (!timer_pending(&ledtrig_ide_timer))
		mod_timer(&ledtrig_ide_timer, jiffies + msecs_to_jiffies(10));
}
EXPORT_SYMBOL(ledtrig_ide_activity);

static void ledtrig_ide_timerfunc(unsigned long data)
{
	if (ide_lastactivity != ide_activity) {
		ide_lastactivity = ide_activity;
		led_trigger_event(ledtrig_ide, LED_FULL);
		mod_timer(&ledtrig_ide_timer, jiffies + msecs_to_jiffies(10));
	} else {
		led_trigger_event(ledtrig_ide, LED_OFF);
	}
}

static int __init ledtrig_ide_init(void)
{
	led_trigger_register_simple("ide-disk", &ledtrig_ide);
	return 0;
}

static void __exit ledtrig_ide_exit(void)
{
	led_trigger_unregister_simple(ledtrig_ide);
}

module_init(ledtrig_ide_init);
module_exit(ledtrig_ide_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("LED IDE Disk Activity Trigger");
MODULE_LICENSE("GPL");
