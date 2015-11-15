/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2015 Imagination Technologies, Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/mips-boards/sead3-addr.h>

static void sead3_pled_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	writel(value, (void __iomem *)SEAD3_CPLD_P_LED);
}

static void sead3_fled_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	writel(value, (void __iomem *)SEAD3_CPLD_F_LED);
}

static struct led_classdev sead3_pled = {
	.name		= "sead3::pled",
	.brightness_set = sead3_pled_set,
	.flags		= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev sead3_fled = {
	.name		= "sead3::fled",
	.brightness_set = sead3_fled_set,
	.flags		= LED_CORE_SUSPENDRESUME,
};

static int sead3_led_probe(struct platform_device *pdev)
{
	int ret;

	ret = led_classdev_register(&pdev->dev, &sead3_pled);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&pdev->dev, &sead3_fled);
	if (ret < 0)
		led_classdev_unregister(&sead3_pled);

	return ret;
}

static int sead3_led_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&sead3_pled);
	led_classdev_unregister(&sead3_fled);

	return 0;
}

static struct platform_driver sead3_led_driver = {
	.probe		= sead3_led_probe,
	.remove		= sead3_led_remove,
	.driver		= {
		.name		= "sead3-led",
	},
};

module_platform_driver(sead3_led_driver);

MODULE_AUTHOR("Kristian Kielhofner <kris@krisk.org>");
MODULE_DESCRIPTION("SEAD3 LED driver");
MODULE_LICENSE("GPL");
