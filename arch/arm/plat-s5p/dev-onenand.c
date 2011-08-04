/* linux/arch/arm/plat-s5p/dev-onenand.c
 *
 * Copyright 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 *  Copyright (c) 2008-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * S5P series device definition for OneNAND devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <mach/irqs.h>
#include <mach/map.h>

static struct resource s5p_onenand_resources[] = {
	[0] = {
		.start	= S5P_PA_ONENAND,
		.end	= S5P_PA_ONENAND + SZ_128K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S5P_PA_ONENAND_DMA,
		.end	= S5P_PA_ONENAND_DMA + SZ_8K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_ONENAND_AUDI,
		.end	= IRQ_ONENAND_AUDI,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_onenand = {
	.name		= "s5pc110-onenand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_onenand_resources),
	.resource	= s5p_onenand_resources,
};
