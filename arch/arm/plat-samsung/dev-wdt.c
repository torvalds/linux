/* linux/arch/arm/plat-samsung/dev-wdt.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C series device definition for the watchdog timer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/devs.h>

static struct resource s3c_wdt_resource[] = {
	[0] = {
		.start	= S3C_PA_WDT,
		.end	= S3C_PA_WDT + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_WDT,
		.end	= IRQ_WDT,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_wdt = {
	.name		= "s3c2410-wdt",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_wdt_resource),
	.resource	= s3c_wdt_resource,
};
EXPORT_SYMBOL(s3c_device_wdt);
