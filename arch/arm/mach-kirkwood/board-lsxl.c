/*
 * Copyright 2012 (C), Michael Walle <michael@walle.cc>
 *
 * arch/arm/mach-kirkwood/board-lsxl.c
 *
 * Buffalo Linkstation LS-XHL and LS-CHLv2 init for drivers not
 * converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include "common.h"

static struct mv643xx_eth_platform_data lsxl_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data lsxl_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

void __init lsxl_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */

	kirkwood_ge00_init(&lsxl_ge00_data);
	kirkwood_ge01_init(&lsxl_ge01_data);
}
