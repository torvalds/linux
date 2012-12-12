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
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include "common.h"
#include "mpp.h"

#define IB62X0_GPIO_POWER_OFF	24

static struct mv643xx_eth_platform_data ib62x0_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
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
	if (gpio_request(IB62X0_GPIO_POWER_OFF, "ib62x0:power:off") == 0 &&
	    gpio_direction_output(IB62X0_GPIO_POWER_OFF, 0) == 0)
		pm_power_off = ib62x0_power_off;
	else
		pr_err("board-ib62x0: failed to configure power-off GPIO\n");
}
