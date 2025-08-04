// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell 88Q2XXX automotive 100BASE-T1/1000BASE-T1 PHY driver
 *
 * Derived from Marvell Q222x API
 *
 * Copyright (C) 2024 Liebherr-Electronics and Drives GmbH
 */
#include <linux/ethtool_netlink.h>
#include <linux/hwmon.h>
#include <linux/marvell_phy.h>
#include <linux/of.h>
#include <linux/phy.h>

#define PHY_ID_88Q2220_REVB0				(MARVELL_PHY_ID_88Q2220 | 0x1)
#define PHY_ID_88Q2220_REVB1				(MARVELL_PHY_ID_88Q2220 | 0x2)
#define PHY_ID_88Q2220_REVB2				(MARVELL_PHY_ID_88Q2220 | 0x3)

#define MDIO_MMD_AN_MV_STAT				32769
#define MDIO_MMD_AN_MV_STAT_ANEG			0x0100
#define MDIO_MMD_AN_MV_STAT_LOCAL_RX			0x1000
#define MDIO_MMD_AN_MV_STAT_REMOTE_RX			0x2000
#define MDIO_MMD_AN_MV_STAT_LOCAL_MASTER		0x4000
#define MDIO_MMD_AN_MV_STAT_MS_CONF_FAULT		0x8000

#define MDIO_MMD_AN_MV_STAT2				32794
#define MDIO_MMD_AN_MV_STAT2_AN_RESOLVED		0x0800
#define MDIO_MMD_AN_MV_STAT2_100BT1			0x2000
#define MDIO_MMD_AN_MV_STAT2_1000BT1			0x4000

#define MDIO_MMD_PCS_MV_RESET_CTRL			32768
#define MDIO_MMD_PCS_MV_RESET_CTRL_TX_DISABLE		0x8

#define MDIO_MMD_PCS_MV_INT_EN				32784
#define MDIO_MMD_PCS_MV_INT_EN_LINK_UP			0x0040
#define MDIO_MMD_PCS_MV_INT_EN_LINK_DOWN		0x0080
#define MDIO_MMD_PCS_MV_INT_EN_100BT1			0x1000

#define MDIO_MMD_PCS_MV_GPIO_INT_STAT			32785
#define MDIO_MMD_PCS_MV_GPIO_INT_STAT_LINK_UP		0x0040
#define MDIO_MMD_PCS_MV_GPIO_INT_STAT_LINK_DOWN		0x0080
#define MDIO_MMD_PCS_MV_GPIO_INT_STAT_100BT1_GEN	0x1000

#define MDIO_MMD_PCS_MV_GPIO_INT_CTRL			32787
#define MDIO_MMD_PCS_MV_GPIO_INT_CTRL_TRI_DIS		0x0800

#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL			32790
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_1_MASK	GENMASK(7, 4)
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_0_MASK	GENMASK(3, 0)
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK		0x0 /* Link established */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_RX_TX	0x1 /* Link established, blink for rx or tx activity */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_1000BT1	0x2 /* Blink 3x for 1000BT1 link established */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_RX_TX_ON		0x3 /* Receive or transmit activity */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_RX_TX		0x4 /* Blink on receive or transmit activity */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_TX		0x5 /* Transmit activity */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_COPPER	0x6 /* Copper Link established */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_1000BT1_ON	0x7 /* 1000BT1 link established */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_FORCE_OFF		0x8 /* Force off */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_FORCE_ON		0x9 /* Force on */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_FORCE_HIGHZ	0xa /* Force Hi-Z */
#define MDIO_MMD_PCS_MV_LED_FUNC_CTRL_FORCE_BLINK	0xb /* Force blink */

#define MDIO_MMD_PCS_MV_TEMP_SENSOR1			32833
#define MDIO_MMD_PCS_MV_TEMP_SENSOR1_RAW_INT		0x0001
#define MDIO_MMD_PCS_MV_TEMP_SENSOR1_INT		0x0040
#define MDIO_MMD_PCS_MV_TEMP_SENSOR1_INT_EN		0x0080

#define MDIO_MMD_PCS_MV_TEMP_SENSOR2			32834
#define MDIO_MMD_PCS_MV_TEMP_SENSOR2_DIS_MASK		0xc000

#define MDIO_MMD_PCS_MV_TEMP_SENSOR3			32835
#define MDIO_MMD_PCS_MV_TEMP_SENSOR3_INT_THRESH_MASK	0xff00
#define MDIO_MMD_PCS_MV_TEMP_SENSOR3_MASK		0x00ff

#define MDIO_MMD_PCS_MV_100BT1_STAT1			33032
#define MDIO_MMD_PCS_MV_100BT1_STAT1_IDLE_ERROR		0x00ff
#define MDIO_MMD_PCS_MV_100BT1_STAT1_JABBER		0x0100
#define MDIO_MMD_PCS_MV_100BT1_STAT1_LINK		0x0200
#define MDIO_MMD_PCS_MV_100BT1_STAT1_LOCAL_RX		0x1000
#define MDIO_MMD_PCS_MV_100BT1_STAT1_REMOTE_RX		0x2000
#define MDIO_MMD_PCS_MV_100BT1_STAT1_LOCAL_MASTER	0x4000

