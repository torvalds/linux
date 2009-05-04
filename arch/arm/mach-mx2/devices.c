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
 * Resource definition for the MXC IrDA
 */
static struct resource mxc_irda_resources[] = {
	[0] = {
		.start   = UART3_BASE_ADDR,
		.end     = UART3_BASE_ADDR + SZ_4K - 1,
		.flags   = IORESOURCE_MEM,
	},
	[1] = {
		.start   = MXC_INT_UART3,
		.end     = MXC_INT_UART3,
		.flags   = IORESOURCE_IRQ,
	},
};

/* Platform Data for MXC IrDA */
struct platform_device mxc_irda_device = {
	.name = "mxc_irda",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_irda_resources),
	.resource = mxc_irda_resources,
};

/*
 * General Purpose Timer
 * - i.MX1: 2 timer (slighly different register handling)
 * - i.MX21: 3 timer
 * - i.MX27: 6 timer
 */

/* We use gpt0 as system timer, so do not add a device for this one */

static struct resource timer1_resources[] = {
	[0] = {
		.start	= GPT2_BASE_ADDR,
		.end	= GPT2_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start   = MXC_INT_GPT2,
		.end     = MXC_INT_GPT2,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt1 = {
	.name = "imx_gpt",
	.id = 1,
	.num_resources = ARRAY_SIZE(timer1_resources),
	.resource = timer1_resources
};

static struct resource timer2_resources[] = {
	[0] = {
		.start	= GPT3_BASE_ADDR,
		.end	= GPT3_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start   = MXC_INT_GPT3,
		.end     = MXC_INT_GPT3,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt2 = {
	.name = "imx_gpt",
	.id = 2,
	.num_resources = ARRAY_SIZE(timer2_resources),
	.resource = timer2_resources
};

#ifdef CONFIG_MACH_MX27
static struct resource timer3_resources[] = {
	[0] = {
		.start	= GPT4_BASE_ADDR,
		.end	= GPT4_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start   = MXC_INT_GPT4,
		.end     = MXC_INT_GPT4,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt3 = {
	.name = "imx_gpt",
	.id = 3,
	.num_resources = ARRAY_SIZE(timer3_resources),
	.resource = timer3_resources
};

static struct resource timer4_resources[] = {
	[0] = {
		.start	= GPT5_BASE_ADDR,
		.end	= GPT5_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start   = MXC_INT_GPT5,
		.end     = MXC_INT_GPT5,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt4 = {
	.name = "imx_gpt",
	.id = 4,
	.num_resources = ARRAY_SIZE(timer4_resources),
	.resource = timer4_resources
};

static struct resource timer5_resources[] = {
	[0] = {
		.start	= GPT6_BASE_ADDR,
		.end	= GPT6_BASE_ADDR + 0x17,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start   = MXC_INT_GPT6,
		.end     = MXC_INT_GPT6,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_gpt5 = {
	.name = "imx_gpt",
	.id = 5,
	.num_resources = ARRAY_SIZE(timer5_resources),
	.resource = timer5_resources
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
		.flags	= IORESOURCE_MEM
	}, {
		.start	= MXC_INT_NANDFC,
		.end	= MXC_INT_NANDFC,
		.flags	= IORESOURCE_IRQ
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
	},
	{
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
		.flags	= IORESOURCE_MEM
	}, {
		.start	= MXC_INT_FEC,
		.end	= MXC_INT_FEC,
		.flags	= IORESOURCE_IRQ
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
	[0] = {
		.start	= I2C_BASE_ADDR,
		.end	= I2C_BASE_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= MXC_INT_I2C,
		.end	= MXC_INT_I2C,
		.flags	= IORESOURCE_IRQ
	}
};

struct platform_device mxc_i2c_device0 = {
	.name = "imx-i2c",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_i2c_1_resources),
	.resource = mxc_i2c_1_resources
};

#ifdef CONFIG_MACH_MX27
static struct resource mxc_i2c_2_resources[] = {
	[0] = {
		.start	= I2C2_BASE_ADDR,
		.end	= I2C2_BASE_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= MXC_INT_I2C2,
		.end	= MXC_INT_I2C2,
		.flags	= IORESOURCE_IRQ
	}
};

struct platform_device mxc_i2c_device1 = {
	.name = "imx-i2c",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_i2c_2_resources),
	.resource = mxc_i2c_2_resources
};
#endif

static struct resource mxc_pwm_resources[] = {
	[0] = {
		.start	= PWM_BASE_ADDR,
		.end	= PWM_BASE_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start   = MXC_INT_PWM,
		.end     = MXC_INT_PWM,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device = {
	.name = "mxc_pwm",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources),
	.resource = mxc_pwm_resources
};

/*
 * Resource definition for the MXC SDHC
 */
static struct resource mxc_sdhc1_resources[] = {
	[0] = {
			.start = SDHC1_BASE_ADDR,
			.end   = SDHC1_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
			},
	[1] = {
			.start = MXC_INT_SDHC1,
			.end   = MXC_INT_SDHC1,
			.flags = IORESOURCE_IRQ,
			},
	[2] = {
			.start  = DMA_REQ_SDHC1,
			.end    = DMA_REQ_SDHC1,
			.flags  = IORESOURCE_DMA
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
	[0] = {
			.start = SDHC2_BASE_ADDR,
			.end   = SDHC2_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
			},
	[1] = {
			.start = MXC_INT_SDHC2,
			.end   = MXC_INT_SDHC2,
			.flags = IORESOURCE_IRQ,
			},
	[2] = {
			.start  = DMA_REQ_SDHC2,
			.end    = DMA_REQ_SDHC2,
			.flags  = IORESOURCE_DMA
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

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	[0] = {
		.chip.label = "gpio-0",
		.irq = MXC_INT_GPIO,
		.base = IO_ADDRESS(GPIO_BASE_ADDR),
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	},
	[1] = {
		.chip.label = "gpio-1",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x100),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	},
	[2] = {
		.chip.label = "gpio-2",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x200),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	},
	[3] = {
		.chip.label = "gpio-3",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x300),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	},
	[4] = {
		.chip.label = "gpio-4",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x400),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 128,
	},
	[5] = {
		.chip.label = "gpio-5",
		.base = IO_ADDRESS(GPIO_BASE_ADDR + 0x500),
		.virtual_irq_start = MXC_GPIO_IRQ_START + 160,
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
