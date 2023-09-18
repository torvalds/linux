// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell 88Q2XXX automotive 100BASE-T1/1000BASE-T1 PHY driver
 */
#include <linux/ethtool_netlink.h>
#include <linux/marvell_phy.h>
#include <linux/phy.h>

#define MDIO_MMD_AN_MV_STAT			32769
#define MDIO_MMD_AN_MV_STAT_ANEG		0x0100
#define MDIO_MMD_AN_MV_STAT_LOCAL_RX		0x1000
#define MDIO_MMD_AN_MV_STAT_REMOTE_RX		0x2000
#define MDIO_MMD_AN_MV_STAT_LOCAL_MASTER	0x4000
#define MDIO_MMD_AN_MV_STAT_MS_CONF_FAULT	0x8000

#define MDIO_MMD_PCS_MV_100BT1_STAT1			33032
#define MDIO_MMD_PCS_MV_100BT1_STAT1_IDLE_ERROR	0x00FF
#define MDIO_MMD_PCS_MV_100BT1_STAT1_JABBER		0x0100
#define MDIO_MMD_PCS_MV_100BT1_STAT1_LINK		0x0200
#define MDIO_MMD_PCS_MV_100BT1_STAT1_LOCAL_RX		0x1000
#define MDIO_MMD_PCS_MV_100BT1_STAT1_REMOTE_RX		0x2000
#define MDIO_MMD_PCS_MV_100BT1_STAT1_LOCAL_MASTER	0x4000

#define MDIO_MMD_PCS_MV_100BT1_STAT2		33033
#define MDIO_MMD_PCS_MV_100BT1_STAT2_JABBER	0x0001
#define MDIO_MMD_PCS_MV_100BT1_STAT2_POL	0x0002
#define MDIO_MMD_PCS_MV_100BT1_STAT2_LINK	0x0004
#define MDIO_MMD_PCS_MV_100BT1_STAT2_ANGE	0x0008

static int mv88q2xxx_soft_reset(struct phy_device *phydev)
{
	int ret;
	int val;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
			    MDIO_PCS_1000BT1_CTRL, MDIO_PCS_1000BT1_CTRL_RESET);
	if (ret < 0)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					 MDIO_PCS_1000BT1_CTRL, val,
					 !(val & MDIO_PCS_1000BT1_CTRL_RESET),
					 50000, 600000, true);
}

static int mv88q2xxx_read_link_gbit(struct phy_device *phydev)
{
	int ret;
	bool link = false;

	/* Read vendor specific Auto-Negotiation status register to get local
	 * and remote receiver status according to software initialization
	 * guide.
	 */
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_MMD_AN_MV_STAT);
	if (ret < 0) {
		return ret;
	} else if ((ret & MDIO_MMD_AN_MV_STAT_LOCAL_RX) &&
		   (ret & MDIO_MMD_AN_MV_STAT_REMOTE_RX)) {
		/* The link state is latched low so that momentary link
		 * drops can be detected. Do not double-read the status
		 * in polling mode to detect such short link drops except
		 * the link was already down.
		 */
		if (!phy_polling_mode(phydev) || !phydev->link) {
			ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_1000BT1_STAT);
			if (ret < 0)
				return ret;
			else if (ret & MDIO_PCS_1000BT1_STAT_LINK)
				link = true;
		}

		if (!link) {
			ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_1000BT1_STAT);
			if (ret < 0)
				return ret;
			else if (ret & MDIO_PCS_1000BT1_STAT_LINK)
				link = true;
		}
	}

	phydev->link = link;

	return 0;
}

static int mv88q2xxx_read_link_100m(struct phy_device *phydev)
{
	int ret;

	/* The link state is latched low so that momentary link
	 * drops can be detected. Do not double-read the status
	 * in polling mode to detect such short link drops except
	 * the link was already down. In case we are not polling,
	 * we always read the realtime status.
	 */
	if (!phy_polling_mode(phydev) || !phydev->link) {
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_100BT1_STAT1);
		if (ret < 0)
			return ret;
		else if (ret & MDIO_MMD_PCS_MV_100BT1_STAT1_LINK)
			goto out;
	}

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_100BT1_STAT1);
	if (ret < 0)
		return ret;