#define MDIO_MMD_PCS_MV_100BT1_STAT2			33033
#define MDIO_MMD_PCS_MV_100BT1_STAT2_JABBER		0x0001
#define MDIO_MMD_PCS_MV_100BT1_STAT2_POL		0x0002
#define MDIO_MMD_PCS_MV_100BT1_STAT2_LINK		0x0004
#define MDIO_MMD_PCS_MV_100BT1_STAT2_ANGE		0x0008

#define MDIO_MMD_PCS_MV_100BT1_INT_EN			33042
#define MDIO_MMD_PCS_MV_100BT1_INT_EN_LINKEVENT		0x0400

#define MDIO_MMD_PCS_MV_COPPER_INT_STAT			33043
#define MDIO_MMD_PCS_MV_COPPER_INT_STAT_LINKEVENT	0x0400

#define MDIO_MMD_PCS_MV_RX_STAT				33328

#define MDIO_MMD_PCS_MV_TDR_RESET			65226
#define MDIO_MMD_PCS_MV_TDR_RESET_TDR_RST		0x1000

#define MDIO_MMD_PCS_MV_TDR_OFF_SHORT_CABLE		65241

#define MDIO_MMD_PCS_MV_TDR_OFF_LONG_CABLE		65242

#define MDIO_MMD_PCS_MV_TDR_STATUS			65245
#define MDIO_MMD_PCS_MV_TDR_STATUS_MASK			0x0003
#define MDIO_MMD_PCS_MV_TDR_STATUS_OFF			0x0001
#define MDIO_MMD_PCS_MV_TDR_STATUS_ON			0x0002
#define MDIO_MMD_PCS_MV_TDR_STATUS_DIST_MASK		0xff00
#define MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_MASK	0x00f0
#define MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_SHORT	0x0030
#define MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_OPEN	0x00e0
#define MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_OK		0x0070
#define MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_IN_PROGR	0x0080
#define MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_NOISE	0x0050

#define MDIO_MMD_PCS_MV_TDR_OFF_CUTOFF			65246

#define MV88Q2XXX_LED_INDEX_TX_ENABLE			0
#define MV88Q2XXX_LED_INDEX_GPIO			1

struct mv88q2xxx_priv {
	bool enable_led0;
};

struct mmd_val {
	int devad;
	u32 regnum;
	u16 val;
};

static const struct mmd_val mv88q2110_init_seq0[] = {
	{ MDIO_MMD_PCS, 0xffe4, 0x07b5 },
	{ MDIO_MMD_PCS, 0xffe4, 0x06b6 },
};

static const struct mmd_val mv88q2110_init_seq1[] = {
	{ MDIO_MMD_PCS, 0xffde, 0x402f },
	{ MDIO_MMD_PCS, 0xfe34, 0x4040 },
	{ MDIO_MMD_PCS, 0xfe2a, 0x3c1d },
	{ MDIO_MMD_PCS, 0xfe34, 0x0040 },
	{ MDIO_MMD_AN, 0x8032, 0x0064 },
	{ MDIO_MMD_AN, 0x8031, 0x0a01 },
	{ MDIO_MMD_AN, 0x8031, 0x0c01 },
	{ MDIO_MMD_PCS, 0xffdb, 0x0010 },
};

static const struct mmd_val mv88q222x_revb0_init_seq0[] = {
	{ MDIO_MMD_PCS, 0x8033, 0x6801 },
	{ MDIO_MMD_AN, MDIO_AN_T1_CTRL, 0x0 },
	{ MDIO_MMD_PMAPMD, MDIO_CTRL1,
	  MDIO_CTRL1_LPOWER | MDIO_PMA_CTRL1_SPEED1000 },
	{ MDIO_MMD_PCS, 0xfe1b, 0x48 },
	{ MDIO_MMD_PCS, 0xffe4, 0x6b6 },
	{ MDIO_MMD_PMAPMD, MDIO_CTRL1, 0x0 },
	{ MDIO_MMD_PCS, MDIO_CTRL1, 0x0 },
};

