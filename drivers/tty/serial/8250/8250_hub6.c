/*
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

#define HUB6(card,port)							\
	{								\
		.iobase		= 0x302,				\
		.irq		= 3,					\
		.uartclk	= 1843200,				\
		.iotype		= UPIO_HUB6,				\
		.flags		= UPF_BOOT_AUTOCONF,			\
		.hub6		= (card) << 6 | (port) << 3 | 1,	\
	}

static struct plat_serial8250_port hub6_data[] = {
	HUB6(0, 0),
	HUB6(0, 1),
	HUB6(0, 2),
	HUB6(0, 3),
	HUB6(0, 4),
	HUB6(0, 5),
	HUB6(1, 0),
	HUB6(1, 1),
	HUB6(1, 2),
	HUB6(1, 3),
	HUB6(1, 4),
	HUB6(1, 5),
	{ },
};

static struct platform_device hub6_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_HUB6,
	.dev			= {
		.platform_data	= hub6_data,
	},
};

static int __init hub6_init(void)
{
	return platform_device_register(&hub6_device);
}

module_init(hub6_init);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("8250 serial probe module for Hub6 cards");
MODULE_LICENSE("GPL");
