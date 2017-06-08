/*
 * arch/arm/mach-orion5x/board-d2net.c
 *
 * LaCie d2Network and Big Disk Network NAS setup
 *
 * Copyright (C) 2009 Simon Guinot <sguinot@lacie.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <plat/orion-gpio.h>
#include "common.h"
#include "orion5x.h"

/*****************************************************************************
 * LaCie d2 Network Info
 ****************************************************************************/

/*****************************************************************************
 * GPIO LED's
 ****************************************************************************/

/*
 * The blue front LED is wired to the CPLD and can blink in relation with the
 * SATA activity.
 *
 * The following array detail the different LED registers and the combination
 * of their possible values:
 *
 * led_off   | blink_ctrl | SATA active | LED state
 *           |            |             |
 *    1      |     x      |      x      |  off
 *    0      |     0      |      0      |  off
 *    0      |     1      |      0      |  blink (rate 300ms)
 *    0      |     x      |      1      |  on
 *
 * Notes: The blue and the red front LED's can't be on at the same time.
 *        Red LED have priority.
 */

#define D2NET_GPIO_RED_LED		6
#define D2NET_GPIO_BLUE_LED_BLINK_CTRL	16
#define D2NET_GPIO_BLUE_LED_OFF		23

static struct gpio_led d2net_leds[] = {
	{
		.name = "d2net:blue:sata",
		.default_trigger = "default-on",
		.gpio = D2NET_GPIO_BLUE_LED_OFF,
		.active_low = 1,
	},
	{
		.name = "d2net:red:fail",
		.gpio = D2NET_GPIO_RED_LED,
	},
};

static struct gpio_led_platform_data d2net_led_data = {
	.num_leds = ARRAY_SIZE(d2net_leds),
	.leds = d2net_leds,
};

static struct platform_device d2net_gpio_leds = {
	.name           = "leds-gpio",
	.id             = -1,
	.dev            = {
		.platform_data  = &d2net_led_data,
	},
};

static void __init d2net_gpio_leds_init(void)
{
	int err;

	/* Configure register blink_ctrl to allow SATA activity LED blinking. */
	err = gpio_request(D2NET_GPIO_BLUE_LED_BLINK_CTRL, "blue LED blink");
	if (err == 0) {
		err = gpio_direction_output(D2NET_GPIO_BLUE_LED_BLINK_CTRL, 1);
		if (err)
			gpio_free(D2NET_GPIO_BLUE_LED_BLINK_CTRL);
	}
	if (err)
		pr_err("d2net: failed to configure blue LED blink GPIO\n");

	platform_device_register(&d2net_gpio_leds);
}

/*****************************************************************************
 * General Setup
 ****************************************************************************/

void __init d2net_init(void)
{
	d2net_gpio_leds_init();

	pr_notice("d2net: Flash write are not yet supported.\n");
}
