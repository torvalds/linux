/*
 * arch/arm/mach-kirkwood/board-iconnect.c
 *
 * Iomega i-connect Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mv643xx_eth.h>
#include "common.h"

static struct mv643xx_eth_platform_data iconnect_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(11),
};

void __init iconnect_init(void)
{
	kirkwood_ge00_init(&iconnect_ge00_data);
}
