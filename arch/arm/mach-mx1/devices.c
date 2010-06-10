/*
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 * Copyright (c) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 * Copyright (c) 2008 Darius Augulis <darius.augulis@teltonika.lt>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <mach/irqs.h>
#include <mach/hardware.h>

#include "devices.h"

static struct resource imx1_camera_resources[] = {
	{
		.start  = 0x00224000,
		.end    = 0x00224010,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = MX1_CSI_INT,
		.end    = MX1_CSI_INT,
		.flags  = IORESOURCE_IRQ,
	},
};

static u64 imx1_camera_dmamask = DMA_BIT_MASK(32);

struct platform_device imx1_camera_device = {
	.name           = "mx1-camera",
	.id             = 0, /* This is used to put cameras on this interface */
	.dev		= {
		.dma_mask = &imx1_camera_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource       = imx1_camera_resources,
	.num_resources  = ARRAY_SIZE(imx1_camera_resources),
};

static struct resource imx_i2c_resources[] = {
	{
		.start  = 0x00217000,
		.end    = 0x00217010,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = MX1_I2C_INT,
		.end    = MX1_I2C_INT,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device imx_i2c_device0 = {
	.name           = "imx-i2c",
	.id             = 0,
	.resource       = imx_i2c_resources,
	.num_resources  = ARRAY_SIZE(imx_i2c_resources),
};

static struct resource imx_uart1_resources[] = {
	{
		.start = MX1_UART1_BASE_ADDR,
		.end = MX1_UART1_BASE_ADDR + 0xD0,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX1_UART1_MINT_RX,
		.end = MX1_UART1_MINT_RX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MX1_UART1_MINT_TX,
		.end = MX1_UART1_MINT_TX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MX1_UART1_MINT_RTS,
		.end = MX1_UART1_MINT_RTS,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device imx_uart1_device = {
	.name		= "imx-uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imx_uart1_resources),
	.resource	= imx_uart1_resources,
};

static struct resource imx_uart2_resources[] = {
	{
		.start = MX1_UART2_BASE_ADDR,
		.end = MX1_UART2_BASE_ADDR + 0xD0,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX1_UART2_MINT_RX,
		.end = MX1_UART2_MINT_RX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MX1_UART2_MINT_TX,
		.end = MX1_UART2_MINT_TX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MX1_UART2_MINT_RTS,
		.end = MX1_UART2_MINT_RTS,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device imx_uart2_device = {
	.name		= "imx-uart",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(imx_uart2_resources),
	.resource	= imx_uart2_resources,
};

static struct resource imx_rtc_resources[] = {
	{
		.start  = 0x00204000,
		.end    = 0x00204024,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = MX1_RTC_INT,
		.end    = MX1_RTC_INT,
		.flags  = IORESOURCE_IRQ,
	}, {
		.start  = MX1_RTC_SAMINT,
		.end    = MX1_RTC_SAMINT,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device imx_rtc_device = {
	.name           = "rtc-imx",
	.id             = 0,
	.resource       = imx_rtc_resources,
	.num_resources  = ARRAY_SIZE(imx_rtc_resources),
};

static struct resource imx_wdt_resources[] = {
	{
		.start  = 0x00201000,
		.end    = 0x00201008,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = MX1_WDT_INT,
		.end    = MX1_WDT_INT,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device imx_wdt_device = {
	.name           = "imx-wdt",
	.id             = 0,
	.resource       = imx_wdt_resources,
	.num_resources  = ARRAY_SIZE(imx_wdt_resources),
};

static struct resource imx_usb_resources[] = {
	{
		.start	= 0x00212000,
		.end	= 0x00212148,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MX1_USBD_INT0,
		.end	= MX1_USBD_INT0,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= MX1_USBD_INT1,
		.end	= MX1_USBD_INT1,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= MX1_USBD_INT2,
		.end	= MX1_USBD_INT2,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= MX1_USBD_INT3,
		.end	= MX1_USBD_INT3,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= MX1_USBD_INT4,
		.end	= MX1_USBD_INT4,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= MX1_USBD_INT5,
		.end	= MX1_USBD_INT5,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= MX1_USBD_INT6,
		.end	= MX1_USBD_INT6,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device imx_usb_device = {
	.name		= "imx_udc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imx_usb_resources),
	.resource	= imx_usb_resources,
};

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = (void __iomem *)MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR),
		.irq = MX1_GPIO_INT_PORTA,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = (void __iomem *)MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x100),
		.irq = MX1_GPIO_INT_PORTB,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = (void __iomem *)MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x200),
		.irq = MX1_GPIO_INT_PORTC,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}, {
		.chip.label = "gpio-3",
		.base = (void __iomem *)MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x300),
		.irq = MX1_GPIO_INT_PORTD,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	}
};

int __init imx1_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
