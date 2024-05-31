// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input Events LED trigger
 *
 * Copyright (C) 2024 Hans de Goede <hansg@kernel.org>
 * Partially based on Atsushi Nemoto's ledtrig-heartbeat.c.
 */

#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include "../leds.h"

#define DEFAULT_LED_OFF_DELAY_MS			5000

struct input_events_data {
	struct input_handler handler;
	struct delayed_work work;
	spinlock_t lock;
	struct led_classdev *led_cdev;
	int led_cdev_saved_flags;
	/* To avoid repeatedly setting the brightness while there are events */
	bool led_on;
	unsigned long led_off_time;
	unsigned long led_off_delay;
};

static void led_input_events_work(struct work_struct *work)
{
	struct input_events_data *data =
		container_of(work, struct input_events_data, work.work);

	spin_lock_irq(&data->lock);

	/*
	 * This time_after_eq() check avoids a race where this work starts
	 * running before a new event pushed led_off_time back.
	 */
	if (time_after_eq(jiffies, data->led_off_time)) {
		led_set_brightness_nosleep(data->led_cdev, LED_OFF);
		data->led_on = false;
	}

	spin_unlock_irq(&data->lock);
}

static ssize_t delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct input_events_data *input_events_data = led_trigger_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", input_events_data->led_off_delay);
}

static ssize_t delay_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	struct input_events_data *input_events_data = led_trigger_get_drvdata(dev);
	unsigned long delay;
	int ret;

	ret = kstrtoul(buf, 0, &delay);
	if (ret)
		return ret;

	/* Clamp between 0.5 and 1000 seconds */
	delay = clamp_val(delay, 500UL, 1000000UL);
	input_events_data->led_off_delay = msecs_to_jiffies(delay);

	return size;
}

static DEVICE_ATTR_RW(delay);

static struct attribute *input_events_led_attrs[] = {
	&dev_attr_delay.attr,
	NULL
};
ATTRIBUTE_GROUPS(input_events_led);

static void input_events_event(struct input_handle *handle, unsigned int type,
			       unsigned int code, int val)
{
	struct input_events_data *data =
		container_of(handle->handler, struct input_events_data, handler);
	unsigned long led_off_delay = READ_ONCE(data->led_off_delay);
	struct led_classdev *led_cdev = data->led_cdev;
	unsigned long flags;

	if (test_and_clear_bit(LED_BLINK_BRIGHTNESS_CHANGE, &led_cdev->work_flags))
		led_cdev->blink_brightness = led_cdev->new_blink_brightness;

	spin_lock_irqsave(&data->lock, flags);

	if (!data->led_on) {
		led_set_brightness_nosleep(led_cdev, led_cdev->blink_brightness);
		data->led_on = true;
	}
	data->led_off_time = jiffies + led_off_delay;

	spin_unlock_irqrestore(&data->lock, flags);

	mod_delayed_work(system_wq, &data->work, led_off_delay);
}

static int input_events_connect(struct input_handler *handler, struct input_dev *dev,
				const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "input-events";

	ret = input_register_handle(handle);
	if (ret)
		goto err_free_handle;

	ret = input_open_device(handle);
	if (ret)
		goto err_unregister_handle;

	return 0;

err_unregister_handle:
	input_unregister_handle(handle);
err_free_handle:
	kfree(handle);
	return ret;
}

static void input_events_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id input_events_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_REL) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_ABS) },
	},
	{ }
};

static int input_events_activate(struct led_classdev *led_cdev)
{
	struct input_events_data *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->handler.name = "input-events";
	data->handler.event = input_events_event;
	data->handler.connect = input_events_connect;
	data->handler.disconnect = input_events_disconnect;
	data->handler.id_table = input_events_ids;

	INIT_DELAYED_WORK(&data->work, led_input_events_work);
	spin_lock_init(&data->lock);

	data->led_cdev = led_cdev;
	data->led_cdev_saved_flags = led_cdev->flags;
	data->led_off_delay = msecs_to_jiffies(DEFAULT_LED_OFF_DELAY_MS);

	/*
	 * Use led_cdev->blink_brightness + LED_BLINK_SW flag so that sysfs
	 * brightness writes will change led_cdev->new_blink_brightness for
	 * configuring the on state brightness (like ledtrig-heartbeat).
	 */
	if (!led_cdev->blink_brightness)
		led_cdev->blink_brightness = led_cdev->max_brightness;

	/* Start with LED off */
	led_set_brightness_nosleep(data->led_cdev, LED_OFF);

	ret = input_register_handler(&data->handler);
	if (ret) {
		kfree(data);
		return ret;
	}

	set_bit(LED_BLINK_SW, &led_cdev->work_flags);

	/* Turn LED off during suspend, original flags are restored on deactivate() */
	led_cdev->flags |= LED_CORE_SUSPENDRESUME;

	led_set_trigger_data(led_cdev, data);
	return 0;
}

static void input_events_deactivate(struct led_classdev *led_cdev)
{
	struct input_events_data *data = led_get_trigger_data(led_cdev);

	led_cdev->flags = data->led_cdev_saved_flags;
	clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
	input_unregister_handler(&data->handler);
	cancel_delayed_work_sync(&data->work);
	kfree(data);
}

static struct led_trigger input_events_led_trigger = {
	.name       = "input-events",
	.activate   = input_events_activate,
	.deactivate = input_events_deactivate,
	.groups     = input_events_led_groups,
};
module_led_trigger(input_events_led_trigger);

MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("Input Events LED trigger");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ledtrig:input-events");
