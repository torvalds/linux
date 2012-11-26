/*
 * Copyright 2012 (C), Jason Cooper <jason@lakedaemon.net>
 *
 * arch/arm/mach-kirkwood/board-dreamplug.c
 *
 * Marvell DreamPlug Reference Board Init for drivers not converted to
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
#include <linux/platform_data/mmc-mvsdio.h>
#include "common.h"

static struct mv643xx_eth_platform_data dreamplug_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data dreamplug_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(1),
};

static struct mvsdio_platform_data dreamplug_mvsdio_data = {
	/* unfortunately the CD signal has not been connected */
};

void __init dreamplug_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&dreamplug_ge00_data);
	kirkwood_ge01_init(&dreamplug_ge01_data);
	kirkwood_sdio_init(&dreamplug_mvsdio_data);
}
