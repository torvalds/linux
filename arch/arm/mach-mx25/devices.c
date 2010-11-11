/*
 * Copyright 2009 Sascha Hauer, <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <mach/mx25.h>
#include <mach/irqs.h>

static struct resource mx25_rtc_resources[] = {
	{
		.start	= MX25_DRYICE_BASE_ADDR,
		.end	= MX25_DRYICE_BASE_ADDR + 0x40,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_DRYICE,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mx25_rtc_device = {
	.name	= "imxdi_rtc",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(mx25_rtc_resources),
	.resource	= mx25_rtc_resources,
};

static struct resource mx25_fb_resources[] = {
	{
		.start	= MX25_LCDC_BASE_ADDR,
		.end	= MX25_LCDC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_LCDC,
		.end	= MX25_INT_LCDC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mx25_fb_device = {
	.name		= "imx-fb",
	.id		= 0,
	.resource	= mx25_fb_resources,
	.num_resources	= ARRAY_SIZE(mx25_fb_resources),
	.dev		= {
		.coherent_dma_mask = 0xFFFFFFFF,
	},
};

static struct resource mxc_wdt_resources[] = {
	{
		.start = MX25_WDOG_BASE_ADDR,
		.end = MX25_WDOG_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_wdt = {
	.name = "imx2-wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_wdt_resources),
	.resource = mxc_wdt_resources,
};

static struct resource mx25_csi_resources[] = {
	{
		.start	= MX25_CSI_BASE_ADDR,
		.end	= MX25_CSI_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_CSI,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mx25_csi_device = {
	.name	= "mx2-camera",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(mx25_csi_resources),
	.resource	= mx25_csi_resources,
	.dev		= {
		.coherent_dma_mask = 0xffffffff,
	},
};
