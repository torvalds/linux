/*
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
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

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/common.h>
#include <mach/mx3_camera.h>

#include "devices.h"

/* i.MX31 Image Processing Unit */

/* The resource order is important! */
static struct resource mx3_ipu_rsrc[] = {
	{
		.start = MX3x_IPU_CTRL_BASE_ADDR,
		.end = MX3x_IPU_CTRL_BASE_ADDR + 0x5F,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX3x_IPU_CTRL_BASE_ADDR + 0x88,
		.end = MX3x_IPU_CTRL_BASE_ADDR + 0xB3,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX3x_INT_IPU_SYN,
		.end = MX3x_INT_IPU_SYN,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MX3x_INT_IPU_ERR,
		.end = MX3x_INT_IPU_ERR,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mx3_ipu = {
	.name = "ipu-core",
	.id = -1,
	.num_resources = ARRAY_SIZE(mx3_ipu_rsrc),
	.resource = mx3_ipu_rsrc,
};

static struct resource fb_resources[] = {
	{
		.start	= MX3x_IPU_CTRL_BASE_ADDR + 0xB4,
		.end	= MX3x_IPU_CTRL_BASE_ADDR + 0x1BF,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mx3_fb = {
	.name		= "mx3_sdc_fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(fb_resources),
	.resource	= fb_resources,
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
       },
};

static struct resource camera_resources[] = {
	{
		.start	= MX3x_IPU_CTRL_BASE_ADDR + 0x60,
		.end	= MX3x_IPU_CTRL_BASE_ADDR + 0x87,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mx3_camera = {
	.name		= "mx3-camera",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(camera_resources),
	.resource	= camera_resources,
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource imx_rtc_resources[] = {
	{
		.start  = MX31_RTC_BASE_ADDR,
		.end    = MX31_RTC_BASE_ADDR + 0x3fff,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = MX31_INT_RTC,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device imx_rtc_device0 = {
	.name           = "mxc_rtc",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(imx_rtc_resources),
	.resource       = imx_rtc_resources,
};
