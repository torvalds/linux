/* linux/arch/arm/plat-s5p/dev-fimc2.c
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * Base S5P FIMC2 resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <mach/map.h>

static struct resource s5p_fimc2_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMC2,
		.end	= S5P_PA_FIMC2 + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC2,
		.end	= IRQ_FIMC2,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_fimc2 = {
	.name		= "s5p-fimc",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(s5p_fimc2_resource),
	.resource	= s5p_fimc2_resource,
};
