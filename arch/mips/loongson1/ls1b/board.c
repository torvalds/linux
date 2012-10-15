/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <platform.h>

#include <linux/serial_8250.h>
#include <loongson1.h>

static struct platform_device *ls1b_platform_devices[] __initdata = {
	&ls1x_uart_device,
	&ls1x_eth0_device,
	&ls1x_ehci_device,
	&ls1x_rtc_device,
};

static int __init ls1b_platform_init(void)
{
	int err;

	ls1x_serial_setup();

	err = platform_add_devices(ls1b_platform_devices,
				   ARRAY_SIZE(ls1b_platform_devices));
	return err;
}

arch_initcall(ls1b_platform_init);
