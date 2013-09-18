/*
 *
 * QNAP TS-11x/TS-21x Turbo NAS Board Setup via DT
 *
 * Copyright (C) 2012 Andrew Lunn <andrew@lunn.ch>
 *
 * Based on the board file ts219-setup.c:
 *
 * Copyright (C) 2009  Martin Michlmayr <tbm@cyrius.com>
 * Copyright (C) 2008  Byron Bradley <byron.bbradley@gmail.com>
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
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"

static struct mv643xx_eth_platform_data qnap_ts219_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

void __init qnap_dt_ts219_init(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);
	if (dev == MV88F6282_DEV_ID)
		qnap_ts219_ge00_data.phy_addr = MV643XX_ETH_PHY_ADDR(0);

	kirkwood_ge00_init(&qnap_ts219_ge00_data);
}
