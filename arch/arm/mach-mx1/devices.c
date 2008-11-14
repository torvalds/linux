/*
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 * Copyright (c) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
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
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <mach/hardware.h>

static struct resource imx_uart1_resources[] = {
	[0] = {
		.start	= UART1_BASE_ADDR,
		.end	= UART1_BASE_ADDR + 0xD0,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= UART1_MINT_RX,
		.end	= UART1_MINT_RX,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= UART1_MINT_TX,
		.end	= UART1_MINT_TX,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= UART1_MINT_RTS,
		.end	= UART1_MINT_RTS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device imx_uart1_device = {
	.name		= "imx-uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imx_uart1_resources),
	.resource	= imx_uart1_resources,
};

static struct resource imx_uart2_resources[] = {
	[0] = {
		.start	= UART2_BASE_ADDR,
		.end	= UART2_BASE_ADDR + 0xD0,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= UART2_MINT_RX,
		.end	= UART2_MINT_RX,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= UART2_MINT_TX,
		.end	= UART2_MINT_TX,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= UART2_MINT_RTS,
		.end	= UART2_MINT_RTS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device imx_uart2_device = {
	.name		= "imx-uart",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(imx_uart2_resources),
	.resource	= imx_uart2_resources,
};

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	[0] = {
		.chip.label = "gpio-0",
		.base = (void __iomem *)IO_ADDRESS(GPIO_BASE_ADDR),
		.irq = GPIO_INT_PORTA,
		.virtual_irq_start = MXC_MAX_INT_LINES
	},
	[1] = {
		.chip.label = "gpio-1",
		.base = (void __iomem *)IO_ADDRESS(GPIO_BASE_ADDR + 0x100),
		.irq = GPIO_INT_PORTB,
		.virtual_irq_start = MXC_MAX_INT_LINES + 32
	},
	[2] = {
		.chip.label = "gpio-2",
		.base = (void __iomem *)IO_ADDRESS(GPIO_BASE_ADDR + 0x200),
		.irq = GPIO_INT_PORTC,
		.virtual_irq_start = MXC_MAX_INT_LINES + 64
	},
	[3] = {
		.chip.label = "gpio-3",
		.base = (void __iomem *)IO_ADDRESS(GPIO_BASE_ADDR + 0x300),
		.irq = GPIO_INT_PORTD,
		.virtual_irq_start = MXC_MAX_INT_LINES + 96
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
