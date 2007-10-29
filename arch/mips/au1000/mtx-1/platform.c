/*
 * MTX-1 platform devices registration
 *
 * Copyright (C) 2007, Florian Fainelli <florian@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

#include <asm/gpio.h>

static struct resource mtx1_wdt_res[] = {
	[0] = {
		.start	= 15,
		.end	= 15,
		.name	= "mtx1-wdt-gpio",
		.flags	= IORESOURCE_IRQ,
	}
};

static struct resource mtx1_sys_btn[] = {
	[0] = {
		.start	= 7,
		.end	= 7,
		.name	= "mtx1-sys-btn-gpio",
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device mtx1_wdt = {
	.name = "mtx1-wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(mtx1_wdt_res),
	.resource = mtx1_wdt_res,
};

static struct gpio_led default_leds[] = {
	{
		.name	= "mtx1:green",
		.gpio = 211,
	}, {
		.name = "mtx1:red",
		.gpio = 212,
	},
};

static struct gpio_led_platform_data mtx1_led_data = {
	.num_leds = ARRAY_SIZE(default_leds),
	.leds = default_leds,
};

static struct platform_device mtx1_gpio_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &mtx1_led_data,
	}
};

static struct __initdata platform_device * mtx1_devs[] = {
	&mtx1_gpio_leds,
	&mtx1_wdt
};

static int __init mtx1_register_devices(void)
{
	return platform_add_devices(mtx1_devs, ARRAY_SIZE(mtx1_devs));
}

arch_initcall(mtx1_register_devices);
