// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-footbridge/isa.c
 *
 *  Copyright (C) 2004 Russell King.
 */
#include <linux/init.h>
#include <linux/serial_8250.h>

#include <asm/irq.h>
#include <asm/hardware/dec21285.h>

#include "common.h"

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0x70,
		.end	= 0x73,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.start	= IRQ_ISA_RTC_ALARM,
		.end	= IRQ_ISA_RTC_ALARM,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device rtc_device = {
	.name		= "rtc_cmos",
	.id		= -1,
	.resource	= rtc_resources,
	.num_resources	= ARRAY_SIZE(rtc_resources),
};

static struct resource serial_resources[] = {
	[0] = {
		.start	= 0x3f8,
		.end	= 0x3ff,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.start	= 0x2f8,
		.end	= 0x2ff,
		.flags	= IORESOURCE_IO,
	},
};

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.iobase		= 0x3f8,
		.irq		= IRQ_ISA_UART,
		.uartclk	= 1843200,
		.regshift	= 0,
		.iotype		= UPIO_PORT,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{
		.iobase		= 0x2f8,
		.irq		= IRQ_ISA_UART2,
		.uartclk	= 1843200,
		.regshift	= 0,
		.iotype		= UPIO_PORT,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
	.resource		= serial_resources,
	.num_resources		= ARRAY_SIZE(serial_resources),
};

static int __init footbridge_isa_init(void)
{
	int err = 0;

	/* Personal server doesn't have RTC */
	isa_rtc_init();
	err = platform_device_register(&rtc_device);
	if (err)
		printk(KERN_ERR "Unable to register RTC device: %d\n", err);

	err = platform_device_register(&serial_device);
	if (err)
		printk(KERN_ERR "Unable to register serial device: %d\n", err);
	return 0;
}

arch_initcall(footbridge_isa_init);
