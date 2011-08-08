/*
 * linux/arch/arm/mach-exynos4/dev-dwmci.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Platform device for Synopsys DesignWare Mobile Storage IP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mmc/dw_mmc.h>

#include <plat/devs.h>

#include <mach/map.h>

static int exynos4_dwmci_get_bus_wd(u32 slot_id)
{
	return 4;
}

static int exynos4_dwmci_init(u32 slot_id, irq_handler_t handler, void *data)
{
	return 0;
}

static struct resource exynos4_dwmci_resource[] = {
	[0] = {
		.start	= EXYNOS4_PA_DWMCI,
		.end	= EXYNOS4_PA_DWMCI + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DWMCI,
		.end	= IRQ_DWMCI,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct dw_mci_board exynos4_dwci_pdata = {
	.num_slots			= 1,
	.quirks				= DW_MCI_QUIRK_BROKEN_CARD_DETECTION,
	.bus_hz				= 80 * 1000 * 1000,
	.detect_delay_ms	= 200,
	.init				= exynos4_dwmci_init,
	.get_bus_wd			= exynos4_dwmci_get_bus_wd,
};

static u64 exynos4_dwmci_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos4_device_dwmci = {
	.name		= "dw_mmc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos4_dwmci_resource),
	.resource	= exynos4_dwmci_resource,
	.dev		= {
		.dma_mask		= &exynos4_dwmci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data	= &exynos4_dwci_pdata,
	},
};

void __init exynos4_dwmci_set_platdata(struct dw_mci_board *pd)
{
	struct dw_mci_board *npd;

	npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
			&exynos4_device_dwmci);

	if (!npd->init)
		npd->init = exynos4_dwmci_init;
	if (!npd->get_bus_wd)
		npd->get_bus_wd = exynos4_dwmci_get_bus_wd;
}
