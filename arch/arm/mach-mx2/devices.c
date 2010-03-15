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

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/mmc.h>

#include "devices.h"

/*
 * SPI master controller
 *
 * - i.MX1: 2 channel (slighly different register setting)
 * - i.MX21: 2 channel
 * - i.MX27: 3 channel
 */
#define DEFINE_IMX_SPI_DEVICE(n, baseaddr, irq)					\
	static struct resource mxc_spi_resources ## n[] = {			\
		{								\
			.start = baseaddr,					\
			.end = baseaddr + SZ_4K - 1,				\
			.flags = IORESOURCE_MEM,				\
		}, {								\
			.start = irq,						\
			.end = irq,						\
			.flags = IORESOURCE_IRQ,				\
		},								\
	};									\
										\
	struct platform_device mxc_spi_device ## n = {				\
		.name = "spi_imx",						\
		.id = n,							\
		.num_resources = ARRAY_SIZE(mxc_spi_resources ## n),		\
		.resource = mxc_spi_resources ## n,				\
	}

DEFINE_IMX_SPI_DEVICE(0, MX2x_CSPI1_BASE_ADDR, MX2x_INT_CSPI1);
DEFINE_IMX_SPI_DEVICE(1, MX2x_CSPI2_BASE_ADDR, MX2x_INT_CSPI2);

#ifdef CONFIG_MACH_MX27
DEFINE_IMX_SPI_DEVICE(2, MX27_CSPI3_BASE_ADDR, MX27_INT_CSPI3);
#endif

/*
 * General Purpose Timer
 * - i.MX21: 3 timers
 * - i.MX27: 6 timers
 */
#define DEFINE_IMX_GPT_DEVICE(n, baseaddr, irq)				\
	static struct resource timer ## n ##_resources[] = {		\
		{							\
			.start = baseaddr,				\
			.end = baseaddr + SZ_4K - 1,			\
			.flags = IORESOURCE_MEM,			\
		}, {							\
			.start = irq,					\
			.end = irq,					\
			.flags = IORESOURCE_IRQ,			\
		}							\
	};								\
									\
	struct platform_device mxc_gpt ## n = {				\
		.name = "imx_gpt",					\
		.id = n,						\
		.num_resources = ARRAY_SIZE(timer ## n ## _resources),	\
		.resource = timer ## n ## _resources,			\
	}

/* We use gpt1 as system timer, so do not add a device for this one */
DEFINE_IMX_GPT_DEVICE(1, MX2x_GPT2_BASE_ADDR, MX2x_INT_GPT2);
DEFINE_IMX_GPT_DEVICE(2, MX2x_GPT3_BASE_ADDR, MX2x_INT_GPT3);

#ifdef CONFIG_MACH_MX27
DEFINE_IMX_GPT_DEVICE(3, MX27_GPT4_BASE_ADDR, MX27_INT_GPT4);
DEFINE_IMX_GPT_DEVICE(4, MX27_GPT5_BASE_ADDR, MX27_INT_GPT5);
DEFINE_IMX_GPT_DEVICE(5, MX27_GPT6_BASE_ADDR, MX27_INT_GPT6);
#endif

/*
 * Watchdog:
 * - i.MX1
 * - i.MX21
 * - i.MX27
 */
