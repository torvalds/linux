/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/serial_8250.h>

#include <asm/ddb5xxx/ddb5477.h>

#define DDB_UART_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP)

#define DDB5477_PORT(base, int)						\
{									\
	.mapbase	= base,						\
	.irq		= int,						\
	.uartclk	= 1843200,					\
	.iotype		= UPIO_MEM,					\
	.flags		= DDB_UART_FLAGS,				\
	.regshift	= 3,						\
}

static struct plat_serial8250_port uart8250_data[] = {
	DDB5477_PORT(0xbfa04200, VRC5477_IRQ_UART0),
	DDB5477_PORT(0xbfa04240, VRC5477_IRQ_UART1),
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
MODULE_DESCRIPTION("8250 UART probe driver for the NEC DDB5477");
