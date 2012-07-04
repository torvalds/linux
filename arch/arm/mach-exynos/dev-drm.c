/*
 * linux/arch/arm/mach-exynos/dev-drm.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - core DRM device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <plat/devs.h>

static u64 exynos_drm_dma_mask = DMA_BIT_MASK(32);

struct platform_device exynos_device_drm = {
	.name	= "exynos-drm",
	.dev	= {
		.dma_mask		= &exynos_drm_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
