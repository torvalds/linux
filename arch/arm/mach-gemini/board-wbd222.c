/*
 *  Support for Wiliboard WBD-222
 *
 *  Copyright (C) 2009 Imre Kaloz <kaloz@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/skbuff.h>
#include <linux/gpio_keys.h>
#include <linux/mdio-gpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>


#include "common.h"

static struct gpio_keys_button wbd222_keys[] = {
	{
		.code		= KEY_SETUP,
		.gpio		= 5,
		.active_low	= 1,
		.desc		= "reset",
		.type		= EV_KEY,
	},
};

static struct gpio_keys_platform_data wbd222_keys_data = {
	.buttons	= wbd222_keys,
	.nbuttons	= ARRAY_SIZE(wbd222_keys),
};

static struct platform_device wbd222_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &wbd222_keys_data,
	},
};

static struct gpio_led wbd222_leds[] = {
	{
		.name			= "L3red",
		.gpio			= 1,
	},
	{
		.name			= "L4green",
		.gpio			= 2,
	},
	{
		.name			= "L4red",
		.gpio			= 3,
	},
	{
		.name			= "L3green",
		.gpio			= 5,
	},
};

static struct gpio_led_platform_data wbd222_leds_data = {
	.num_leds	= ARRAY_SIZE(wbd222_leds),
	.leds		= wbd222_leds,
};

static struct platform_device wbd222_leds_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &wbd222_leds_data,
	},
};

static struct sys_timer wbd222_timer = {
	.init	= gemini_timer_init,
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition wbd222_partitions[] = {
	{
		.name		= "RedBoot",
		.offset		= 0,
		.size		= 0x020000,
		.mask_flags	= MTD_WRITEABLE,
	} , {
		.name		= "kernel",
		.offset		= 0x020000,
		.size		= 0x100000,
	} , {
		.name		= "rootfs",
		.offset		= 0x120000,
		.size		= 0x6a0000,
	} , {
		.name		= "VCTL",
		.offset		= 0x7c0000,
		.size		= 0x010000,
		.mask_flags	= MTD_WRITEABLE,
	} , {
		.name		= "cfg",
		.offset		= 0x7d0000,
		.size		= 0x010000,
		.mask_flags	= MTD_WRITEABLE,
	} , {
		.name		= "FIS",
		.offset		= 0x7e0000,
		.size		= 0x010000,
		.mask_flags	= MTD_WRITEABLE,
	}
};
#define wbd222_num_partitions	ARRAY_SIZE(wbd222_partitions)
#else
#define wbd222_partitions	NULL
#define wbd222_num_partitions	0
#endif /* CONFIG_MTD_PARTITIONS */

static void __init wbd222_init(void)
{
	gemini_gpio_init();
	platform_register_uart();
	platform_register_pflash(SZ_8M, wbd222_partitions,
		wbd222_num_partitions);
	platform_device_register(&wbd222_leds_device);
	platform_device_register(&wbd222_keys_device);
	platform_register_rtc();
}

MACHINE_START(WBD222, "Wiliboard WBD-222")
	.boot_params	= 0x100,
	.map_io		= gemini_map_io,
	.init_irq	= gemini_init_irq,
	.timer		= &wbd222_timer,
	.init_machine	= wbd222_init,
MACHINE_END
