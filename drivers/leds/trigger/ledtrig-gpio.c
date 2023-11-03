// SPDX-License-Identifier: GPL-2.0-only
/*
 * ledtrig-gio.c - LED Trigger Based on GPIO events
 *
 * Copyright 2009 Felipe Balbi <me@felipebalbi.com>
 * Copyright 2023 Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include "../leds.h"

struct gpio_trig_data {
	struct led_classdev *led;
	unsigned desired_brightness;	/* desired brightness when led is on */
	struct gpio_desc *gpiod;	/* gpio that triggers the led */
};

static irqreturn_t gpio_trig_irq(int irq, void *_led)
{
	struct led_classdev *led = _led;
	struct gpio_trig_data *gpio_data = led_get_trigger_data(led);
	int tmp;

	tmp = gpiod_get_value_cansleep(gpio_data->gpiod);
	if (tmp) {
		if (gpio_data->desired_brightness)
			led_set_brightness_nosleep(gpio_data->led,
					   gpio_data->desired_brightness);
		else
			led_set_brightness_nosleep(gpio_data->led, LED_FULL);
	} else {
		led_set_brightness_nosleep(gpio_data->led, LED_OFF);
	}

	return IRQ_HANDLED;
}

static ssize_t gpio_trig_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gpio_trig_data *gpio_data = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n", gpio_data->desired_brightness);
}

static ssize_t gpio_trig_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct gpio_trig_data *gpio_data = led_trigger_get_drvdata(dev);
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

static struct attribute *gpio_trig_attrs[] = {
	&dev_attr_desired_brightness.attr,
	NULL
};
ATTRIBUTE_GROUPS(gpio_trig);

static int gpio_trig_activate(struct led_classdev *led)
{
	struct gpio_trig_data *gpio_data;
	struct device *dev = led->dev;
	int ret;

	gpio_data = kzalloc(sizeof(*gpio_data), GFP_KERNEL);
	if (!gpio_data)
		return -ENOMEM;

	/*
	 * The generic property "trigger-sources" is followed,
	 * and we hope that this is a GPIO.
	 */
	gpio_data->gpiod = fwnode_gpiod_get_index(dev->fwnode,
						  "trigger-sources",
						  0, GPIOD_IN,
						  "led-trigger");
	if (IS_ERR(gpio_data->gpiod)) {
		ret = PTR_ERR(gpio_data->gpiod);
		kfree(gpio_data);
		return ret;
	}
	if (!gpio_data->gpiod) {
		dev_err(dev, "no valid GPIO for the trigger\n");
		kfree(gpio_data);
		return -EINVAL;
	}

	gpio_data->led = led;
	led_set_trigger_data(led, gpio_data);

	ret = request_threaded_irq(gpiod_to_irq(gpio_data->gpiod), NULL, gpio_trig_irq,
			IRQF_ONESHOT | IRQF_SHARED | IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING, "ledtrig-gpio", led);
	if (ret) {
		dev_err(dev, "request_irq failed with error %d\n", ret);
		gpiod_put(gpio_data->gpiod);
		kfree(gpio_data);
		return ret;
	}

	/* Finally update the LED to initial status */
	gpio_trig_irq(0, led);

	return 0;
}

static void gpio_trig_deactivate(struct led_classdev *led)
{
	struct gpio_trig_data *gpio_data = led_get_trigger_data(led);

	free_irq(gpiod_to_irq(gpio_data->gpiod), led);
	gpiod_put(gpio_data->gpiod);
	kfree(gpio_data);
}

static struct led_trigger gpio_led_trigger = {
	.name		= "gpio",
	.activate	= gpio_trig_activate,
	.deactivate	= gpio_trig_deactivate,
	.groups		= gpio_trig_groups,
};
module_led_trigger(gpio_led_trigger);

MODULE_AUTHOR("Felipe Balbi <me@felipebalbi.com>");
MODULE_DESCRIPTION("GPIO LED trigger");
MODULE_LICENSE("GPL v2");
