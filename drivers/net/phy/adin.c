// SPDX-License-Identifier: GPL-2.0+
/**
 *  Driver for Analog Devices Industrial Ethernet PHYs
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>

#define PHY_ID_ADIN1200				0x0283bc20
#define PHY_ID_ADIN1300				0x0283bc30

static int adin_config_init(struct phy_device *phydev)
{
	return genphy_config_init(phydev);
}

static struct phy_driver adin_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_ADIN1200),
		.name		= "ADIN1200",
		.config_init	= adin_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_ADIN1300),
		.name		= "ADIN1300",
		.config_init	= adin_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
	},
};

module_phy_driver(adin_driver);

static struct mdio_device_id __maybe_unused adin_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1200) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1300) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, adin_tbl);
MODULE_DESCRIPTION("Analog Devices Industrial Ethernet PHY driver");
MODULE_LICENSE("GPL");
