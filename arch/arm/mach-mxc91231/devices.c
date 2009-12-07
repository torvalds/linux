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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/imx-uart.h>

static struct resource uart0[] = {
	{
		.start = MXC91231_UART1_BASE_ADDR,
		.end = MXC91231_UART1_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_UART1_RX,
		.end = MXC91231_INT_UART1_RX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC91231_INT_UART1_TX,
		.end = MXC91231_INT_UART1_TX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC91231_INT_UART1_MINT,
		.end = MXC91231_INT_UART1_MINT,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device0 = {
	.name = "imx-uart",
	.id = 0,
	.resource = uart0,
	.num_resources = ARRAY_SIZE(uart0),
};

static struct resource uart1[] = {
	{
		.start = MXC91231_UART2_BASE_ADDR,
		.end = MXC91231_UART2_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_UART2_RX,
		.end = MXC91231_INT_UART2_RX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC91231_INT_UART2_TX,
		.end = MXC91231_INT_UART2_TX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC91231_INT_UART2_MINT,
		.end = MXC91231_INT_UART2_MINT,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device1 = {
	.name = "imx-uart",
	.id = 1,
	.resource = uart1,
	.num_resources = ARRAY_SIZE(uart1),
};

static struct resource uart2[] = {
	{
		.start = MXC91231_UART3_BASE_ADDR,
		.end = MXC91231_UART3_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_UART3_RX,
		.end = MXC91231_INT_UART3_RX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC91231_INT_UART3_TX,
		.end = MXC91231_INT_UART3_TX,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC91231_INT_UART3_MINT,
		.end = MXC91231_INT_UART3_MINT,
		.flags = IORESOURCE_IRQ,

	},
};

struct platform_device mxc_uart_device2 = {
	.name = "imx-uart",
	.id = 2,
	.resource = uart2,
	.num_resources = ARRAY_SIZE(uart2),
};

/* GPIO port description */
static struct mxc_gpio_port mxc_gpio_ports[] = {
	[0] = {
		.chip.label = "gpio-0",
		.base = MXC91231_IO_ADDRESS(MXC91231_GPIO1_AP_BASE_ADDR),
		.irq = MXC91231_INT_GPIO1,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	},
	[1] = {
		.chip.label = "gpio-1",
		.base = MXC91231_IO_ADDRESS(MXC91231_GPIO2_AP_BASE_ADDR),
		.irq = MXC91231_INT_GPIO2,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	},
	[2] = {
		.chip.label = "gpio-2",
		.base = MXC91231_IO_ADDRESS(MXC91231_GPIO3_AP_BASE_ADDR),
		.irq = MXC91231_INT_GPIO3,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	},
	[3] = {
		.chip.label = "gpio-3",
		.base = MXC91231_IO_ADDRESS(MXC91231_GPIO4_SH_BASE_ADDR),
		.irq = MXC91231_INT_GPIO4,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	},
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(mxc_gpio_ports, ARRAY_SIZE(mxc_gpio_ports));
}

static struct resource mxc_nand_resources[] = {
	{
		.start	= MXC91231_NFC_BASE_ADDR,
		.end	= MXC91231_NFC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM
	}, {
		.start	= MXC91231_INT_NANDFC,
		.end	= MXC91231_INT_NANDFC,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mxc_nand_device = {
	.name = "mxc_nand",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_nand_resources),
	.resource = mxc_nand_resources,
};

static struct resource mxc_sdhc0_resources[] = {
	{
		.start = MXC91231_MMC_SDHC1_BASE_ADDR,
		.end = MXC91231_MMC_SDHC1_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_MMC_SDHC1,
		.end = MXC91231_INT_MMC_SDHC1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource mxc_sdhc1_resources[] = {
	{
		.start = MXC91231_MMC_SDHC2_BASE_ADDR,
		.end = MXC91231_MMC_SDHC2_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_MMC_SDHC2,
		.end = MXC91231_INT_MMC_SDHC2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_sdhc_device0 = {
	.name = "mxc-mmc",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_sdhc0_resources),
	.resource = mxc_sdhc0_resources,
};

struct platform_device mxc_sdhc_device1 = {
	.name = "mxc-mmc",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_sdhc1_resources),
	.resource = mxc_sdhc1_resources,
};

static struct resource mxc_cspi0_resources[] = {
	{
		.start = MXC91231_CSPI1_BASE_ADDR,
		.end = MXC91231_CSPI1_BASE_ADDR + 0x20,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_CSPI1,
		.end = MXC91231_INT_CSPI1,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_cspi_device0 = {
	.name = "spi_imx",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_cspi0_resources),
	.resource = mxc_cspi0_resources,
};

static struct resource mxc_cspi1_resources[] = {
	{
		.start = MXC91231_CSPI2_BASE_ADDR,
		.end = MXC91231_CSPI2_BASE_ADDR + 0x20,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC91231_INT_CSPI2,
		.end = MXC91231_INT_CSPI2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_cspi_device1 = {
	.name = "spi_imx",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_cspi1_resources),
	.resource = mxc_cspi1_resources,
};

static struct resource mxc_wdog0_resources[] = {
	{
		.start = MXC91231_WDOG1_BASE_ADDR,
		.end = MXC91231_WDOG1_BASE_ADDR + 0x10,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_wdog_device0 = {
	.name = "mxc-wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_wdog0_resources),
	.resource = mxc_wdog0_resources,
};