out:
	/* Check if we have link and if the remote and local receiver are ok */
	if ((ret & MDIO_MMD_PCS_MV_100BT1_STAT1_LINK) &&
	    (ret & MDIO_MMD_PCS_MV_100BT1_STAT1_LOCAL_RX) &&
	    (ret & MDIO_MMD_PCS_MV_100BT1_STAT1_REMOTE_RX))
		phydev->link = true;
	else
		phydev->link = false;

	return 0;
}

static int mv88q2xxx_read_link(struct phy_device *phydev)
{
	int ret;

	/* The 88Q2XXX PHYs do not have the PMA/PMD status register available,
	 * therefore we need to read the link status from the vendor specific
	 * registers depending on the speed.
	 */
	if (phydev->speed == SPEED_1000)
		ret = mv88q2xxx_read_link_gbit(phydev);
	else
		ret = mv88q2xxx_read_link_100m(phydev);

	return ret;
}

static int mv88q2xxx_read_status(struct phy_device *phydev)
{
	int ret;

	ret = mv88q2xxx_read_link(phydev);
	if (ret < 0)
		return ret;

	return genphy_c45_read_pma(phydev);
}

static int mv88q2xxx_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret)
		return ret;

	/* We need to read the baset1 extended abilities manually because the
	 * PHY does not signalize it has the extended abilities register
	 * available.
	 */
	ret = genphy_c45_pma_baset1_read_abilities(phydev);
	if (ret)
		return ret;

	/* The PHY signalizes it supports autonegotiation. Unfortunately, so
	 * far it was not possible to get a link even when following the init
	 * sequence provided by Marvell. Disable it for now until a proper
	 * workaround is found or a new PHY revision is released.
	 */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported);

	return 0;
}

static int mv88q2xxx_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_config_aneg(phydev);
	if (ret)
		return ret;

	return mv88q2xxx_soft_reset(phydev);
}

static int mv88q2xxx_config_init(struct phy_device *phydev)
{
	int ret;

	/* The 88Q2XXX PHYs do have the extended ability register available, but
	 * register MDIO_PMA_EXTABLE where they should signalize it does not
	 * work according to specification. Therefore, we force it here.
	 */
	phydev->pma_extable = MDIO_PMA_EXTABLE_BT1;

	/* Read the current PHY configuration */
	ret = genphy_c45_read_pma(phydev);
	if (ret)
		return ret;

	return mv88q2xxx_config_aneg(phydev);
}

static int mv88q2xxxx_get_sqi(struct phy_device *phydev)
{
	int ret;

	if (phydev->speed == SPEED_100) {
		/* Read the SQI from the vendor specific receiver status
		 * register
		 */
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS, 0x8230);
		if (ret < 0)
			return ret;

		ret = ret >> 12;
	} else {
		/* Read from vendor specific registers, they are not documented
		 * but can be found in the Software Initialization Guide. Only
		 * revisions >= A0 are supported.
		 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, 0xFC5D, 0x00FF, 0x00AC);
		if (ret < 0)
			return ret;

		ret = phy_read_mmd(phydev, MDIO_MMD_PCS, 0xfc88);
		if (ret < 0)
			return ret;
	}

	return ret & 0x0F;
}

static int mv88q2xxxx_get_sqi_max(struct phy_device *phydev)
{
	return 15;
}

static struct phy_driver mv88q2xxx_driver[] = {
	{
		.phy_id			= MARVELL_PHY_ID_88Q2110,
		.phy_id_mask		= MARVELL_PHY_ID_MASK,
		.name			= "mv88q2110",
		.get_features		= mv88q2xxx_get_features,
		.config_aneg		= mv88q2xxx_config_aneg,
		.config_init		= mv88q2xxx_config_init,
		.read_status		= mv88q2xxx_read_status,
		.soft_reset		= mv88q2xxx_soft_reset,
		.set_loopback		= genphy_c45_loopback,
		.get_sqi		= mv88q2xxxx_get_sqi,
		.get_sqi_max		= mv88q2xxxx_get_sqi_max,
	},
};

module_phy_driver(mv88q2xxx_driver);

static struct mdio_device_id __maybe_unused mv88q2xxx_tbl[] = {
	{ MARVELL_PHY_ID_88Q2110, MARVELL_PHY_ID_MASK },
	{ /*sentinel*/ }
};
MODULE_DEVICE_TABLE(mdio, mv88q2xxx_tbl);

MODULE_DESCRIPTION("Marvell 88Q2XXX 100/1000BASE-T1 Automotive Ethernet PHY driver");
MODULE_LICENSE("GPL");
