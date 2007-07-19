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

#include <asm/jazz.h>

/*
 * Confusion ...  It seems the original Microsoft Jazz machine used to have a
 * 4.096MHz clock for its UART while the MIPS Magnum and Millenium systems
 * had 8MHz.  The Olivetti M700-10 and the Acer PICA have 1.8432MHz like PCs.
 */
#ifdef CONFIG_OLIVETTI_M700
#define JAZZ_BASE_BAUD 1843200
#else
#define JAZZ_BASE_BAUD	8000000	/* 3072000 */
#endif

#define JAZZ_UART_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP)

#define JAZZ_PORT(base, int)						\
{									\
	.mapbase	= base,						\
	.irq		= int,						\
	.uartclk	= JAZZ_BASE_BAUD,				\
	.iotype		= UPIO_MEM,					\
	.flags		= JAZZ_UART_FLAGS,				\
	.regshift	= 0,						\
}

static struct plat_serial8250_port uart8250_data[] = {
	JAZZ_PORT(JAZZ_SERIAL1_BASE, JAZZ_SERIAL1_IRQ),
	JAZZ_PORT(JAZZ_SERIAL2_BASE, JAZZ_SERIAL2_IRQ),
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
MODULE_DESCRIPTION("8250 UART probe driver for the Jazz family");
