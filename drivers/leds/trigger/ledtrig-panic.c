// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel Panic LED Trigger
 *
 * Copyright 2016 Ezequiel Garcia <ezequiel@vanguardiasur.com.ar>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/leds.h>
#include "../leds.h"

static struct led_trigger *trigger;

/*
 * This is called in a special context by the atomic panic
 * notifier. This means the trigger can be changed without
 * worrying about locking.
 */
static void led_trigger_set_panic(struct led_classdev *led_cdev)
{
	if (led_cdev->trigger)
		list_del(&led_cdev->trig_list);
	list_add_tail(&led_cdev->trig_list, &trigger->led_cdevs);

	/* Avoid the delayed blink path */
	led_cdev->blink_delay_on = 0;
	led_cdev->blink_delay_off = 0;

	led_cdev->trigger = trigger;
}

static int led_trigger_panic_notifier(struct notifier_block *nb,
				      unsigned long code, void *unused)
{
	struct led_classdev *led_cdev;

	list_for_each_entry(led_cdev, &leds_list, node)
		if (led_cdev->flags & LED_PANIC_INDICATOR)
			led_trigger_set_panic(led_cdev);
	return NOTIFY_DONE;
}

static struct notifier_block led_trigger_panic_nb = {
	.notifier_call = led_trigger_panic_notifier,
};

static long led_panic_blink(int state)
{
	led_trigger_event(trigger, state ? LED_FULL : LED_OFF);
	return 0;
}

static int __init ledtrig_panic_init(void)
{
	led_trigger_register_simple("panic", &trigger);
	if (!trigger)
		return -ENOMEM;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &led_trigger_panic_nb);

	panic_blink = led_panic_blink;
	return 0;
}
device_initcall(ledtrig_panic_init);
