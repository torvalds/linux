/*
 * linux/arch/arm/plat-samsung/dev-onenand.c
 *
 *  Copyright (c) 2008-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * S3C64XX/S5PC100 series device definition for OneNAND devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>

#include <mach/irqs.h>
#include <mach/map.h>

static struct resource s3c_onenand_resources[] = {
	[0] = {
		.start	= S3C_PA_ONENAND,
		.end	= S3C_PA_ONENAND + 0x400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S3C_PA_ONENAND_BUF,
		.end	= S3C_PA_ONENAND_BUF + S3C_SZ_ONENAND_BUF - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_ONENAND,
		.end	= IRQ_ONENAND,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_onenand = {
	.name		= "samsung-onenand",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c_onenand_resources),
	.resource	= s3c_onenand_resources,
};

void s3c_onenand_set_platdata(struct onenand_platform_data *pdata)
{
	struct onenand_platform_data *pd;

	pd = kmemdup(pdata, sizeof(struct onenand_platform_data), GFP_KERNEL);
	if (!pd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	s3c_device_onenand.dev.platform_data = pd;
}
