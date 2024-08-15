// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Registration of Cobalt RTC platform device.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
#include <linux/platform_device.h>

static struct resource cobalt_rtc_resource[] __initdata = {
	{
		.start	= 0x70,
		.end	= 0x77,
		.flags	= IORESOURCE_IO,
	},
	{
		.start	= RTC_IRQ,
		.end	= RTC_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static __init int cobalt_rtc_add(void)
{
	struct platform_device *pdev;
	int retval;

	pdev = platform_device_alloc("rtc_cmos", -1);
	if (!pdev)
		return -ENOMEM;

	retval = platform_device_add_resources(pdev, cobalt_rtc_resource,
					       ARRAY_SIZE(cobalt_rtc_resource));
	if (retval)
		goto err_free_device;

	retval = platform_device_add(pdev);
	if (retval)
		goto err_free_device;

	return 0;

err_free_device:
	platform_device_put(pdev);

	return retval;
}
device_initcall(cobalt_rtc_add);
