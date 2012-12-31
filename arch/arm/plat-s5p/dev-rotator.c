/* linux/arch/arm/plat-s5p/dev-rotator.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * Base S5P Rotator resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <mach/map.h>

static struct resource exynos_rot_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_ROTATOR,
		.end	= EXYNOS_PA_ROTATOR + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_ROTATOR,
		.end	= IRQ_ROTATOR,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 exynos_rot_dma_mask = DMA_BIT_MASK(32);

struct platform_device exynos_device_rotator = {
	.name		= "exynos-rot",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos_rot_resource),
	.resource	= exynos_rot_resource,
	.dev		= {
		.dma_mask		= &exynos_rot_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