static const struct mmd_val mv88q222x_revb0_init_seq1[] = {
	{ MDIO_MMD_PCS, 0xfe79, 0x0 },
	{ MDIO_MMD_PCS, 0xfe07, 0x125a },
	{ MDIO_MMD_PCS, 0xfe09, 0x1288 },
	{ MDIO_MMD_PCS, 0xfe08, 0x2588 },
	{ MDIO_MMD_PCS, 0xfe11, 0x1105 },
	{ MDIO_MMD_PCS, 0xfe72, 0x042c },
	{ MDIO_MMD_PCS, 0xfbba, 0xcb2 },
	{ MDIO_MMD_PCS, 0xfbbb, 0xc4a },
	{ MDIO_MMD_AN, 0x8032, 0x2020 },
	{ MDIO_MMD_AN, 0x8031, 0xa28 },
	{ MDIO_MMD_AN, 0x8031, 0xc28 },
	{ MDIO_MMD_PCS, 0xffdb, 0xfc10 },
	{ MDIO_MMD_PCS, 0xfe1b, 0x58 },
	{ MDIO_MMD_PCS, 0xfe79, 0x4 },
	{ MDIO_MMD_PCS, 0xfe5f, 0xe8 },
	{ MDIO_MMD_PCS, 0xfe05, 0x755c },
};

static const struct mmd_val mv88q222x_revb1_init_seq0[] = {
	{ MDIO_MMD_PCS, 0xffe4, 0x0007 },
	{ MDIO_MMD_AN, MDIO_AN_T1_CTRL, 0x0 },
	{ MDIO_MMD_PCS, 0xffe3, 0x7000 },
	{ MDIO_MMD_PMAPMD, MDIO_CTRL1, 0x0840 },
};

static const struct mmd_val mv88q222x_revb2_init_seq0[] = {
	{ MDIO_MMD_PCS, 0xffe4, 0x0007 },
	{ MDIO_MMD_AN, MDIO_AN_T1_CTRL, 0x0 },
	{ MDIO_MMD_PMAPMD, MDIO_CTRL1, 0x0840 },
};

static const struct mmd_val mv88q222x_revb1_revb2_init_seq1[] = {
	{ MDIO_MMD_PCS, 0xfe07, 0x125a },
	{ MDIO_MMD_PCS, 0xfe09, 0x1288 },
	{ MDIO_MMD_PCS, 0xfe08, 0x2588 },
	{ MDIO_MMD_PCS, 0xfe72, 0x042c },
	{ MDIO_MMD_PCS, 0xffe4, 0x0071 },
	{ MDIO_MMD_PCS, 0xffe4, 0x0001 },
	{ MDIO_MMD_PCS, 0xfe1b, 0x0048 },
	{ MDIO_MMD_PMAPMD, 0x0000, 0x0000 },
	{ MDIO_MMD_PCS, 0x0000, 0x0000 },
	{ MDIO_MMD_PCS, 0xffdb, 0xfc10 },
	{ MDIO_MMD_PCS, 0xfe1b, 0x58 },
	{ MDIO_MMD_PCS, 0xfcad, 0x030c },
	{ MDIO_MMD_PCS, 0x8032, 0x6001 },
	{ MDIO_MMD_PCS, 0xfdff, 0x05a5 },
	{ MDIO_MMD_PCS, 0xfdec, 0xdbaf },
	{ MDIO_MMD_PCS, 0xfcab, 0x1054 },
	{ MDIO_MMD_PCS, 0xfcac, 0x1483 },
	{ MDIO_MMD_PCS, 0x8033, 0xc801 },
	{ MDIO_MMD_AN, 0x8032, 0x2020 },
	{ MDIO_MMD_AN, 0x8031, 0xa28 },
	{ MDIO_MMD_AN, 0x8031, 0xc28 },
	{ MDIO_MMD_PCS, 0xfbba, 0x0cb2 },
	{ MDIO_MMD_PCS, 0xfbbb, 0x0c4a },
	{ MDIO_MMD_PCS, 0xfe5f, 0xe8 },
	{ MDIO_MMD_PCS, 0xfe05, 0x755c },
	{ MDIO_MMD_PCS, 0xfa20, 0x002a },
	{ MDIO_MMD_PCS, 0xfe11, 0x1105 },
};

