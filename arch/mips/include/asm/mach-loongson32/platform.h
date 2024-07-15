/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#ifndef __ASM_MACH_LOONGSON32_PLATFORM_H
#define __ASM_MACH_LOONGSON32_PLATFORM_H

#include <linux/platform_device.h>

extern struct platform_device ls1x_uart_pdev;
extern struct platform_device ls1x_eth0_pdev;
extern struct platform_device ls1x_eth1_pdev;
extern struct platform_device ls1x_ehci_pdev;
extern struct platform_device ls1x_gpio0_pdev;
extern struct platform_device ls1x_gpio1_pdev;
extern struct platform_device ls1x_rtc_pdev;
extern struct platform_device ls1x_wdt_pdev;

void __init ls1x_rtc_set_extclk(struct platform_device *pdev);
void __init ls1x_serial_set_uartclk(struct platform_device *pdev);

#endif /* __ASM_MACH_LOONGSON32_PLATFORM_H */
