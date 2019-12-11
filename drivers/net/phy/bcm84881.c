// SPDX-License-Identifier: GPL-2.0
// Broadcom BCM84881 NBASE-T PHY driver, as found on a SFP+ module.
// Copyright (C) 2019 Russell King, Deep Blue Solutions Ltd.
//
// Like the Marvell 88x3310, the Broadcom 84881 changes its host-side
// interface according to the operating speed between 10GBASE-R,
// 2500BASE-X and SGMII (but unlike the 88x3310, without the control
// word).
//
// This driver only supports those aspects of the PHY that I'm able to
// observe and test with the SFP+ module, which is an incomplete subset
// of what this PHY is able to support. For example, I only assume it
// supports a single lane Serdes connection, but it may be that the PHY
// is able to support more than that.
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/phy.h>

enum {
	MDIO_AN_C22 = 0xffe0,
};

static int bcm84881_wait_init(struct phy_device *phydev)
{
	unsigned int tries = 20;
	int ret, val;

	do {
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
		if (val < 0) {
			ret = val;
			break;
		}
		if (!(val & MDIO_CTRL1_RESET)) {
			ret = 0;
			break;
		}
		if (!--tries) {
			ret = -ETIMEDOUT;
			break;
		}
		msleep(100);
	} while (1);

	if (ret)
		phydev_err(phydev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static int bcm84881_config_init(struct phy_device *phydev)
{
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_10GKR:
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

static int bcm84881_probe(struct phy_device *phydev)
{
	/* This driver requires PMAPMD and AN blocks */
	const u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	return 0;
}

static int bcm84881_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret)
		return ret;

	/* Although the PHY sets bit 1.11.8, it does not support 10M modes */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
			   phydev->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
			   phydev->supported);

	return 0;
}

static int bcm84881_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u32 adv;
	int ret;

	/* Wait for the PHY to finish initialising, otherwise our
	 * advertisement may be overwritten.
	 */
	ret = bcm84881_wait_init(phydev);
	if (ret)
		return ret;

	/* We don't support manual MDI control */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	/* disabled autoneg doesn't seem to work with this PHY */
	if (phydev->autoneg == AUTONEG_DISABLE)
		return -EINVAL;

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN,
				     MDIO_AN_C22 + MII_CTRL1000,
				     ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				     adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int bcm84881_aneg_done(struct phy_device *phydev)
{
	int bmsr, val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	bmsr = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_C22 + MII_BMSR);
	if (bmsr < 0)
		return val;

	return !!(val & MDIO_AN_STAT1_COMPLETE) &&
	       !!(bmsr & BMSR_ANEGCOMPLETE);
}

static int bcm84881_read_status(struct phy_device *phydev)
{
	unsigned int mode;
	int bmsr, val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
	if (val < 0)
		return val;

	if (val & MDIO_AN_CTRL1_RESTART) {
		phydev->link = 0;
		return 0;
	}

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	bmsr = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_C22 + MII_BMSR);
	if (bmsr < 0)
		return val;

	phydev->autoneg_complete = !!(val & MDIO_AN_STAT1_COMPLETE) &&
				   !!(bmsr & BMSR_ANEGCOMPLETE);
	phydev->link = !!(val & MDIO_STAT1_LSTATUS) &&
		       !!(bmsr & BMSR_LSTATUS);
	if (phydev->autoneg == AUTONEG_ENABLE && !phydev->autoneg_complete)
		phydev->link = false;

	if (!phydev->link)
		return 0;

	linkmode_zero(phydev->lp_advertising);
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->mdix = 0;

	if (phydev->autoneg_complete) {
		val = genphy_c45_read_lpa(phydev);
		if (val < 0)
			return val;

		val = phy_read_mmd(phydev, MDIO_MMD_AN,
				   MDIO_AN_C22 + MII_STAT1000);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);

		if (phydev->autoneg == AUTONEG_ENABLE)
			phy_resolve_aneg_linkmode(phydev);
	}

	if (phydev->autoneg == AUTONEG_DISABLE) {
		/* disabled autoneg doesn't seem to work, so force the link
		 * down.
		 */
		phydev->link = 0;
		return 0;
	}

	/* Set the host link mode - we set the phy interface mode and
	 * the speed according to this register so that downshift works.
	 * We leave the duplex setting as per the resolution from the
	 * above.
	 */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, 0x4011);
	mode = (val & 0x1e) >> 1;
	if (mode == 1 || mode == 2)
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
	else if (mode == 3)
		phydev->interface = PHY_INTERFACE_MODE_10GKR;
	else if (mode == 4)
		phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
	switch (mode & 7) {
	case 1:
		phydev->speed = SPEED_100;
		break;
	case 2:
		phydev->speed = SPEED_1000;
		break;
	case 3:
		phydev->speed = SPEED_10000;
		break;
	case 4:
		phydev->speed = SPEED_2500;
		break;
	case 5:
		phydev->speed = SPEED_5000;
		break;
	}

	return genphy_c45_read_mdix(phydev);
}

static struct phy_driver bcm84881_drivers[] = {
	{
		.phy_id		= 0xae025150,
		.phy_id_mask	= 0xfffffff0,
		.name		= "Broadcom BCM84881",
		.config_init	= bcm84881_config_init,
		.probe		= bcm84881_probe,
		.get_features	= bcm84881_get_features,
		.config_aneg	= bcm84881_config_aneg,
		.aneg_done	= bcm84881_aneg_done,
		.read_status	= bcm84881_read_status,
	},
};

module_phy_driver(bcm84881_drivers);

/* FIXME: module auto-loading for Clause 45 PHYs seems non-functional */
static struct mdio_device_id __maybe_unused bcm84881_tbl[] = {
	{ 0xae025150, 0xfffffff0 },
	{ },
};
MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Broadcom BCM84881 PHY driver");
MODULE_DEVICE_TABLE(mdio, bcm84881_tbl);
MODULE_LICENSE("GPL");
