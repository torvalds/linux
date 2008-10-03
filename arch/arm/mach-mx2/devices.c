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

#include <mach/hardware.h>

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

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	[0] = {
		.chip.label = "gpio-0",
		.irq = MXC_INT_GPIO,
		.base = (void*)(AIPI_BASE_ADDR_VIRT + 0x15000 + 0x100 * 0),
		.virtual_irq_start = MXC_MAX_INT_LINES,
	},
	[1] = {
		.chip.label = "gpio-1",
		.base = (void*)(AIPI_BASE_ADDR_VIRT + 0x15000 + 0x100 * 1),
		.virtual_irq_start = MXC_MAX_INT_LINES + 32,
	},
	[2] = {
		.chip.label = "gpio-2",
		.base = (void*)(AIPI_BASE_ADDR_VIRT + 0x15000 + 0x100 * 2),
		.virtual_irq_start = MXC_MAX_INT_LINES + 64,
	},
	[3] = {
		.chip.label = "gpio-3",
		.base = (void*)(AIPI_BASE_ADDR_VIRT + 0x15000 + 0x100 * 3),
		.virtual_irq_start = MXC_MAX_INT_LINES + 96,
	},
	[4] = {
		.chip.label = "gpio-4",
		.base = (void*)(AIPI_BASE_ADDR_VIRT + 0x15000 + 0x100 * 4),
		.virtual_irq_start = MXC_MAX_INT_LINES + 128,
	},
	[5] = {
		.chip.label = "gpio-5",
		.base = (void*)(AIPI_BASE_ADDR_VIRT + 0x15000 + 0x100 * 5),
		.virtual_irq_start = MXC_MAX_INT_LINES + 160,
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
