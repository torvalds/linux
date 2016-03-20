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
#include <linux/platform_data/gpio-ath79.h>
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
		.irq		= ATH79_MISC_IRQ(3),
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

static struct resource ar933x_uart_resources[] = {
	{
		.start	= AR933X_UART_BASE,
		.end	= AR933X_UART_BASE + AR71XX_UART_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= ATH79_MISC_IRQ(3),
		.end	= ATH79_MISC_IRQ(3),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ar933x_uart_device = {
	.name		= "ar933x-uart",
	.id		= -1,
	.resource	= ar933x_uart_resources,
	.num_resources	= ARRAY_SIZE(ar933x_uart_resources),
};

void __init ath79_register_uart(void)
{
	unsigned long uart_clk_rate;

	uart_clk_rate = ath79_get_sys_clk_rate("uart");

	if (soc_is_ar71xx() ||
	    soc_is_ar724x() ||
	    soc_is_ar913x() ||
	    soc_is_ar934x() ||
	    soc_is_qca955x()) {
		ath79_uart_data[0].uartclk = uart_clk_rate;
		platform_device_register(&ath79_uart_device);
	} else if (soc_is_ar933x()) {
		platform_device_register(&ar933x_uart_device);
	} else {
		BUG();
	}
}

void __init ath79_register_wdt(void)
{
	struct resource res;

	memset(&res, 0, sizeof(res));

	res.flags = IORESOURCE_MEM;
	res.start = AR71XX_RESET_BASE + AR71XX_RESET_REG_WDOG_CTRL;
	res.end = res.start + 0x8 - 1;

	platform_device_register_simple("ath79-wdt", -1, &res, 1);
}

static struct ath79_gpio_platform_data ath79_gpio_pdata;

static struct resource ath79_gpio_resources[] = {
	{
		.flags = IORESOURCE_MEM,
		.start = AR71XX_GPIO_BASE,
		.end = AR71XX_GPIO_BASE + AR71XX_GPIO_SIZE - 1,
	},
	{
		.start	= ATH79_MISC_IRQ(2),
		.end	= ATH79_MISC_IRQ(2),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ath79_gpio_device = {
	.name		= "ath79-gpio",
	.id		= -1,
	.resource	= ath79_gpio_resources,
	.num_resources	= ARRAY_SIZE(ath79_gpio_resources),
	.dev = {
		.platform_data	= &ath79_gpio_pdata
	},
};

void __init ath79_gpio_init(void)
{
	if (soc_is_ar71xx()) {
		ath79_gpio_pdata.ngpios = AR71XX_GPIO_COUNT;
	} else if (soc_is_ar7240()) {
		ath79_gpio_pdata.ngpios = AR7240_GPIO_COUNT;
	} else if (soc_is_ar7241() || soc_is_ar7242()) {
		ath79_gpio_pdata.ngpios = AR7241_GPIO_COUNT;
	} else if (soc_is_ar913x()) {
		ath79_gpio_pdata.ngpios = AR913X_GPIO_COUNT;
	} else if (soc_is_ar933x()) {
		ath79_gpio_pdata.ngpios = AR933X_GPIO_COUNT;
	} else if (soc_is_ar934x()) {
		ath79_gpio_pdata.ngpios = AR934X_GPIO_COUNT;
		ath79_gpio_pdata.oe_inverted = 1;
	} else if (soc_is_qca955x()) {
		ath79_gpio_pdata.ngpios = QCA955X_GPIO_COUNT;
		ath79_gpio_pdata.oe_inverted = 1;
	} else {
		BUG();
	}

	platform_device_register(&ath79_gpio_device);
}
