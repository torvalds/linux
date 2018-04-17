/*
 * Amlogic Meson GXL Internal PHY Driver
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2016 BayLibre, SAS. All rights reserved.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>

static int meson_gxl_config_init(struct phy_device *phydev)
{
	/* Enable Analog and DSP register Bank access by */
	phy_write(phydev, 0x14, 0x0000);
	phy_write(phydev, 0x14, 0x0400);
	phy_write(phydev, 0x14, 0x0000);
	phy_write(phydev, 0x14, 0x0400);

	/* Write Analog register 23 */
	phy_write(phydev, 0x17, 0x8E0D);
	phy_write(phydev, 0x14, 0x4417);

	/* Enable fractional PLL */
	phy_write(phydev, 0x17, 0x0005);
	phy_write(phydev, 0x14, 0x5C1B);

	/* Program fraction FR_PLL_DIV1 */
	phy_write(phydev, 0x17, 0x029A);
	phy_write(phydev, 0x14, 0x5C1D);

	/* Program fraction FR_PLL_DIV1 */
	phy_write(phydev, 0x17, 0xAAAA);
	phy_write(phydev, 0x14, 0x5C1C);

	return 0;
}

/* This function is provided to cope with the possible failures of this phy
 * during aneg process. When aneg fails, the PHY reports that aneg is done
 * but the value found in MII_LPA is wrong:
 *  - Early failures: MII_LPA is just 0x0001. if MII_EXPANSION reports that
 *    the link partner (LP) supports aneg but the LP never acked our base
 *    code word, it is likely that we never sent it to begin with.
 *  - Late failures: MII_LPA is filled with a value which seems to make sense
 *    but it actually is not what the LP is advertising. It seems that we
 *    can detect this using a magic bit in the WOL bank (reg 12 - bit 12).
 *    If this particular bit is not set when aneg is reported being done,
 *    it means MII_LPA is likely to be wrong.
 *
 * In both case, forcing a restart of the aneg process solve the problem.
 * When this failure happens, the first retry is usually successful but,
 * in some cases, it may take up to 6 retries to get a decent result
 */
static int meson_gxl_read_status(struct phy_device *phydev)
{
	int ret, wol, lpa, exp;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_aneg_done(phydev);
		if (ret < 0)
			return ret;
		else if (!ret)
			goto read_status_continue;

		/* Need to access WOL bank, make sure the access is open */
		ret = phy_write(phydev, 0x14, 0x0000);
		if (ret)
			return ret;
		ret = phy_write(phydev, 0x14, 0x0400);
		if (ret)
			return ret;
		ret = phy_write(phydev, 0x14, 0x0000);
		if (ret)
			return ret;
		ret = phy_write(phydev, 0x14, 0x0400);
		if (ret)
			return ret;

		/* Request LPI_STATUS WOL register */
		ret = phy_write(phydev, 0x14, 0x8D80);
		if (ret)
			return ret;

		/* Read LPI_STATUS value */
		wol = phy_read(phydev, 0x15);
		if (wol < 0)
			return wol;

		lpa = phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		exp = phy_read(phydev, MII_EXPANSION);
		if (exp < 0)
			return exp;

		if (!(wol & BIT(12)) ||
		    ((exp & EXPANSION_NWAY) && !(lpa & LPA_LPACK))) {
			/* Looks like aneg failed after all */
			phydev_dbg(phydev, "LPA corruption - aneg restart\n");
			return genphy_restart_aneg(phydev);
		}
	}

read_status_continue:
	return genphy_read_status(phydev);
}

static struct phy_driver meson_gxl_phy[] = {
	{
		.phy_id		= 0x01814400,
		.phy_id_mask	= 0xfffffff0,
		.name		= "Meson GXL Internal PHY",
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_IS_INTERNAL,
		.config_init	= meson_gxl_config_init,
		.config_aneg	= genphy_config_aneg,
		.aneg_done      = genphy_aneg_done,
		.read_status	= meson_gxl_read_status,
		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
	},
};

static struct mdio_device_id __maybe_unused meson_gxl_tbl[] = {
	{ 0x01814400, 0xfffffff0 },
	{ }
};

module_phy_driver(meson_gxl_phy);

MODULE_DEVICE_TABLE(mdio, meson_gxl_tbl);

MODULE_DESCRIPTION("Amlogic Meson GXL Internal PHY driver");
MODULE_AUTHOR("Baoqi wang");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
