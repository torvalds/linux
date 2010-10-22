/* arch/arm/mach-rk29/devices.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
 
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
 
 
/*
 * rk29 4 uarts device
 */
#ifdef CONFIG_UART0_RK29
static struct resource resources_uart0[] = {
	{
		.start	= IRQ_UART0,
		.end	= IRQ_UART0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART0_PHYS,
		.end	= RK29_UART0_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART1_RK29
static struct resource resources_uart1[] = {
	{
		.start	= IRQ_UART1,
		.end	= IRQ_UART1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART1_PHYS,
		.end	= RK29_UART1_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART2_RK29
static struct resource resources_uart2[] = {
	{
		.start	= IRQ_UART2,
		.end	= IRQ_UART2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART2_PHYS,
		.end	= RK29_UART2_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART3_RK29
static struct resource resources_uart3[] = {
	{
		.start	= IRQ_UART3,
		.end	= IRQ_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART3_PHYS,
		.end	= RK29_UART3_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART0_RK29
struct platform_device rk29_device_uart0 = {
	.name	= "rk29_serial",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_uart0),
	.resource	= resources_uart0,
	.dev = {
		.platform_data = &rk2818_serial0_platdata,
	},
};
#endif
#ifdef CONFIG_UART1_RK29
struct platform_device rk29_device_uart1 = {
	.name	= "rk29_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart1),
	.resource	= resources_uart1,
};
#endif
#ifdef CONFIG_UART2_RK29
struct platform_device rk29_device_uart2 = {
	.name	= "rk29_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
};
#endif
#ifdef CONFIG_UART3_RK29
struct platform_device rk29_device_uart3 = {
	.name	= "rk29_serial",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_uart3),
	.resource	= resources_uart3,
};
#endif