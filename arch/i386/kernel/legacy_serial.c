/*
 * Legacy COM port devices for x86 platforms without PNPBIOS or ACPI.
 * Data taken from include/asm-i386/serial.h.
 *
 * (c) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pnp.h>
#include <linux/serial_8250.h>

/* Standard COM flags (except for COM4, because of the 8514 problem) */
#ifdef CONFIG_SERIAL_DETECT_IRQ
#define COM_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ)
#define COM4_FLAGS (UPF_BOOT_AUTOCONF | UPF_AUTO_IRQ)
#else
#define COM_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST)
#define COM4_FLAGS UPF_BOOT_AUTOCONF
#endif

#define PORT(_base,_irq,_flags)				\
	{						\
		.iobase		= _base,		\
		.irq		= _irq,			\
		.uartclk	= 1843200,		\
		.iotype		= UPIO_PORT,		\
		.flags		= _flags,		\
	}

static struct plat_serial8250_port x86_com_data[] = {
	PORT(0x3F8, 4, COM_FLAGS),
	PORT(0x2F8, 3, COM_FLAGS),
	PORT(0x3E8, 4, COM_FLAGS),
	PORT(0x2E8, 3, COM4_FLAGS),
	{ },
};

static struct platform_device x86_com_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= x86_com_data,
	},
};

static int force_legacy_probe;
module_param_named(force, force_legacy_probe, bool, 0);
MODULE_PARM_DESC(force, "Force legacy serial port probe");

static int __init serial8250_x86_com_init(void)
{
	if (pnp_platform_devices && !force_legacy_probe)
		return -ENODEV;

	return platform_device_register(&x86_com_device);
}

module_init(serial8250_x86_com_init);

MODULE_AUTHOR("Bjorn Helgaas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic 8250/16x50 legacy probe module");
