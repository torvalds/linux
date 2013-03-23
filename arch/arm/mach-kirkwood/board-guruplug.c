/*
 * arch/arm/mach-kirkwood/board-guruplug.c
 *
 * Marvell Guruplug Reference Board Init for drivers not converted to
 * flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include "common.h"

static struct mv643xx_eth_platform_data guruplug_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data guruplug_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(1),
};

void __init guruplug_dt_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&guruplug_ge00_data);
	kirkwood_ge01_init(&guruplug_ge01_data);
}
