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

static struct resource mxc_keypad_resources[] = {
	{
		.start	= 0x43fa8000,
		.end	= 0x43fabfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = 24,
		.end     = 24,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_keypad_device = {
	.name = "mxc-keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(mxc_keypad_resources),
	.resource = mxc_keypad_resources,
};

static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = MX25_IO_ADDRESS(MX25_GPIO1_BASE_ADDR),
		.irq = 52,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = MX25_IO_ADDRESS(MX25_GPIO2_BASE_ADDR),
		.irq = 51,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = MX25_IO_ADDRESS(MX25_GPIO3_BASE_ADDR),
		.irq = 16,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}, {
		.chip.label = "gpio-3",
		.base = MX25_IO_ADDRESS(MX25_GPIO4_BASE_ADDR),
		.irq = 23,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	}
};

int __init imx25_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}

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

static struct resource mx25_kpp_resources[] = {
	{
		.start	= MX25_KPP_BASE_ADDR,
		.end	= MX25_KPP_BASE_ADDR + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_KPP,
		.end	= MX25_INT_KPP,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mx25_kpp_device = {
	.name	= "imx-keypad",
	.id	= -1,
	.num_resources	= ARRAY_SIZE(mx25_kpp_resources),
	.resource	= mx25_kpp_resources,
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
