// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2009,2018 Daniel Mack <daniel@zonque.org>

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <uapi/linux/uleds.h>

struct lt3593_led_data {
	char name[LED_MAX_NAME_SIZE];
	struct led_classdev cdev;
	struct gpio_desc *gpiod;
};

static int lt3593_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct lt3593_led_data *led_dat =
		container_of(led_cdev, struct lt3593_led_data, cdev);
	int pulses;

	/*
	 * The LT3593 resets its internal current level register to the maximum
	 * level on the first falling edge on the control pin. Each following
	 * falling edge decreases the current level by 625uA. Up to 32 pulses
	 * can be sent, so the maximum power reduction is 20mA.
	 * After a timeout of 128us, the value is taken from the register and
	 * applied is to the output driver.
	 */

	if (value == 0) {
		gpiod_set_value_cansleep(led_dat->gpiod, 0);
		return 0;
	}

	pulses = 32 - (value * 32) / 255;

	if (pulses == 0) {
		gpiod_set_value_cansleep(led_dat->gpiod, 0);
		mdelay(1);
		gpiod_set_value_cansleep(led_dat->gpiod, 1);
		return 0;
	}

	gpiod_set_value_cansleep(led_dat->gpiod, 1);

	while (pulses--) {
		gpiod_set_value_cansleep(led_dat->gpiod, 0);
		udelay(1);
		gpiod_set_value_cansleep(led_dat->gpiod, 1);
		udelay(1);
	}

	return 0;
}

static struct lt3593_led_data *lt3593_led_probe_pdata(struct device *dev)
{
	struct gpio_led_platform_data *pdata = dev_get_platdata(dev);
	const struct gpio_led *template = &pdata->leds[0];
	struct lt3593_led_data *led_data;
	int ret, state;

	if (pdata->num_leds != 1)
		return ERR_PTR(-EINVAL);

	led_data = devm_kzalloc(dev, sizeof(*led_data), GFP_KERNEL);
	if (!led_data)
		return ERR_PTR(-ENOMEM);

	led_data->cdev.name = template->name;
	led_data->cdev.default_trigger = template->default_trigger;
	led_data->cdev.brightness_set_blocking = lt3593_led_set;

	state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);
	led_data->cdev.brightness = state ? LED_FULL : LED_OFF;

	if (!template->retain_state_suspended)
		led_data->cdev.flags |= LED_CORE_SUSPENDRESUME;

	ret = devm_gpio_request_one(dev, template->gpio, state ?
				    GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
				    template->name);
	if (ret < 0)
		return ERR_PTR(ret);

	led_data->gpiod = gpio_to_desc(template->gpio);
	if (!led_data->gpiod)
		return ERR_PTR(-EPROBE_DEFER);

	ret = devm_led_classdev_register(dev, &led_data->cdev);
	if (ret < 0)
		return ERR_PTR(ret);

	dev_info(dev, "registered LT3593 LED '%s' at GPIO %d\n",
		 template->name, template->gpio);

	return led_data;
}

static int lt3593_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lt3593_led_data *led_data;
	struct fwnode_handle *child;
	int ret, state = LEDS_GPIO_DEFSTATE_OFF;
	enum gpiod_flags flags = GPIOD_OUT_LOW;
	const char *tmp;

	if (dev_get_platdata(dev)) {
		led_data = lt3593_led_probe_pdata(dev);
		if (IS_ERR(led_data))
			return PTR_ERR(led_data);

		goto out;
	}

	if (!dev->of_node)
		return -ENODEV;

	led_data = devm_kzalloc(dev, sizeof(*led_data), GFP_KERNEL);
	if (!led_data)
		return -ENOMEM;

	if (device_get_child_node_count(dev) != 1) {
		dev_err(dev, "Device must have exactly one LED sub-node.");
		return -EINVAL;
	}

	led_data->gpiod = devm_gpiod_get(dev, "lltc,ctrl", 0);
	if (IS_ERR(led_data->gpiod))
		return PTR_ERR(led_data->gpiod);

	child = device_get_next_child_node(dev, NULL);

	ret = fwnode_property_read_string(child, "label", &tmp);
	if (ret < 0)
		snprintf(led_data->name, sizeof(led_data->name),
			 "lt3593::");
	else
		snprintf(led_data->name, sizeof(led_data->name),
			 "lt3593:%s", tmp);

	fwnode_property_read_string(child, "linux,default-trigger",
				    &led_data->cdev.default_trigger);

	if (!fwnode_property_read_string(child, "default-state", &tmp)) {
		if (!strcmp(tmp, "keep")) {
			state = LEDS_GPIO_DEFSTATE_KEEP;
			flags = GPIOD_ASIS;
		} else if (!strcmp(tmp, "on")) {
			state = LEDS_GPIO_DEFSTATE_ON;
			flags = GPIOD_OUT_HIGH;
		}
	}

	led_data->cdev.name = led_data->name;
	led_data->cdev.brightness_set_blocking = lt3593_led_set;
	led_data->cdev.brightness = state ? LED_FULL : LED_OFF;

	ret = devm_led_classdev_register(dev, &led_data->cdev);
	if (ret < 0) {
		fwnode_handle_put(child);
		return ret;
	}

	led_data->cdev.dev->of_node = dev->of_node;

out:
	platform_set_drvdata(pdev, led_data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id of_lt3593_leds_match[] = {
	{ .compatible = "lltc,lt3593", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lt3593_leds_match);
#endif

static struct platform_driver lt3593_led_driver = {
	.probe		= lt3593_led_probe,
	.driver		= {
		.name	= "leds-lt3593",
		.of_match_table = of_match_ptr(of_lt3593_leds_match),
	},
};

module_platform_driver(lt3593_led_driver);

MODULE_AUTHOR("Daniel Mack <daniel@zonque.org>");
MODULE_DESCRIPTION("LED driver for LT3593 controllers");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-lt3593");
