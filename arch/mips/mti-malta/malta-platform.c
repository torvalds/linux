/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006, 07 MIPS Technologies, Inc.
 *   written by Ralf Baechle (ralf@linux-mips.org)
 *     written by Ralf Baechle <ralf@linux-mips.org>
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
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/mc146818rtc.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <asm/mips-boards/maltaint.h>
#include <mtd/mtd-abi.h>

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
#ifndef CONFIG_MIPS_CMP
	{
		.mapbase	= 0x1f000900,	/* The CBUS UART */
		.irq		= MIPS_CPU_IRQ_BASE + MIPSCPU_INT_MB2,
		.uartclk	= 3686400,	/* Twice the usual clk! */
		.iotype		= UPIO_MEM32,
		.flags		= CBUS_UART_FLAGS,
		.regshift	= 3,
	},
#endif
	{ },
};

static struct platform_device malta_uart8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
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

static struct mtd_partition malta_mtd_partitions[] = {
	{
		.name =		"YAMON",
		.offset =	0x0,
		.size =		0x100000,
		.mask_flags =	MTD_WRITEABLE
	}, {
		.name =		"User FS",
		.offset =	0x100000,
		.size =		0x2e0000
	}, {
		.name =		"Board Config",
		.offset =	0x3e0000,
		.size =		0x020000,
		.mask_flags =	MTD_WRITEABLE
	}
};

static struct physmap_flash_data malta_flash_data = {
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(malta_mtd_partitions),
	.parts		= malta_mtd_partitions
};

static struct resource malta_flash_resource = {
	.start		= 0x1e000000,
	.end		= 0x1e3fffff,
	.flags		= IORESOURCE_MEM
};

static struct platform_device malta_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &malta_flash_data,
	},
	.num_resources	= 1,
	.resource	= &malta_flash_resource,
};

static struct platform_device *malta_devices[] __initdata = {
	&malta_uart8250_device,
	&malta_rtc_device,
	&malta_flash_device,
};

static int __init malta_add_devices(void)
{
	int err;

	err = platform_add_devices(malta_devices, ARRAY_SIZE(malta_devices));
	if (err)
		return err;

	return 0;
}

device_initcall(malta_add_devices);
