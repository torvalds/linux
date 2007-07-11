/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 * A NS16552 DUART with a 20MHz crystal.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serial_8250.h>

#define OCELOT_UART_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP)

static struct plat_serial8250_port uart8250_data[] = {
	{
		.mapbase	= 0xe0001020,
		.irq		= 4,
		.uartclk	= 20000000,
		.iotype		= UPIO_MEM,
		.flags		= OCELOT_UART_FLAGS,
		.regshift	= 2,
	},
	{ },
};

static struct platform_device uart8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= uart8250_data,
	},
};

static int __init uart8250_init(void)
{
	return platform_device_register(&uart8250_device);
}

module_init(uart8250_init);

MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("8250 UART probe driver for the Momenco Ocelot");
