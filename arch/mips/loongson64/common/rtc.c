/*
 *  Lemote Fuloong platform support
 *
 *  Copyright(c) 2010 Arnaud Patard <apatard@mandriva.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mc146818rtc.h>

static struct resource loongson_rtc_resources[] = {
	{
		.start	= RTC_PORT(0),
		.end	= RTC_PORT(1),
		.flags	= IORESOURCE_IO,
	}, {
		.start	= RTC_IRQ,
		.end	= RTC_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device loongson_rtc_device = {
	.name		= "rtc_cmos",
	.id		= -1,
	.resource	= loongson_rtc_resources,
	.num_resources	= ARRAY_SIZE(loongson_rtc_resources),
};


static int __init loongson_rtc_platform_init(void)
{
	platform_device_register(&loongson_rtc_device);
	return 0;
}

device_initcall(loongson_rtc_platform_init);
