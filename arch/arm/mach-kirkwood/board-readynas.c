/*
 * NETGEAR ReadyNAS Duo v2 Board setup for drivers not already
 * converted to DT.
 *
 * Copyright (C) 2013, Arnaud EBALARD <arno@natisbad.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include <mach/kirkwood.h>
#include "common.h"

static struct mv643xx_eth_platform_data netgear_readynas_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

void __init netgear_readynas_init(void)
{
	kirkwood_ge00_init(&netgear_readynas_ge00_data);
	kirkwood_pcie_init(KW_PCIE0);
}
