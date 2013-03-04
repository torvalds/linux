/*
 * Copyright (C) 2012 MPL AG, Switzerland
 * Stefan Peter <s.peter@mpl.ch>
 *
 * arch/arm/mach-kirkwood/board-mplcec4.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx_eth.h>
#include "common.h"

static struct mv643xx_eth_platform_data mplcec4_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(1),
};

static struct mv643xx_eth_platform_data mplcec4_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(2),
};

void __init mplcec4_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&mplcec4_ge00_data);
	kirkwood_ge01_init(&mplcec4_ge01_data);
	kirkwood_pcie_init(KW_PCIE0);
}



