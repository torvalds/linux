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
#include <linux/ioport.h>

#include <mach/map.h>

static struct resource exynos5_rot_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_ROTATOR, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_ROTATOR),
};

static u64 exynos_rot_dma_mask = DMA_BIT_MASK(32);

struct platform_device exynos5_device_rotator = {
	.name		= "exynos-rot",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos5_rot_resource),
	.resource	= exynos5_rot_resource,
	.dev		= {
		.dma_mask		= &exynos_rot_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
