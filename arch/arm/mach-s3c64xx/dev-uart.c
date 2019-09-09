// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2008 Openmoko, Inc.
// Copyright 2008 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//	http://armlinux.simtec.co.uk/
//
// Base S3C64XX UART resource and device definitions

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/irqs.h>

#include <plat/devs.h>

/* Serial port registrations */

/* 64xx uarts are closer together */

static struct resource s3c64xx_uart0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_UART0, SZ_256),
	[1] = DEFINE_RES_IRQ(IRQ_UART0),
};

static struct resource s3c64xx_uart1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_UART1, SZ_256),
	[1] = DEFINE_RES_IRQ(IRQ_UART1),
};

static struct resource s3c6xx_uart2_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_UART2, SZ_256),
	[1] = DEFINE_RES_IRQ(IRQ_UART2),
};

static struct resource s3c64xx_uart3_resource[] = {
	[0] = DEFINE_RES_MEM(S3C_PA_UART3, SZ_256),
	[1] = DEFINE_RES_IRQ(IRQ_UART3),
};


struct s3c24xx_uart_resources s3c64xx_uart_resources[] __initdata = {
	[0] = {
		.resources	= s3c64xx_uart0_resource,
		.nr_resources	= ARRAY_SIZE(s3c64xx_uart0_resource),
	},
	[1] = {
		.resources	= s3c64xx_uart1_resource,
		.nr_resources	= ARRAY_SIZE(s3c64xx_uart1_resource),
	},
	[2] = {
		.resources	= s3c6xx_uart2_resource,
		.nr_resources	= ARRAY_SIZE(s3c6xx_uart2_resource),
	},
	[3] = {
		.resources	= s3c64xx_uart3_resource,
		.nr_resources	= ARRAY_SIZE(s3c64xx_uart3_resource),
	},
};
