/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - USB3.0 DRD controller support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/dwc3-exynos.h>
#include <linux/gpio.h>

#include <plat/devs.h>

#include <mach/irqs.h>
#include <mach/map.h>

static u64 exynos5_usb3_drd_dma_mask = DMA_BIT_MASK(32);

/* DRD0 */
static struct resource exynos5_usb3_drd0_resources[2] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_USB3_DRD0, SZ_1M),
	[1] = DEFINE_RES_IRQ(EXYNOS5_IRQ_USB3_DRD0),
};

struct platform_device exynos5_device_usb3_drd0 = {
	.name		= "exynos-dwc3",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(exynos5_usb3_drd0_resources),
	.resource	= exynos5_usb3_drd0_resources,
	.dev		= {
		.dma_mask		= &exynos5_usb3_drd_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init exynos5_usb3_drd0_set_platdata(struct dwc3_exynos_data *pd)
{
	s3c_set_platdata(pd, sizeof(struct dwc3_exynos_data),
		&exynos5_device_usb3_drd0);
}

/* DRD1 */
static struct resource exynos5_usb3_drd1_resources[2] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_USB3_DRD1, SZ_1M),
	[1] = DEFINE_RES_IRQ(EXYNOS5_IRQ_USB3_DRD1),
};

struct platform_device exynos5_device_usb3_drd1 = {
	.name		= "exynos-dwc3",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exynos5_usb3_drd1_resources),
	.resource	= exynos5_usb3_drd1_resources,
	.dev		= {
		.dma_mask		= &exynos5_usb3_drd_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init exynos5_usb3_drd1_set_platdata(struct dwc3_exynos_data *pd)
{
	s3c_set_platdata(pd, sizeof(struct dwc3_exynos_data),
		&exynos5_device_usb3_drd1);
}
