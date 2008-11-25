/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 MIPS Technologies, Inc.
 *   written by Ralf Baechle (ralf@linux-mips.org)
 *
 * Copyright (C) 2008 Wind River Systems, Inc.
 *   updated by Tiejun Chen <tiejun.chen@windriver.com>
 *
 * 1. Probe driver for the Malta's UART ports:
 *
 *   o 2 ports in the SMC SuperIO
 *   o 1 port in the CBUS UART, a discrete 16550 which normally is only used
 *     for bringups.
 *
 * We don't use 8250_platform.c on Malta as it would result in the CBUS
 * UART becoming ttyS0.
 *
 * 2. Register RTC-CMOS platform device on Malta.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/mc146818rtc.h>
#include <linux/platform_device.h>

#define SMC_PORT(base, int)						\
{									\
	.iobase		= base,						\
	.irq		= int,						\
	.uartclk	= 1843200,					\
	.iotype		= UPIO_PORT,					\
	.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,		\
	.regshift	= 0,						\
}

#define CBUS_UART_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP)

static struct plat_serial8250_port uart8250_data[] = {
	SMC_PORT(0x3F8, 4),
	SMC_PORT(0x2F8, 3),
	{
		.mapbase	= 0x1f000900,	/* The CBUS UART */
		.irq		= MIPS_CPU_IRQ_BASE + 2,
		.uartclk	= 3686400,	/* Twice the usual clk! */
		.iotype		= UPIO_MEM32,
		.flags		= CBUS_UART_FLAGS,
		.regshift	= 3,
	},
	{ },
};

static struct platform_device malta_uart8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM2,
	.dev			= {
		.platform_data	= uart8250_data,
	},
};

struct resource malta_rtc_resources[] = {
	{
		.start	= RTC_PORT(0),
		.end	= RTC_PORT(7),
		.flags	= IORESOURCE_IO,
	}, {
		.start	= RTC_IRQ,
		.end	= RTC_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device malta_rtc_device = {
	.name		= "rtc_cmos",
	.id		= -1,
	.resource	= malta_rtc_resources,
	.num_resources	= ARRAY_SIZE(malta_rtc_resources),
};

static struct platform_device *malta_devices[] __initdata = {
	&malta_uart8250_device,
	&malta_rtc_device,
};

static int __init malta_add_devices(void)
{
	int err;

	err = platform_add_devices(malta_devices, ARRAY_SIZE(malta_devices));
	if (err)
		return err;

	/*
	 * Set RTC to BCD mode to support current alarm code.
	 */
	CMOS_WRITE(CMOS_READ(RTC_CONTROL) & ~RTC_DM_BINARY, RTC_CONTROL);

	return 0;
}

device_initcall(malta_add_devices);
