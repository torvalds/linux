/* linux/arch/arm/plat-s5p/dev-fimc0.c
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * Base S5P FIMC0 resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <mach/map.h>
#include <media/s5p_fimc.h>

static struct resource s5p_fimc0_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMC0,
		.end	= S5P_PA_FIMC0 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC0,
		.end	= IRQ_FIMC0,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 s5p_fimc0_dma_mask = DMA_BIT_MASK(32);

struct platform_device s5p_device_fimc0 = {
	.name		= "s5p-fimc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p_fimc0_resource),
	.resource	= s5p_fimc0_resource,
	.dev		= {
		.dma_mask		= &s5p_fimc0_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

struct s5p_platform_fimc s3c_fimc0_default_data __initdata;
