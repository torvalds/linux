/*
 * Copyright (c) 2016 Yang Ling <gnaygnil@gmail.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <platform.h>

static struct platform_device *ls1c_platform_devices[] __initdata = {
	&ls1x_uart_pdev,
	&ls1x_eth0_pdev,
	&ls1x_rtc_pdev,
};

static int __init ls1c_platform_init(void)
{
	ls1x_serial_set_uartclk(&ls1x_uart_pdev);
	ls1x_rtc_set_extclk(&ls1x_rtc_pdev);

	return platform_add_devices(ls1c_platform_devices,
				   ARRAY_SIZE(ls1c_platform_devices));
}

arch_initcall(ls1c_platform_init);
