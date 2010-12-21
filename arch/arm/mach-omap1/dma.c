/*
 * OMAP1/OMAP7xx - specific DMA driver
 *
 * Copyright (C) 2003 - 2008 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 * DMA channel linking for 1610 by Samuel Ortiz <samuel.ortiz@nokia.com>
 * Graphics DMA and LCD DMA graphics tranformations
 * by Imre Deak <imre.deak@nokia.com>
 * OMAP2/3 support Copyright (C) 2004-2007 Texas Instruments, Inc.
 * Some functions based on earlier dma-omap.c Copyright (C) 2001 RidgeRun, Inc.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Converted DMA library into platform driver
 *                   - G, Manjunath Kondaiah <manjugk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include <plat/dma.h>
#include <plat/tc.h>
#include <plat/irqs.h>

#define OMAP1_DMA_BASE			(0xfffed800)

static struct resource res[] __initdata = {
	[0] = {
		.start	= OMAP1_DMA_BASE,
		.end	= OMAP1_DMA_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name   = "0",
		.start  = INT_DMA_CH0_6,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.name   = "1",
		.start  = INT_DMA_CH1_7,
		.flags  = IORESOURCE_IRQ,
	},
	[3] = {
		.name   = "2",
		.start  = INT_DMA_CH2_8,
		.flags  = IORESOURCE_IRQ,
	},
	[4] = {
		.name   = "3",
		.start  = INT_DMA_CH3,
		.flags  = IORESOURCE_IRQ,
	},
	[5] = {
		.name   = "4",
		.start  = INT_DMA_CH4,
		.flags  = IORESOURCE_IRQ,
	},
	[6] = {
		.name   = "5",
		.start  = INT_DMA_CH5,
		.flags  = IORESOURCE_IRQ,
	},
	[7] = {
		.name   = "6",
		.start  = INT_1610_DMA_CH6,
		.flags  = IORESOURCE_IRQ,
	},
	/* irq's for omap16xx and omap7xx */
	[8] = {
		.name   = "7",
		.start  = INT_1610_DMA_CH7,
		.flags  = IORESOURCE_IRQ,
	},
	[9] = {
		.name   = "8",
		.start  = INT_1610_DMA_CH8,
		.flags  = IORESOURCE_IRQ,
	},
	[10] = {
		.name  = "9",
		.start = INT_1610_DMA_CH9,
		.flags = IORESOURCE_IRQ,
	},
	[11] = {
		.name  = "10",
		.start = INT_1610_DMA_CH10,
		.flags = IORESOURCE_IRQ,
	},
	[12] = {
		.name  = "11",
		.start = INT_1610_DMA_CH11,
		.flags = IORESOURCE_IRQ,
	},
	[13] = {
		.name  = "12",
		.start = INT_1610_DMA_CH12,
		.flags = IORESOURCE_IRQ,
	},
	[14] = {
		.name  = "13",
		.start = INT_1610_DMA_CH13,
		.flags = IORESOURCE_IRQ,
	},
	[15] = {
		.name  = "14",
		.start = INT_1610_DMA_CH14,
		.flags = IORESOURCE_IRQ,
	},
	[16] = {
		.name  = "15",
		.start = INT_1610_DMA_CH15,
		.flags = IORESOURCE_IRQ,
	},
	[17] = {
		.name  = "16",
		.start = INT_DMA_LCD,
		.flags = IORESOURCE_IRQ,
	},
};

static int __init omap1_system_dma_init(void)
{
	struct omap_system_dma_plat_info	*p;
	struct platform_device			*pdev;
	int ret;

	pdev = platform_device_alloc("omap_dma_system", 0);
	if (!pdev) {
		pr_err("%s: Unable to device alloc for dma\n",
			__func__);
		return -ENOMEM;
	}

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_device_del;
	}

	p = kzalloc(sizeof(struct omap_system_dma_plat_info), GFP_KERNEL);
	if (!p) {
		dev_err(&pdev->dev, "%s: Unable to allocate 'p' for %s\n",
			__func__, pdev->name);
		ret = -ENOMEM;
		goto exit_device_put;
	}

	ret = platform_device_add_data(pdev, p, sizeof(*p));
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: Unable to add resources for %s%d\n",
			__func__, pdev->name, pdev->id);
		goto exit_device_put;
	}

	return ret;

exit_device_put:
	platform_device_put(pdev);
exit_device_del:
	platform_device_del(pdev);

	return ret;
}
arch_initcall(omap1_system_dma_init);
