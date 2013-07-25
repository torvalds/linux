/*
 * Saeed Bishara <saeed@marvell.com>
 *
 * Marvell DB-88F628{1,2}-BP Development Board Setup
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

static struct mv643xx_eth_platform_data db88f628x_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

void __init db88f628x_init(void)
{
	kirkwood_ge00_init(&db88f628x_ge00_data);
}
