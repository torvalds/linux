/*
 * leds-max8997.c - LED class driver for MAX8997 LEDs.
 *
 * Copyright (C) 2011 Samsung Electronics
 * Donggeun Kim <dg77.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/platform_device.h>

#define MAX8997_LED_FLASH_SHIFT			3
#define MAX8997_LED_FLASH_CUR_MASK		0xf8
#define MAX8997_LED_MOVIE_SHIFT			4
#define MAX8997_LED_MOVIE_CUR_MASK		0xf0

#define MAX8997_LED_FLASH_MAX_BRIGHTNESS	0x1f
#define MAX8997_LED_MOVIE_MAX_BRIGHTNESS	0xf
#define MAX8997_LED_NONE_MAX_BRIGHTNESS		0

#define MAX8997_LED0_FLASH_MASK			0x1
#define MAX8997_LED0_FLASH_PIN_MASK		0x5
#define MAX8997_LED0_MOVIE_MASK			0x8
#define MAX8997_LED0_MOVIE_PIN_MASK		0x28

#define MAX8997_LED1_FLASH_MASK			0x2
#define MAX8997_LED1_FLASH_PIN_MASK		0x6
#define MAX8997_LED1_MOVIE_MASK			0x10
#define MAX8997_LED1_MOVIE_PIN_MASK		0x30

#define MAX8997_LED_BOOST_ENABLE_MASK		(1 << 6)

struct max8997_led {
	struct max8997_dev *iodev;
	struct led_classdev cdev;
	bool enabled;
	int id;
	enum max8997_led_mode led_mode;
	struct mutex mutex;
};

static void max8997_led_set_mode(struct max8997_led *led,
			enum max8997_led_mode mode)
{
	int ret;
	struct i2c_client *client = led->iodev->i2c;
	u8 mask = 0, val;

	switch (mode) {
	case MAX8997_FLASH_MODE:
		mask = MAX8997_LED1_FLASH_MASK | MAX8997_LED0_FLASH_MASK;
		val = led->id ?
		      MAX8997_LED1_FLASH_MASK : MAX8997_LED0_FLASH_MASK;
		led->cdev.max_brightness = MAX8997_LED_FLASH_MAX_BRIGHTNESS;
		break;
	case MAX8997_MOVIE_MODE:
		mask = MAX8997_LED1_MOVIE_MASK | MAX8997_LED0_MOVIE_MASK;
		val = led->id ?
		      MAX8997_LED1_MOVIE_MASK : MAX8997_LED0_MOVIE_MASK;
		led->cdev.max_brightness = MAX8997_LED_MOVIE_MAX_BRIGHTNESS;
		break;
	case MAX8997_FLASH_PIN_CONTROL_MODE:
		mask = MAX8997_LED1_FLASH_PIN_MASK |
		       MAX8997_LED0_FLASH_PIN_MASK;
		val = led->id ?
		      MAX8997_LED1_FLASH_PIN_MASK : MAX8997_LED0_FLASH_PIN_MASK;
		led->cdev.max_brightness = MAX8997_LED_FLASH_MAX_BRIGHTNESS;
		break;
	case MAX8997_MOVIE_PIN_CONTROL_MODE:
		mask = MAX8997_LED1_MOVIE_PIN_MASK |
		       MAX8997_LED0_MOVIE_PIN_MASK;
		val = led->id ?
		      MAX8997_LED1_MOVIE_PIN_MASK : MAX8997_LED0_MOVIE_PIN_MASK;
		led->cdev.max_brightness = MAX8997_LED_MOVIE_MAX_BRIGHTNESS;
		break;
	default:
		led->cdev.max_brightness = MAX8997_LED_NONE_MAX_BRIGHTNESS;
		break;
	}

	if (mask) {
		ret = max8997_update_reg(client, MAX8997_REG_LEN_CNTL, val,
					 mask);
		if (ret)
			dev_err(led->iodev->dev,
				"failed to update register(%d)\n", ret);
	}

	led->led_mode = mode;
}

static void max8997_led_enable(struct max8997_led *led, bool enable)
{
	int ret;
	struct i2c_client *client = led->iodev->i2c;
	u8 val = 0, mask = MAX8997_LED_BOOST_ENABLE_MASK;

	if (led->enabled == enable)
		return;

	val = enable ? MAX8997_LED_BOOST_ENABLE_MASK : 0;

	ret = max8997_update_reg(client, MAX8997_REG_BOOST_CNTL, val, mask);
	if (ret)
		dev_err(led->iodev->dev,
			"failed to update register(%d)\n", ret);

	led->enabled = enable;
}

static void max8997_led_set_current(struct max8997_led *led,
				enum led_brightness value)
{
	int ret;
	struct i2c_client *client = led->iodev->i2c;
	u8 val = 0, mask = 0, reg = 0;

	switch (led->led_mode) {
	case MAX8997_FLASH_MODE:
	case MAX8997_FLASH_PIN_CONTROL_MODE:
		val = value << MAX8997_LED_FLASH_SHIFT;
		mask = MAX8997_LED_FLASH_CUR_MASK;
		reg = led->id ? MAX8997_REG_FLASH2_CUR : MAX8997_REG_FLASH1_CUR;
		break;
	case MAX8997_MOVIE_MODE:
	case MAX8997_MOVIE_PIN_CONTROL_MODE:
		val = value << MAX8997_LED_MOVIE_SHIFT;
		mask = MAX8997_LED_MOVIE_CUR_MASK;
		reg = MAX8997_REG_MOVIE_CUR;
		break;
	default:
		break;
	}

	if (mask) {
		ret = max8997_update_reg(client, reg, val, mask);
		if (ret)
			dev_err(led->iodev->dev,
				"failed to update register(%d)\n", ret);
	}
}

static void max8997_led_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct max8997_led *led =
			container_of(led_cdev, struct max8997_led, cdev);

	if (value) {
		max8997_led_set_current(led, value);
		max8997_led_enable(led, true);
	} else {
		max8997_led_set_current(led, value);
		max8997_led_enable(led, false);
	}
}

static ssize_t max8997_led_show_mode(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct max8997_led *led =
			container_of(led_cdev, struct max8997_led, cdev);
	ssize_t ret = 0;

	mutex_lock(&led->mutex);

	switch (led->led_mode) {
	case MAX8997_FLASH_MODE:
		ret += sprintf(buf, "FLASH\n");
		break;
	case MAX8997_MOVIE_MODE:
		ret += sprintf(buf, "MOVIE\n");
		break;
	case MAX8997_FLASH_PIN_CONTROL_MODE:
		ret += sprintf(buf, "FLASH_PIN_CONTROL\n");
		break;
	case MAX8997_MOVIE_PIN_CONTROL_MODE:
		ret += sprintf(buf, "MOVIE_PIN_CONTROL\n");
		break;
	default:
		ret += sprintf(buf, "NONE\n");
		break;
	}

	mutex_unlock(&led->mutex);

	return ret;
}

static ssize_t max8997_led_store_mode(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct max8997_led *led =
			container_of(led_cdev, struct max8997_led, cdev);
	enum max8997_led_mode mode;

	mutex_lock(&led->mutex);

	if (!strncmp(buf, "FLASH_PIN_CONTROL", 17))
		mode = MAX8997_FLASH_PIN_CONTROL_MODE;
	else if (!strncmp(buf, "MOVIE_PIN_CONTROL", 17))
		mode = MAX8997_MOVIE_PIN_CONTROL_MODE;
	else if (!strncmp(buf, "FLASH", 5))
		mode = MAX8997_FLASH_MODE;
	else if (!strncmp(buf, "MOVIE", 5))
		mode = MAX8997_MOVIE_MODE;
	else
		mode = MAX8997_NONE;

	max8997_led_set_mode(led, mode);

	mutex_unlock(&led->mutex);

	return size;
}

static DEVICE_ATTR(mode, 0644, max8997_led_show_mode, max8997_led_store_mode);

static struct attribute *max8997_attrs[] = {
	&dev_attr_mode.attr,
	NULL
};
ATTRIBUTE_GROUPS(max8997);

static int max8997_led_probe(struct platform_device *pdev)
{
	struct max8997_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8997_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct max8997_led *led;
	char name[20];
	int ret = 0;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	led->id = pdev->id;
	snprintf(name, sizeof(name), "max8997-led%d", pdev->id);

	led->cdev.name = name;
	led->cdev.brightness_set = max8997_led_brightness_set;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led->cdev.brightness = 0;
	led->cdev.groups = max8997_groups;
	led->iodev = iodev;

	/* initialize mode and brightness according to platform_data */
	if (pdata->led_pdata) {
		u8 mode = 0, brightness = 0;

		mode = pdata->led_pdata->mode[led->id];
		brightness = pdata->led_pdata->brightness[led->id];

		max8997_led_set_mode(led, mode);

		if (brightness > led->cdev.max_brightness)
			brightness = led->cdev.max_brightness;
		max8997_led_set_current(led, brightness);
		led->cdev.brightness = brightness;
	} else {
		max8997_led_set_mode(led, MAX8997_NONE);
		max8997_led_set_current(led, 0);
	}

	mutex_init(&led->mutex);

	ret = devm_led_classdev_register(&pdev->dev, &led->cdev);
	if (ret < 0)
		return ret;

	return 0;
}

static struct platform_driver max8997_led_driver = {
	.driver = {
		.name  = "max8997-led",
	},
	.probe  = max8997_led_probe,
};

module_platform_driver(max8997_led_driver);

MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_DESCRIPTION("MAX8997 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:max8997-led");
