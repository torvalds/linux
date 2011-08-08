/*
 * linux/arch/arm/mach-s3c64xx/dev-onenand1.c
 *
 *  Copyright (c) 2008-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * S3C64XX series device definition for OneNAND devices
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

#include <plat/devs.h>

static struct resource s3c64xx_onenand1_resources[] = {
	[0] = {
		.start	= S3C64XX_PA_ONENAND1,
		.end	= S3C64XX_PA_ONENAND1 + 0x400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S3C64XX_PA_ONENAND1_BUF,
		.end	= S3C64XX_PA_ONENAND1_BUF + S3C64XX_SZ_ONENAND1_BUF - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_ONENAND1,
		.end	= IRQ_ONENAND1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s3c64xx_device_onenand1 = {
	.name		= "samsung-onenand",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s3c64xx_onenand1_resources),
	.resource	= s3c64xx_onenand1_resources,
};

void s3c64xx_onenand1_set_platdata(struct onenand_platform_data *pdata)
{
	s3c_set_platdata(pdata, sizeof(struct onenand_platform_data),
			 &s3c64xx_device_onenand1);
}
