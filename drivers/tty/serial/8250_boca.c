/*
 *  linux/drivers/serial/8250_boca.c
 *
 *  Copyright (C) 2005 Russell King.
 *  Data taken from include/asm-i386/serial.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serial_8250.h>

#define PORT(_base,_irq)				\
	{						\
		.iobase		= _base,		\
		.irq		= _irq,			\
		.uartclk	= 1843200,		\
		.iotype		= UPIO_PORT,		\
		.flags		= UPF_BOOT_AUTOCONF,	\
	}

static struct plat_serial8250_port boca_data[] = {
	PORT(0x100, 12),
	PORT(0x108, 12),
	PORT(0x110, 12),
	PORT(0x118, 12),
	PORT(0x120, 12),
	PORT(0x128, 12),
	PORT(0x130, 12),
	PORT(0x138, 12),
	PORT(0x140, 12),
	PORT(0x148, 12),
	PORT(0x150, 12),
	PORT(0x158, 12),
	PORT(0x160, 12),
	PORT(0x168, 12),
	PORT(0x170, 12),
	PORT(0x178, 12),
	{ },
};

static struct platform_device boca_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_BOCA,
	.dev			= {
		.platform_data	= boca_data,
	},
};

static int __init boca_init(void)
{
	return platform_device_register(&boca_device);
}

module_init(boca_init);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("8250 serial probe module for Boca cards");
MODULE_LICENSE("GPL");
