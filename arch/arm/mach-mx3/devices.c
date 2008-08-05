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
#include <mach/imx-uart.h>

static struct resource uart0[] = {
	{
		.start = UART1_BASE_ADDR,
		.end = UART1_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART1,
		.end = MXC_INT_UART1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device mxc_uart_device0 = {
	.name = "imx-uart",
	.id = 0,
	.resource = uart0,
	.num_resources = ARRAY_SIZE(uart0),
};

static struct resource uart1[] = {
	{
		.start = UART2_BASE_ADDR,
		.end = UART2_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART2,
		.end = MXC_INT_UART2,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device mxc_uart_device1 = {
	.name = "imx-uart",
	.id = 1,
	.resource = uart1,
	.num_resources = ARRAY_SIZE(uart1),
};

static struct resource uart2[] = {
	{
		.start = UART3_BASE_ADDR,
		.end = UART3_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART3,
		.end = MXC_INT_UART3,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device mxc_uart_device2 = {
	.name = "imx-uart",
	.id = 2,
	.resource = uart2,
	.num_resources = ARRAY_SIZE(uart2),
};

static struct resource uart3[] = {
	{
		.start = UART4_BASE_ADDR,
		.end = UART4_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART4,
		.end = MXC_INT_UART4,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device mxc_uart_device3 = {
	.name = "imx-uart",
	.id = 3,
	.resource = uart3,
	.num_resources = ARRAY_SIZE(uart3),
};

static struct resource uart4[] = {
	{
		.start = UART5_BASE_ADDR,
		.end = UART5_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART5,
		.end = MXC_INT_UART5,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device mxc_uart_device4 = {
	.name = "imx-uart",
	.id = 4,
	.resource = uart4,
	.num_resources = ARRAY_SIZE(uart4),
};

/*
 * Register only those UARTs that physically exist
 */
int __init imx_init_uart(int uart_no, struct imxuart_platform_data *pdata)
{
	switch (uart_no) {
	case 0:
		mxc_uart_device0.dev.platform_data = pdata;
		platform_device_register(&mxc_uart_device0);
		break;
	case 1:
		mxc_uart_device1.dev.platform_data = pdata;
		platform_device_register(&mxc_uart_device1);
		break;
	case 2:
		mxc_uart_device2.dev.platform_data = pdata;
		platform_device_register(&mxc_uart_device2);
		break;
	case 3:
		mxc_uart_device3.dev.platform_data = pdata;
		platform_device_register(&mxc_uart_device3);
		break;
	case 4:
		mxc_uart_device4.dev.platform_data = pdata;
		platform_device_register(&mxc_uart_device4);
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	[0] = {
		.chip.label = "gpio-0",
		.base = IO_ADDRESS(GPIO1_BASE_ADDR),
		.irq = MXC_INT_GPIO1,
		.virtual_irq_start = MXC_GPIO_INT_BASE
	},
	[1] = {
		.chip.label = "gpio-1",
		.base = IO_ADDRESS(GPIO2_BASE_ADDR),
		.irq = MXC_INT_GPIO2,
		.virtual_irq_start = MXC_GPIO_INT_BASE + GPIO_NUM_PIN
	},
	[2] = {
		.chip.label = "gpio-2",
		.base = IO_ADDRESS(GPIO3_BASE_ADDR),
		.irq = MXC_INT_GPIO3,
		.virtual_irq_start = MXC_GPIO_INT_BASE + GPIO_NUM_PIN * 2
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
