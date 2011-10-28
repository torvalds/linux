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
		goto ret0;

	led->cdev = kzalloc(sizeof(struct led_classdev), GFP_KERNEL);
	if (!led->cdev) {
		ret = -ENOMEM;
		goto ret1;
	}

	led->cdev->name = led->name;
	led->cdev->default_trigger = led->default_trigger;
	led->cdev->brightness_set = brightness_set;
	led->cdev->blink_set = blink_set;

	ret = led_classdev_register(&pdev->dev, led->cdev);
	if (ret < 0)
		goto ret2;

	return 0;

ret2:
	kfree(led->cdev);
ret1:
	(void) mfd_cell_disable(pdev);
ret0:
	return ret;
}

static int __devexit asic3_led_remove(struct platform_device *pdev)
{
	struct asic3_led *led = pdev->dev.platform_data;

	led_classdev_unregister(led->cdev);

	kfree(led->cdev);

	return mfd_cell_disable(pdev);
}

static struct platform_driver asic3_led_driver = {
	.probe		= asic3_led_probe,
	.remove		= __devexit_p(asic3_led_remove),
	.driver		= {
		.name	= "leds-asic3",
		.owner	= THIS_MODULE,
	},
};

MODULE_ALIAS("platform:leds-asic3");

static int __init asic3_led_init(void)
{
	return platform_driver_register(&asic3_led_driver);
}

static void __exit asic3_led_exit(void)
{
	platform_driver_unregister(&asic3_led_driver);
}

module_init(asic3_led_init);
module_exit(asic3_led_exit);

MODULE_AUTHOR("Paul Parsons <lost.distance@yahoo.com>");
MODULE_DESCRIPTION("HTC ASIC3 LED driver");
MODULE_LICENSE("GPL");
