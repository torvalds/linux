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

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = IO_ADDRESS(GPIO1_BASE_ADDR),
		.irq = MXC_INT_GPIO1,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = IO_ADDRESS(GPIO2_BASE_ADDR),
		.irq = MXC_INT_GPIO2,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = IO_ADDRESS(GPIO3_BASE_ADDR),
		.irq = MXC_INT_GPIO3,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}
};

int __init imx3x_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}

static struct resource mxc_w1_master_resources[] = {
	{
		.start = OWIRE_BASE_ADDR,
		.end   = OWIRE_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_w1_master_device = {
	.name = "mxc_w1",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_w1_master_resources),
	.resource = mxc_w1_master_resources,
};

#ifdef CONFIG_ARCH_MX31
static struct resource mxcsdhc0_resources[] = {
	{
		.start = MX31_MMC_SDHC1_BASE_ADDR,
		.end = MX31_MMC_SDHC1_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX31_INT_MMC_SDHC1,
		.end = MX31_INT_MMC_SDHC1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource mxcsdhc1_resources[] = {
	{
		.start = MX31_MMC_SDHC2_BASE_ADDR,
		.end = MX31_MMC_SDHC2_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX31_INT_MMC_SDHC2,
		.end = MX31_INT_MMC_SDHC2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxcsdhc_device0 = {
	.name = "mxc-mmc",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxcsdhc0_resources),
	.resource = mxcsdhc0_resources,
};

struct platform_device mxcsdhc_device1 = {
	.name = "mxc-mmc",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxcsdhc1_resources),
	.resource = mxcsdhc1_resources,
};

static struct resource rnga_resources[] = {
	{
		.start = RNGA_BASE_ADDR,
		.end = RNGA_BASE_ADDR + 0x28,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_rnga_device = {
	.name = "mxc_rnga",
	.id = -1,
	.num_resources = 1,
	.resource = rnga_resources,
};
#endif /* CONFIG_ARCH_MX31 */

/* i.MX31 Image Processing Unit */

/* The resource order is important! */
static struct resource mx3_ipu_rsrc[] = {
	{
		.start = IPU_CTRL_BASE_ADDR,
		.end = IPU_CTRL_BASE_ADDR + 0x5F,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IPU_CTRL_BASE_ADDR + 0x88,
		.end = IPU_CTRL_BASE_ADDR + 0xB3,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_IPU_SYN,
		.end = MXC_INT_IPU_SYN,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC_INT_IPU_ERR,
		.end = MXC_INT_IPU_ERR,
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
		.start	= IPU_CTRL_BASE_ADDR + 0xB4,
		.end	= IPU_CTRL_BASE_ADDR + 0x1BF,
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
		.start	= IPU_CTRL_BASE_ADDR + 0x60,
		.end	= IPU_CTRL_BASE_ADDR + 0x87,
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
		.start	= MX31_OTG_BASE_ADDR,
		.end	= MX31_OTG_BASE_ADDR + 0x1ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_USB3,
		.end	= MXC_INT_USB3,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 otg_dmamask = DMA_BIT_MASK(32);

/* OTG gadget device */
struct platform_device mxc_otg_udc_device = {
	.name		= "fsl-usb2-udc",
	.id		= -1,
	.dev		= {
		.dma_mask		= &otg_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= otg_resources,
	.num_resources	= ARRAY_SIZE(otg_resources),
};

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
		.start = MX31_OTG_BASE_ADDR + 0x200,
		.end = MX31_OTG_BASE_ADDR + 0x3ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_USB1,
		.end = MXC_INT_USB1,
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

/* USB host 2 */
static u64 usbh2_dmamask = ~(u32)0;

static struct resource mxc_usbh2_resources[] = {
	{
		.start = MX31_OTG_BASE_ADDR + 0x400,
		.end = MX31_OTG_BASE_ADDR + 0x5ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_USB2,
		.end = MXC_INT_USB2,
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
		mxc_register_device(&mxc_rnga_device, NULL);
	}
#endif
#if defined(CONFIG_ARCH_MX35)
	if (cpu_is_mx35()) {
		otg_resources[0].start = MX35_OTG_BASE_ADDR;
		otg_resources[0].end = MX35_OTG_BASE_ADDR + 0x1ff;
		otg_resources[1].start = MXC_INT_USBOTG;
		otg_resources[1].end = MXC_INT_USBOTG;
		mxc_usbh1_resources[0].start = MX35_OTG_BASE_ADDR + 0x400;
		mxc_usbh1_resources[0].end = MX35_OTG_BASE_ADDR + 0x5ff;
		mxc_usbh1_resources[1].start = MXC_INT_USBHS;
		mxc_usbh1_resources[1].end = MXC_INT_USBHS;
		imx_wdt_resources[0].start = MX35_WDOG_BASE_ADDR;
		imx_wdt_resources[0].end = MX35_WDOG_BASE_ADDR + 0x3fff;
	}
#endif

	return 0;
}

subsys_initcall(mx3_devices_init);
