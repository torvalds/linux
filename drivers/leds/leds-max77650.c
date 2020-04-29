// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 BayLibre SAS
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
//
// LED driver for MAXIM 77650/77651 charger/power-supply.

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/mfd/max77650.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MAX77650_LED_NUM_LEDS		3

#define MAX77650_LED_A_BASE		0x40
#define MAX77650_LED_B_BASE		0x43

#define MAX77650_LED_BR_MASK		GENMASK(4, 0)
#define MAX77650_LED_EN_MASK		GENMASK(7, 6)

#define MAX77650_LED_MAX_BRIGHTNESS	MAX77650_LED_BR_MASK

/* Enable EN_LED_MSTR. */
#define MAX77650_LED_TOP_DEFAULT	BIT(0)

#define MAX77650_LED_ENABLE		GENMASK(7, 6)
#define MAX77650_LED_DISABLE		0x00

#define MAX77650_LED_A_DEFAULT		MAX77650_LED_DISABLE
/* 100% on duty */
#define MAX77650_LED_B_DEFAULT		GENMASK(3, 0)

struct max77650_led {
	struct led_classdev cdev;
	struct regmap *map;
	unsigned int regA;
	unsigned int regB;
};

static struct max77650_led *max77650_to_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct max77650_led, cdev);
}

static int max77650_led_brightness_set(struct led_classdev *cdev,
				       enum led_brightness brightness)
{
	struct max77650_led *led = max77650_to_led(cdev);
	int val, mask;

	mask = MAX77650_LED_BR_MASK | MAX77650_LED_EN_MASK;

	if (brightness == LED_OFF)
		val = MAX77650_LED_DISABLE;
	else
		val = MAX77650_LED_ENABLE | brightness;

	return regmap_update_bits(led->map, led->regA, mask, val);
}

static int max77650_led_probe(struct platform_device *pdev)
{
	struct fwnode_handle *child;
	struct max77650_led *leds, *led;
	struct device *dev;
	struct regmap *map;
	const char *label;
	int rv, num_leds;
	u32 reg;

	dev = &pdev->dev;

	leds = devm_kcalloc(dev, sizeof(*leds),
			    MAX77650_LED_NUM_LEDS, GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	map = dev_get_regmap(dev->parent, NULL);
	if (!map)
		return -ENODEV;

	num_leds = device_get_child_node_count(dev);
	if (!num_leds || num_leds > MAX77650_LED_NUM_LEDS)
		return -ENODEV;

	device_for_each_child_node(dev, child) {
		rv = fwnode_property_read_u32(child, "reg", &reg);
		if (rv || reg >= MAX77650_LED_NUM_LEDS) {
			rv = -EINVAL;
			goto err_node_put;
		}

		led = &leds[reg];
		led->map = map;
		led->regA = MAX77650_LED_A_BASE + reg;
		led->regB = MAX77650_LED_B_BASE + reg;
		led->cdev.brightness_set_blocking = max77650_led_brightness_set;
		led->cdev.max_brightness = MAX77650_LED_MAX_BRIGHTNESS;

		rv = fwnode_property_read_string(child, "label", &label);
		if (rv) {
			led->cdev.name = "max77650::";
		} else {
			led->cdev.name = devm_kasprintf(dev, GFP_KERNEL,
							"max77650:%s", label);
			if (!led->cdev.name) {
				rv = -ENOMEM;
				goto err_node_put;
			}
		}

		fwnode_property_read_string(child, "linux,default-trigger",
					    &led->cdev.default_trigger);

		rv = devm_led_classdev_register(dev, &led->cdev);
		if (rv)
			goto err_node_put;

		rv = regmap_write(map, led->regA, MAX77650_LED_A_DEFAULT);
		if (rv)
			goto err_node_put;

		rv = regmap_write(map, led->regB, MAX77650_LED_B_DEFAULT);
		if (rv)
			goto err_node_put;
	}

	return regmap_write(map,
			    MAX77650_REG_CNFG_LED_TOP,
			    MAX77650_LED_TOP_DEFAULT);
err_node_put:
	fwnode_handle_put(child);
	return rv;
}

static const struct of_device_id max77650_led_of_match[] = {
	{ .compatible = "maxim,max77650-led" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77650_led_of_match);

static struct platform_driver max77650_led_driver = {
	.driver = {
		.name = "max77650-led",
		.of_match_table = max77650_led_of_match,
	},
	.probe = max77650_led_probe,
};
module_platform_driver(max77650_led_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 LED driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:max77650-led");
