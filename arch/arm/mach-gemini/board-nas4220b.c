/*
 *  Support for Raidsonic NAS-4220-B
 *
 *  Copyright (C) 2009 Janos Laube <janos.dev@gmail.com>
 *
 * based on rut1xx.c
 *  Copyright (C) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
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
#include <linux/gpio_keys.h>
#include <linux/mdio-gpio.h>
#include <linux/io.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/hardware.h>
#include <mach/global_reg.h>

#include "common.h"

static struct gpio_led ib4220b_leds[] = {
	{
		.name			= "nas4220b:orange:hdd",
		.default_trigger	= "none",
		.gpio			= 60,
	},
	{
		.name			= "nas4220b:green:os",
		.default_trigger	= "heartbeat",
		.gpio			= 62,
	},
};

static struct gpio_led_platform_data ib4220b_leds_data = {
	.num_leds	= ARRAY_SIZE(ib4220b_leds),
	.leds		= ib4220b_leds,
};

static struct platform_device ib4220b_led_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &ib4220b_leds_data,
	},
};

static struct gpio_keys_button ib4220b_keys[] = {
	{
		.code		= KEY_SETUP,
		.gpio		= 61,
		.active_low	= 1,
		.desc		= "Backup Button",
		.type		= EV_KEY,
	},
	{
		.code		= KEY_RESTART,
		.gpio		= 63,
		.active_low	= 1,
		.desc		= "Softreset Button",
		.type		= EV_KEY,
	},
};

static struct gpio_keys_platform_data ib4220b_keys_data = {
	.buttons	= ib4220b_keys,
	.nbuttons	= ARRAY_SIZE(ib4220b_keys),
};

static struct platform_device ib4220b_key_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &ib4220b_keys_data,
	},
};

static void __init ib4220b_init(void)
{
	gemini_gpio_init();
	platform_register_uart();
	platform_register_pflash(SZ_16M, NULL, 0);
	platform_device_register(&ib4220b_led_device);
	platform_device_register(&ib4220b_key_device);
	platform_register_rtc();
}

MACHINE_START(NAS4220B, "Raidsonic NAS IB-4220-B")
	.atag_offset	= 0x100,
	.map_io		= gemini_map_io,
	.init_irq	= gemini_init_irq,
	.init_time	= gemini_timer_init,
	.init_machine	= ib4220b_init,
MACHINE_END
