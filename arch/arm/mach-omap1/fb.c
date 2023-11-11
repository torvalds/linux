// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * File: arch/arm/plat-omap/fb.c
 *
 * Framebuffer device registration for TI OMAP platforms
 *
 * Copyright (C) 2006 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/memblock.h>
#include <linux/io.h>
#include <linux/omapfb.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>

#include <asm/mach/map.h>

#include "irqs.h"

#if IS_ENABLED(CONFIG_FB_OMAP)

static bool omapfb_lcd_configured;
static struct omapfb_platform_data omapfb_config;

static u64 omap_fb_dma_mask = ~(u32)0;

static struct resource omap_fb_resources[] = {
	{
		.name  = "irq",
		.start = INT_LCD_CTRL,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "irq",
		.start = INT_SOSSI_MATCH,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device omap_fb_device = {
	.name		= "omapfb",
	.id		= -1,
	.dev = {
		.dma_mask		= &omap_fb_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &omapfb_config,
	},
	.num_resources = ARRAY_SIZE(omap_fb_resources),
	.resource = omap_fb_resources,
};

void __init omapfb_set_lcd_config(const struct omap_lcd_config *config)
{
	omapfb_config.lcd = *config;
	omapfb_lcd_configured = true;
}

static int __init omap_init_fb(void)
{
	/*
	 * If the board file has not set the lcd config with
	 * omapfb_set_lcd_config(), don't bother registering the omapfb device
	 */
	if (!omapfb_lcd_configured)
		return 0;

	return platform_device_register(&omap_fb_device);
}

arch_initcall(omap_init_fb);

#else

void __init omapfb_set_lcd_config(const struct omap_lcd_config *config)
{
}

#endif
