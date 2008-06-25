/*
 * RTC driver for ppc_md RTC functions
 *
 * © 2007 Red Hat, Inc.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <asm/machdep.h>

static int ppc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	ppc_md.get_rtc_time(tm);
	return 0;
}

static int ppc_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return ppc_md.set_rtc_time(tm);
}

static const struct rtc_class_ops ppc_rtc_ops = {
	.set_time = ppc_rtc_set_time,
	.read_time = ppc_rtc_read_time,
};

static struct rtc_device *rtc;
static struct platform_device *ppc_rtc_pdev;

static int __init ppc_rtc_init(void)
{
	if (!ppc_md.get_rtc_time || !ppc_md.set_rtc_time)
		return -ENODEV;

	ppc_rtc_pdev = platform_device_register_simple("ppc-rtc", 0, NULL, 0);
	if (IS_ERR(ppc_rtc_pdev))
		return PTR_ERR(ppc_rtc_pdev);

	rtc = rtc_device_register("ppc_md", &ppc_rtc_pdev->dev,
				  &ppc_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		platform_device_unregister(ppc_rtc_pdev);
		return PTR_ERR(rtc);
	}

	return 0;
}

static void __exit ppc_rtc_exit(void)
{
	rtc_device_unregister(rtc);
	platform_device_unregister(ppc_rtc_pdev);
}

module_init(ppc_rtc_init);
module_exit(ppc_rtc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Generic RTC class driver for PowerPC");
