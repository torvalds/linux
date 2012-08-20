/*
 *  Copyright (C) 2011 Paul Parsons <lost.distance@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/slab.h>

#include <linux/mfd/asic3.h>
#include <linux/mfd/core.h>
#include <linux/module.h>

/*
 *	The HTC ASIC3 LED GPIOs are inputs, not outputs.
 *	Hence we turn the LEDs on/off via the TimeBase register.
 */

/*
 *	When TimeBase is 4 the clock resolution is about 32Hz.
 *	This driver supports hardware blinking with an on+off
 *	period from 62ms (2 clocks) to 125s (4000 clocks).
 */
#define MS_TO_CLK(ms)	DIV_ROUND_CLOSEST(((ms)*1024), 32000)
#define CLK_TO_MS(clk)	(((clk)*32000)/1024)
#define MAX_CLK		4000            /* Fits into 12-bit Time registers */
#define MAX_MS		CLK_TO_MS(MAX_CLK)

static const unsigned int led_n_base[ASIC3_NUM_LEDS] = {
	[0] = ASIC3_LED_0_Base,
	[1] = ASIC3_LED_1_Base,
	[2] = ASIC3_LED_2_Base,
};

static void brightness_set(struct led_classdev *cdev,
	enum led_brightness value)
{
	struct platform_device *pdev = to_platform_device(cdev->dev->parent);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);
	u32 timebase;
	unsigned int base;

	timebase = (value == LED_OFF) ? 0 : (LED_EN|0x4);

	base = led_n_base[cell->id];
	asic3_write_register(asic, (base + ASIC3_LED_PeriodTime), 32);
	asic3_write_register(asic, (base + ASIC3_LED_DutyTime), 32);
	asic3_write_register(asic, (base + ASIC3_LED_AutoStopCount), 0);
	asic3_write_register(asic, (base + ASIC3_LED_TimeBase), timebase);
}

static int blink_set(struct led_classdev *cdev,
	unsigned long *delay_on,
	unsigned long *delay_off)
{
	struct platform_device *pdev = to_platform_device(cdev->dev->parent);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);
	u32 on;
	u32 off;
	unsigned int base;

	if (*delay_on > MAX_MS || *delay_off > MAX_MS)
		return -EINVAL;

	if (*delay_on == 0 && *delay_off == 0) {
		/* If both are zero then a sensible default should be chosen */
		on = MS_TO_CLK(500);
		off = MS_TO_CLK(500);
	} else {
		on = MS_TO_CLK(*delay_on);
		off = MS_TO_CLK(*delay_off);
		if ((on + off) > MAX_CLK)
			return -EINVAL;
	}

	base = led_n_base[cell->id];
	asic3_write_register(asic, (base + ASIC3_LED_PeriodTime), (on + off));
	asic3_write_register(asic, (base + ASIC3_LED_DutyTime), on);
	asic3_write_register(asic, (base + ASIC3_LED_AutoStopCount), 0);
	asic3_write_register(asic, (base + ASIC3_LED_TimeBase), (LED_EN|0x4));

	*delay_on = CLK_TO_MS(on);
	*delay_off = CLK_TO_MS(off);

	return 0;
}

static int __devinit asic3_led_probe(struct platform_device *pdev)
{
	struct asic3_led *led = pdev->dev.platform_data;
	int ret;

	ret = mfd_cell_enable(pdev);
	if (ret < 0)
		return ret;

	led->cdev = devm_kzalloc(&pdev->dev, sizeof(struct led_classdev),
				GFP_KERNEL);
	if (!led->cdev) {
		ret = -ENOMEM;
		goto out;
	}

	led->cdev->name = led->name;
	led->cdev->flags = LED_CORE_SUSPENDRESUME;
	led->cdev->brightness_set = brightness_set;
	led->cdev->blink_set = blink_set;
	led->cdev->default_trigger = led->default_trigger;

	ret = led_classdev_register(&pdev->dev, led->cdev);
	if (ret < 0)
		goto out;

	return 0;

out:
	(void) mfd_cell_disable(pdev);
	return ret;
}

static int __devexit asic3_led_remove(struct platform_device *pdev)
{
	struct asic3_led *led = pdev->dev.platform_data;

	led_classdev_unregister(led->cdev);

	return mfd_cell_disable(pdev);
}

static int asic3_led_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	int ret;

	ret = 0;
	if (cell->suspend)
		ret = (*cell->suspend)(pdev);

	return ret;
}

static int asic3_led_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	int ret;

	ret = 0;
	if (cell->resume)
		ret = (*cell->resume)(pdev);

	return ret;
}

static const struct dev_pm_ops asic3_led_pm_ops = {
	.suspend	= asic3_led_suspend,
	.resume		= asic3_led_resume,
};

static struct platform_driver asic3_led_driver = {
	.probe		= asic3_led_probe,
	.remove		= __devexit_p(asic3_led_remove),
	.driver		= {
		.name	= "leds-asic3",
		.owner	= THIS_MODULE,
		.pm	= &asic3_led_pm_ops,
	},
};

module_platform_driver(asic3_led_driver);

MODULE_AUTHOR("Paul Parsons <lost.distance@yahoo.com>");
MODULE_DESCRIPTION("HTC ASIC3 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-asic3");
