// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Analog Devices, Inc. ADIN1140 10BASE-T1S PHY
 *
 * Copyright 2026 Analog Devices Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define ADIN1140_PHY_ID		0x0283be00

#define ADIN1140_PCS_CTRL		0x08f3
#define ADIN1140_PCS_CTRL_LOOPBACK	BIT(14)

static int adin1140_config_aneg(struct phy_device *phydev)
{
	/* phylib tries to clear BIT(12) in MDIO_CTRL1, since AN is disabled.
	 * However, on the ADIN1140, that field is non-standard, being used
	 * to control the reset status of the PHY (thus it needs to remain set).
	 */
	return 0;
}

static int adin1140_loopback(struct phy_device *phydev, bool enable, int speed)
{
	if (enable && speed)
		return -EOPNOTSUPP;

	return phy_modify_mmd(phydev, MDIO_MMD_PCS, ADIN1140_PCS_CTRL,
			      ADIN1140_PCS_CTRL_LOOPBACK,
			      enable ? ADIN1140_PCS_CTRL_LOOPBACK : 0);
}

static int adin1140_read_status(struct phy_device *phydev)
{
	phydev->link = 1;
	phydev->duplex = DUPLEX_HALF;
	phydev->speed = SPEED_10;
	phydev->autoneg = AUTONEG_DISABLE;

	return 0;
}

static struct phy_driver adin1140_driver[] = {
	{
		PHY_ID_MATCH_EXACT(ADIN1140_PHY_ID),
		.name = "ADIN1140_PHY",
		.features = PHY_BASIC_T1S_P2MP_FEATURES,
		.read_status = adin1140_read_status,
		.config_aneg = adin1140_config_aneg,
		.set_loopback = adin1140_loopback,
		.read_mmd = genphy_read_mmd_c45,
		.write_mmd = genphy_write_mmd_c45,
		.get_plca_cfg = genphy_c45_plca_get_cfg,
		.set_plca_cfg = genphy_c45_plca_set_cfg,
		.get_plca_status = genphy_c45_plca_get_status,
	},
};
module_phy_driver(adin1140_driver);

static const struct mdio_device_id __maybe_unused adin1140_tbl[] = {
	{ PHY_ID_MATCH_EXACT(ADIN1140_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, adin1140_tbl);

MODULE_DESCRIPTION("Analog Devices, Inc. ADIN1140 10BASE-T1S PHY");
MODULE_AUTHOR("Ciprian Regus <ciprian.regus@analog.com>");
MODULE_LICENSE("GPL");