static int mv88q2xxx_write_mmd_vals(struct phy_device *phydev,
				    const struct mmd_val *vals, size_t len)
{
	int ret;

	for (; len; vals++, len--) {
		ret = phy_write_mmd(phydev, vals->devad, vals->regnum,
				    vals->val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mv88q2xxx_soft_reset(struct phy_device *phydev)
{
	int ret;
	int val;

	/* Enable RESET of DCL */
	if (phydev->autoneg == AUTONEG_ENABLE || phydev->speed == SPEED_1000) {
		ret = phy_write_mmd(phydev, MDIO_MMD_PCS, 0xfe1b, 0x48);
		if (ret < 0)
			return ret;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_1000BT1_CTRL,
			    MDIO_PCS_1000BT1_CTRL_RESET);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					MDIO_PCS_1000BT1_CTRL, val,
					!(val & MDIO_PCS_1000BT1_CTRL_RESET),
					50000, 600000, true);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS, 0xffe4, 0xc);
	if (ret < 0)
		return ret;

	/* Disable RESET of DCL */
	if (phydev->autoneg == AUTONEG_ENABLE || phydev->speed == SPEED_1000)
		return phy_write_mmd(phydev, MDIO_MMD_PCS, 0xfe1b, 0x58);

	return 0;
}

static int mv88q2xxx_read_link_gbit(struct phy_device *phydev)
{
	int ret;
	bool link = false;

	/* Read vendor specific Auto-Negotiation status register to get local
	 * and remote receiver status according to software initialization
	 * guide. However, when not in polling mode the local and remote
	 * receiver status are not evaluated due to the Marvell 88Q2xxx APIs.
	 */
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_MMD_AN_MV_STAT);
	if (ret < 0) {
		return ret;
	} else if (((ret & MDIO_MMD_AN_MV_STAT_LOCAL_RX) &&
		   (ret & MDIO_MMD_AN_MV_STAT_REMOTE_RX)) ||
		   !phy_polling_mode(phydev)) {
		/* The link state is latched low so that momentary link
		 * drops can be detected. Do not double-read the status
		 * in polling mode to detect such short link drops except
		 * the link was already down.
		 */
		if (!phy_polling_mode(phydev) || !phydev->link) {
			ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
					   MDIO_PCS_1000BT1_STAT);
			if (ret < 0)
				return ret;
			else if (ret & MDIO_PCS_1000BT1_STAT_LINK)
				link = true;
		}

		if (!link) {
			ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
					   MDIO_PCS_1000BT1_STAT);
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
	if (!phy_polling_mode(phydev)) {
		phydev->link = false;
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
				   MDIO_MMD_PCS_MV_100BT1_STAT2);
		if (ret < 0)
			return ret;

		if (ret & MDIO_MMD_PCS_MV_100BT1_STAT2_LINK)
			phydev->link = true;

		return 0;
	} else if (!phydev->link) {
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
				   MDIO_MMD_PCS_MV_100BT1_STAT1);
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
	/* The 88Q2XXX PHYs do not have the PMA/PMD status register available,
	 * therefore we need to read the link status from the vendor specific
	 * registers depending on the speed.
	 */

	if (phydev->speed == SPEED_1000)
		return mv88q2xxx_read_link_gbit(phydev);
	else if (phydev->speed == SPEED_100)
		return mv88q2xxx_read_link_100m(phydev);

	phydev->link = false;
	return 0;
}

static int mv88q2xxx_read_master_slave_state(struct phy_device *phydev)
{
	int ret;

	phydev->master_slave_state = MASTER_SLAVE_STATE_UNKNOWN;
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_MMD_AN_MV_STAT);
	if (ret < 0)
		return ret;

	if (ret & MDIO_MMD_AN_MV_STAT_LOCAL_MASTER)
		phydev->master_slave_state = MASTER_SLAVE_STATE_MASTER;
	else
		phydev->master_slave_state = MASTER_SLAVE_STATE_SLAVE;

	return 0;
}

static int mv88q2xxx_read_aneg_speed(struct phy_device *phydev)
{
	int ret;

	phydev->speed = SPEED_UNKNOWN;
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_MMD_AN_MV_STAT2);
	if (ret < 0)
		return ret;

	if (!(ret & MDIO_MMD_AN_MV_STAT2_AN_RESOLVED))
		return 0;

	if (ret & MDIO_MMD_AN_MV_STAT2_100BT1)
		phydev->speed = SPEED_100;
	else if (ret & MDIO_MMD_AN_MV_STAT2_1000BT1)
		phydev->speed = SPEED_1000;

	return 0;
}

static int mv88q2xxx_read_status(struct phy_device *phydev)
{
	int ret;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		/* We have to get the negotiated speed first, otherwise we are
		 * not able to read the link.
		 */
		ret = mv88q2xxx_read_aneg_speed(phydev);
		if (ret < 0)
			return ret;

		ret = mv88q2xxx_read_link(phydev);
		if (ret < 0)
			return ret;

		ret = genphy_c45_read_lpa(phydev);
		if (ret < 0)
			return ret;

		ret = genphy_c45_baset1_read_status(phydev);
		if (ret < 0)
			return ret;

		ret = mv88q2xxx_read_master_slave_state(phydev);
		if (ret < 0)
			return ret;

		phy_resolve_aneg_linkmode(phydev);

		return 0;
	}

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

	return 0;
}

static int mv88q2xxx_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_config_aneg(phydev);
	if (ret)
		return ret;

	return phydev->drv->soft_reset(phydev);
}

