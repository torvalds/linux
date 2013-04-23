/*
 * arch/arm/mach-kirkwood/board-iomega_ix2_200.c
 *
 * Iomega StorCenter ix2-200
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include "common.h"

static struct mv643xx_eth_platform_data iomega_ix2_200_ge00_data = {
	.phy_addr       = MV643XX_ETH_PHY_NONE,
	.speed          = SPEED_1000,
	.duplex         = DUPLEX_FULL,
};

static struct mv643xx_eth_platform_data iomega_ix2_200_ge01_data = {
        .phy_addr       = MV643XX_ETH_PHY_ADDR(11),
};

void __init iomega_ix2_200_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&iomega_ix2_200_ge00_data);
	kirkwood_ge01_init(&iomega_ix2_200_ge01_data);
}
