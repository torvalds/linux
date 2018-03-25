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

static int meson_gxl_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable Analog and DSP register Bank access by */
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

	/* Write Analog register 23 */
	ret = phy_write(phydev, 0x17, 0x8E0D);
	if (ret)
		return ret;
	ret = phy_write(phydev, 0x14, 0x4417);
	if (ret)
		return ret;

	/* Enable fractional PLL */
	ret = phy_write(phydev, 0x17, 0x0005);
	if (ret)
		return ret;
	ret = phy_write(phydev, 0x14, 0x5C1B);
	if (ret)
		return ret;

	/* Program fraction FR_PLL_DIV1 */
	ret = phy_write(phydev, 0x17, 0x029A);
	if (ret)
		return ret;
	ret = phy_write(phydev, 0x14, 0x5C1D);
	if (ret)
		return ret;

	/* Program fraction FR_PLL_DIV1 */
	ret = phy_write(phydev, 0x17, 0xAAAA);
	if (ret)
		return ret;
	ret = phy_write(phydev, 0x14, 0x5C1C);
	if (ret)
		return ret;

	return 0;
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
		.read_status	= genphy_read_status,
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
