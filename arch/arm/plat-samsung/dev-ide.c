/* linux/arch/arm/plat-samsung/dev-ide.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung CF-ATA device definition.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <mach/map.h>
#include <plat/ata.h>
#include <plat/devs.h>

static struct resource s3c_cfcon_resource[] = {
	[0] = {
		.start	= SAMSUNG_PA_CFCON,
		.end	= SAMSUNG_PA_CFCON + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CFCON,
		.end	= IRQ_CFCON,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s3c_device_cfcon = {
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c_cfcon_resource),
	.resource	= s3c_cfcon_resource,
};

void s3c_ide_set_platdata(struct s3c_ide_platdata *pdata)
{
	s3c_set_platdata(pdata, sizeof(struct s3c_ide_platdata),
			 &s3c_device_cfcon);
}
