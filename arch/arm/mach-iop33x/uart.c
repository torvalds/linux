/*
 * arch/arm/mach-iop33x/uart.c
 *
 * Author: Dave Jiang (dave.jiang@intel.com)
 * Copyright (C) 2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/hardware/iop3xx.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#define IOP33X_UART_XTAL 33334000

static struct plat_serial8250_port iop33x_uart0_data[] = {
	{
		.membase	= (char *)IOP33X_UART0_VIRT,
		.mapbase	= IOP33X_UART0_PHYS,
		.irq		= IRQ_IOP33X_UART0,
		.uartclk	= IOP33X_UART_XTAL,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_SKIP_TEST,
	},
	{ },
};

static struct resource iop33x_uart0_resources[] = {
	[0] = {
		.start	= IOP33X_UART0_PHYS,
		.end	= IOP33X_UART0_PHYS + 0x3f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_IOP33X_UART0,
		.end	= IRQ_IOP33X_UART0,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device iop33x_uart0_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= iop33x_uart0_data,
	},
	.num_resources	= 2,
	.resource	= iop33x_uart0_resources,
};


static struct resource iop33x_uart1_resources[] = {
	[0] = {
		.start	= IOP33X_UART1_PHYS,
		.end	= IOP33X_UART1_PHYS + 0x3f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_IOP33X_UART1,
		.end	= IRQ_IOP33X_UART1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct plat_serial8250_port iop33x_uart1_data[] = {
	{
		.membase	= (char *)IOP33X_UART1_VIRT,
		.mapbase	= IOP33X_UART1_PHYS,
		.irq		= IRQ_IOP33X_UART1,
		.uartclk	= IOP33X_UART_XTAL,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_SKIP_TEST,
	},
	{ },
};

struct platform_device iop33x_uart1_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM1,
	.dev		= {
		.platform_data		= iop33x_uart1_data,
	},
	.num_resources	= 2,
	.resource	= iop33x_uart1_resources,
};
