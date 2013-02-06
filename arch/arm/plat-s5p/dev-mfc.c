/* linux/arch/arm/plat-s5p/dev-mfc.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * Base S5P MFC resource and device definitions
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

static struct resource s5p_mfc_resource[] = {
	[0] = {
		.start	= S5P_PA_MFC,
		.end	= S5P_PA_MFC + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MFC,
		.end	= IRQ_MFC,
		.flags	= IORESOURCE_IRQ,
	},
};

#if defined(CONFIG_DMA_CMA) && defined(CONFIG_USE_MFC_CMA)
static u64 s5p_mfc_dma_mask = DMA_BIT_MASK(32);
#endif

struct platform_device s5p_device_mfc = {
	.name		= "s3c-mfc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_mfc_resource),
	.resource	= s5p_mfc_resource,
#if defined(CONFIG_DMA_CMA) && defined(CONFIG_USE_MFC_CMA)
	.dev		= {
		.dma_mask	= &s5p_mfc_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
#endif
};

