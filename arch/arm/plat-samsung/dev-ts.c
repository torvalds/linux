/* linux/arch/arm/mach-s3c64xx/dev-ts.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>, <ben-linux@fluff.org>
 *
 * Adapted by Maurus Cuelenaere for s3c64xx
 *
 * S3C64XX series device definition for touchscreen device
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
#include <plat/ts.h>

static struct resource s3c_ts_resource[] = {
	[0] = {
		.start = SAMSUNG_PA_ADC,
		.end   = SAMSUNG_PA_ADC + SZ_256 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TC,
		.end   = IRQ_TC,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_ts = {
	.name		= "s3c64xx-ts",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_ts_resource),
	.resource	= s3c_ts_resource,
};

void __init s3c24xx_ts_set_platdata(struct s3c2410_ts_mach_info *pd)
{
	struct s3c2410_ts_mach_info *npd;

	if (!pd) {
		printk(KERN_ERR "%s: no platform data\n", __func__);
		return;
	}

	npd = kmemdup(pd, sizeof(struct s3c2410_ts_mach_info), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);

	s3c_device_ts.dev.platform_data = npd;
}
EXPORT_SYMBOL(s3c24xx_ts_set_platdata);
