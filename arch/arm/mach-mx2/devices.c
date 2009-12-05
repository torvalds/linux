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
static struct resource mxc_spi_resources0[] = {
	{
	       .start = CSPI1_BASE_ADDR,
	       .end = CSPI1_BASE_ADDR + SZ_4K - 1,
	       .flags = IORESOURCE_MEM,
	}, {
	       .start = MXC_INT_CSPI1,
	       .end = MXC_INT_CSPI1,
	       .flags = IORESOURCE_IRQ,
	},
};

static struct resource mxc_spi_resources1[] = {
	{
		.start = CSPI2_BASE_ADDR,
		.end = CSPI2_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_CSPI2,
		.end = MXC_INT_CSPI2,
		.flags = IORESOURCE_IRQ,
	},
};

#ifdef CONFIG_MACH_MX27
static struct resource mxc_spi_resources2[] = {
	{
		.start = CSPI3_BASE_ADDR,
		.end = CSPI3_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_CSPI3,
		.end = MXC_INT_CSPI3,
		.flags = IORESOURCE_IRQ,
	},
};
#endif

struct platform_device mxc_spi_device0 = {
	.name = "spi_imx",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_spi_resources0),
	.resource = mxc_spi_resources0,
};

struct platform_device mxc_spi_device1 = {
	.name = "spi_imx",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_spi_resources1),
	.resource = mxc_spi_resources1,
};

#ifdef CONFIG_MACH_MX27
struct platform_device mxc_spi_device2 = {
	.name = "spi_imx",
	.id = 2,
	.num_resources = ARRAY_SIZE(mxc_spi_resources2),
	.resource = mxc_spi_resources2,
};
#endif

/*
 * General Purpose Timer
 * - i.MX21: 3 timers
 * - i.MX27: 6 timers
 */

/* We use gpt0 as system timer, so do not add a device for this one */

