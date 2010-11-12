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

static struct resource otg_resources[] = {
	{
		.start	= MX31_USB_OTG_BASE_ADDR,
		.end	= MX31_USB_OTG_BASE_ADDR + 0x1ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MX31_INT_USB_OTG,
		.end	= MX31_INT_USB_OTG,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 otg_dmamask = DMA_BIT_MASK(32);

/* OTG host */
struct platform_device mxc_otg_host = {
	.name = "mxc-ehci",
	.id = 0,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
		.dma_mask = &otg_dmamask,
	},
	.resource = otg_resources,
	.num_resources = ARRAY_SIZE(otg_resources),
};

/* USB host 1 */

static u64 usbh1_dmamask = ~(u32)0;

static struct resource mxc_usbh1_resources[] = {
	{
		.start = MX31_USB_HS1_BASE_ADDR,
		.end = MX31_USB_HS1_BASE_ADDR + 0x1ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX31_INT_USB_HS1,
		.end = MX31_INT_USB_HS1,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_usbh1 = {
	.name = "mxc-ehci",
	.id = 1,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
		.dma_mask = &usbh1_dmamask,
	},
	.resource = mxc_usbh1_resources,
	.num_resources = ARRAY_SIZE(mxc_usbh1_resources),
};

#ifdef CONFIG_ARCH_MX31
/* USB host 2 */
static u64 usbh2_dmamask = ~(u32)0;

static struct resource mxc_usbh2_resources[] = {
	{
		.start = MX31_USB_HS2_BASE_ADDR,
		.end = MX31_USB_HS2_BASE_ADDR + 0x1ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX31_INT_USB_HS2,
		.end = MX31_INT_USB_HS2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_usbh2 = {
	.name = "mxc-ehci",
	.id = 2,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
		.dma_mask = &usbh2_dmamask,
	},
	.resource = mxc_usbh2_resources,
	.num_resources = ARRAY_SIZE(mxc_usbh2_resources),
};
#endif

static struct resource imx_wdt_resources[] = {
	{
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device imx_wdt_device0 = {
	.name           = "imx2-wdt",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(imx_wdt_resources),
	.resource       = imx_wdt_resources,
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

static struct resource imx_kpp_resources[] = {
	{
		.start	= MX3x_KPP_BASE_ADDR,
		.end	= MX3x_KPP_BASE_ADDR + 0xf,
		.flags	= IORESOURCE_MEM
	}, {
		.start	= MX3x_INT_KPP,
		.end	= MX3x_INT_KPP,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device imx_kpp_device = {
	.name = "imx-keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(imx_kpp_resources),
	.resource = imx_kpp_resources,
};

static int __init mx3_devices_init(void)
{
#if defined(CONFIG_ARCH_MX31)
	if (cpu_is_mx31()) {
		imx_wdt_resources[0].start = MX31_WDOG_BASE_ADDR;
		imx_wdt_resources[0].end = MX31_WDOG_BASE_ADDR + 0x3fff;
	}
#endif
#if defined(CONFIG_ARCH_MX35)
	if (cpu_is_mx35()) {
		otg_resources[0].start = MX35_USB_OTG_BASE_ADDR;
		otg_resources[0].end = MX35_USB_OTG_BASE_ADDR + 0x1ff;
		otg_resources[1].start = MX35_INT_USB_OTG;
		otg_resources[1].end = MX35_INT_USB_OTG;
		mxc_usbh1_resources[0].start = MX35_USB_HS_BASE_ADDR;
		mxc_usbh1_resources[0].end = MX35_USB_HS_BASE_ADDR + 0x1ff;
		mxc_usbh1_resources[1].start = MX35_INT_USB_HS;
		mxc_usbh1_resources[1].end = MX35_INT_USB_HS;
		imx_wdt_resources[0].start = MX35_WDOG_BASE_ADDR;
		imx_wdt_resources[0].end = MX35_WDOG_BASE_ADDR + 0x3fff;
	}
#endif

	return 0;
}

subsys_initcall(mx3_devices_init);
