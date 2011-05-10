/*
 *  Atheros AR71XX/AR724X/AR913X common devices
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"
#include "dev-common.h"

static struct resource ath79_uart_resources[] = {
	{
		.start	= AR71XX_UART_BASE,
		.end	= AR71XX_UART_BASE + AR71XX_UART_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

#define AR71XX_UART_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP)
static struct plat_serial8250_port ath79_uart_data[] = {
	{
		.mapbase	= AR71XX_UART_BASE,
		.irq		= ATH79_MISC_IRQ_UART,
		.flags		= AR71XX_UART_FLAGS,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	}, {
		/* terminating entry */
	}
};

static struct platform_device ath79_uart_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.resource	= ath79_uart_resources,
	.num_resources	= ARRAY_SIZE(ath79_uart_resources),
	.dev = {
		.platform_data	= ath79_uart_data
	},
};

void __init ath79_register_uart(void)
{
	struct clk *clk;

	clk = clk_get(NULL, "uart");
	if (IS_ERR(clk))
		panic("unable to get UART clock, err=%ld", PTR_ERR(clk));

	ath79_uart_data[0].uartclk = clk_get_rate(clk);
	platform_device_register(&ath79_uart_device);
}

static struct platform_device ath79_wdt_device = {
	.name		= "ath79-wdt",
	.id		= -1,
};

void __init ath79_register_wdt(void)
{
	platform_device_register(&ath79_wdt_device);
}