static int mv88q2xxx_get_sqi(struct phy_device *phydev)
{
	int ret;

	if (phydev->speed == SPEED_100) {
		/* Read the SQI from the vendor specific receiver status
		 * register
		 */
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
				   MDIO_MMD_PCS_MV_RX_STAT);
		if (ret < 0)
			return ret;

		ret = ret >> 12;
	} else {
		/* Read from vendor specific registers, they are not documented
		 * but can be found in the Software Initialization Guide. Only
		 * revisions >= A0 are supported.
		 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, 0xfc5d, 0xff, 0xac);
		if (ret < 0)
			return ret;

		ret = phy_read_mmd(phydev, MDIO_MMD_PCS, 0xfc88);
		if (ret < 0)
			return ret;
	}

	return ret & 0x0f;
}

static int mv88q2xxx_get_sqi_max(struct phy_device *phydev)
{
	return 15;
}

static int mv88q2xxx_config_intr(struct phy_device *phydev)
{
	int ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* Enable interrupts for 1000BASE-T1 link up and down events
		 * and enable general interrupts for 100BASE-T1.
		 */
		ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
				    MDIO_MMD_PCS_MV_INT_EN,
				    MDIO_MMD_PCS_MV_INT_EN_LINK_UP |
				    MDIO_MMD_PCS_MV_INT_EN_LINK_DOWN |
				    MDIO_MMD_PCS_MV_INT_EN_100BT1);
		if (ret < 0)
			return ret;

		/* Enable interrupts for 100BASE-T1 link events */
		return phy_write_mmd(phydev, MDIO_MMD_PCS,
				     MDIO_MMD_PCS_MV_100BT1_INT_EN,
				     MDIO_MMD_PCS_MV_100BT1_INT_EN_LINKEVENT);
	} else {
		ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
				    MDIO_MMD_PCS_MV_INT_EN, 0);
		if (ret < 0)
			return ret;

		return phy_write_mmd(phydev, MDIO_MMD_PCS,
				     MDIO_MMD_PCS_MV_100BT1_INT_EN, 0);
	}
}

