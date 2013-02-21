/*
 * Copyright 2012 (C), Simon Guinot <simon.guinot@sequanux.org>
 *
 * arch/arm/mach-kirkwood/board-ns2.c
 *
 * LaCie Network Space v2 board (and parents) initialization for drivers
 * not converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include "common.h"

static struct mv643xx_eth_platform_data ns2_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

#define NS2_GPIO_POWER_OFF	31

static void ns2_power_off(void)
{
	gpio_set_value(NS2_GPIO_POWER_OFF, 1);
}

void __init ns2_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	if (of_machine_is_compatible("lacie,netspace_lite_v2") ||
	    of_machine_is_compatible("lacie,netspace_mini_v2"))
		ns2_ge00_data.phy_addr = MV643XX_ETH_PHY_ADDR(0);
	kirkwood_ge00_init(&ns2_ge00_data);

	if (gpio_request(NS2_GPIO_POWER_OFF, "power-off") == 0 &&
	    gpio_direction_output(NS2_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = ns2_power_off;
	else
		pr_err("ns2: failed to configure power-off GPIO\n");
}
