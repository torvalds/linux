/*
 * LED Heartbeat Trigger
 *
 * Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * Based on Richard Purdie's ledtrig-timer.c and some arch's
 * CONFIG_HEARTBEAT code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/leds.h>
#include <linux/reboot.h>
#include "../leds.h"

static int panic_heartbeats;

struct heartbeat_trig_data {
	unsigned int phase;
	unsigned int period;
	struct timer_list timer;
	unsigned int invert;
};

static void led_heartbeat_function(unsigned long data)
{
	struct led_classdev *led_cdev = (struct led_classdev *) data;
	struct heartbeat_trig_data *heartbeat_data = led_cdev->trigger_data;
	unsigned long brightness = LED_OFF;
	unsigned long delay = 0;

	if (unlikely(panic_heartbeats)) {
		led_set_brightness(led_cdev, LED_OFF);
		return;
	}

	/* acts like an actual heart beat -- ie thump-thump-pause... */
	switch (heartbeat_data->phase) {
	case 0:
		/*
		 * The hyperbolic function below modifies the
		 * heartbeat period length in dependency of the
		 * current (1min) load. It goes through the points
		 * f(0)=1260, f(1)=860, f(5)=510, f(inf)->300.
		 */
		heartbeat_data->period = 300 +
			(6720 << FSHIFT) / (5 * avenrun[0] + (7 << FSHIFT));
		heartbeat_data->period =
			msecs_to_jiffies(heartbeat_data->period);
		delay = msecs_to_jiffies(70);
		heartbeat_data->phase++;
		if (!heartbeat_data->invert)
			brightness = led_cdev->max_brightness;
		break;
	case 1:
		delay = heartbeat_data->period / 4 - msecs_to_jiffies(70);
		heartbeat_data->phase++;
		if (heartbeat_data->invert)
			brightness = led_cdev->max_brightness;
		break;
	case 2:
		delay = msecs_to_jiffies(70);
		heartbeat_data->phase++;
		if (!heartbeat_data->invert)
			brightness = led_cdev->max_brightness;
		break;
	default:
		delay = heartbeat_data->period - heartbeat_data->period / 4 -
			msecs_to_jiffies(70);
		heartbeat_data->phase = 0;
		if (heartbeat_data->invert)
			brightness = led_cdev->max_brightness;
		break;
	}

	led_set_brightness_async(led_cdev, brightness);
	mod_timer(&heartbeat_data->timer, jiffies + delay);
}

static ssize_t led_invert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct heartbeat_trig_data *heartbeat_data = led_cdev->trigger_data;

	return sprintf(buf, "%u\n", heartbeat_data->invert);
}

static ssize_t led_invert_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct heartbeat_trig_data *heartbeat_data = led_cdev->trigger_data;
	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);
	if (ret)
		return ret;

	heartbeat_data->invert = !!state;

	return size;
}

static DEVICE_ATTR(invert, 0644, led_invert_show, led_invert_store);

static void heartbeat_trig_activate(struct led_classdev *led_cdev)
{
	struct heartbeat_trig_data *heartbeat_data;
	int rc;

	heartbeat_data = kzalloc(sizeof(*heartbeat_data), GFP_KERNEL);
	if (!heartbeat_data)
		return;

	led_cdev->trigger_data = heartbeat_data;
	rc = device_create_file(led_cdev->dev, &dev_attr_invert);
	if (rc) {
		kfree(led_cdev->trigger_data);
		return;
	}

	setup_timer(&heartbeat_data->timer,
		    led_heartbeat_function, (unsigned long) led_cdev);
	heartbeat_data->phase = 0;
	led_heartbeat_function(heartbeat_data->timer.data);
	led_cdev->activated = true;
}

static void heartbeat_trig_deactivate(struct led_classdev *led_cdev)
{
	struct heartbeat_trig_data *heartbeat_data = led_cdev->trigger_data;

	if (led_cdev->activated) {
		del_timer_sync(&heartbeat_data->timer);
		device_remove_file(led_cdev->dev, &dev_attr_invert);
		kfree(heartbeat_data);
		led_cdev->activated = false;
	}
}

static struct led_trigger heartbeat_led_trigger = {
	.name     = "heartbeat",
	.activate = heartbeat_trig_activate,
	.deactivate = heartbeat_trig_deactivate,
};

static int heartbeat_reboot_notifier(struct notifier_block *nb,
				     unsigned long code, void *unused)
{
	led_trigger_unregister(&heartbeat_led_trigger);
	return NOTIFY_DONE;
}

static int heartbeat_panic_notifier(struct notifier_block *nb,
				     unsigned long code, void *unused)
{
	panic_heartbeats = 1;
	return NOTIFY_DONE;
}

static struct notifier_block heartbeat_reboot_nb = {
	.notifier_call = heartbeat_reboot_notifier,
};

static struct notifier_block heartbeat_panic_nb = {
	.notifier_call = heartbeat_panic_notifier,
};

static int __init heartbeat_trig_init(void)
{
	int rc = led_trigger_register(&heartbeat_led_trigger);

	if (!rc) {
		atomic_notifier_chain_register(&panic_notifier_list,
					       &heartbeat_panic_nb);
		register_reboot_notifier(&heartbeat_reboot_nb);
	}
	return rc;
}

static void __exit heartbeat_trig_exit(void)
{
	unregister_reboot_notifier(&heartbeat_reboot_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &heartbeat_panic_nb);
	led_trigger_unregister(&heartbeat_led_trigger);
}

module_init(heartbeat_trig_init);
module_exit(heartbeat_trig_exit);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("Heartbeat LED trigger");
MODULE_LICENSE("GPL");