static irqreturn_t mv88q2xxx_handle_interrupt(struct phy_device *phydev)
{
	bool trigger_machine = false;
	int irq;

	/* Before we can acknowledge the 100BT1 general interrupt, that is in
	 * the 1000BT1 interrupt status register, we have to acknowledge any
	 * interrupts that are related to it. Therefore we read first the 100BT1
	 * interrupt status register, followed by reading the 1000BT1 interrupt
	 * status register.
	 */

	irq = phy_read_mmd(phydev, MDIO_MMD_PCS,
			   MDIO_MMD_PCS_MV_COPPER_INT_STAT);
	if (irq < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	/* Check link status for 100BT1 */
	if (irq & MDIO_MMD_PCS_MV_COPPER_INT_STAT_LINKEVENT)
		trigger_machine = true;

	irq = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_GPIO_INT_STAT);
	if (irq < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	/* Check link status for 1000BT1 */
	if ((irq & MDIO_MMD_PCS_MV_GPIO_INT_STAT_LINK_UP) ||
	    (irq & MDIO_MMD_PCS_MV_GPIO_INT_STAT_LINK_DOWN))
		trigger_machine = true;

	if (!trigger_machine)
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int mv88q2xxx_suspend(struct phy_device *phydev)
{
	int ret;

	/* Disable PHY interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_DISABLED;
		ret = mv88q2xxx_config_intr(phydev);
		if (ret)
			return ret;
	}

	return phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
				MDIO_CTRL1_LPOWER);
}

static int mv88q2xxx_resume(struct phy_device *phydev)
{
	int ret;

	/* Enable PHY interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_ENABLED;
		ret = mv88q2xxx_config_intr(phydev);
		if (ret)
			return ret;
	}

	return phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
				  MDIO_CTRL1_LPOWER);
}

#if IS_ENABLED(CONFIG_HWMON)
static int mv88q2xxx_enable_temp_sense(struct phy_device *phydev)
{
	return phy_modify_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_TEMP_SENSOR2,
			      MDIO_MMD_PCS_MV_TEMP_SENSOR2_DIS_MASK, 0);
}

static const struct hwmon_channel_info * const mv88q2xxx_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_ALARM),
	NULL
};

static umode_t mv88q2xxx_hwmon_is_visible(const void *data,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	case hwmon_temp_max:
		return 0644;
	case hwmon_temp_alarm:
		return 0444;
	default:
		return 0;
	}
}

static int mv88q2xxx_hwmon_read(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, long *val)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
				   MDIO_MMD_PCS_MV_TEMP_SENSOR3);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(MDIO_MMD_PCS_MV_TEMP_SENSOR3_MASK, ret);
		*val = (ret - 75) * 1000;
		return 0;
	case hwmon_temp_max:
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
				   MDIO_MMD_PCS_MV_TEMP_SENSOR3);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(MDIO_MMD_PCS_MV_TEMP_SENSOR3_INT_THRESH_MASK,
				ret);
		*val = (ret - 75) * 1000;
		return 0;
	case hwmon_temp_alarm:
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
				   MDIO_MMD_PCS_MV_TEMP_SENSOR1);
		if (ret < 0)
			return ret;

		*val = !!(ret & MDIO_MMD_PCS_MV_TEMP_SENSOR1_RAW_INT);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mv88q2xxx_hwmon_write(struct device *dev,
				 enum hwmon_sensor_types type, u32 attr,
				 int channel, long val)
{
	struct phy_device *phydev = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_max:
		clamp_val(val, -75000, 180000);
		val = (val / 1000) + 75;
		val = FIELD_PREP(MDIO_MMD_PCS_MV_TEMP_SENSOR3_INT_THRESH_MASK,
				 val);
		return phy_modify_mmd(phydev, MDIO_MMD_PCS,
				      MDIO_MMD_PCS_MV_TEMP_SENSOR3,
				      MDIO_MMD_PCS_MV_TEMP_SENSOR3_INT_THRESH_MASK,
				      val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops mv88q2xxx_hwmon_hwmon_ops = {
	.is_visible = mv88q2xxx_hwmon_is_visible,
	.read = mv88q2xxx_hwmon_read,
	.write = mv88q2xxx_hwmon_write,
};

static const struct hwmon_chip_info mv88q2xxx_hwmon_chip_info = {
	.ops = &mv88q2xxx_hwmon_hwmon_ops,
	.info = mv88q2xxx_hwmon_info,
};

static int mv88q2xxx_hwmon_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct device *hwmon;
	int ret;

	ret = mv88q2xxx_enable_temp_sense(phydev);
	if (ret < 0)
		return ret;

	hwmon = devm_hwmon_device_register_with_info(dev, NULL, phydev,
						     &mv88q2xxx_hwmon_chip_info,
						     NULL);

	return PTR_ERR_OR_ZERO(hwmon);
}

#else
static int mv88q2xxx_enable_temp_sense(struct phy_device *phydev)
{
	return 0;
}

static int mv88q2xxx_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_OF_MDIO)
static int mv88q2xxx_leds_probe(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct mv88q2xxx_priv *priv = phydev->priv;
	struct device_node *leds;
	int ret = 0;
	u32 index;

	if (!node)
		return 0;

	leds = of_get_child_by_name(node, "leds");
	if (!leds)
		return 0;

	for_each_available_child_of_node_scoped(leds, led) {
		ret = of_property_read_u32(led, "reg", &index);
		if (ret)
			goto exit;

		if (index > MV88Q2XXX_LED_INDEX_GPIO) {
			ret = -EINVAL;
			goto exit;
		}

		if (index == MV88Q2XXX_LED_INDEX_TX_ENABLE)
			priv->enable_led0 = true;
	}

exit:
	of_node_put(leds);

	return ret;
}

#else
static int mv88q2xxx_leds_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

static int mv88q2xxx_probe(struct phy_device *phydev)
{
	struct mv88q2xxx_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	ret = mv88q2xxx_leds_probe(phydev);
	if (ret)
		return ret;

	return mv88q2xxx_hwmon_probe(phydev);
}

static int mv88q2xxx_config_init(struct phy_device *phydev)
{
	struct mv88q2xxx_priv *priv = phydev->priv;
	int ret;

	/* The 88Q2XXX PHYs do have the extended ability register available, but
	 * register MDIO_PMA_EXTABLE where they should signalize it does not
	 * work according to specification. Therefore, we force it here.
	 */
	phydev->pma_extable = MDIO_PMA_EXTABLE_BT1;

	/* Configure interrupt with default settings, output is driven low for
	 * active interrupt and high for inactive.
	 */
	if (phy_interrupt_is_valid(phydev)) {
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_PCS,
				       MDIO_MMD_PCS_MV_GPIO_INT_CTRL,
				       MDIO_MMD_PCS_MV_GPIO_INT_CTRL_TRI_DIS);
		if (ret < 0)
			return ret;
	}

	/* Enable LED function and disable TX disable feature on LED/TX_ENABLE */
	if (priv->enable_led0) {
		ret = phy_clear_bits_mmd(phydev, MDIO_MMD_PCS,
					 MDIO_MMD_PCS_MV_RESET_CTRL,
					 MDIO_MMD_PCS_MV_RESET_CTRL_TX_DISABLE);
		if (ret < 0)
			return ret;
	}

	/* Enable temperature sense again. There might have been a hard reset
	 * of the PHY and in this case the register content is restored to
	 * defaults and we need to enable it again.
	 */
	ret = mv88q2xxx_enable_temp_sense(phydev);
	if (ret < 0)
		return ret;

	return 0;
}

static int mv88q2110_config_init(struct phy_device *phydev)
{
	int ret;

	ret = mv88q2xxx_write_mmd_vals(phydev, mv88q2110_init_seq0,
				       ARRAY_SIZE(mv88q2110_init_seq0));
	if (ret < 0)
		return ret;

	usleep_range(5000, 10000);

	ret = mv88q2xxx_write_mmd_vals(phydev, mv88q2110_init_seq1,
				       ARRAY_SIZE(mv88q2110_init_seq1));
	if (ret < 0)
		return ret;

	return mv88q2xxx_config_init(phydev);
}

static int mv88q222x_revb0_config_init(struct phy_device *phydev)
{
	int ret;

	ret = mv88q2xxx_write_mmd_vals(phydev, mv88q222x_revb0_init_seq0,
				       ARRAY_SIZE(mv88q222x_revb0_init_seq0));
	if (ret < 0)
		return ret;

	usleep_range(5000, 10000);

	ret = mv88q2xxx_write_mmd_vals(phydev, mv88q222x_revb0_init_seq1,
				       ARRAY_SIZE(mv88q222x_revb0_init_seq1));
	if (ret < 0)
		return ret;

	return mv88q2xxx_config_init(phydev);
}

static int mv88q222x_revb1_revb2_config_init(struct phy_device *phydev)
{
	bool is_rev_b1 = phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] == PHY_ID_88Q2220_REVB1;
	int ret;

	if (is_rev_b1)
		ret = mv88q2xxx_write_mmd_vals(phydev, mv88q222x_revb1_init_seq0,
					       ARRAY_SIZE(mv88q222x_revb1_init_seq0));
	else
		ret = mv88q2xxx_write_mmd_vals(phydev, mv88q222x_revb2_init_seq0,
					       ARRAY_SIZE(mv88q222x_revb2_init_seq0));
	if (ret < 0)
		return ret;

	usleep_range(3000, 5000);

	ret = mv88q2xxx_write_mmd_vals(phydev, mv88q222x_revb1_revb2_init_seq1,
				       ARRAY_SIZE(mv88q222x_revb1_revb2_init_seq1));
	if (ret < 0)
		return ret;

	return mv88q2xxx_config_init(phydev);
}

