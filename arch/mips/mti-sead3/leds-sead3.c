/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/io.h>

#define DRVNAME "sead3-led"

static struct platform_device *pdev;

static void sead3_pled_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	pr_debug("sead3_pled_set\n");
	writel(value, (void __iomem *)0xBF000210);	/* FIXME */
}

static void sead3_fled_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	pr_debug("sead3_fled_set\n");
	writel(value, (void __iomem *)0xBF000218);	/* FIXME */
}

static struct led_classdev sead3_pled = {
	.name		= "sead3::pled",
	.brightness_set	= sead3_pled_set,
};

static struct led_classdev sead3_fled = {
	.name		= "sead3::fled",
	.brightness_set	= sead3_fled_set,
};

#ifdef CONFIG_PM
static int sead3_led_suspend(struct platform_device *dev,
		pm_message_t state)
{
	led_classdev_suspend(&sead3_pled);
	led_classdev_suspend(&sead3_fled);
	return 0;
}

static int sead3_led_resume(struct platform_device *dev)
{
	led_classdev_resume(&sead3_pled);
	led_classdev_resume(&sead3_fled);
	return 0;
}
#else
#define sead3_led_suspend NULL
#define sead3_led_resume NULL
#endif

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
	.suspend	= sead3_led_suspend,
	.resume		= sead3_led_resume,
	.driver		= {
		.name		= DRVNAME,
		.owner		= THIS_MODULE,
	},
};

static int __init sead3_led_init(void)
{
	int ret;

	ret = platform_driver_register(&sead3_led_driver);
	if (ret < 0)
		goto out;

	pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		platform_driver_unregister(&sead3_led_driver);
		goto out;
	}

out:
	return ret;
}

static void __exit sead3_led_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&sead3_led_driver);
}

module_init(sead3_led_init);
module_exit(sead3_led_exit);

MODULE_AUTHOR("Kristian Kielhofner <kris@krisk.org>");
MODULE_DESCRIPTION("SEAD3 LED driver");
MODULE_LICENSE("GPL");
