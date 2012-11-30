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
#include <linux/mv643xx_eth.h>
#include "common.h"

static struct mv643xx_eth_platform_data goflexnet_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

void __init goflexnet_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&goflexnet_ge00_data);
}
