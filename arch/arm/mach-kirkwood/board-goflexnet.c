/*
 * Copyright 2012 (C), Jason Cooper <jason@lakedaemon.net>
 *
 * arch/arm/mach-kirkwood/board-goflexnet.c
 *
 * Seagate GoFlext Net Board Init for drivers not converted to
 * flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Copied and modified for Seagate GoFlex Net support by
 * Joshua Coombs <josh.coombs@gmail.com> based on ArchLinux ARM's
 * GoFlex kernel patches.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data goflexnet_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv_sata_platform_data goflexnet_sata_data = {
	.n_ports	= 2,
};

static struct gpio_led goflexnet_led_pins[] = {
	{
		.name			= "status:green:health",
		.default_trigger	= "default-on",
		.gpio			= 46,
		.active_low		= 1,
	},
	{
		.name			= "status:orange:fault",
		.default_trigger	= "none",
		.gpio			= 47,
		.active_low		= 1,
	},
	{
		.name			= "status:white:left0",
		.default_trigger	= "none",
		.gpio			= 42,
		.active_low		= 0,
	},
	{
		.name			= "status:white:left1",
		.default_trigger	= "none",
		.gpio			= 43,
		.active_low		= 0,
	},
	{
		.name			= "status:white:left2",
		.default_trigger	= "none",
		.gpio			= 44,
		.active_low		= 0,
	},
	{
		.name			= "status:white:left3",
		.default_trigger	= "none",
		.gpio			= 45,
		.active_low		= 0,
	},
	{
		.name			= "status:white:right0",
		.default_trigger	= "none",
		.gpio			= 38,
		.active_low		= 0,
	},
	{
		.name			= "status:white:right1",
		.default_trigger	= "none",
		.gpio			= 39,
		.active_low		= 0,
	},
	{
		.name			= "status:white:right2",
		.default_trigger	= "none",
		.gpio			= 40,
		.active_low		= 0,
	},
	{
		.name			= "status:white:right3",
		.default_trigger	= "none",
		.gpio			= 41,
		.active_low		= 0,
	},
};

static struct gpio_led_platform_data goflexnet_led_data = {
	.leds		= goflexnet_led_pins,
	.num_leds	= ARRAY_SIZE(goflexnet_led_pins),
};

static struct platform_device goflexnet_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &goflexnet_led_data,
	}
};

static unsigned int goflexnet_mpp_config[] __initdata = {
	MPP29_GPIO,	/* USB Power Enable */
	MPP47_GPIO,	/* LED Orange */
	MPP46_GPIO,	/* LED Green */
	MPP45_GPIO,	/* LED Left Capacity 3 */
	MPP44_GPIO,	/* LED Left Capacity 2 */
	MPP43_GPIO,	/* LED Left Capacity 1 */
	MPP42_GPIO,	/* LED Left Capacity 0 */
	MPP41_GPIO,	/* LED Right Capacity 3 */
	MPP40_GPIO,	/* LED Right Capacity 2 */
	MPP39_GPIO,	/* LED Right Capacity 1 */
	MPP38_GPIO,	/* LED Right Capacity 0 */
	0
};

void __init goflexnet_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(goflexnet_mpp_config);

	if (gpio_request(29, "USB Power Enable") != 0 ||
	    gpio_direction_output(29, 1) != 0)
		pr_err("can't setup GPIO 29 (USB Power Enable)\n");
	kirkwood_ehci_init();

	kirkwood_ge00_init(&goflexnet_ge00_data);
	kirkwood_sata_init(&goflexnet_sata_data);

	platform_device_register(&goflexnet_leds);
}
