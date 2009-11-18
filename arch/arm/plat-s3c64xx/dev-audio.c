/* linux/arch/arm/plat-s3c/dev-audio.c
 *
 * Copyright 2009 Wolfson Microelectronics
 *      Mark Brown <broonie@opensource.wolfsonmicro.com>
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


static struct resource s3c64xx_iis0_resource[] = {
	[0] = {
		.start = S3C64XX_PA_IIS0,
		.end   = S3C64XX_PA_IIS0 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device s3c64xx_device_iis0 = {
	.name		  = "s3c64xx-iis",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis0_resource),
	.resource	  = s3c64xx_iis0_resource,
};
EXPORT_SYMBOL(s3c64xx_device_iis0);

static struct resource s3c64xx_iis1_resource[] = {
	[0] = {
		.start = S3C64XX_PA_IIS1,
		.end   = S3C64XX_PA_IIS1 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device s3c64xx_device_iis1 = {
	.name		  = "s3c64xx-iis",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis1_resource),
	.resource	  = s3c64xx_iis1_resource,
};
EXPORT_SYMBOL(s3c64xx_device_iis1);

static struct resource s3c64xx_iisv4_resource[] = {
	[0] = {
		.start = S3C64XX_PA_IISV4,
		.end   = S3C64XX_PA_IISV4 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device s3c64xx_device_iisv4 = {
	.name		  = "s3c64xx-iis-v4",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iisv4_resource),
	.resource	  = s3c64xx_iisv4_resource,
};
EXPORT_SYMBOL(s3c64xx_device_iisv4);