static int mv88q222x_config_init(struct phy_device *phydev)
{
	if (phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] == PHY_ID_88Q2220_REVB0)
		return mv88q222x_revb0_config_init(phydev);
	else
		return mv88q222x_revb1_revb2_config_init(phydev);
}

static int mv88q222x_cable_test_start(struct phy_device *phydev)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
			    MDIO_MMD_PCS_MV_TDR_OFF_CUTOFF, 0x0058);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
			    MDIO_MMD_PCS_MV_TDR_OFF_LONG_CABLE, 0x00eb);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
			    MDIO_MMD_PCS_MV_TDR_OFF_SHORT_CABLE, 0x010e);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_TDR_RESET,
			    0x0d90);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_TDR_STATUS,
			    MDIO_MMD_PCS_MV_TDR_STATUS_ON);
	if (ret < 0)
		return ret;

	/* According to the Marvell API the test is finished within 500 ms */
	msleep(500);

	return 0;
}

static int mv88q222x_cable_test_get_status(struct phy_device *phydev,
					   bool *finished)
{
	int ret, status;
	u32 dist;

	status = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_TDR_STATUS);
	if (status < 0)
		return status;

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_TDR_RESET,
			    MDIO_MMD_PCS_MV_TDR_RESET_TDR_RST | 0xd90);
	if (ret < 0)
		return ret;

	/* Test could not be finished */
	if (FIELD_GET(MDIO_MMD_PCS_MV_TDR_STATUS_MASK, status) !=
	    MDIO_MMD_PCS_MV_TDR_STATUS_OFF)
		return -ETIMEDOUT;

	*finished = true;
	/* Fault length reported in meters, convert to centimeters */
	dist = FIELD_GET(MDIO_MMD_PCS_MV_TDR_STATUS_DIST_MASK, status) * 100;
	switch (status & MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_MASK) {
	case MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_OPEN:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_OPEN);
		ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_A,
					      dist);
		break;
	case MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_SHORT:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT);
		ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_A,
					      dist);
		break;
	case MDIO_MMD_PCS_MV_TDR_STATUS_VCT_STAT_OK:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_OK);
		break;
	default:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC);
	}

	return 0;
}

static int mv88q2xxx_led_mode(u8 index, unsigned long rules)
{
	switch (rules) {
	case BIT(TRIGGER_NETDEV_LINK):
		return MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK;
	case BIT(TRIGGER_NETDEV_LINK_1000):
		return MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_1000BT1_ON;
	case BIT(TRIGGER_NETDEV_TX):
		return MDIO_MMD_PCS_MV_LED_FUNC_CTRL_TX;
	case BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX):
		return MDIO_MMD_PCS_MV_LED_FUNC_CTRL_RX_TX;
	case BIT(TRIGGER_NETDEV_LINK) | BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX):
		return MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_RX_TX;
	default:
		return -EOPNOTSUPP;
	}
}

static int mv88q2xxx_led_hw_is_supported(struct phy_device *phydev, u8 index,
					 unsigned long rules)
{
	int mode;

	mode = mv88q2xxx_led_mode(index, rules);
	if (mode < 0)
		return mode;

	return 0;
}

