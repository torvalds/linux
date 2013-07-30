/*
 * LED driver for WM8350 driven LEDS.
 *
 * Copyright(C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/mfd/wm8350/pmic.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>

/* Microamps */
static const int isink_cur[] = {
	4,
	5,
	6,
	7,
	8,
	10,
	11,
	14,
	16,
	19,
	23,
	27,
	32,
	39,
	46,
	54,
	65,
	77,
	92,
	109,
	130,
	154,
	183,
	218,
	259,
	308,
	367,
	436,
	518,
	616,
	733,
	872,
	1037,
	1233,
	1466,
	1744,
	2073,
	2466,
	2933,
	3487,
	4147,
	4932,
	5865,
	6975,
	8294,
	9864,
	11730,
	13949,
	16589,
	19728,
	23460,
	27899,
	33178,
	39455,
	46920,
	55798,
	66355,
	78910,
	93840,
	111596,
	132710,
	157820,
	187681,
	223191
};

#define to_wm8350_led(led_cdev) \
	container_of(led_cdev, struct wm8350_led, cdev)

static void wm8350_led_enable(struct wm8350_led *led)
{
	int ret;

	if (led->enabled)
		return;

	ret = regulator_enable(led->isink);
	if (ret != 0) {
		dev_err(led->cdev.dev, "Failed to enable ISINK: %d\n", ret);
		return;
	}

	ret = regulator_enable(led->dcdc);
	if (ret != 0) {
		dev_err(led->cdev.dev, "Failed to enable DCDC: %d\n", ret);
		regulator_disable(led->isink);
		return;
	}

	led->enabled = 1;
}

static void wm8350_led_disable(struct wm8350_led *led)
{
	int ret;

	if (!led->enabled)
		return;

	ret = regulator_disable(led->dcdc);
	if (ret != 0) {
		dev_err(led->cdev.dev, "Failed to disable DCDC: %d\n", ret);
		return;
	}

	ret = regulator_disable(led->isink);
	if (ret != 0) {
		dev_err(led->cdev.dev, "Failed to disable ISINK: %d\n", ret);
		ret = regulator_enable(led->dcdc);
		if (ret != 0)
			dev_err(led->cdev.dev, "Failed to reenable DCDC: %d\n",
				ret);
		return;
	}

	led->enabled = 0;
}

static void led_work(struct work_struct *work)
{
	struct wm8350_led *led = container_of(work, struct wm8350_led, work);
	int ret;
	int uA;
	unsigned long flags;

	mutex_lock(&led->mutex);

	spin_lock_irqsave(&led->value_lock, flags);

	if (led->value == LED_OFF) {
		spin_unlock_irqrestore(&led->value_lock, flags);
		wm8350_led_disable(led);
		goto out;
	}

	/* This scales linearly into the index of valid current
	 * settings which results in a linear scaling of perceived
	 * brightness due to the non-linear current settings provided
	 * by the hardware.
	 */
	uA = (led->max_uA_index * led->value) / LED_FULL;
	spin_unlock_irqrestore(&led->value_lock, flags);
	BUG_ON(uA >= ARRAY_SIZE(isink_cur));

	ret = regulator_set_current_limit(led->isink, isink_cur[uA],
					  isink_cur[uA]);
	if (ret != 0)
		dev_err(led->cdev.dev, "Failed to set %duA: %d\n",
			isink_cur[uA], ret);

	wm8350_led_enable(led);

out:
	mutex_unlock(&led->mutex);
}

static void wm8350_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct wm8350_led *led = to_wm8350_led(led_cdev);
	unsigned long flags;

	spin_lock_irqsave(&led->value_lock, flags);
	led->value = value;
	schedule_work(&led->work);
	spin_unlock_irqrestore(&led->value_lock, flags);
}

static void wm8350_led_shutdown(struct platform_device *pdev)
{
	struct wm8350_led *led = platform_get_drvdata(pdev);

	mutex_lock(&led->mutex);
	led->value = LED_OFF;
	wm8350_led_disable(led);
	mutex_unlock(&led->mutex);
}

static int wm8350_led_probe(struct platform_device *pdev)
{
	struct regulator *isink, *dcdc;
	struct wm8350_led *led;
	struct wm8350_led_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int i;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}

	if (pdata->max_uA < isink_cur[0]) {
		dev_err(&pdev->dev, "Invalid maximum current %duA\n",
			pdata->max_uA);
		return -EINVAL;
	}

	isink = devm_regulator_get(&pdev->dev, "led_isink");
	if (IS_ERR(isink)) {
		dev_err(&pdev->dev, "%s: can't get ISINK\n", __func__);
		return PTR_ERR(isink);
	}

	dcdc = devm_regulator_get(&pdev->dev, "led_vcc");
	if (IS_ERR(dcdc)) {
		dev_err(&pdev->dev, "%s: can't get DCDC\n", __func__);
		return PTR_ERR(dcdc);
	}

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	led->cdev.brightness_set = wm8350_led_set;
	led->cdev.default_trigger = pdata->default_trigger;
	led->cdev.name = pdata->name;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led->enabled = regulator_is_enabled(isink);
	led->isink = isink;
	led->dcdc = dcdc;

	for (i = 0; i < ARRAY_SIZE(isink_cur) - 1; i++)
		if (isink_cur[i] >= pdata->max_uA)
			break;
	led->max_uA_index = i;
	if (pdata->max_uA != isink_cur[i])
		dev_warn(&pdev->dev,
			 "Maximum current %duA is not directly supported,"
			 " check platform data\n",
			 pdata->max_uA);

	spin_lock_init(&led->value_lock);
	mutex_init(&led->mutex);
	INIT_WORK(&led->work, led_work);
	led->value = LED_OFF;
	platform_set_drvdata(pdev, led);

	return led_classdev_register(&pdev->dev, &led->cdev);
}

static int wm8350_led_remove(struct platform_device *pdev)
{
	struct wm8350_led *led = platform_get_drvdata(pdev);

	led_classdev_unregister(&led->cdev);
	flush_work(&led->work);
	wm8350_led_disable(led);
	return 0;
}

static struct platform_driver wm8350_led_driver = {
	.driver = {
		   .name = "wm8350-led",
		   .owner = THIS_MODULE,
		   },
	.probe = wm8350_led_probe,
	.remove = wm8350_led_remove,
	.shutdown = wm8350_led_shutdown,
};

module_platform_driver(wm8350_led_driver);

MODULE_AUTHOR("Mark Brown");
MODULE_DESCRIPTION("WM8350 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8350-led");
