/*
 * Copyright 2012 Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 *
 * arch/arm/mach-kirkwood/board-openblocks_a6.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx_eth.h>
#include "common.h"

static struct mv643xx_eth_platform_data openblocks_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

void __init openblocks_a6_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&openblocks_ge00_data);
}