static struct resource mxc_wdt_resources[] = {
	{
		.start = MX2x_WDOG_BASE_ADDR,
		.end = MX2x_WDOG_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_wdt = {
	.name = "mxc_wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_wdt_resources),
	.resource = mxc_wdt_resources,
};

static struct resource mxc_w1_master_resources[] = {
	{
		.start = MX2x_OWIRE_BASE_ADDR,
		.end = MX2x_OWIRE_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_w1_master_device = {
	.name = "mxc_w1",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_w1_master_resources),
	.resource = mxc_w1_master_resources,
};

#define DEFINE_MXC_NAND_DEVICE(pfx, baseaddr, irq)			\
	static struct resource pfx ## _nand_resources[] = {		\
		{							\
			.start = baseaddr,				\
			.end = baseaddr + SZ_4K - 1,			\
			.flags = IORESOURCE_MEM,			\
		}, {							\
			.start = irq,					\
			.end = irq,					\
			.flags = IORESOURCE_IRQ,			\
		},							\
	};								\
									\
	struct platform_device pfx ## _nand_device = {			\
		.name = "mxc_nand",					\
		.id = 0,						\
		.num_resources = ARRAY_SIZE(pfx ## _nand_resources),	\
		.resource = pfx ## _nand_resources,			\
	}

#ifdef CONFIG_MACH_MX21
DEFINE_MXC_NAND_DEVICE(imx21, MX21_NFC_BASE_ADDR, MX21_INT_NANDFC);
#endif

#ifdef CONFIG_MACH_MX27
DEFINE_MXC_NAND_DEVICE(imx27, MX27_NFC_BASE_ADDR, MX27_INT_NANDFC);
#endif

/*
 * lcdc:
 * - i.MX1: the basic controller
 * - i.MX21: to be checked
 * - i.MX27: like i.MX1, with slightly variations
 */
static struct resource mxc_fb[] = {
	{
		.start = MX2x_LCDC_BASE_ADDR,
		.end = MX2x_LCDC_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX2x_INT_LCDC,
		.end = MX2x_INT_LCDC,
		.flags = IORESOURCE_IRQ,
	}
};

/* mxc lcd driver */
struct platform_device mxc_fb_device = {
	.name = "imx-fb",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_fb),
	.resource = mxc_fb,
	.dev = {
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

#ifdef CONFIG_MACH_MX27
static struct resource mxc_fec_resources[] = {
	{
		.start = MX27_FEC_BASE_ADDR,
		.end = MX27_FEC_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX27_INT_FEC,
		.end = MX27_INT_FEC,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_fec_device = {
	.name = "fec",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_fec_resources),
	.resource = mxc_fec_resources,
};
#endif

#define DEFINE_IMX_I2C_DEVICE(n, baseaddr, irq)				\
	static struct resource mxc_i2c_resources ## n[] = {		\
		{							\
			.start = baseaddr,				\
			.end = baseaddr + SZ_4K - 1,			\
			.flags = IORESOURCE_MEM,			\
		}, {							\
			.start = irq,					\
			.end = irq,					\
			.flags = IORESOURCE_IRQ,			\
		}							\
	};								\
									\
	struct platform_device mxc_i2c_device ## n = {			\
		.name = "imx-i2c",					\
		.id = n,						\
		.num_resources = ARRAY_SIZE(mxc_i2c_resources ## n),	\
		.resource = mxc_i2c_resources ## n,			\
	}

DEFINE_IMX_I2C_DEVICE(0, MX2x_I2C_BASE_ADDR, MX2x_INT_I2C);

#ifdef CONFIG_MACH_MX27
DEFINE_IMX_I2C_DEVICE(1, MX27_I2C2_BASE_ADDR, MX27_INT_I2C2);
#endif

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

#define DEFINE_IMX_SSI_DMARES(_name, ssin, suffix)			\
	{								\
		.name = _name,						\
		.start = MX2x_DMA_REQ_SSI ## ssin ## _ ## suffix,	\
		.end = MX2x_DMA_REQ_SSI ## ssin ## _ ## suffix,		\
		.flags = IORESOURCE_DMA,				\
	}

#define DEFINE_IMX_SSI_DEVICE(n, ssin, baseaddr, irq)			\
	static struct resource imx_ssi_resources ## n[] = {		\
		{							\
			.start = MX2x_SSI ## ssin ## _BASE_ADDR,	\
			.end = MX2x_SSI ## ssin ## _BASE_ADDR + 0x6f,	\
			.flags = IORESOURCE_MEM,			\
		}, {							\
			.start = MX2x_INT_SSI1,				\
			.end = MX2x_INT_SSI1,				\
			.flags = IORESOURCE_IRQ,			\
		},							\
		DEFINE_IMX_SSI_DMARES("tx0", ssin, TX0),		\
		DEFINE_IMX_SSI_DMARES("rx0", ssin, RX0),		\
		DEFINE_IMX_SSI_DMARES("tx1", ssin, TX1),		\
		DEFINE_IMX_SSI_DMARES("rx1", ssin, RX1),		\
	};								\
									\
	struct platform_device imx_ssi_device ## n = {			\
		.name = "imx-ssi",					\
		.id = n,						\
		.num_resources = ARRAY_SIZE(imx_ssi_resources ## n),	\
		.resource = imx_ssi_resources ## n,			\
	}

DEFINE_IMX_SSI_DEVICE(0, 1, MX2x_SSI1_BASE_ADDR, MX2x_INT_SSI1);
DEFINE_IMX_SSI_DEVICE(1, 2, MX2x_SSI1_BASE_ADDR, MX2x_INT_SSI1);

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
#endif

#ifdef CONFIG_MACH_MX27
DEFINE_MXC_GPIO_PORTS(MX27, imx27);
#endif

int __init mxc_register_gpios(void)
{
#ifdef CONFIG_MACH_MX21
	if (cpu_is_mx21())
		return mxc_gpio_init(imx21_gpio_ports, ARRAY_SIZE(imx21_gpio_ports));
	else
#endif
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27())
		return mxc_gpio_init(imx27_gpio_ports, ARRAY_SIZE(imx27_gpio_ports));
	else
#endif
		return 0;
}

#ifdef CONFIG_MACH_MX21
static struct resource mx21_usbhc_resources[] = {
	{
		.start	= MX21_BASE_ADDR,
		.end	= MX21_BASE_ADDR + 0x1FFF,
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

