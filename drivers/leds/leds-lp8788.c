/*
 * TI LP8788 MFD - keyled driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/mfd/lp8788.h>
#include <linux/mfd/lp8788-isink.h>

#define MAX_BRIGHTNESS			LP8788_ISINK_MAX_PWM
#define DEFAULT_LED_NAME		"keyboard-backlight"

struct lp8788_led {
	struct lp8788 *lp;
	struct mutex lock;
	struct led_classdev led_dev;
	enum lp8788_isink_number isink_num;
	int on;
};

struct lp8788_led_config {
	enum lp8788_isink_scale scale;
	enum lp8788_isink_number num;
	int iout;
};

static struct lp8788_led_config default_led_config = {
	.scale = LP8788_ISINK_SCALE_100mA,
	.num   = LP8788_ISINK_3,
	.iout  = 0,
};

static int lp8788_led_init_device(struct lp8788_led *led,
				struct lp8788_led_platform_data *pdata)
{
	struct lp8788_led_config *cfg = &default_led_config;
	u8 addr, mask, val;
	int ret;

	if (pdata) {
		cfg->scale = pdata->scale;
		cfg->num = pdata->num;
		cfg->iout = pdata->iout_code;
	}

	led->isink_num = cfg->num;

	/* scale configuration */
	addr = LP8788_ISINK_CTRL;
	mask = 1 << (cfg->num + LP8788_ISINK_SCALE_OFFSET);
	val = cfg->scale << (cfg->num + LP8788_ISINK_SCALE_OFFSET);
	ret = lp8788_update_bits(led->lp, addr, mask, val);
	if (ret)
		return ret;

	/* current configuration */
	addr = lp8788_iout_addr[cfg->num];
	mask = lp8788_iout_mask[cfg->num];
	val = cfg->iout;

	return lp8788_update_bits(led->lp, addr, mask, val);
}

static int lp8788_led_enable(struct lp8788_led *led,
			enum lp8788_isink_number num, int on)
{
	int ret;

	u8 mask = 1 << num;
	u8 val = on << num;

	ret = lp8788_update_bits(led->lp, LP8788_ISINK_CTRL, mask, val);
	if (ret == 0)
		led->on = on;

	return ret;
}

static int lp8788_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness val)
{
	struct lp8788_led *led =
			container_of(led_cdev, struct lp8788_led, led_dev);

	enum lp8788_isink_number num = led->isink_num;
	int enable, ret;

	mutex_lock(&led->lock);

	switch (num) {
	case LP8788_ISINK_1:
	case LP8788_ISINK_2:
	case LP8788_ISINK_3:
		ret = lp8788_write_byte(led->lp, lp8788_pwm_addr[num], val);
		if (ret < 0)
			goto unlock;
		break;
	default:
		mutex_unlock(&led->lock);
		return -EINVAL;
	}

	enable = (val > 0) ? 1 : 0;
	if (enable != led->on)
		ret = lp8788_led_enable(led, num, enable);
unlock:
	mutex_unlock(&led->lock);
	return ret;
}

static int lp8788_led_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	struct lp8788_led_platform_data *led_pdata;
	struct lp8788_led *led;
	struct device *dev = &pdev->dev;
	int ret;

	led = devm_kzalloc(dev, sizeof(struct lp8788_led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->lp = lp;
	led->led_dev.max_brightness = MAX_BRIGHTNESS;
	led->led_dev.brightness_set_blocking = lp8788_brightness_set;

	led_pdata = lp->pdata ? lp->pdata->led_pdata : NULL;

	if (!led_pdata || !led_pdata->name)
		led->led_dev.name = DEFAULT_LED_NAME;
	else
		led->led_dev.name = led_pdata->name;

	mutex_init(&led->lock);

	ret = lp8788_led_init_device(led, led_pdata);
	if (ret) {
		dev_err(dev, "led init device err: %d\n", ret);
		return ret;
	}

	ret = devm_led_classdev_register(dev, &led->led_dev);
	if (ret) {
		dev_err(dev, "led register err: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver lp8788_led_driver = {
	.probe = lp8788_led_probe,
	.driver = {
		.name = LP8788_DEV_KEYLED,
	},
};
module_platform_driver(lp8788_led_driver);

MODULE_DESCRIPTION("Texas Instruments LP8788 Keyboard LED Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-keyled");
