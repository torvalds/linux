/*
 *  Generic RDC321x platform devices
 *
 *  Copyright (C) 2007 Florian Fainelli <florian@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/leds.h>

#include <asm/gpio.h>

/* LEDS */
static struct gpio_led default_leds[] = {
	{ .name = "rdc:dmz", .gpio = 1, },
};

static struct gpio_led_platform_data rdc321x_led_data = {
	.num_leds = ARRAY_SIZE(default_leds),
	.leds = default_leds,
};

static struct platform_device rdc321x_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &rdc321x_led_data,
	}
};

/* Watchdog */
static struct platform_device rdc321x_wdt = {
	.name = "rdc321x-wdt",
	.id = -1,
	.num_resources = 0,
};

static struct platform_device *rdc321x_devs[] = {
	&rdc321x_leds,
	&rdc321x_wdt
};

static int __init rdc_board_setup(void)
{
	return platform_add_devices(rdc321x_devs, ARRAY_SIZE(rdc321x_devs));
}

arch_initcall(rdc_board_setup);
