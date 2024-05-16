// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for leds in MAX5970 and MAX5978 IC
 *
 * Copyright (c) 2022 9elements GmbH
 *
 * Author: Patrick Rudolph <patrick.rudolph@9elements.com>
 */

#include <linux/bits.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/mfd/max5970.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define ldev_to_maxled(c)       container_of(c, struct max5970_led, cdev)

struct max5970_led {
	struct device *dev;
	struct regmap *regmap;
	struct led_classdev cdev;
	unsigned int index;
};

static int max5970_led_set_brightness(struct led_classdev *cdev,
				      enum led_brightness brightness)
{
	struct max5970_led *ddata = ldev_to_maxled(cdev);
	int ret, val;

	/* Set/clear corresponding bit for given led index */
	val = !brightness ? BIT(ddata->index) : 0;

	ret = regmap_update_bits(ddata->regmap, MAX5970_REG_LED_FLASH, BIT(ddata->index), val);
	if (ret < 0)
		dev_err(cdev->dev, "failed to set brightness %d", ret);

	return ret;
}

static int max5970_led_probe(struct platform_device *pdev)
{
	struct fwnode_handle *led_node, *child;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct max5970_led *ddata;
	int ret = -ENODEV;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	led_node = device_get_named_child_node(dev->parent, "leds");
	if (!led_node)
		return -ENODEV;

	fwnode_for_each_available_child_node(led_node, child) {
		u32 reg;

		if (fwnode_property_read_u32(child, "reg", &reg))
			continue;

		if (reg >= MAX5970_NUM_LEDS) {
			dev_err_probe(dev, -EINVAL, "invalid LED (%u >= %d)\n", reg, MAX5970_NUM_LEDS);
			continue;
		}

		ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
		if (!ddata) {
			fwnode_handle_put(child);
			return -ENOMEM;
		}

		ddata->index = reg;
		ddata->regmap = regmap;
		ddata->dev = dev;

		if (fwnode_property_read_string(child, "label", &ddata->cdev.name))
			ddata->cdev.name = fwnode_get_name(child);

		ddata->cdev.max_brightness = 1;
		ddata->cdev.brightness_set_blocking = max5970_led_set_brightness;
		ddata->cdev.default_trigger = "none";

		ret = devm_led_classdev_register(dev, &ddata->cdev);
		if (ret < 0) {
			fwnode_handle_put(child);
			return dev_err_probe(dev, ret, "Failed to initialize LED %u\n", reg);
		}
	}

	return ret;
}

static struct platform_driver max5970_led_driver = {
	.driver = {
		.name = "max5970-led",
	},
	.probe = max5970_led_probe,
};
module_platform_driver(max5970_led_driver);

MODULE_AUTHOR("Patrick Rudolph <patrick.rudolph@9elements.com>");
MODULE_AUTHOR("Naresh Solanki <Naresh.Solanki@9elements.com>");
MODULE_DESCRIPTION("MAX5970_hot-swap controller LED driver");
MODULE_LICENSE("GPL");
