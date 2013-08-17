/* linux/arch/arm/mach-exynos/dev-gsc.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Base G-Scaler resource and device definitions
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
#include <media/exynos_gscaler.h>
#include <plat/devs.h>
#include <mach/map.h>

static u64 exynos5_gsc_dma_mask = DMA_BIT_MASK(32);

static struct resource exynos5_gsc0_resource[] = {
	[0] = {
		.start	= EXYNOS5_PA_GSC0,
		.end	= EXYNOS5_PA_GSC0 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GSC0,
		.end	= IRQ_GSC0,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos5_device_gsc0 = {
	.name		= "exynos-gsc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(exynos5_gsc0_resource),
	.resource	= exynos5_gsc0_resource,
	.dev		= {
		.dma_mask		= &exynos5_gsc_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource exynos5_gsc1_resource[] = {
	[0] = {
		.start	= EXYNOS5_PA_GSC1,
		.end	= EXYNOS5_PA_GSC1 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GSC1,
		.end	= IRQ_GSC1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos5_device_gsc1 = {
	.name		= "exynos-gsc",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exynos5_gsc1_resource),
	.resource	= exynos5_gsc1_resource,
	.dev		= {
		.dma_mask		= &exynos5_gsc_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource exynos5_gsc2_resource[] = {
	[0] = {
		.start	= EXYNOS5_PA_GSC2,
		.end	= EXYNOS5_PA_GSC2 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GSC2,
		.end	= IRQ_GSC2,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos5_device_gsc2 = {
	.name		= "exynos-gsc",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(exynos5_gsc2_resource),
	.resource	= exynos5_gsc2_resource,
	.dev		= {
		.dma_mask		= &exynos5_gsc_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource exynos5_gsc3_resource[] = {
	[0] = {
		.start	= EXYNOS5_PA_GSC3,
		.end	= EXYNOS5_PA_GSC3 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_GSC3,
		.end	= IRQ_GSC3,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos5_device_gsc3 = {
	.name		= "exynos-gsc",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(exynos5_gsc3_resource),
	.resource	= exynos5_gsc3_resource,
	.dev		= {
		.dma_mask		= &exynos5_gsc_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

struct exynos_platform_gscaler exynos_gsc0_default_data __initdata;
struct exynos_platform_gscaler exynos_gsc1_default_data __initdata;
struct exynos_platform_gscaler exynos_gsc2_default_data __initdata;
struct exynos_platform_gscaler exynos_gsc3_default_data __initdata;
