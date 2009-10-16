/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Yan hua (yanhua@lemote.com)
 * Author: Wu Zhangjin (wuzj@lemote.com)
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/serial_8250.h>

#include <asm/bootinfo.h>

#include <loongson.h>
#include <machine.h>

#define PORT(int)			\
{								\
	.irq		= int,					\
	.uartclk	= 1843200,				\
	.iobase		= (LOONGSON_UART_BASE - LOONGSON_PCIIO_BASE),\
	.iotype		= UPIO_PORT,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,	\
	.regshift	= 0,					\
}

#define PORT_M(int)				\
{								\
	.irq		= MIPS_CPU_IRQ_BASE + (int),		\
	.uartclk	= 3686400,				\
	.iotype		= UPIO_MEM,				\
	.membase	= (void __iomem *)NULL,			\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,	\
	.regshift	= 0,					\
}

static struct plat_serial8250_port uart8250_data[][2] = {
	[MACH_LOONGSON_UNKNOWN]         {},
	[MACH_LEMOTE_FL2E]              {PORT(4), {} },
	[MACH_LEMOTE_FL2F]              {PORT(3), {} },
	[MACH_LEMOTE_ML2F7]             {PORT_M(3), {} },
	[MACH_LEMOTE_YL2F89]            {PORT_M(3), {} },
	[MACH_DEXXON_GDIUM2F10]         {PORT_M(3), {} },
	[MACH_LOONGSON_END]             {},
};

static struct platform_device uart8250_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = uart8250_data[LOONGSON_MACHTYPE],
		},
};

static int __init serial_init(void)
{
	if (uart8250_data[LOONGSON_MACHTYPE][0].iotype == UPIO_MEM)
		uart8250_data[LOONGSON_MACHTYPE][0].membase =
			ioremap_nocache(LOONGSON_UART_BASE, 8);

	platform_device_register(&uart8250_device);

	return 0;
}

device_initcall(serial_init);