static struct resource timer1_resources[] = {
	{
		.start	= GPT2_BASE_ADDR,
		.end	= GPT2_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = MXC_INT_GPT2,
		.end     = MXC_INT_GPT2,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt1 = {
	.name = "imx_gpt",
	.id = 1,
	.num_resources = ARRAY_SIZE(timer1_resources),
	.resource = timer1_resources,
};

static struct resource timer2_resources[] = {
	{
		.start	= GPT3_BASE_ADDR,
		.end	= GPT3_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = MXC_INT_GPT3,
		.end     = MXC_INT_GPT3,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt2 = {
	.name = "imx_gpt",
	.id = 2,
	.num_resources = ARRAY_SIZE(timer2_resources),
	.resource = timer2_resources,
};

#ifdef CONFIG_MACH_MX27
static struct resource timer3_resources[] = {
	{
		.start	= GPT4_BASE_ADDR,
		.end	= GPT4_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = MXC_INT_GPT4,
		.end     = MXC_INT_GPT4,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt3 = {
	.name = "imx_gpt",
	.id = 3,
	.num_resources = ARRAY_SIZE(timer3_resources),
	.resource = timer3_resources,
};

static struct resource timer4_resources[] = {
	{
		.start	= GPT5_BASE_ADDR,
		.end	= GPT5_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = MXC_INT_GPT5,
		.end     = MXC_INT_GPT5,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt4 = {
	.name = "imx_gpt",
	.id = 4,
	.num_resources = ARRAY_SIZE(timer4_resources),
	.resource = timer4_resources,
};

static struct resource timer5_resources[] = {
	{
		.start	= GPT6_BASE_ADDR,
		.end	= GPT6_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = MXC_INT_GPT6,
		.end     = MXC_INT_GPT6,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt5 = {
	.name = "imx_gpt",
	.id = 5,
	.num_resources = ARRAY_SIZE(timer5_resources),
	.resource = timer5_resources,
};
#endif

/*
 * Watchdog:
 * - i.MX1
 * - i.MX21
 * - i.MX27
 */
static struct resource mxc_wdt_resources[] = {
	{
		.start	= WDOG_BASE_ADDR,
		.end	= WDOG_BASE_ADDR + 0x30,
		.flags	= IORESOURCE_MEM,
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

static struct resource mxc_nand_resources[] = {
	{
		.start	= NFC_BASE_ADDR,
		.end	= NFC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_NANDFC,
		.end	= MXC_INT_NANDFC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mxc_nand_device = {
	.name = "mxc_nand",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_nand_resources),
	.resource = mxc_nand_resources,
};

/*
 * lcdc:
 * - i.MX1: the basic controller
 * - i.MX21: to be checked
 * - i.MX27: like i.MX1, with slightly variations
 */
static struct resource mxc_fb[] = {
	{
		.start = LCDC_BASE_ADDR,
		.end   = LCDC_BASE_ADDR + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_LCDC,
		.end   = MXC_INT_LCDC,
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
		.coherent_dma_mask = 0xFFFFFFFF,
	},
};

#ifdef CONFIG_MACH_MX27
static struct resource mxc_fec_resources[] = {
	{
		.start	= FEC_BASE_ADDR,
		.end	= FEC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_FEC,
		.end	= MXC_INT_FEC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mxc_fec_device = {
	.name = "fec",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_fec_resources),
	.resource = mxc_fec_resources,
};
#endif

static struct resource mxc_i2c_1_resources[] = {
	{
		.start	= I2C_BASE_ADDR,
		.end	= I2C_BASE_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_I2C,
		.end	= MXC_INT_I2C,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device mxc_i2c_device0 = {
	.name = "imx-i2c",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_i2c_1_resources),
	.resource = mxc_i2c_1_resources,
};

#ifdef CONFIG_MACH_MX27
static struct resource mxc_i2c_2_resources[] = {
	{
		.start	= I2C2_BASE_ADDR,
		.end	= I2C2_BASE_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_I2C2,
		.end	= MXC_INT_I2C2,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device mxc_i2c_device1 = {
	.name = "imx-i2c",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_i2c_2_resources),
	.resource = mxc_i2c_2_resources,
};
#endif

static struct resource mxc_pwm_resources[] = {
	{
		.start	= PWM_BASE_ADDR,
		.end	= PWM_BASE_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = MXC_INT_PWM,
		.end     = MXC_INT_PWM,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device = {
	.name = "mxc_pwm",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources),
	.resource = mxc_pwm_resources,
};

/*
 * Resource definition for the MXC SDHC
 */
static struct resource mxc_sdhc1_resources[] = {
	{
		.start = SDHC1_BASE_ADDR,
		.end   = SDHC1_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_SDHC1,
		.end   = MXC_INT_SDHC1,
		.flags = IORESOURCE_IRQ,
	}, {
		.start  = DMA_REQ_SDHC1,
		.end    = DMA_REQ_SDHC1,
		.flags  = IORESOURCE_DMA,
	},
};

static u64 mxc_sdhc1_dmamask = 0xffffffffUL;

struct platform_device mxc_sdhc_device0 = {
       .name           = "mxc-mmc",
       .id             = 0,
       .dev            = {
               .dma_mask = &mxc_sdhc1_dmamask,
               .coherent_dma_mask = 0xffffffff,
       },
       .num_resources  = ARRAY_SIZE(mxc_sdhc1_resources),
       .resource       = mxc_sdhc1_resources,
};

static struct resource mxc_sdhc2_resources[] = {
	{
		.start = SDHC2_BASE_ADDR,
		.end   = SDHC2_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_SDHC2,
		.end   = MXC_INT_SDHC2,
		.flags = IORESOURCE_IRQ,
	}, {
		.start  = DMA_REQ_SDHC2,
		.end    = DMA_REQ_SDHC2,
		.flags  = IORESOURCE_DMA,
	},
};

static u64 mxc_sdhc2_dmamask = 0xffffffffUL;

struct platform_device mxc_sdhc_device1 = {
       .name           = "mxc-mmc",
       .id             = 1,
       .dev            = {
               .dma_mask = &mxc_sdhc2_dmamask,
               .coherent_dma_mask = 0xffffffff,
       },
       .num_resources  = ARRAY_SIZE(mxc_sdhc2_resources),
       .resource       = mxc_sdhc2_resources,
};

#ifdef CONFIG_MACH_MX27
static struct resource otg_resources[] = {
	{
		.start	= OTG_BASE_ADDR,
		.end	= OTG_BASE_ADDR + 0x1ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_USB3,
		.end	= MXC_INT_USB3,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 otg_dmamask = 0xffffffffUL;

/* OTG gadget device */
struct platform_device mxc_otg_udc_device = {
	.name		= "fsl-usb2-udc",
	.id		= -1,
	.dev		= {
		.dma_mask		= &otg_dmamask,
		.coherent_dma_mask	= 0xffffffffUL,
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

static u64 usbh1_dmamask = 0xffffffffUL;

static struct resource mxc_usbh1_resources[] = {
	{
		.start = OTG_BASE_ADDR + 0x200,
		.end = OTG_BASE_ADDR + 0x3ff,
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
static u64 usbh2_dmamask = 0xffffffffUL;

static struct resource mxc_usbh2_resources[] = {
	{
		.start = OTG_BASE_ADDR + 0x400,
		.end = OTG_BASE_ADDR + 0x5ff,
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
#endif

static struct resource imx_ssi_resources0[] = {
	{
		.start	= SSI1_BASE_ADDR,
		.end	= SSI1_BASE_ADDR + 0x6F,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_SSI1,
		.end	= MXC_INT_SSI1,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "tx0",
		.start	= DMA_REQ_SSI1_TX0,
		.end	= DMA_REQ_SSI1_TX0,
		.flags	= IORESOURCE_DMA,
	}, {
		.name	= "rx0",
		.start	= DMA_REQ_SSI1_RX0,
		.end	= DMA_REQ_SSI1_RX0,
		.flags	= IORESOURCE_DMA,
	}, {
		.name	= "tx1",
		.start	= DMA_REQ_SSI1_TX1,
		.end	= DMA_REQ_SSI1_TX1,
		.flags	= IORESOURCE_DMA,
	}, {
		.name	= "rx1",
		.start	= DMA_REQ_SSI1_RX1,
		.end	= DMA_REQ_SSI1_RX1,
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource imx_ssi_resources1[] = {
	{
		.start	= SSI2_BASE_ADDR,
		.end	= SSI2_BASE_ADDR + 0x6F,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_SSI2,
		.end	= MXC_INT_SSI2,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "tx0",
		.start	= DMA_REQ_SSI2_TX0,
		.end	= DMA_REQ_SSI2_TX0,
		.flags	= IORESOURCE_DMA,
	}, {
		.name	= "rx0",
		.start	= DMA_REQ_SSI2_RX0,
		.end	= DMA_REQ_SSI2_RX0,
		.flags	= IORESOURCE_DMA,
	}, {
		.name	= "tx1",
		.start	= DMA_REQ_SSI2_TX1,
		.end	= DMA_REQ_SSI2_TX1,
		.flags	= IORESOURCE_DMA,
	}, {
		.name	= "rx1",
		.start	= DMA_REQ_SSI2_RX1,
		.end	= DMA_REQ_SSI2_RX1,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device imx_ssi_device0 = {
	.name = "imx-ssi",
	.id = 0,
	.num_resources = ARRAY_SIZE(imx_ssi_resources0),
	.resource = imx_ssi_resources0,
};

struct platform_device imx_ssi_device1 = {
	.name = "imx-ssi",
	.id = 1,
	.num_resources = ARRAY_SIZE(imx_ssi_resources1),
	.resource = imx_ssi_resources1,
};

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.irq = MXC_INT_GPIO,
		.base = IO_ADDRESS(GPIO_BASE_ADDR),
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x100),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x200),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}, {
		.chip.label = "gpio-3",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x300),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	}, {
		.chip.label = "gpio-4",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x400),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 128,
	}, {
		.chip.label = "gpio-5",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x500),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 160,
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
