// SPDX-License-Identifier: GPL-2.0-only
/*
 * leds-regulator.c - LED class driver for regulator driven LEDs.
 *
 * Copyright (C) 2009 Antonio Ospite <ospite@studenti.unina.it>
 *
 * Inspired by leds-wm8350 driver.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/leds-regulator.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define to_regulator_led(led_cdev) \
	container_of(led_cdev, struct regulator_led, cdev)

struct regulator_led {
	struct led_classdev cdev;
	int enabled;
	struct mutex mutex;

	struct regulator *vcc;
};

static inline int led_regulator_get_max_brightness(struct regulator *supply)
{
	int ret;
	int voltage = regulator_list_voltage(supply, 0);

	if (voltage <= 0)
		return 1;

	/* even if regulator can't change voltages,
	 * we still assume it can change status
	 * and the LED can be turned on and off.
	 */
	ret = regulator_set_voltage(supply, voltage, voltage);
	if (ret < 0)
		return 1;

	return regulator_count_voltages(supply);
}

static int led_regulator_get_voltage(struct regulator *supply,
		enum led_brightness brightness)
{
	if (brightness == 0)
		return -EINVAL;

	return regulator_list_voltage(supply, brightness - 1);
}


static void regulator_led_enable(struct regulator_led *led)
{
	int ret;

	if (led->enabled)
		return;

	ret = regulator_enable(led->vcc);
	if (ret != 0) {
		dev_err(led->cdev.dev, "Failed to enable vcc: %d\n", ret);
		return;
	}

	led->enabled = 1;
}

static void regulator_led_disable(struct regulator_led *led)
{
	int ret;

	if (!led->enabled)
		return;

	ret = regulator_disable(led->vcc);
	if (ret != 0) {
		dev_err(led->cdev.dev, "Failed to disable vcc: %d\n", ret);
		return;
	}

	led->enabled = 0;
}

static int regulator_led_brightness_set(struct led_classdev *led_cdev,
					 enum led_brightness value)
{
	struct regulator_led *led = to_regulator_led(led_cdev);
	int voltage;
	int ret = 0;

	mutex_lock(&led->mutex);

	if (value == LED_OFF) {
		regulator_led_disable(led);
		goto out;
	}

	if (led->cdev.max_brightness > 1) {
		voltage = led_regulator_get_voltage(led->vcc, value);
		dev_dbg(led->cdev.dev, "brightness: %d voltage: %d\n",
				value, voltage);

		ret = regulator_set_voltage(led->vcc, voltage, voltage);
		if (ret != 0)
			dev_err(led->cdev.dev, "Failed to set voltage %d: %d\n",
				voltage, ret);
	}

	regulator_led_enable(led);

out:
	mutex_unlock(&led->mutex);
	return ret;
}

static int regulator_led_probe(struct platform_device *pdev)
{
	struct led_regulator_platform_data *pdata =
			dev_get_platdata(&pdev->dev);
	struct regulator_led *led;
	struct regulator *vcc;
	int ret = 0;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}

	vcc = devm_regulator_get_exclusive(&pdev->dev, "vled");
	if (IS_ERR(vcc)) {
		dev_err(&pdev->dev, "Cannot get vcc for %s\n", pdata->name);
		return PTR_ERR(vcc);
	}

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	led->cdev.max_brightness = led_regulator_get_max_brightness(vcc);
	if (pdata->brightness > led->cdev.max_brightness) {
		dev_err(&pdev->dev, "Invalid default brightness %d\n",
				pdata->brightness);
		return -EINVAL;
	}

	led->cdev.brightness_set_blocking = regulator_led_brightness_set;
	led->cdev.name = pdata->name;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led->vcc = vcc;

	/* to handle correctly an already enabled regulator */
	if (regulator_is_enabled(led->vcc))
		led->enabled = 1;

	mutex_init(&led->mutex);

	platform_set_drvdata(pdev, led);

	ret = led_classdev_register(&pdev->dev, &led->cdev);
	if (ret < 0)
		return ret;

	/* to expose the default value to userspace */
	led->cdev.brightness = pdata->brightness;

	/* Set the default led status */
	regulator_led_brightness_set(&led->cdev, led->cdev.brightness);

	return 0;
}

static int regulator_led_remove(struct platform_device *pdev)
{
	struct regulator_led *led = platform_get_drvdata(pdev);

	led_classdev_unregister(&led->cdev);
	regulator_led_disable(led);
	return 0;
}

static struct platform_driver regulator_led_driver = {
	.driver = {
		   .name  = "leds-regulator",
		   },
	.probe  = regulator_led_probe,
	.remove = regulator_led_remove,
};

module_platform_driver(regulator_led_driver);

MODULE_AUTHOR("Antonio Ospite <ospite@studenti.unina.it>");
MODULE_DESCRIPTION("Regulator driven LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-regulator");
