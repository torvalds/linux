// SPDX-License-Identifier: GPL-2.0-only
/* rtc-generic: RTC driver using the generic RTC abstraction
 *
 * Copyright (C) 2008 Kyle McMartin <kyle@mcmartin.ca>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

static int __init generic_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc;
	const struct rtc_class_ops *ops = dev_get_platdata(&dev->dev);

	rtc = devm_rtc_device_register(&dev->dev, "rtc-generic",
					ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(dev, rtc);

	return 0;
}

static struct platform_driver generic_rtc_driver = {
	.driver = {
		.name = "rtc-generic",
	},
};

module_platform_driver_probe(generic_rtc_driver, generic_rtc_probe);

MODULE_AUTHOR("Kyle McMartin <kyle@mcmartin.ca>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic RTC driver");
MODULE_ALIAS("platform:rtc-generic");
