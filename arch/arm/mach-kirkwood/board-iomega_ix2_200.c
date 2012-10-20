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
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data iomega_ix2_200_ge00_data = {
	.phy_addr       = MV643XX_ETH_PHY_NONE,
	.speed          = SPEED_1000,
	.duplex         = DUPLEX_FULL,
};

static unsigned int iomega_ix2_200_mpp_config[] __initdata = {
	MPP12_GPIO,			/* Reset Button */
	MPP14_GPIO,			/* Power Button */
	MPP15_GPIO,			/* Backup LED (blue) */
	MPP16_GPIO,			/* Power LED (white) */
	MPP35_GPIO,			/* OTB Button */
	MPP36_GPIO,			/* Rebuild LED (white) */
	MPP37_GPIO,			/* Health LED (red) */
	MPP38_GPIO,			/* SATA LED brightness control 1 */
	MPP39_GPIO,			/* SATA LED brightness control 2 */
	MPP40_GPIO,			/* Backup LED brightness control 1 */
	MPP41_GPIO,			/* Backup LED brightness control 2 */
	MPP42_GPIO,			/* Power LED brightness control 1 */
	MPP43_GPIO,			/* Power LED brightness control 2 */
	MPP44_GPIO,			/* Health LED brightness control 1 */
	MPP45_GPIO,			/* Health LED brightness control 2 */
	MPP46_GPIO,			/* Rebuild LED brightness control 1 */
	MPP47_GPIO,			/* Rebuild LED brightness control 2 */
	0
};

void __init iomega_ix2_200_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(iomega_ix2_200_mpp_config);

	kirkwood_ge01_init(&iomega_ix2_200_ge00_data);
}
