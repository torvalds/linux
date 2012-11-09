/*
 * File: arch/arm/plat-omap/fb.c
 *
 * Framebuffer device registration for TI OMAP platforms
 *
 * Copyright (C) 2006 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
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

#include <mach/hardware.h>
#include <asm/mach/map.h>

#if defined(CONFIG_FB_OMAP) || defined(CONFIG_FB_OMAP_MODULE)

static bool omapfb_lcd_configured;
static struct omapfb_platform_data omapfb_config;

static u64 omap_fb_dma_mask = ~(u32)0;

static struct platform_device omap_fb_device = {
	.name		= "omapfb",
	.id		= -1,
	.dev = {
		.dma_mask		= &omap_fb_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &omapfb_config,
	},
	.num_resources = 0,
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

#elif defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE)

static u64 omap_fb_dma_mask = ~(u32)0;
static struct omapfb_platform_data omapfb_config;

static struct platform_device omap_fb_device = {
	.name		= "omapfb",
	.id		= -1,
	.dev = {
		.dma_mask		= &omap_fb_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &omapfb_config,
	},
	.num_resources = 0,
};

static int __init omap_init_fb(void)
{
	return platform_device_register(&omap_fb_device);
}

arch_initcall(omap_init_fb);

#else

void __init omapfb_set_lcd_config(const struct omap_lcd_config *config)
{
}

#endif
