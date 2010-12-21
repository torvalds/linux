/*
 * OMAP2+ DMA driver
 *
 * Copyright (C) 2003 - 2008 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 * DMA channel linking for 1610 by Samuel Ortiz <samuel.ortiz@nokia.com>
 * Graphics DMA and LCD DMA graphics tranformations
 * by Imre Deak <imre.deak@nokia.com>
 * OMAP2/3 support Copyright (C) 2004-2007 Texas Instruments, Inc.
 * Some functions based on earlier dma-omap.c Copyright (C) 2001 RidgeRun, Inc.
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Converted DMA library into platform driver
 *	- G, Manjunath Kondaiah <manjugk@ti.com>
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

#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/dma.h>

static struct omap_device_pm_latency omap2_dma_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func	 = omap_device_enable_hwmods,
		.flags		 = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

/* One time initializations */
static int __init omap2_system_dma_init_dev(struct omap_hwmod *oh, void *unused)
{
	struct omap_device			*od;
	struct omap_system_dma_plat_info	*p;
	char					*name = "omap_dma_system";

	p = kzalloc(sizeof(struct omap_system_dma_plat_info), GFP_KERNEL);
	if (!p) {
		pr_err("%s: Unable to allocate pdata for %s:%s\n",
			__func__, name, oh->name);
		return -ENOMEM;
	}

	od = omap_device_build(name, 0, oh, p, sizeof(*p),
			omap2_dma_latency, ARRAY_SIZE(omap2_dma_latency), 0);
	kfree(p);
	if (IS_ERR(od)) {
		pr_err("%s: Cant build omap_device for %s:%s.\n",
			__func__, name, oh->name);
		return IS_ERR(od);
	}

	return 0;
}

static int __init omap2_system_dma_init(void)
{
	return omap_hwmod_for_each_by_class("dma",
			omap2_system_dma_init_dev, NULL);
}
arch_initcall(omap2_system_dma_init);
