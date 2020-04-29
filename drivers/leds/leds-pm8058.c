// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010, 2011, 2016 The Linux Foundation. All rights reserved.
 */
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>

#define PM8058_LED_TYPE_COMMON	0x00
#define PM8058_LED_TYPE_KEYPAD	0x01
#define PM8058_LED_TYPE_FLASH	0x02

#define PM8058_LED_TYPE_COMMON_MASK	0xf8
#define PM8058_LED_TYPE_KEYPAD_MASK	0xf0
#define PM8058_LED_TYPE_COMMON_SHIFT	3
#define PM8058_LED_TYPE_KEYPAD_SHIFT	4

struct pm8058_led {
	struct regmap *map;
	u32 reg;
	u32 ledtype;
	struct led_classdev cdev;
};

static void pm8058_led_set(struct led_classdev *cled,
	enum led_brightness value)
{
	struct pm8058_led *led;
	int ret = 0;
	unsigned int mask = 0;
	unsigned int val = 0;

	led = container_of(cled, struct pm8058_led, cdev);
	switch (led->ledtype) {
	case PM8058_LED_TYPE_COMMON:
		mask = PM8058_LED_TYPE_COMMON_MASK;
		val = value << PM8058_LED_TYPE_COMMON_SHIFT;
		break;
	case PM8058_LED_TYPE_KEYPAD:
	case PM8058_LED_TYPE_FLASH:
		mask = PM8058_LED_TYPE_KEYPAD_MASK;
		val = value << PM8058_LED_TYPE_KEYPAD_SHIFT;
		break;
	default:
		break;
	}

	ret = regmap_update_bits(led->map, led->reg, mask, val);
	if (ret)
		pr_err("Failed to set LED brightness\n");
}

static enum led_brightness pm8058_led_get(struct led_classdev *cled)
{
	struct pm8058_led *led;
	int ret;
	unsigned int val;

	led = container_of(cled, struct pm8058_led, cdev);

	ret = regmap_read(led->map, led->reg, &val);
	if (ret) {
		pr_err("Failed to get LED brightness\n");
		return LED_OFF;
	}

	switch (led->ledtype) {
	case PM8058_LED_TYPE_COMMON:
		val &= PM8058_LED_TYPE_COMMON_MASK;
		val >>= PM8058_LED_TYPE_COMMON_SHIFT;
		break;
	case PM8058_LED_TYPE_KEYPAD:
	case PM8058_LED_TYPE_FLASH:
		val &= PM8058_LED_TYPE_KEYPAD_MASK;
		val >>= PM8058_LED_TYPE_KEYPAD_SHIFT;
		break;
	default:
		val = LED_OFF;
		break;
	}

	return val;
}

static int pm8058_led_probe(struct platform_device *pdev)
{
	struct pm8058_led *led;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	struct regmap *map;
	const char *state;
	enum led_brightness maxbright;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->ledtype = (u32)(unsigned long)of_device_get_match_data(&pdev->dev);

	map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!map) {
		dev_err(&pdev->dev, "Parent regmap unavailable.\n");
		return -ENXIO;
	}
	led->map = map;

	ret = of_property_read_u32(np, "reg", &led->reg);
	if (ret) {
		dev_err(&pdev->dev, "no register offset specified\n");
		return -EINVAL;
	}

	/* Use label else node name */
	led->cdev.name = of_get_property(np, "label", NULL) ? : np->name;
	led->cdev.default_trigger =
		of_get_property(np, "linux,default-trigger", NULL);
	led->cdev.brightness_set = pm8058_led_set;
	led->cdev.brightness_get = pm8058_led_get;
	if (led->ledtype == PM8058_LED_TYPE_COMMON)
		maxbright = 31; /* 5 bits */
	else
		maxbright = 15; /* 4 bits */
	led->cdev.max_brightness = maxbright;

	state = of_get_property(np, "default-state", NULL);
	if (state) {
		if (!strcmp(state, "keep")) {
			led->cdev.brightness = pm8058_led_get(&led->cdev);
		} else if (!strcmp(state, "on")) {
			led->cdev.brightness = maxbright;
			pm8058_led_set(&led->cdev, maxbright);
		} else {
			led->cdev.brightness = LED_OFF;
			pm8058_led_set(&led->cdev, LED_OFF);
		}
	}

	if (led->ledtype == PM8058_LED_TYPE_KEYPAD ||
	    led->ledtype == PM8058_LED_TYPE_FLASH)
		led->cdev.flags	= LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(&pdev->dev, &led->cdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register led \"%s\"\n",
			led->cdev.name);
		return ret;
	}

	return 0;
}

static const struct of_device_id pm8058_leds_id_table[] = {
	{
		.compatible = "qcom,pm8058-led",
		.data = (void *)PM8058_LED_TYPE_COMMON
	},
	{
		.compatible = "qcom,pm8058-keypad-led",
		.data = (void *)PM8058_LED_TYPE_KEYPAD
	},
	{
		.compatible = "qcom,pm8058-flash-led",
		.data = (void *)PM8058_LED_TYPE_FLASH
	},
	{ },
};
MODULE_DEVICE_TABLE(of, pm8058_leds_id_table);

static struct platform_driver pm8058_led_driver = {
	.probe		= pm8058_led_probe,
	.driver		= {
		.name	= "pm8058-leds",
		.of_match_table = pm8058_leds_id_table,
	},
};
module_platform_driver(pm8058_led_driver);

MODULE_DESCRIPTION("PM8058 LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pm8058-leds");
