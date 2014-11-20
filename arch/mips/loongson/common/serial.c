/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Yan hua (yanhua@lemote.com)
 * Author: Wu Zhangjin (wuzhangjin@gmail.com)
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/serial_8250.h>

#include <asm/bootinfo.h>

#include <loongson.h>
#include <machine.h>

#define PORT(int, clk)			\
{								\
	.irq		= int,					\
	.uartclk	= clk,					\
	.iotype		= UPIO_PORT,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,	\
	.regshift	= 0,					\
}

#define PORT_M(int, clk)				\
{								\
	.irq		= MIPS_CPU_IRQ_BASE + (int),		\
	.uartclk	= clk,					\
	.iotype		= UPIO_MEM,				\
	.membase	= (void __iomem *)NULL,			\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,	\
	.regshift	= 0,					\
}

static struct plat_serial8250_port uart8250_data[][2] = {
	[MACH_LOONGSON_UNKNOWN]		{},
	[MACH_LEMOTE_FL2E]              {PORT(4, 1843200), {} },
	[MACH_LEMOTE_FL2F]              {PORT(3, 1843200), {} },
	[MACH_LEMOTE_ML2F7]             {PORT_M(3, 3686400), {} },
	[MACH_LEMOTE_YL2F89]            {PORT_M(3, 3686400), {} },
	[MACH_DEXXON_GDIUM2F10]         {PORT_M(3, 3686400), {} },
	[MACH_LEMOTE_NAS]               {PORT_M(3, 3686400), {} },
	[MACH_LEMOTE_LL2F]              {PORT(3, 1843200), {} },
	[MACH_LEMOTE_A1004]             {PORT_M(2, 33177600), {} },
	[MACH_LEMOTE_A1101]             {PORT_M(2, 25000000), {} },
	[MACH_LEMOTE_A1201]             {PORT_M(2, 25000000), {} },
	[MACH_LEMOTE_A1205]             {PORT_M(2, 25000000), {} },
	[MACH_LOONGSON_END]		{},
};

static struct platform_device uart8250_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
};

static int __init serial_init(void)
{
	unsigned char iotype;

	iotype = uart8250_data[mips_machtype][0].iotype;

	if (UPIO_MEM == iotype)
		uart8250_data[mips_machtype][0].membase =
			(void __iomem *)_loongson_uart_base;
	else if (UPIO_PORT == iotype)
		uart8250_data[mips_machtype][0].iobase =
		    loongson_uart_base - LOONGSON_PCIIO_BASE;

	uart8250_device.dev.platform_data = uart8250_data[mips_machtype];

	return platform_device_register(&uart8250_device);
}

device_initcall(serial_init);
