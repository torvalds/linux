/*
 * Copyright (c) 2009,2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Base S5P UART resource and device definitions
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

 /* Serial port registrations */

static struct resource s5p_uart0_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_UART0, S5P_SZ_UART),
	[1] = DEFINE_RES_IRQ(IRQ_UART0),
};

static struct resource s5p_uart1_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_UART1, S5P_SZ_UART),
	[1] = DEFINE_RES_IRQ(IRQ_UART1),
};

static struct resource s5p_uart2_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_UART2, S5P_SZ_UART),
	[1] = DEFINE_RES_IRQ(IRQ_UART2),
};

static struct resource s5p_uart3_resource[] = {
#if CONFIG_SERIAL_SAMSUNG_UARTS > 3
	[0] = DEFINE_RES_MEM(S5P_PA_UART3, S5P_SZ_UART),
	[1] = DEFINE_RES_IRQ(IRQ_UART3),
#endif
};

static struct resource s5p_uart4_resource[] = {
#if CONFIG_SERIAL_SAMSUNG_UARTS > 4
	[0] = DEFINE_RES_MEM(S5P_PA_UART4, S5P_SZ_UART),
	[1] = DEFINE_RES_IRQ(IRQ_UART4),
#endif
};

static struct resource s5p_uart5_resource[] = {
#if CONFIG_SERIAL_SAMSUNG_UARTS > 5
	[0] = DEFINE_RES_MEM(S5P_PA_UART5, S5P_SZ_UART),
	[1] = DEFINE_RES_IRQ(IRQ_UART5),
#endif
};

struct s3c24xx_uart_resources s5p_uart_resources[] __initdata = {
	[0] = {
		.resources	= s5p_uart0_resource,
		.nr_resources	= ARRAY_SIZE(s5p_uart0_resource),
	},
	[1] = {
		.resources	= s5p_uart1_resource,
		.nr_resources	= ARRAY_SIZE(s5p_uart1_resource),
	},
	[2] = {
		.resources	= s5p_uart2_resource,
		.nr_resources	= ARRAY_SIZE(s5p_uart2_resource),
	},
	[3] = {
		.resources	= s5p_uart3_resource,
		.nr_resources	= ARRAY_SIZE(s5p_uart3_resource),
	},
	[4] = {
		.resources	= s5p_uart4_resource,
		.nr_resources	= ARRAY_SIZE(s5p_uart4_resource),
	},
	[5] = {
		.resources	= s5p_uart5_resource,
		.nr_resources	= ARRAY_SIZE(s5p_uart5_resource),
	},
};
