// SPDX-License-Identifier: GPL-2.0
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

#include "8250.h"

#define SERIAL8250_FOURPORT(_base, _irq) \
	SERIAL8250_PORT_FLAGS(_base, _irq, UPF_FOURPORT)

static struct plat_serial8250_port fourport_data[] = {
	SERIAL8250_FOURPORT(0x1a0, 9),
	SERIAL8250_FOURPORT(0x1a8, 9),
	SERIAL8250_FOURPORT(0x1b0, 9),
	SERIAL8250_FOURPORT(0x1b8, 9),
	SERIAL8250_FOURPORT(0x2a0, 5),
	SERIAL8250_FOURPORT(0x2a8, 5),
	SERIAL8250_FOURPORT(0x2b0, 5),
	SERIAL8250_FOURPORT(0x2b8, 5),
	{ },
};

static struct platform_device fourport_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_FOURPORT,
	.dev			= {
		.platform_data	= fourport_data,
	},
};

static int __init fourport_init(void)
{
	return platform_device_register(&fourport_device);
}

module_init(fourport_init);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("8250 serial probe module for AST Fourport cards");
MODULE_LICENSE("GPL");
