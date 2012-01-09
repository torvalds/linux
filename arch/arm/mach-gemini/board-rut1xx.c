/*
 *  Support for Teltonika RUT1xx
 *
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
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

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"

static struct gpio_keys_button rut1xx_keys[] = {
	{
		.code		= KEY_SETUP,
		.gpio		= 60,
		.active_low	= 1,
		.desc		= "Reset to defaults",
		.type		= EV_KEY,
	},
};

static struct gpio_keys_platform_data rut1xx_keys_data = {
	.buttons	= rut1xx_keys,
	.nbuttons	= ARRAY_SIZE(rut1xx_keys),
};

static struct platform_device rut1xx_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &rut1xx_keys_data,
	},
};

static struct gpio_led rut100_leds[] = {
	{
		.name			= "Power",
		.default_trigger	= "heartbeat",
		.gpio			= 17,
	},
	{
		.name			= "GSM",
		.default_trigger	= "default-on",
		.gpio			= 7,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data rut100_leds_data = {
	.num_leds	= ARRAY_SIZE(rut100_leds),
	.leds		= rut100_leds,
};

static struct platform_device rut1xx_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &rut100_leds_data,
	},
};

static struct sys_timer rut1xx_timer = {
	.init	= gemini_timer_init,
};

static void __init rut1xx_init(void)
{
	gemini_gpio_init();
	platform_register_uart();
	platform_register_pflash(SZ_8M, NULL, 0);
	platform_device_register(&rut1xx_leds);
	platform_device_register(&rut1xx_keys_device);
	platform_register_rtc();
}

MACHINE_START(RUT100, "Teltonika RUT100")
	.atag_offset	= 0x100,
	.map_io		= gemini_map_io,
	.init_irq	= gemini_init_irq,
	.timer		= &rut1xx_timer,
	.init_machine	= rut1xx_init,
MACHINE_END
