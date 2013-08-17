/*
 *  Copyright (C) 2005 Russell King.
 *  Data taken from include/asm-i386/serial.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mca.h>
#include <linux/serial_8250.h>

/*
 * FIXME: Should we be doing AUTO_IRQ here?
 */
#ifdef CONFIG_SERIAL_8250_DETECT_IRQ
#define MCA_FLAGS	UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ
#else
#define MCA_FLAGS	UPF_BOOT_AUTOCONF | UPF_SKIP_TEST
#endif

#define PORT(_base,_irq)			\
	{					\
		.iobase		= _base,	\
		.irq		= _irq,		\
		.uartclk	= 1843200,	\
		.iotype		= UPIO_PORT,	\
		.flags		= MCA_FLAGS,	\
	}

static struct plat_serial8250_port mca_data[] = {
	PORT(0x3220, 3),
	PORT(0x3228, 3),
	PORT(0x4220, 3),
	PORT(0x4228, 3),
	PORT(0x5220, 3),
	PORT(0x5228, 3),
	{ },
};

static struct platform_device mca_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_MCA,
	.dev			= {
		.platform_data	= mca_data,
	},
};

static int __init mca_init(void)
{
	if (!MCA_bus)
		return -ENODEV;
	return platform_device_register(&mca_device);
}

module_init(mca_init);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("8250 serial probe module for MCA ports");
MODULE_LICENSE("GPL");
