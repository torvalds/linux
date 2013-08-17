/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Base EXYNOS UART resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <plat/devs.h>

#define EXYNOS_UART_RESOURCE(_series, _nr)	\
static struct resource exynos##_series##_uart##_nr##_resource[] = {	\
	[0] = DEFINE_RES_MEM(EXYNOS##_series##_PA_UART##_nr, EXYNOS##_series##_SZ_UART),	\
	[1] = DEFINE_RES_IRQ(EXYNOS##_series##_IRQ_UART##_nr),	\
};

EXYNOS_UART_RESOURCE(4, 0)
EXYNOS_UART_RESOURCE(4, 1)
EXYNOS_UART_RESOURCE(4, 2)
EXYNOS_UART_RESOURCE(4, 3)

struct s3c24xx_uart_resources exynos4_uart_resources[] __initdata = {
	[0] = {
		.resources	= exynos4_uart0_resource,
		.nr_resources	= ARRAY_SIZE(exynos4_uart0_resource),
	},
	[1] = {
		.resources	= exynos4_uart1_resource,
		.nr_resources	= ARRAY_SIZE(exynos4_uart1_resource),
	},
	[2] = {
		.resources	= exynos4_uart2_resource,
		.nr_resources	= ARRAY_SIZE(exynos4_uart2_resource),
	},
	[3] = {
		.resources	= exynos4_uart3_resource,
		.nr_resources	= ARRAY_SIZE(exynos4_uart3_resource),
	},
};

EXYNOS_UART_RESOURCE(5, 0)
EXYNOS_UART_RESOURCE(5, 1)
EXYNOS_UART_RESOURCE(5, 2)
EXYNOS_UART_RESOURCE(5, 3)

struct s3c24xx_uart_resources exynos5_uart_resources[] __initdata = {
	[0] = {
		.resources	= exynos5_uart0_resource,
		.nr_resources	= ARRAY_SIZE(exynos5_uart0_resource),
	},
	[1] = {
		.resources	= exynos5_uart1_resource,
		.nr_resources	= ARRAY_SIZE(exynos5_uart0_resource),
	},
	[2] = {
		.resources	= exynos5_uart2_resource,
		.nr_resources	= ARRAY_SIZE(exynos5_uart2_resource),
	},
	[3] = {
		.resources	= exynos5_uart3_resource,
		.nr_resources	= ARRAY_SIZE(exynos5_uart3_resource),
	},
};
