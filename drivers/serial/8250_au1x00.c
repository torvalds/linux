/*
 * Serial Device Initialisation for Au1x00
 *
 * (C) Copyright Embedded Alley Solutions, Inc 2005
 * Author: Pantelis Antoniou <pantelis@embeddedalley.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/serial_8250.h>

#include <asm/mach-au1x00/au1000.h>

#include "8250.h"

#define PORT(_base, _irq)				\
	{						\
		.iobase		= _base,		\
		.membase	= (void __iomem *)_base,\
		.mapbase	= _base,		\
		.irq		= _irq,			\
		.uartclk	= 0,	/* filled */	\
		.regshift	= 2,			\
		.iotype		= UPIO_AU,		\
		.flags		= UPF_SKIP_TEST | 	\
				  UPF_IOREMAP,		\
	}

static struct plat_serial8250_port au1x00_data[] = {
#if defined(CONFIG_SOC_AU1000)
	PORT(UART0_ADDR, AU1000_UART0_INT),
	PORT(UART1_ADDR, AU1000_UART1_INT),
	PORT(UART2_ADDR, AU1000_UART2_INT),
	PORT(UART3_ADDR, AU1000_UART3_INT),
#elif defined(CONFIG_SOC_AU1500)
	PORT(UART0_ADDR, AU1500_UART0_INT),
	PORT(UART3_ADDR, AU1500_UART3_INT),
#elif defined(CONFIG_SOC_AU1100)
	PORT(UART0_ADDR, AU1100_UART0_INT),
	PORT(UART1_ADDR, AU1100_UART1_INT),
	/* The internal UART2 does not exist on the AU1100 processor. */
	PORT(UART3_ADDR, AU1100_UART3_INT),
#elif defined(CONFIG_SOC_AU1550)
	PORT(UART0_ADDR, AU1550_UART0_INT),
	PORT(UART1_ADDR, AU1550_UART1_INT),
	PORT(UART3_ADDR, AU1550_UART3_INT),
#elif defined(CONFIG_SOC_AU1200)
	PORT(UART0_ADDR, AU1200_UART0_INT),
	PORT(UART1_ADDR, AU1200_UART1_INT),
#endif
	{ },
};

static struct platform_device au1x00_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_AU1X00,
	.dev			= {
		.platform_data	= au1x00_data,
	},
};

static int __init au1x00_init(void)
{
	int i;
	unsigned int uartclk;

	/* get uart clock */
	uartclk = get_au1x00_uart_baud_base() * 16;

	/* fill up uartclk */
	for (i = 0; au1x00_data[i].flags ; i++)
		au1x00_data[i].uartclk = uartclk;

	return platform_device_register(&au1x00_device);
}

/* XXX: Yes, I know this doesn't yet work. */
static void __exit au1x00_exit(void)
{
	platform_device_unregister(&au1x00_device);
}

module_init(au1x00_init);
module_exit(au1x00_exit);

MODULE_AUTHOR("Pantelis Antoniou <pantelis@embeddedalley.com>");
MODULE_DESCRIPTION("8250 serial probe module for Au1x000 cards");
MODULE_LICENSE("GPL");
