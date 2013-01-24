/*
 * Copyright 2012 (C), Jason Cooper <jason@lakedaemon.net>
 *
 * arch/arm/mach-kirkwood/board-usi_topkick.c
 *
 * USI Topkick Init for drivers not converted to flattened device tree yet.
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
#include "mpp.h"

static struct mv643xx_eth_platform_data topkick_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mvsdio_platform_data topkick_mvsdio_data = {
	/* unfortunately the CD signal has not been connected */
};

/*
 * GPIO LED layout
 *
 *       /-SYS_LED(2)
 *       |
 *       |   /-DISK_LED
 *       |   |
 *       |   |   /-WLAN_LED(2)
 *       |   |   |
 * [SW] [*] [*] [*]
 */

/*
 * Switch positions
 *
 *     /-SW_LEFT
 *     |
 *     |   /-SW_IDLE
 *     |   |
 *     |   |   /-SW_RIGHT
 *     |   |   |
 * PS [L] [I] [R] LEDS
 */

static unsigned int topkick_mpp_config[] __initdata = {
	MPP21_GPIO,	/* DISK_LED           (low active) - yellow */
	MPP36_GPIO,	/* SATA0 power enable (high active) */
	MPP37_GPIO,	/* SYS_LED2           (low active) - red */
	MPP38_GPIO,	/* SYS_LED            (low active) - blue */
	MPP39_GPIO,	/* WLAN_LED           (low active) - green */
	MPP43_GPIO,	/* SW_LEFT            (low active) */
	MPP44_GPIO,     /* SW_RIGHT           (low active) */
	MPP45_GPIO,	/* SW_IDLE            (low active) */
	MPP46_GPIO,     /* SW_LEFT            (low active) */
	MPP48_GPIO,	/* WLAN_LED2          (low active) - yellow */
	0
};

void __init usi_topkick_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(topkick_mpp_config);


	kirkwood_ge00_init(&topkick_ge00_data);
	kirkwood_sdio_init(&topkick_mvsdio_data);
}
