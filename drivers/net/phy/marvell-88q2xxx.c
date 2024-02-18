// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell 88Q2XXX automotive 100BASE-T1/1000BASE-T1 PHY driver
 *
 * Derived from Marvell Q222x API
 *
 * Copyright (C) 2024 Liebherr-Electronics and Drives GmbH
 */
#include <linux/ethtool_netlink.h>
#include <linux/marvell_phy.h>
#include <linux/phy.h>

#define PHY_ID_88Q2220_REVB0	(MARVELL_PHY_ID_88Q2220 | 0x1)

#define MDIO_MMD_AN_MV_STAT			32769
#define MDIO_MMD_AN_MV_STAT_ANEG		0x0100
#define MDIO_MMD_AN_MV_STAT_LOCAL_RX		0x1000
#define MDIO_MMD_AN_MV_STAT_REMOTE_RX		0x2000
#define MDIO_MMD_AN_MV_STAT_LOCAL_MASTER	0x4000
#define MDIO_MMD_AN_MV_STAT_MS_CONF_FAULT	0x8000

#define MDIO_MMD_AN_MV_STAT2			32794
#define MDIO_MMD_AN_MV_STAT2_AN_RESOLVED	0x0800
#define MDIO_MMD_AN_MV_STAT2_100BT1		0x2000
#define MDIO_MMD_AN_MV_STAT2_1000BT1		0x4000

#define MDIO_MMD_PCS_MV_INT_EN			32784
#define MDIO_MMD_PCS_MV_INT_EN_LINK_UP		0x0040
#define MDIO_MMD_PCS_MV_INT_EN_LINK_DOWN	0x0080
#define MDIO_MMD_PCS_MV_INT_EN_100BT1		0x1000

#define MDIO_MMD_PCS_MV_GPIO_INT_STAT			32785
#define MDIO_MMD_PCS_MV_GPIO_INT_STAT_LINK_UP		0x0040
#define MDIO_MMD_PCS_MV_GPIO_INT_STAT_LINK_DOWN		0x0080
#define MDIO_MMD_PCS_MV_GPIO_INT_STAT_100BT1_GEN	0x1000

#define MDIO_MMD_PCS_MV_GPIO_INT_CTRL			32787
#define MDIO_MMD_PCS_MV_GPIO_INT_CTRL_TRI_DIS		0x0800

#define MDIO_MMD_PCS_MV_100BT1_STAT1			33032
#define MDIO_MMD_PCS_MV_100BT1_STAT1_IDLE_ERROR		0x00ff
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

#define MDIO_MMD_PCS_MV_100BT1_INT_EN			33042
#define MDIO_MMD_PCS_MV_100BT1_INT_EN_LINKEVENT		0x0400

#define MDIO_MMD_PCS_MV_COPPER_INT_STAT			33043
#define MDIO_MMD_PCS_MV_COPPER_INT_STAT_LINKEVENT	0x0400

#define MDIO_MMD_PCS_MV_RX_STAT			33328

struct mmd_val {
	int devad;
	u32 regnum;
	u16 val;
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

	/* The PHY signalizes it supports autonegotiation. Unfortunately, so
	 * far it was not possible to get a link even when following the init
	 * sequence provided by Marvell. Disable it for now until a proper
	 * workaround is found or a new PHY revision is released.
	 */
	if (phydev->drv->phy_id == MARVELL_PHY_ID_88Q2110)
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				   phydev->supported);

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

static int mv88q222x_soft_reset(struct phy_device *phydev)
{
	int ret;

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

	ret = phy_write_mmd(phydev, MDIO_MMD_PCS, 0xffe4, 0xc);
	if (ret < 0)
		return ret;

	/* Disable RESET of DCL */
	if (phydev->autoneg == AUTONEG_ENABLE || phydev->speed == SPEED_1000)
		return phy_write_mmd(phydev, MDIO_MMD_PCS, 0xfe1b, 0x58);

	return 0;
}

static int mv88q222x_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_config_aneg(phydev);
	if (ret)
		return ret;

	return mv88q222x_soft_reset(phydev);
}

static int mv88q222x_revb0_config_init(struct phy_device *phydev)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(mv88q222x_revb0_init_seq0); i++) {
		ret = phy_write_mmd(phydev, mv88q222x_revb0_init_seq0[i].devad,
				    mv88q222x_revb0_init_seq0[i].regnum,
				    mv88q222x_revb0_init_seq0[i].val);
		if (ret < 0)
			return ret;
	}

	usleep_range(5000, 10000);

	for (i = 0; i < ARRAY_SIZE(mv88q222x_revb0_init_seq1); i++) {
		ret = phy_write_mmd(phydev, mv88q222x_revb0_init_seq1[i].devad,
				    mv88q222x_revb0_init_seq1[i].regnum,
				    mv88q222x_revb0_init_seq1[i].val);
		if (ret < 0)
			return ret;
	}

	/* The 88Q2XXX PHYs do have the extended ability register available, but
	 * register MDIO_PMA_EXTABLE where they should signalize it does not
	 * work according to specification. Therefore, we force it here.
	 */
	phydev->pma_extable = MDIO_PMA_EXTABLE_BT1;

	/* Configure interrupt with default settings, output is driven low for
	 * active interrupt and high for inactive.
	 */
	if (phy_interrupt_is_valid(phydev))
		return phy_set_bits_mmd(phydev, MDIO_MMD_PCS,
					MDIO_MMD_PCS_MV_GPIO_INT_CTRL,
					MDIO_MMD_PCS_MV_GPIO_INT_CTRL_TRI_DIS);

	return 0;
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
		.get_sqi		= mv88q2xxx_get_sqi,
		.get_sqi_max		= mv88q2xxx_get_sqi_max,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_88Q2220_REVB0),
		.name			= "mv88q2220",
		.get_features		= mv88q2xxx_get_features,
		.config_aneg		= mv88q222x_config_aneg,
		.aneg_done		= genphy_c45_aneg_done,
		.config_init		= mv88q222x_revb0_config_init,
		.read_status		= mv88q2xxx_read_status,
		.soft_reset		= mv88q222x_soft_reset,
		.config_intr		= mv88q2xxx_config_intr,
		.handle_interrupt	= mv88q2xxx_handle_interrupt,
		.set_loopback		= genphy_c45_loopback,
		.get_sqi		= mv88q2xxx_get_sqi,
		.get_sqi_max		= mv88q2xxx_get_sqi_max,
		.suspend		= mv88q2xxx_suspend,
		.resume			= mv88q2xxx_resume,
	},
};

module_phy_driver(mv88q2xxx_driver);

static struct mdio_device_id __maybe_unused mv88q2xxx_tbl[] = {
	{ MARVELL_PHY_ID_88Q2110, MARVELL_PHY_ID_MASK },
	{ PHY_ID_MATCH_EXACT(PHY_ID_88Q2220_REVB0), },
	{ /*sentinel*/ }
};
MODULE_DEVICE_TABLE(mdio, mv88q2xxx_tbl);

MODULE_DESCRIPTION("Marvell 88Q2XXX 100/1000BASE-T1 Automotive Ethernet PHY driver");
MODULE_LICENSE("GPL");
