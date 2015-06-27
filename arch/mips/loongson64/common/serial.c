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
#include <linux/module.h>
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

static struct plat_serial8250_port uart8250_data[][MAX_UARTS + 1] = {
	[MACH_LOONGSON_UNKNOWN]	= {},
	[MACH_LEMOTE_FL2E]	= {PORT(4, 1843200), {} },
	[MACH_LEMOTE_FL2F]	= {PORT(3, 1843200), {} },
	[MACH_LEMOTE_ML2F7]	= {PORT_M(3, 3686400), {} },
	[MACH_LEMOTE_YL2F89]	= {PORT_M(3, 3686400), {} },
	[MACH_DEXXON_GDIUM2F10]	= {PORT_M(3, 3686400), {} },
	[MACH_LEMOTE_NAS]	= {PORT_M(3, 3686400), {} },
	[MACH_LEMOTE_LL2F]	= {PORT(3, 1843200), {} },
	[MACH_LOONGSON_GENERIC]	= {PORT_M(2, 25000000), {} },
	[MACH_LOONGSON_END]	= {},
};

static struct platform_device uart8250_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
};

static int __init serial_init(void)
{
	int i;
	unsigned char iotype;

	iotype = uart8250_data[mips_machtype][0].iotype;

	if (UPIO_MEM == iotype) {
		uart8250_data[mips_machtype][0].mapbase =
			loongson_uart_base[0];
		uart8250_data[mips_machtype][0].membase =
			(void __iomem *)_loongson_uart_base[0];
	}
	else if (UPIO_PORT == iotype)
		uart8250_data[mips_machtype][0].iobase =
			loongson_uart_base[0] - LOONGSON_PCIIO_BASE;

	if (loongson_sysconf.uarts[0].uartclk)
		uart8250_data[mips_machtype][0].uartclk =
			loongson_sysconf.uarts[0].uartclk;

	for (i = 1; i < loongson_sysconf.nr_uarts; i++) {
		iotype = loongson_sysconf.uarts[i].iotype;
		uart8250_data[mips_machtype][i].iotype = iotype;
		loongson_uart_base[i] = loongson_sysconf.uarts[i].uart_base;

		if (UPIO_MEM == iotype) {
			uart8250_data[mips_machtype][i].irq =
				MIPS_CPU_IRQ_BASE + loongson_sysconf.uarts[i].int_offset;
			uart8250_data[mips_machtype][i].mapbase =
				loongson_uart_base[i];
			uart8250_data[mips_machtype][i].membase =
				ioremap_nocache(loongson_uart_base[i], 8);
		} else if (UPIO_PORT == iotype) {
			uart8250_data[mips_machtype][i].irq =
				loongson_sysconf.uarts[i].int_offset;
			uart8250_data[mips_machtype][i].iobase =
				loongson_uart_base[i] - LOONGSON_PCIIO_BASE;
		}

		uart8250_data[mips_machtype][i].uartclk =
			loongson_sysconf.uarts[i].uartclk;
		uart8250_data[mips_machtype][i].flags =
			UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	}

	memset(&uart8250_data[mips_machtype][loongson_sysconf.nr_uarts],
			0, sizeof(struct plat_serial8250_port));
	uart8250_device.dev.platform_data = uart8250_data[mips_machtype];

	return platform_device_register(&uart8250_device);
}
module_init(serial_init);

static void __init serial_exit(void)
{
	platform_device_unregister(&uart8250_device);
}
module_exit(serial_exit);
