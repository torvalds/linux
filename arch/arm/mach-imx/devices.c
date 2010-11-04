/*
 * Author: MontaVista Software, Inc.
 *       <source@mvista.com>
 *
 * Based on the OMAP devices.c
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/serial.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/mmc.h>

#include "devices.h"

#if defined(CONFIG_ARCH_MX1)
/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR),
		.irq = MX1_GPIO_INT_PORTA,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x100),
		.irq = MX1_GPIO_INT_PORTB,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x200),
		.irq = MX1_GPIO_INT_PORTC,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}, {
		.chip.label = "gpio-3",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x300),
		.irq = MX1_GPIO_INT_PORTD,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	}
};

int __init imx1_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
#endif

#if defined(CONFIG_MACH_MX21) || defined(CONFIG_MACH_MX27)
static struct resource mxc_pwm_resources[] = {
	{
		.start = MX2x_PWM_BASE_ADDR,
		.end = MX2x_PWM_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX2x_INT_PWM,
		.end = MX2x_INT_PWM,
		.flags = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device = {
	.name = "mxc_pwm",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources),
	.resource = mxc_pwm_resources,
};

#define DEFINE_MXC_MMC_DEVICE(n, baseaddr, irq, dmareq)			\
	static struct resource mxc_sdhc_resources ## n[] = {		\
		{							\
			.start = baseaddr,				\
			.end = baseaddr + SZ_4K - 1,			\
			.flags = IORESOURCE_MEM,			\
		}, {							\
			.start = irq,					\
			.end = irq,					\
			.flags = IORESOURCE_IRQ,			\
		}, {							\
			.start = dmareq,				\
			.end = dmareq,					\
			.flags = IORESOURCE_DMA,			\
		},							\
	};								\
									\
	static u64 mxc_sdhc ## n ## _dmamask = DMA_BIT_MASK(32);	\
									\
	struct platform_device mxc_sdhc_device ## n = {			\
		.name = "mxc-mmc",					\
		.id = n,						\
		.dev = {						\
			.dma_mask = &mxc_sdhc ## n ## _dmamask,		\
			.coherent_dma_mask = DMA_BIT_MASK(32),		\
		},							\
		.num_resources = ARRAY_SIZE(mxc_sdhc_resources ## n),	\
		.resource = mxc_sdhc_resources ## n,		\
	}

DEFINE_MXC_MMC_DEVICE(0, MX2x_SDHC1_BASE_ADDR, MX2x_INT_SDHC1, MX2x_DMA_REQ_SDHC1);
DEFINE_MXC_MMC_DEVICE(1, MX2x_SDHC2_BASE_ADDR, MX2x_INT_SDHC2, MX2x_DMA_REQ_SDHC2);

#ifdef CONFIG_MACH_MX27
static struct resource otg_resources[] = {
	{
		.start = MX27_USBOTG_BASE_ADDR,
		.end = MX27_USBOTG_BASE_ADDR + 0x1ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX27_INT_USB3,
		.end = MX27_INT_USB3,
		.flags = IORESOURCE_IRQ,
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
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &otg_dmamask,
	},
	.resource = otg_resources,
	.num_resources = ARRAY_SIZE(otg_resources),
};

/* USB host 1 */

static u64 usbh1_dmamask = DMA_BIT_MASK(32);

static struct resource mxc_usbh1_resources[] = {
	{
		.start = MX27_USBOTG_BASE_ADDR + 0x200,
		.end = MX27_USBOTG_BASE_ADDR + 0x3ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX27_INT_USB1,
		.end = MX27_INT_USB1,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_usbh1 = {
	.name = "mxc-ehci",
	.id = 1,
	.dev = {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &usbh1_dmamask,
	},
	.resource = mxc_usbh1_resources,
	.num_resources = ARRAY_SIZE(mxc_usbh1_resources),
};

/* USB host 2 */
static u64 usbh2_dmamask = DMA_BIT_MASK(32);

static struct resource mxc_usbh2_resources[] = {
	{
		.start = MX27_USBOTG_BASE_ADDR + 0x400,
		.end = MX27_USBOTG_BASE_ADDR + 0x5ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX27_INT_USB2,
		.end = MX27_INT_USB2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_usbh2 = {
	.name = "mxc-ehci",
	.id = 2,
	.dev = {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &usbh2_dmamask,
	},
	.resource = mxc_usbh2_resources,
	.num_resources = ARRAY_SIZE(mxc_usbh2_resources),
};
#endif

/* GPIO port description */
#define DEFINE_MXC_GPIO_PORT_IRQ(SOC, n, _irq)				\
	{								\
		.chip.label = "gpio-" #n,				\
		.irq = _irq,						\
		.base = SOC ## _IO_ADDRESS(MX2x_GPIO_BASE_ADDR +	\
				n * 0x100),				\
		.virtual_irq_start = MXC_GPIO_IRQ_START + n * 32,	\
	}

#define DEFINE_MXC_GPIO_PORT(SOC, n)					\
	{								\
		.chip.label = "gpio-" #n,				\
		.base = SOC ## _IO_ADDRESS(MX2x_GPIO_BASE_ADDR +	\
				n * 0x100),				\
		.virtual_irq_start = MXC_GPIO_IRQ_START + n * 32,	\
	}

#define DEFINE_MXC_GPIO_PORTS(SOC, pfx)					\
	static struct mxc_gpio_port pfx ## _gpio_ports[] = {		\
		DEFINE_MXC_GPIO_PORT_IRQ(SOC, 0, SOC ## _INT_GPIO),	\
		DEFINE_MXC_GPIO_PORT(SOC, 1),				\
		DEFINE_MXC_GPIO_PORT(SOC, 2),				\
		DEFINE_MXC_GPIO_PORT(SOC, 3),				\
		DEFINE_MXC_GPIO_PORT(SOC, 4),				\
		DEFINE_MXC_GPIO_PORT(SOC, 5),				\
	}

#ifdef CONFIG_MACH_MX21
DEFINE_MXC_GPIO_PORTS(MX21, imx21);

int __init imx21_register_gpios(void)
{
	return mxc_gpio_init(imx21_gpio_ports, ARRAY_SIZE(imx21_gpio_ports));
}
#endif

#ifdef CONFIG_MACH_MX27
DEFINE_MXC_GPIO_PORTS(MX27, imx27);

int __init imx27_register_gpios(void)
{
	return mxc_gpio_init(imx27_gpio_ports, ARRAY_SIZE(imx27_gpio_ports));
}
#endif

#ifdef CONFIG_MACH_MX21
static struct resource mx21_usbhc_resources[] = {
	{
		.start	= MX21_USBOTG_BASE_ADDR,
		.end	= MX21_USBOTG_BASE_ADDR + SZ_8K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start		= MX21_INT_USBHOST,
		.end		= MX21_INT_USBHOST,
		.flags		= IORESOURCE_IRQ,
	},
};

struct platform_device mx21_usbhc_device = {
	.name		= "imx21-hcd",
	.id		= 0,
	.dev		= {
		.dma_mask = &mx21_usbhc_device.dev.coherent_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(mx21_usbhc_resources),
	.resource	= mx21_usbhc_resources,
};
#endif

static struct resource imx_kpp_resources[] = {
	{
		.start  = MX2x_KPP_BASE_ADDR,
		.end    = MX2x_KPP_BASE_ADDR + 0xf,
		.flags  = IORESOURCE_MEM
	}, {
		.start  = MX2x_INT_KPP,
		.end    = MX2x_INT_KPP,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device imx_kpp_device = {
	.name = "imx-keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(imx_kpp_resources),
	.resource = imx_kpp_resources,
};

#endif
