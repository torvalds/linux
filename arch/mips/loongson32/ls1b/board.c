/*
 * Copyright (c) 2011-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/leds.h>
#include <linux/mtd/partitions.h>
#include <linux/sizes.h>

#include <loongson1.h>
#include <dma.h>
#include <nand.h>
#include <platform.h>

struct plat_ls1x_dma ls1x_dma_pdata = {
	.nr_channels	= 3,
};

static struct mtd_partition ls1x_nand_parts[] = {
	{
		.name        = "kernel",
		.offset      = 0,
		.size        = SZ_16M,
	},
	{
		.name        = "rootfs",
		.offset      = MTDPART_OFS_APPEND,
		.size        = MTDPART_SIZ_FULL,
	},
};

struct plat_ls1x_nand ls1x_nand_pdata = {
	.parts		= ls1x_nand_parts,
	.nr_parts	= ARRAY_SIZE(ls1x_nand_parts),
	.hold_cycle	= 0x2,
	.wait_cycle	= 0xc,
};

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
	&ls1x_cpufreq_pdev,
	&ls1x_dma_pdev,
	&ls1x_eth0_pdev,
	&ls1x_eth1_pdev,
	&ls1x_ehci_pdev,
	&ls1x_gpio0_pdev,
	&ls1x_gpio1_pdev,
	&ls1x_nand_pdev,
	&ls1x_rtc_pdev,
	&ls1x_wdt_pdev,
};

static int __init ls1b_platform_init(void)
{
	ls1x_serial_set_uartclk(&ls1x_uart_pdev);
	ls1x_dma_set_platdata(&ls1x_dma_pdata);
	ls1x_nand_set_platdata(&ls1x_nand_pdata);

	gpio_led_register_device(-1, &ls1x_led_pdata);

	return platform_add_devices(ls1b_platform_devices,
				   ARRAY_SIZE(ls1b_platform_devices));
}

arch_initcall(ls1b_platform_init);
