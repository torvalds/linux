/*
 * LEDs driver for Analog Devices ADP5520/ADP5501 MFD PMICs
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Loosely derived from leds-da903x:
 * Copyright (C) 2008 Compulab, Ltd.
 *	Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 *	Eric Miao <eric.miao@marvell.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/mfd/adp5520.h>
#include <linux/slab.h>

struct adp5520_led {
	struct led_classdev	cdev;
	struct work_struct	work;
	struct device		*master;
	enum led_brightness	new_brightness;
	int			id;
	int			flags;
};

static void adp5520_led_work(struct work_struct *work)
{
	struct adp5520_led *led = container_of(work, struct adp5520_led, work);
	adp5520_write(led->master, ADP5520_LED1_CURRENT + led->id - 1,
			 led->new_brightness >> 2);
}

static void adp5520_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct adp5520_led *led;

	led = container_of(led_cdev, struct adp5520_led, cdev);
	led->new_brightness = value;
	schedule_work(&led->work);
}

static int adp5520_led_setup(struct adp5520_led *led)
{
	struct device *dev = led->master;
	int flags = led->flags;
	int ret = 0;

	switch (led->id) {
	case FLAG_ID_ADP5520_LED1_ADP5501_LED0:
		ret |= adp5520_set_bits(dev, ADP5520_LED_TIME,
					(flags >> ADP5520_FLAG_OFFT_SHIFT) &
					ADP5520_FLAG_OFFT_MASK);
		ret |= adp5520_set_bits(dev, ADP5520_LED_CONTROL,
					ADP5520_LED1_EN);
		break;
	case FLAG_ID_ADP5520_LED2_ADP5501_LED1:
		ret |= adp5520_set_bits(dev,  ADP5520_LED_TIME,
					((flags >> ADP5520_FLAG_OFFT_SHIFT) &
					ADP5520_FLAG_OFFT_MASK) << 2);
		ret |= adp5520_clr_bits(dev, ADP5520_LED_CONTROL,
					 ADP5520_R3_MODE);
		ret |= adp5520_set_bits(dev, ADP5520_LED_CONTROL,
					ADP5520_LED2_EN);
		break;
	case FLAG_ID_ADP5520_LED3_ADP5501_LED2:
		ret |= adp5520_set_bits(dev,  ADP5520_LED_TIME,
					((flags >> ADP5520_FLAG_OFFT_SHIFT) &
					ADP5520_FLAG_OFFT_MASK) << 4);
		ret |= adp5520_clr_bits(dev, ADP5520_LED_CONTROL,
					ADP5520_C3_MODE);
		ret |= adp5520_set_bits(dev, ADP5520_LED_CONTROL,
					ADP5520_LED3_EN);
		break;
	}

	return ret;
}

static int adp5520_led_prepare(struct platform_device *pdev)
{
	struct adp5520_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device *dev = pdev->dev.parent;
	int ret = 0;

	ret |= adp5520_write(dev, ADP5520_LED1_CURRENT, 0);
	ret |= adp5520_write(dev, ADP5520_LED2_CURRENT, 0);
	ret |= adp5520_write(dev, ADP5520_LED3_CURRENT, 0);
	ret |= adp5520_write(dev, ADP5520_LED_TIME, pdata->led_on_time << 6);
	ret |= adp5520_write(dev, ADP5520_LED_FADE, FADE_VAL(pdata->fade_in,
		 pdata->fade_out));

	return ret;
}

static int adp5520_led_probe(struct platform_device *pdev)
{
	struct adp5520_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct adp5520_led *led, *led_dat;
	struct led_info *cur_led;
	int ret, i;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	if (pdata->num_leds > ADP5520_01_MAXLEDS) {
		dev_err(&pdev->dev, "can't handle more than %d LEDS\n",
				 ADP5520_01_MAXLEDS);
		return -EFAULT;
	}

	led = devm_kzalloc(&pdev->dev, sizeof(*led) * pdata->num_leds,
				GFP_KERNEL);
	if (led == NULL) {
		dev_err(&pdev->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	ret = adp5520_led_prepare(pdev);

	if (ret) {
		dev_err(&pdev->dev, "failed to write\n");
		return ret;
	}

	for (i = 0; i < pdata->num_leds; ++i) {
		cur_led = &pdata->leds[i];
		led_dat = &led[i];

		led_dat->cdev.name = cur_led->name;
		led_dat->cdev.default_trigger = cur_led->default_trigger;
		led_dat->cdev.brightness_set = adp5520_led_set;
		led_dat->cdev.brightness = LED_OFF;

		if (cur_led->flags & ADP5520_FLAG_LED_MASK)
			led_dat->flags = cur_led->flags;
		else
			led_dat->flags = i + 1;

		led_dat->id = led_dat->flags & ADP5520_FLAG_LED_MASK;

		led_dat->master = pdev->dev.parent;
		led_dat->new_brightness = LED_OFF;

		INIT_WORK(&led_dat->work, adp5520_led_work);

		ret = led_classdev_register(led_dat->master, &led_dat->cdev);
		if (ret) {
			dev_err(&pdev->dev, "failed to register LED %d\n",
				led_dat->id);
			goto err;
		}

		ret = adp5520_led_setup(led_dat);
		if (ret) {
			dev_err(&pdev->dev, "failed to write\n");
			i++;
			goto err;
		}
	}

	platform_set_drvdata(pdev, led);
	return 0;

err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			led_classdev_unregister(&led[i].cdev);
			cancel_work_sync(&led[i].work);
		}
	}

	return ret;
}

static int adp5520_led_remove(struct platform_device *pdev)
{
	struct adp5520_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct adp5520_led *led;
	int i;

	led = platform_get_drvdata(pdev);

	adp5520_clr_bits(led->master, ADP5520_LED_CONTROL,
		 ADP5520_LED1_EN | ADP5520_LED2_EN | ADP5520_LED3_EN);

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&led[i].cdev);
		cancel_work_sync(&led[i].work);
	}

	return 0;
}

static struct platform_driver adp5520_led_driver = {
	.driver	= {
		.name	= "adp5520-led",
		.owner	= THIS_MODULE,
	},
	.probe		= adp5520_led_probe,
	.remove		= adp5520_led_remove,
};

module_platform_driver(adp5520_led_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("LEDS ADP5520(01) Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:adp5520-led");
