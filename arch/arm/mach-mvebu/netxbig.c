/*
 * arch/arm/mach-mvbu/board-netxbig.c
 *
 * LaCie 2Big and 5Big Network v2 board setup
 *
 * Copyright (C) 2010 Simon Guinot <sguinot@lacie.com>
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
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/platform_data/leds-kirkwood-netxbig.h>
#include "common.h"

/*****************************************************************************
 * GPIO extension LEDs
 ****************************************************************************/

/*
 * The LEDs are controlled by a CPLD and can be configured through a GPIO
 * extension bus:
 *
 * - address register : bit [0-2] -> GPIO [47-49]
 * - data register    : bit [0-2] -> GPIO [44-46]
 * - enable register  : GPIO 29
 */

static int netxbig_v2_gpio_ext_addr[] = { 47, 48, 49 };
static int netxbig_v2_gpio_ext_data[] = { 44, 45, 46 };

static struct netxbig_gpio_ext netxbig_v2_gpio_ext = {
	.addr		= netxbig_v2_gpio_ext_addr,
	.num_addr	= ARRAY_SIZE(netxbig_v2_gpio_ext_addr),
	.data		= netxbig_v2_gpio_ext_data,
	.num_data	= ARRAY_SIZE(netxbig_v2_gpio_ext_data),
	.enable		= 29,
};

/*
 * Address register selection:
 *
 * addr | register
 * ----------------------------
 *   0  | front LED
 *   1  | front LED brightness
 *   2  | SATA LED brightness
 *   3  | SATA0 LED
 *   4  | SATA1 LED
 *   5  | SATA2 LED
 *   6  | SATA3 LED
 *   7  | SATA4 LED
 *
 * Data register configuration:
 *
 * data | LED brightness
 * -------------------------------------------------
 *   0  | min (off)
 *   -  | -
 *   7  | max
 *
 * data | front LED mode
 * -------------------------------------------------
 *   0  | fix off
 *   1  | fix blue on
 *   2  | fix red on
 *   3  | blink blue on=1 sec and blue off=1 sec
 *   4  | blink red on=1 sec and red off=1 sec
 *   5  | blink blue on=2.5 sec and red on=0.5 sec
 *   6  | blink blue on=1 sec and red on=1 sec
 *   7  | blink blue on=0.5 sec and blue off=2.5 sec
 *
 * data | SATA LED mode
 * -------------------------------------------------
 *   0  | fix off
 *   1  | SATA activity blink
 *   2  | fix red on
 *   3  | blink blue on=1 sec and blue off=1 sec
 *   4  | blink red on=1 sec and red off=1 sec
 *   5  | blink blue on=2.5 sec and red on=0.5 sec
 *   6  | blink blue on=1 sec and red on=1 sec
 *   7  | fix blue on
 */

static int netxbig_v2_red_mled[NETXBIG_LED_MODE_NUM] = {
	[NETXBIG_LED_OFF]	= 0,
	[NETXBIG_LED_ON]	= 2,
	[NETXBIG_LED_SATA]	= NETXBIG_LED_INVALID_MODE,
	[NETXBIG_LED_TIMER1]	= 4,
	[NETXBIG_LED_TIMER2]	= NETXBIG_LED_INVALID_MODE,
};

static int netxbig_v2_blue_pwr_mled[NETXBIG_LED_MODE_NUM] = {
	[NETXBIG_LED_OFF]	= 0,
	[NETXBIG_LED_ON]	= 1,
	[NETXBIG_LED_SATA]	= NETXBIG_LED_INVALID_MODE,
	[NETXBIG_LED_TIMER1]	= 3,
	[NETXBIG_LED_TIMER2]	= 7,
};

static int netxbig_v2_blue_sata_mled[NETXBIG_LED_MODE_NUM] = {
	[NETXBIG_LED_OFF]	= 0,
	[NETXBIG_LED_ON]	= 7,
	[NETXBIG_LED_SATA]	= 1,
	[NETXBIG_LED_TIMER1]	= 3,
	[NETXBIG_LED_TIMER2]	= NETXBIG_LED_INVALID_MODE,
};

static struct netxbig_led_timer netxbig_v2_led_timer[] = {
	[0] = {
		.delay_on	= 500,
		.delay_off	= 500,
		.mode		= NETXBIG_LED_TIMER1,
	},
	[1] = {
		.delay_on	= 500,
		.delay_off	= 1000,
		.mode		= NETXBIG_LED_TIMER2,
	},
};

#define NETXBIG_LED(_name, maddr, mval, baddr)			\
	{ .name		= _name,				\
	  .mode_addr	= maddr,				\
	  .mode_val	= mval,					\
	  .bright_addr	= baddr }

static struct netxbig_led net2big_v2_leds_ctrl[] = {
	NETXBIG_LED("net2big-v2:blue:power", 0, netxbig_v2_blue_pwr_mled,  1),
	NETXBIG_LED("net2big-v2:red:power",  0, netxbig_v2_red_mled,       1),
	NETXBIG_LED("net2big-v2:blue:sata0", 3, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net2big-v2:red:sata0",  3, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net2big-v2:blue:sata1", 4, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net2big-v2:red:sata1",  4, netxbig_v2_red_mled,       2),
};

static struct netxbig_led_platform_data net2big_v2_leds_data = {
	.gpio_ext	= &netxbig_v2_gpio_ext,
	.timer		= netxbig_v2_led_timer,
	.num_timer	= ARRAY_SIZE(netxbig_v2_led_timer),
	.leds		= net2big_v2_leds_ctrl,
	.num_leds	= ARRAY_SIZE(net2big_v2_leds_ctrl),
};

static struct netxbig_led net5big_v2_leds_ctrl[] = {
	NETXBIG_LED("net5big-v2:blue:power", 0, netxbig_v2_blue_pwr_mled,  1),
	NETXBIG_LED("net5big-v2:red:power",  0, netxbig_v2_red_mled,       1),
	NETXBIG_LED("net5big-v2:blue:sata0", 3, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata0",  3, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata1", 4, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata1",  4, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata2", 5, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata2",  5, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata3", 6, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata3",  6, netxbig_v2_red_mled,       2),
	NETXBIG_LED("net5big-v2:blue:sata4", 7, netxbig_v2_blue_sata_mled, 2),
	NETXBIG_LED("net5big-v2:red:sata4",  7, netxbig_v2_red_mled,       2),
};

static struct netxbig_led_platform_data net5big_v2_leds_data = {
	.gpio_ext	= &netxbig_v2_gpio_ext,
	.timer		= netxbig_v2_led_timer,
	.num_timer	= ARRAY_SIZE(netxbig_v2_led_timer),
	.leds		= net5big_v2_leds_ctrl,
	.num_leds	= ARRAY_SIZE(net5big_v2_leds_ctrl),
};

static struct platform_device netxbig_v2_leds = {
	.name		= "leds-netxbig",
	.id		= -1,
	.dev		= {
		.platform_data	= &net2big_v2_leds_data,
	},
};

void __init netxbig_init(void)
{

	if (of_machine_is_compatible("lacie,net5big_v2"))
		netxbig_v2_leds.dev.platform_data = &net5big_v2_leds_data;
	platform_device_register(&netxbig_v2_leds);
}
