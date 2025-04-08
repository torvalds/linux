// SPDX-License-Identifier: GPL-2.0-only
/*
 * UP board LED driver.
 *
 * Copyright (c) AAEON. All rights reserved.
 * Copyright (C) 2024 Bootlin
 *
 * Author: Gary Wang <garywang@aaeon.com.tw>
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/device.h>
#include <linux/container_of.h>
#include <linux/leds.h>
#include <linux/mfd/upboard-fpga.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define led_cdev_to_led_upboard(c)	container_of(c, struct upboard_led, cdev)

struct upboard_led {
	struct regmap_field *field;
	struct led_classdev cdev;
};

struct upboard_led_profile {
	const char *name;
	unsigned int bit;
};

static struct upboard_led_profile upboard_up_led_profile[] = {
	{ "upboard:yellow:" LED_FUNCTION_STATUS, 0 },
	{ "upboard:green:" LED_FUNCTION_STATUS, 1 },
	{ "upboard:red:" LED_FUNCTION_STATUS, 2 },
};

static struct upboard_led_profile upboard_up2_led_profile[] = {
	{ "upboard:blue:" LED_FUNCTION_STATUS, 0 },
	{ "upboard:yellow:" LED_FUNCTION_STATUS, 1 },
	{ "upboard:green:" LED_FUNCTION_STATUS, 2 },
	{ "upboard:red:" LED_FUNCTION_STATUS, 3 },
};

static enum led_brightness upboard_led_brightness_get(struct led_classdev *cdev)
{
	struct upboard_led *led = led_cdev_to_led_upboard(cdev);
	int brightness, ret;

	ret = regmap_field_read(led->field, &brightness);

	return ret ? LED_OFF : brightness;
};

static int upboard_led_brightness_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct upboard_led *led = led_cdev_to_led_upboard(cdev);

	return regmap_field_write(led->field, brightness != LED_OFF);
};

static int upboard_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct upboard_fpga *fpga = dev_get_drvdata(dev->parent);
	struct upboard_led_profile *led_profile;
	struct upboard_led *led;
	int led_instances, ret, i;

	switch (fpga->fpga_data->type) {
	case UPBOARD_UP_FPGA:
		led_profile = upboard_up_led_profile;
		led_instances = ARRAY_SIZE(upboard_up_led_profile);
		break;
	case UPBOARD_UP2_FPGA:
		led_profile = upboard_up2_led_profile;
		led_instances = ARRAY_SIZE(upboard_up2_led_profile);
		break;
	default:
		return dev_err_probe(dev, -EINVAL, "Unknown device type %d\n",
				     fpga->fpga_data->type);
	}

	for (i = 0; i < led_instances; i++) {
		const struct reg_field fldconf = {
			.reg = UPBOARD_REG_FUNC_EN0,
			.lsb = led_profile[i].bit,
			.msb = led_profile[i].bit,
		};

		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->field = devm_regmap_field_alloc(&pdev->dev, fpga->regmap, fldconf);
		if (IS_ERR(led->field))
			return PTR_ERR(led->field);

		led->cdev.brightness_get = upboard_led_brightness_get;
		led->cdev.brightness_set_blocking = upboard_led_brightness_set;
		led->cdev.max_brightness = LED_ON;

		led->cdev.name = led_profile[i].name;

		ret = devm_led_classdev_register(dev, &led->cdev);
		if (ret)
			return ret;
	}

	return 0;
}

static struct platform_driver upboard_led_driver = {
	.driver = {
		.name = "upboard-leds",
	},
	.probe = upboard_led_probe,
};

module_platform_driver(upboard_led_driver);

MODULE_AUTHOR("Gary Wang <garywang@aaeon.com.tw>");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_DESCRIPTION("UP Board LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:upboard-led");
