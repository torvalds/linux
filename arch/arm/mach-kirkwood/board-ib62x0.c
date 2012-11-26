/*
 * Copyright 2012 (C), Simon Baatz <gmbnomis@gmail.com>
 *
 * arch/arm/mach-kirkwood/board-ib62x0.c
 *
 * RaidSonic ICY BOX IB-NAS6210 & IB-NAS6220 init for drivers not
 * converted to flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/input.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"

static struct mv643xx_eth_platform_data ib62x0_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

void __init ib62x0_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_ge00_init(&ib62x0_ge00_data);
}
