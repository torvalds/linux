/*
 * Copyright 2012 (C), Simon Baatz <gmbnomis@gmail.com>
 *
 * arch/arm/mach-kirkwood/board-ib62x0.c
 *
 * RaidSonic ICY BOX IB-NAS6210 & IB-NAS6220 init for drivers not
 * converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"

#define IB62X0_GPIO_POWER_OFF	24

static struct mv643xx_eth_platform_data ib62x0_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv_sata_platform_data ib62x0_sata_data = {
	.n_ports	= 2,
};

static unsigned int ib62x0_mpp_config[] __initdata = {
	MPP0_NF_IO2,
	MPP1_NF_IO3,
	MPP2_NF_IO4,
	MPP3_NF_IO5,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP22_GPIO,	/* OS LED red */
	MPP24_GPIO,	/* Power off device */
	MPP25_GPIO,	/* OS LED green */
	MPP27_GPIO,	/* USB transfer LED */
	MPP28_GPIO,	/* Reset button */
	MPP29_GPIO,	/* USB Copy button */
	0
};

static struct gpio_led ib62x0_led_pins[] = {
	{
		.name			= "ib62x0:green:os",
		.default_trigger	= "default-on",
		.gpio			= 25,
		.active_low		= 0,
	},
	{
		.name			= "ib62x0:red:os",
		.default_trigger	= "none",
		.gpio			= 22,
		.active_low		= 0,
	},
	{
		.name			= "ib62x0:red:usb_copy",
		.default_trigger	= "none",
		.gpio			= 27,
		.active_low		= 0,
	},
};

static struct gpio_led_platform_data ib62x0_led_data = {
	.leds		= ib62x0_led_pins,
	.num_leds	= ARRAY_SIZE(ib62x0_led_pins),
};

static struct platform_device ib62x0_led_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &ib62x0_led_data,
	}
};

static struct gpio_keys_button ib62x0_button_pins[] = {
	{
		.code		= KEY_COPY,
		.gpio		= 29,
		.desc		= "USB Copy",
		.active_low	= 1,
	},
	{
		.code		= KEY_RESTART,
		.gpio		= 28,
		.desc		= "Reset",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data ib62x0_button_data = {
	.buttons	= ib62x0_button_pins,
	.nbuttons	= ARRAY_SIZE(ib62x0_button_pins),
};

static struct platform_device ib62x0_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &ib62x0_button_data,
	}
};

static void ib62x0_power_off(void)
{
	gpio_set_value(IB62X0_GPIO_POWER_OFF, 1);
}

void __init ib62x0_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(ib62x0_mpp_config);

	kirkwood_ehci_init();
	kirkwood_ge00_init(&ib62x0_ge00_data);
	kirkwood_sata_init(&ib62x0_sata_data);
	platform_device_register(&ib62x0_led_device);
	platform_device_register(&ib62x0_button_device);
	if (gpio_request(IB62X0_GPIO_POWER_OFF, "ib62x0:power:off") == 0 &&
	    gpio_direction_output(IB62X0_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = ib62x0_power_off;
	else
		pr_err("board-ib62x0: failed to configure power-off GPIO\n");
}
