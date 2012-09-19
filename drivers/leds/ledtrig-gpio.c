/*
 * ledtrig-gio.c - LED Trigger Based on GPIO events
 *
 * Copyright 2009 Felipe Balbi <me@felipebalbi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include "leds.h"

struct gpio_trig_data {
	struct led_classdev *led;
	struct work_struct work;

	unsigned desired_brightness;	/* desired brightness when led is on */
	unsigned inverted;		/* true when gpio is inverted */
	unsigned gpio;			/* gpio that triggers the leds */
};

static irqreturn_t gpio_trig_irq(int irq, void *_led)
{
	struct led_classdev *led = _led;
	struct gpio_trig_data *gpio_data = led->trigger_data;

	/* just schedule_work since gpio_get_value can sleep */
	schedule_work(&gpio_data->work);

	return IRQ_HANDLED;
};

static void gpio_trig_work(struct work_struct *work)
{
	struct gpio_trig_data *gpio_data = container_of(work,
			struct gpio_trig_data, work);
	int tmp;

	if (!gpio_data->gpio)
		return;

	tmp = gpio_get_value(gpio_data->gpio);
	if (gpio_data->inverted)
		tmp = !tmp;

	if (tmp) {
		if (gpio_data->desired_brightness)
			__led_set_brightness(gpio_data->led,
					   gpio_data->desired_brightness);
		else
			__led_set_brightness(gpio_data->led, LED_FULL);
	} else {
		__led_set_brightness(gpio_data->led, LED_OFF);
	}
}

static ssize_t gpio_trig_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct gpio_trig_data *gpio_data = led->trigger_data;

	return sprintf(buf, "%u\n", gpio_data->desired_brightness);
}

static ssize_t gpio_trig_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct gpio_trig_data *gpio_data = led->trigger_data;
	unsigned desired_brightness;
	int ret;

	ret = sscanf(buf, "%u", &desired_brightness);
	if (ret < 1 || desired_brightness > 255) {
		dev_err(dev, "invalid value\n");
		return -EINVAL;
	}

	gpio_data->desired_brightness = desired_brightness;

	return n;
}
static DEVICE_ATTR(desired_brightness, 0644, gpio_trig_brightness_show,
		gpio_trig_brightness_store);

static ssize_t gpio_trig_inverted_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct gpio_trig_data *gpio_data = led->trigger_data;

	return sprintf(buf, "%u\n", gpio_data->inverted);
}

static ssize_t gpio_trig_inverted_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct gpio_trig_data *gpio_data = led->trigger_data;
	unsigned long inverted;
	int ret;

	ret = strict_strtoul(buf, 10, &inverted);
	if (ret < 0)
		return ret;

	if (inverted > 1)
		return -EINVAL;

	gpio_data->inverted = inverted;

	/* After inverting, we need to update the LED. */
	schedule_work(&gpio_data->work);

	return n;
}
static DEVICE_ATTR(inverted, 0644, gpio_trig_inverted_show,
		gpio_trig_inverted_store);

static ssize_t gpio_trig_gpio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct gpio_trig_data *gpio_data = led->trigger_data;

	return sprintf(buf, "%u\n", gpio_data->gpio);
}

static ssize_t gpio_trig_gpio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct gpio_trig_data *gpio_data = led->trigger_data;
	unsigned gpio;
	int ret;

	ret = sscanf(buf, "%u", &gpio);
	if (ret < 1) {
		dev_err(dev, "couldn't read gpio number\n");
		flush_work(&gpio_data->work);
		return -EINVAL;
	}

	if (gpio_data->gpio == gpio)
		return n;

	if (!gpio) {
		if (gpio_data->gpio != 0)
			free_irq(gpio_to_irq(gpio_data->gpio), led);
		gpio_data->gpio = 0;
		return n;
	}

	ret = request_irq(gpio_to_irq(gpio), gpio_trig_irq,
			IRQF_SHARED | IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING, "ledtrig-gpio", led);
	if (ret) {
		dev_err(dev, "request_irq failed with error %d\n", ret);
	} else {
		if (gpio_data->gpio != 0)
			free_irq(gpio_to_irq(gpio_data->gpio), led);
		gpio_data->gpio = gpio;
	}

	return ret ? ret : n;
}
static DEVICE_ATTR(gpio, 0644, gpio_trig_gpio_show, gpio_trig_gpio_store);

static void gpio_trig_activate(struct led_classdev *led)
{
	struct gpio_trig_data *gpio_data;
	int ret;

	gpio_data = kzalloc(sizeof(*gpio_data), GFP_KERNEL);
	if (!gpio_data)
		return;

	ret = device_create_file(led->dev, &dev_attr_gpio);
	if (ret)
		goto err_gpio;

	ret = device_create_file(led->dev, &dev_attr_inverted);
	if (ret)
		goto err_inverted;

	ret = device_create_file(led->dev, &dev_attr_desired_brightness);
	if (ret)
		goto err_brightness;

	gpio_data->led = led;
	led->trigger_data = gpio_data;
	INIT_WORK(&gpio_data->work, gpio_trig_work);
	led->activated = true;

	return;

err_brightness:
	device_remove_file(led->dev, &dev_attr_inverted);

err_inverted:
	device_remove_file(led->dev, &dev_attr_gpio);

err_gpio:
	kfree(gpio_data);
}

static void gpio_trig_deactivate(struct led_classdev *led)
{
	struct gpio_trig_data *gpio_data = led->trigger_data;

	if (led->activated) {
		device_remove_file(led->dev, &dev_attr_gpio);
		device_remove_file(led->dev, &dev_attr_inverted);
		device_remove_file(led->dev, &dev_attr_desired_brightness);
		flush_work(&gpio_data->work);
		if (gpio_data->gpio != 0)
			free_irq(gpio_to_irq(gpio_data->gpio), led);
		kfree(gpio_data);
		led->activated = false;
	}
}

static struct led_trigger gpio_led_trigger = {
	.name		= "gpio",
	.activate	= gpio_trig_activate,
	.deactivate	= gpio_trig_deactivate,
};

static int __init gpio_trig_init(void)
{
	return led_trigger_register(&gpio_led_trigger);
}
module_init(gpio_trig_init);

static void __exit gpio_trig_exit(void)
{
	led_trigger_unregister(&gpio_led_trigger);
}
module_exit(gpio_trig_exit);

MODULE_AUTHOR("Felipe Balbi <me@felipebalbi.com>");
MODULE_DESCRIPTION("GPIO LED trigger");
MODULE_LICENSE("GPL");
