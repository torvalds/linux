// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/leds.h>
#include <linux/mtd/partitions.h>
#include <linux/sizes.h>

#include <loongson1.h>
#include <platform.h>

static const struct gpio_led ls1x_gpio_leds[] __initconst = {
	{
		.name			= "LED9",
		.default_trigger	= "heartbeat",
		.gpio			= 38,
		.active_low		= 1,
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name			= "LED6",
		.default_trigger	= "nand-disk",
		.gpio			= 39,
		.active_low		= 1,
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
};

static const struct gpio_led_platform_data ls1x_led_pdata __initconst = {
	.num_leds	= ARRAY_SIZE(ls1x_gpio_leds),
	.leds		= ls1x_gpio_leds,
};

static struct platform_device *ls1b_platform_devices[] __initdata = {
	&ls1x_uart_pdev,
	&ls1x_eth0_pdev,
	&ls1x_eth1_pdev,
	&ls1x_ehci_pdev,
	&ls1x_gpio0_pdev,
	&ls1x_gpio1_pdev,
	&ls1x_rtc_pdev,
	&ls1x_wdt_pdev,
};

static int __init ls1b_platform_init(void)
{
	ls1x_serial_set_uartclk(&ls1x_uart_pdev);

	gpio_led_register_device(-1, &ls1x_led_pdata);

	return platform_add_devices(ls1b_platform_devices,
				   ARRAY_SIZE(ls1b_platform_devices));
}

arch_initcall(ls1b_platform_init);
