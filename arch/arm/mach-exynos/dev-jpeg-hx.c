/*
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Exynos series device definition for JPEG HX
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <mach/map.h>
#include <mach/irqs.h>

static u64 exynos5_jpeg_hx_dma_mask = DMA_BIT_MASK(32);

static struct resource exynos5_jpeg_hx_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_JPEG_HX, SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_JPEG_HX),
};

struct platform_device exynos5_device_jpeg_hx = {
	.name             = "exynos5-jpeg-hx",
	.id               = -1,
	.num_resources    = ARRAY_SIZE(exynos5_jpeg_hx_resource),
	.resource         = exynos5_jpeg_hx_resource,
	.dev		= {
		.dma_mask		= &exynos5_jpeg_hx_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