static int mv88q2xxx_led_hw_control_set(struct phy_device *phydev, u8 index,
					unsigned long rules)
{
	int mode;

	mode = mv88q2xxx_led_mode(index, rules);
	if (mode < 0)
		return mode;

	if (index == MV88Q2XXX_LED_INDEX_TX_ENABLE)
		return phy_modify_mmd(phydev, MDIO_MMD_PCS,
				      MDIO_MMD_PCS_MV_LED_FUNC_CTRL,
				      MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_0_MASK,
				      FIELD_PREP(MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_0_MASK,
						 mode));
	else
		return phy_modify_mmd(phydev, MDIO_MMD_PCS,
				      MDIO_MMD_PCS_MV_LED_FUNC_CTRL,
				      MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_1_MASK,
				      FIELD_PREP(MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_1_MASK,
						 mode));
}

static int mv88q2xxx_led_hw_control_get(struct phy_device *phydev, u8 index,
					unsigned long *rules)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_MMD_PCS_MV_LED_FUNC_CTRL);
	if (val < 0)
		return val;

	if (index == MV88Q2XXX_LED_INDEX_TX_ENABLE)
		val = FIELD_GET(MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_0_MASK, val);
	else
		val = FIELD_GET(MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LED_1_MASK, val);

	switch (val) {
	case MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK:
		*rules = BIT(TRIGGER_NETDEV_LINK);
		break;
	case MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_1000BT1_ON:
		*rules = BIT(TRIGGER_NETDEV_LINK_1000);
		break;
	case MDIO_MMD_PCS_MV_LED_FUNC_CTRL_TX:
		*rules = BIT(TRIGGER_NETDEV_TX);
		break;
	case MDIO_MMD_PCS_MV_LED_FUNC_CTRL_RX_TX:
		*rules = BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX);
		break;
	case MDIO_MMD_PCS_MV_LED_FUNC_CTRL_LINK_RX_TX:
		*rules = BIT(TRIGGER_NETDEV_LINK) | BIT(TRIGGER_NETDEV_TX) |
			 BIT(TRIGGER_NETDEV_RX);
		break;
	default:
		*rules = 0;
		break;
	}

	return 0;
}

static struct phy_driver mv88q2xxx_driver[] = {
	{
		.phy_id			= MARVELL_PHY_ID_88Q2110,
		.phy_id_mask		= MARVELL_PHY_ID_MASK,
		.name			= "mv88q2110",
		.probe			= mv88q2xxx_probe,
		.get_features		= mv88q2xxx_get_features,
		.config_aneg		= mv88q2xxx_config_aneg,
		.config_init		= mv88q2110_config_init,
		.read_status		= mv88q2xxx_read_status,
		.soft_reset		= mv88q2xxx_soft_reset,
		.set_loopback		= genphy_c45_loopback,
		.get_sqi		= mv88q2xxx_get_sqi,
		.get_sqi_max		= mv88q2xxx_get_sqi_max,
	},
	{
		.phy_id			= MARVELL_PHY_ID_88Q2220,
		.phy_id_mask		= MARVELL_PHY_ID_MASK,
		.name			= "mv88q2220",
		.flags			= PHY_POLL_CABLE_TEST,
		.probe			= mv88q2xxx_probe,
		.get_features		= mv88q2xxx_get_features,
		.config_aneg		= mv88q2xxx_config_aneg,
		.aneg_done		= genphy_c45_aneg_done,
		.config_init		= mv88q222x_config_init,
		.read_status		= mv88q2xxx_read_status,
		.soft_reset		= mv88q2xxx_soft_reset,
		.config_intr		= mv88q2xxx_config_intr,
		.handle_interrupt	= mv88q2xxx_handle_interrupt,
		.set_loopback		= genphy_c45_loopback,
		.cable_test_start	= mv88q222x_cable_test_start,
		.cable_test_get_status	= mv88q222x_cable_test_get_status,
		.get_sqi		= mv88q2xxx_get_sqi,
		.get_sqi_max		= mv88q2xxx_get_sqi_max,
		.suspend		= mv88q2xxx_suspend,
		.resume			= mv88q2xxx_resume,
		.led_hw_is_supported	= mv88q2xxx_led_hw_is_supported,
		.led_hw_control_set	= mv88q2xxx_led_hw_control_set,
		.led_hw_control_get	= mv88q2xxx_led_hw_control_get,
	},
};

module_phy_driver(mv88q2xxx_driver);

static const struct mdio_device_id __maybe_unused mv88q2xxx_tbl[] = {
	{ MARVELL_PHY_ID_88Q2110, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88Q2220, MARVELL_PHY_ID_MASK },
	{ /*sentinel*/ }
};
MODULE_DEVICE_TABLE(mdio, mv88q2xxx_tbl);

MODULE_DESCRIPTION("Marvell 88Q2XXX 100/1000BASE-T1 Automotive Ethernet PHY driver");
MODULE_LICENSE("GPL");
