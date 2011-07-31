/* linux/arch/arm/plat-samsung/dev-rtc.c
 *
 * Copyright 2009 by Maurus Cuelenaere <mcuelenaere@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/devs.h>

static struct resource s3c_rtc_resource[] = {
	[0] = {
		.start	= S3C_PA_RTC,
		.end	= S3C_PA_RTC + 0xff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_RTC_ALARM,
		.end	= IRQ_RTC_ALARM,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_RTC_TIC,
		.end	= IRQ_RTC_TIC,
		.flags	= IORESOURCE_IRQ
	}
};

struct platform_device s3c_device_rtc = {
	.name		= "s3c64xx-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_rtc_resource),
	.resource	= s3c_rtc_resource,
};
EXPORT_SYMBOL(s3c_device_rtc);
