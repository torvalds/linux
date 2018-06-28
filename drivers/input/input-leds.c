/*
 * LED support for the input layer
 *
 * Copyright 2010-2015 Samuel Thibault <samuel.thibault@ens-lyon.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/input.h>

#if IS_ENABLED(CONFIG_VT)
#define VT_TRIGGER(_name)	.trigger = _name
#else
#define VT_TRIGGER(_name)	.trigger = NULL
#endif

static const struct {
	const char *name;
	const char *trigger;
} input_led_info[LED_CNT] = {
	[LED_NUML]	= { "numlock", VT_TRIGGER("kbd-numlock") },
	[LED_CAPSL]	= { "capslock", VT_TRIGGER("kbd-capslock") },
	[LED_SCROLLL]	= { "scrolllock", VT_TRIGGER("kbd-scrolllock") },
	[LED_COMPOSE]	= { "compose" },
	[LED_KANA]	= { "kana", VT_TRIGGER("kbd-kanalock") },
	[LED_SLEEP]	= { "sleep" } ,
	[LED_SUSPEND]	= { "suspend" },
	[LED_MUTE]	= { "mute" },
	[LED_MISC]	= { "misc" },
	[LED_MAIL]	= { "mail" },
	[LED_CHARGING]	= { "charging" },
};

struct input_led {
	struct led_classdev cdev;
	struct input_handle *handle;
	unsigned int code; /* One of LED_* constants */
};

struct input_leds {
	struct input_handle handle;
	unsigned int num_leds;
	struct input_led leds[];
};

static enum led_brightness input_leds_brightness_get(struct led_classdev *cdev)
{
	struct input_led *led = container_of(cdev, struct input_led, cdev);
	struct input_dev *input = led->handle->dev;

	return test_bit(led->code, input->led) ? cdev->max_brightness : 0;
}

static void input_leds_brightness_set(struct led_classdev *cdev,
				      enum led_brightness brightness)
{
	struct input_led *led = container_of(cdev, struct input_led, cdev);

	input_inject_event(led->handle, EV_LED, led->code, !!brightness);
}

static void input_leds_event(struct input_handle *handle, unsigned int type,
			     unsigned int code, int value)
{
}

static int input_leds_get_count(struct input_dev *dev)
{
	unsigned int led_code;
	int count = 0;

	for_each_set_bit(led_code, dev->ledbit, LED_CNT)
		if (input_led_info[led_code].name)
			count++;

	return count;
}

static int input_leds_connect(struct input_handler *handler,
			      struct input_dev *dev,
			      const struct input_device_id *id)
{
	struct input_leds *leds;
	struct input_led *led;
	unsigned int num_leds;
	unsigned int led_code;
	int led_no;
	int error;

	num_leds = input_leds_get_count(dev);
	if (!num_leds)
		return -ENXIO;

	leds = kzalloc(struct_size(leds, leds, num_leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->num_leds = num_leds;

	leds->handle.dev = dev;
	leds->handle.handler = handler;
	leds->handle.name = "leds";
	leds->handle.private = leds;

	error = input_register_handle(&leds->handle);
	if (error)
		goto err_free_mem;

	error = input_open_device(&leds->handle);
	if (error)
		goto err_unregister_handle;

	led_no = 0;
	for_each_set_bit(led_code, dev->ledbit, LED_CNT) {
		if (!input_led_info[led_code].name)
			continue;

		led = &leds->leds[led_no];
		led->handle = &leds->handle;
		led->code = led_code;

		led->cdev.name = kasprintf(GFP_KERNEL, "%s::%s",
					   dev_name(&dev->dev),
					   input_led_info[led_code].name);
		if (!led->cdev.name) {
			error = -ENOMEM;
			goto err_unregister_leds;
		}

		led->cdev.max_brightness = 1;
		led->cdev.brightness_get = input_leds_brightness_get;
		led->cdev.brightness_set = input_leds_brightness_set;
		led->cdev.default_trigger = input_led_info[led_code].trigger;

		error = led_classdev_register(&dev->dev, &led->cdev);
		if (error) {
			dev_err(&dev->dev, "failed to register LED %s: %d\n",
				led->cdev.name, error);
			kfree(led->cdev.name);
			goto err_unregister_leds;
		}

		led_no++;
	}

	return 0;

err_unregister_leds:
	while (--led_no >= 0) {
		struct input_led *led = &leds->leds[led_no];

		led_classdev_unregister(&led->cdev);
		kfree(led->cdev.name);
	}

	input_close_device(&leds->handle);

err_unregister_handle:
	input_unregister_handle(&leds->handle);

err_free_mem:
	kfree(leds);
	return error;
}

static void input_leds_disconnect(struct input_handle *handle)
{
	struct input_leds *leds = handle->private;
	int i;

	for (i = 0; i < leds->num_leds; i++) {
		struct input_led *led = &leds->leds[i];

		led_classdev_unregister(&led->cdev);
		kfree(led->cdev.name);
	}

	input_close_device(handle);
	input_unregister_handle(handle);

	kfree(leds);
}

static const struct input_device_id input_leds_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_LED) },
	},
	{ },
};
MODULE_DEVICE_TABLE(input, input_leds_ids);

static struct input_handler input_leds_handler = {
	.event =	input_leds_event,
	.connect =	input_leds_connect,
	.disconnect =	input_leds_disconnect,
	.name =		"leds",
	.id_table =	input_leds_ids,
};

static int __init input_leds_init(void)
{
	return input_register_handler(&input_leds_handler);
}
module_init(input_leds_init);

static void __exit input_leds_exit(void)
{
	input_unregister_handler(&input_leds_handler);
}
module_exit(input_leds_exit);

MODULE_AUTHOR("Samuel Thibault <samuel.thibault@ens-lyon.org>");
MODULE_AUTHOR("Dmitry Torokhov <dmitry.torokhov@gmail.com>");
MODULE_DESCRIPTION("Input -> LEDs Bridge");
MODULE_LICENSE("GPL v2");
