/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/ssb/ssb.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

/* GPIO definitions for the WGT634U */
#define WGT634U_GPIO_LED	3
#define WGT634U_GPIO_RESET	2
#define WGT634U_GPIO_TP1	7
#define WGT634U_GPIO_TP2	6
#define WGT634U_GPIO_TP3	5
#define WGT634U_GPIO_TP4	4
#define WGT634U_GPIO_TP5	1

static struct gpio_led wgt634u_leds[] = {
	{
		.name = "power",
		.gpio = WGT634U_GPIO_LED,
		.active_low = 1,
		.default_trigger = "heartbeat",
	},
};

static struct gpio_led_platform_data wgt634u_led_data = {
	.num_leds =     ARRAY_SIZE(wgt634u_leds),
	.leds =         wgt634u_leds,
};

static struct platform_device wgt634u_gpio_leds = {
	.name =         "leds-gpio",
	.id =           -1,
	.dev = {
		.platform_data = &wgt634u_led_data,
	}
};

static int __init wgt634u_init(void)
{
	/* There is no easy way to detect that we are running on a WGT634U
	 * machine. Use the MAC address as an heuristic. Netgear Inc. has
	 * been allocated ranges 00:09:5b:xx:xx:xx and 00:0f:b5:xx:xx:xx.
	 */

	u8 *et0mac = ssb_bcm47xx.sprom.r1.et0mac;

	if (et0mac[0] == 0x00 &&
	    ((et0mac[1] == 0x09 && et0mac[2] == 0x5b) ||
	     (et0mac[1] == 0x0f && et0mac[2] == 0xb5)))
		return platform_device_register(&wgt634u_gpio_leds);
	else
		return -ENODEV;
}

module_init(wgt634u_init);

