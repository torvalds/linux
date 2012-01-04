/* linux/arch/arm/plat-s3c24xx/dev-uart.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Base S3C24XX UART resource and platform device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <plat/devs.h>
#include <plat/regs-serial.h>

/* Serial port registrations */

static struct resource s3c2410_uart0_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART0,
		.end   = S3C2410_PA_UART0 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX0,
		.end   = IRQ_S3CUART_ERR0,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource s3c2410_uart1_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART1,
		.end   = S3C2410_PA_UART1 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX1,
		.end   = IRQ_S3CUART_ERR1,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource s3c2410_uart2_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART2,
		.end   = S3C2410_PA_UART2 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX2,
		.end   = IRQ_S3CUART_ERR2,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource s3c2410_uart3_resource[] = {
	[0] = {
		.start = S3C2443_PA_UART3,
		.end   = S3C2443_PA_UART3 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX3,
		.end   = IRQ_S3CUART_ERR3,
		.flags = IORESOURCE_IRQ,
	},
};

struct s3c24xx_uart_resources s3c2410_uart_resources[] __initdata = {
	[0] = {
		.resources	= s3c2410_uart0_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart0_resource),
	},
	[1] = {
		.resources	= s3c2410_uart1_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart1_resource),
	},
	[2] = {
		.resources	= s3c2410_uart2_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart2_resource),
	},
	[3] = {
		.resources	= s3c2410_uart3_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart3_resource),
	},
};
