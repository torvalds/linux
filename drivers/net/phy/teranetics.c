// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Teranetics PHY
 *
 * Author: Shaohui Xie <Shaohui.Xie@freescale.com>
 *
 * Copyright 2015 Freescale Semiconductor, Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/mdio.h>
#include <linux/phy.h>

MODULE_DESCRIPTION("Teranetics PHY driver");
MODULE_AUTHOR("Shaohui Xie <Shaohui.Xie@freescale.com>");
MODULE_LICENSE("GPL v2");

#define PHY_ID_TN2020	0x00a19410
#define MDIO_PHYXS_LNSTAT_SYNC0	0x0001
#define MDIO_PHYXS_LNSTAT_SYNC1	0x0002
#define MDIO_PHYXS_LNSTAT_SYNC2	0x0004
#define MDIO_PHYXS_LNSTAT_SYNC3	0x0008
#define MDIO_PHYXS_LNSTAT_ALIGN 0x1000

#define MDIO_PHYXS_LANE_READY	(MDIO_PHYXS_LNSTAT_SYNC0 | \
				MDIO_PHYXS_LNSTAT_SYNC1 | \
				MDIO_PHYXS_LNSTAT_SYNC2 | \
				MDIO_PHYXS_LNSTAT_SYNC3 | \
				MDIO_PHYXS_LNSTAT_ALIGN)

static int teranetics_aneg_done(struct phy_device *phydev)
{
	/* auto negotiation state can only be checked when using copper
	 * port, if using fiber port, just lie it's done.
	 */
	if (!phy_read_mmd(phydev, MDIO_MMD_VEND1, 93))
		return genphy_c45_aneg_done(phydev);

	return 1;
}

static int teranetics_read_status(struct phy_device *phydev)
{
	int reg;

	phydev->link = 1;

	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;

	if (!phy_read_mmd(phydev, MDIO_MMD_VEND1, 93)) {
		reg = phy_read_mmd(phydev, MDIO_MMD_PHYXS, MDIO_PHYXS_LNSTAT);
		if (reg < 0 ||
		    !((reg & MDIO_PHYXS_LANE_READY) == MDIO_PHYXS_LANE_READY)) {
			phydev->link = 0;
			return 0;
		}

		reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
		if (reg < 0 || !(reg & MDIO_STAT1_LSTATUS))
			phydev->link = 0;
	}

	return 0;
}

static int teranetics_match_phy_device(struct phy_device *phydev)
{
	return phydev->c45_ids.device_ids[3] == PHY_ID_TN2020;
}

static struct phy_driver teranetics_driver[] = {
{
	.phy_id		= PHY_ID_TN2020,
	.phy_id_mask	= 0xffffffff,
	.name		= "Teranetics TN2020",
	.features       = PHY_10GBIT_FEATURES,
	.soft_reset	= genphy_no_soft_reset,
	.aneg_done	= teranetics_aneg_done,
	.config_aneg    = gen10g_config_aneg,
	.read_status	= teranetics_read_status,
	.match_phy_device = teranetics_match_phy_device,
},
};

module_phy_driver(teranetics_driver);

static struct mdio_device_id __maybe_unused teranetics_tbl[] = {
	{ PHY_ID_TN2020, 0xffffffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, teranetics_tbl);
