/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#ifndef __ASM_MACH_LOONGSON1_PLATFORM_H
#define __ASM_MACH_LOONGSON1_PLATFORM_H

#include <linux/platform_device.h>

extern struct platform_device ls1x_uart_device;
extern struct platform_device ls1x_eth0_device;
extern struct platform_device ls1x_ehci_device;
extern struct platform_device ls1x_rtc_device;

extern void __init ls1x_clk_init(void);
extern void __init ls1x_serial_setup(struct platform_device *pdev);

#endif /* __ASM_MACH_LOONGSON1_PLATFORM_H */
