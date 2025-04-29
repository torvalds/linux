// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clause 45 PHY support
 */
#include <linux/ethtool.h>
#include <linux/export.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/phy.h>

#include "mdio-open-alliance.h"
#include "phylib-internal.h"

/**
 * genphy_c45_baset1_able - checks if the PMA has BASE-T1 extended abilities
 * @phydev: target phy_device struct
 */
static bool genphy_c45_baset1_able(struct phy_device *phydev)
{
	int val;

	if (phydev->pma_extable == -ENODATA) {
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_EXTABLE);
		if (val < 0)
			return false;

		phydev->pma_extable = val;
	}

	return !!(phydev->pma_extable & MDIO_PMA_EXTABLE_BT1);
}

/**
 * genphy_c45_pma_can_sleep - checks if the PMA have sleep support
 * @phydev: target phy_device struct
 */
static bool genphy_c45_pma_can_sleep(struct phy_device *phydev)
{
	int stat1;

	stat1 = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_STAT1);
	if (stat1 < 0)
		return false;

	return !!(stat1 & MDIO_STAT1_LPOWERABLE);
}

/**
 * genphy_c45_pma_resume - wakes up the PMA module
 * @phydev: target phy_device struct
 */
int genphy_c45_pma_resume(struct phy_device *phydev)
{
	if (!genphy_c45_pma_can_sleep(phydev))
		return -EOPNOTSUPP;

	return phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
				  MDIO_CTRL1_LPOWER);
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_resume);

/**
 * genphy_c45_pma_suspend - suspends the PMA module
 * @phydev: target phy_device struct
 */
int genphy_c45_pma_suspend(struct phy_device *phydev)
{
	if (!genphy_c45_pma_can_sleep(phydev))
		return -EOPNOTSUPP;

	return phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
				MDIO_CTRL1_LPOWER);
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_suspend);

/**
 * genphy_c45_pma_baset1_setup_master_slave - configures forced master/slave
 * role of BaseT1 devices.
 * @phydev: target phy_device struct
 */
int genphy_c45_pma_baset1_setup_master_slave(struct phy_device *phydev)
{
	int ctl = 0;

	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_PREFERRED:
	case MASTER_SLAVE_CFG_MASTER_FORCE:
		ctl = MDIO_PMA_PMD_BT1_CTRL_CFG_MST;
		break;
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
	case MASTER_SLAVE_CFG_SLAVE_PREFERRED:
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		return 0;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	return phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL,
			     MDIO_PMA_PMD_BT1_CTRL_CFG_MST, ctl);
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_baset1_setup_master_slave);

/**
 * genphy_c45_pma_setup_forced - configures a forced speed
 * @phydev: target phy_device struct
 */
int genphy_c45_pma_setup_forced(struct phy_device *phydev)
{
	int bt1_ctrl, ctrl1, ctrl2, ret;

	/* Half duplex is not supported */
	if (phydev->duplex != DUPLEX_FULL)
		return -EINVAL;

	ctrl1 = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	if (ctrl1 < 0)
		return ctrl1;

	ctrl2 = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL2);
	if (ctrl2 < 0)
		return ctrl2;

	ctrl1 &= ~MDIO_CTRL1_SPEEDSEL;
	/*
	 * PMA/PMD type selection is 1.7.5:0 not 1.7.3:0.  See 45.2.1.6.1
	 * in 802.3-2012 and 802.3-2015.
	 */
	ctrl2 &= ~(MDIO_PMA_CTRL2_TYPE | 0x30);

	switch (phydev->speed) {
	case SPEED_10:
		if (genphy_c45_baset1_able(phydev))
			ctrl2 |= MDIO_PMA_CTRL2_BASET1;
		else
			ctrl2 |= MDIO_PMA_CTRL2_10BT;
		break;
	case SPEED_100:
		ctrl1 |= MDIO_PMA_CTRL1_SPEED100;
		ctrl2 |= MDIO_PMA_CTRL2_100BTX;
		break;
	case SPEED_1000:
		ctrl1 |= MDIO_PMA_CTRL1_SPEED1000;
		/* Assume 1000base-T */
		ctrl2 |= MDIO_PMA_CTRL2_1000BT;
		break;
	case SPEED_2500:
		ctrl1 |= MDIO_CTRL1_SPEED2_5G;
		/* Assume 2.5Gbase-T */
		ctrl2 |= MDIO_PMA_CTRL2_2_5GBT;
		break;
	case SPEED_5000:
		ctrl1 |= MDIO_CTRL1_SPEED5G;
		/* Assume 5Gbase-T */
		ctrl2 |= MDIO_PMA_CTRL2_5GBT;
		break;
	case SPEED_10000:
		ctrl1 |= MDIO_CTRL1_SPEED10G;
		/* Assume 10Gbase-T */
		ctrl2 |= MDIO_PMA_CTRL2_10GBT;
		break;
	default:
		return -EINVAL;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1, ctrl1);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL2, ctrl2);
	if (ret < 0)
		return ret;

	if (genphy_c45_baset1_able(phydev)) {
		ret = genphy_c45_pma_baset1_setup_master_slave(phydev);
		if (ret < 0)
			return ret;

		bt1_ctrl = 0;
		if (phydev->speed == SPEED_1000)
			bt1_ctrl = MDIO_PMA_PMD_BT1_CTRL_STRAP_B1000;

		ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL,
				     MDIO_PMA_PMD_BT1_CTRL_STRAP, bt1_ctrl);
		if (ret < 0)
			return ret;
	}

	return genphy_c45_an_disable_aneg(phydev);
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_setup_forced);

/* Sets master/slave preference and supported technologies.
 * The preference is set in the BIT(4) of BASE-T1 AN
 * advertisement register 7.515 and whether the status
 * is forced or not, it is set in the BIT(12) of BASE-T1
 * AN advertisement register 7.514.
 * Sets 10BASE-T1L Ability BIT(14) in BASE-T1 autonegotiation
 * advertisement register [31:16] if supported.
 */
static int genphy_c45_baset1_an_config_aneg(struct phy_device *phydev)
{
	u16 adv_l_mask, adv_l = 0;
	u16 adv_m_mask, adv_m = 0;
	int changed = 0;
	int ret;

	adv_l_mask = MDIO_AN_T1_ADV_L_FORCE_MS | MDIO_AN_T1_ADV_L_PAUSE_CAP |
		MDIO_AN_T1_ADV_L_PAUSE_ASYM;
	adv_m_mask = MDIO_AN_T1_ADV_M_1000BT1 | MDIO_AN_T1_ADV_M_100BT1 |
		MDIO_AN_T1_ADV_M_MST | MDIO_AN_T1_ADV_M_B10L;

	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_FORCE:
		adv_m |= MDIO_AN_T1_ADV_M_MST;
		fallthrough;
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		adv_l |= MDIO_AN_T1_ADV_L_FORCE_MS;
		break;
	case MASTER_SLAVE_CFG_MASTER_PREFERRED:
		adv_m |= MDIO_AN_T1_ADV_M_MST;
		fallthrough;
	case MASTER_SLAVE_CFG_SLAVE_PREFERRED:
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		/* if master/slave role is not specified, do not overwrite it */
		adv_l_mask &= ~MDIO_AN_T1_ADV_L_FORCE_MS;
		adv_m_mask &= ~MDIO_AN_T1_ADV_M_MST;
		break;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	adv_l |= linkmode_adv_to_mii_t1_adv_l_t(phydev->advertising);

	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_T1_ADV_L,
				     adv_l_mask, adv_l);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;

	adv_m |= linkmode_adv_to_mii_t1_adv_m_t(phydev->advertising);

	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_T1_ADV_M,
				     adv_m_mask, adv_m);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;

	return changed;
}

/**
 * genphy_c45_an_config_aneg - configure advertisement registers
 * @phydev: target phy_device struct
 *
 * Configure advertisement registers based on modes set in phydev->advertising
 *
 * Returns negative errno code on failure, 0 if advertisement didn't change,
 * or 1 if advertised modes changed.
 */
int genphy_c45_an_config_aneg(struct phy_device *phydev)
{
	int changed = 0, ret;
	u32 adv;

	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);

	ret = genphy_c45_an_config_eee_aneg(phydev);
	if (ret < 0)
		return ret;
	else if (ret)
		changed = true;

	if (genphy_c45_baset1_able(phydev))
		return genphy_c45_baset1_an_config_aneg(phydev);

	adv = linkmode_adv_to_mii_adv_t(phydev->advertising);

	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE,
				     ADVERTISE_ALL | ADVERTISE_100BASE4 |
				     ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM,
				     adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;

	adv = linkmode_adv_to_mii_10gbt_adv_t(phydev->advertising);

	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				     MDIO_AN_10GBT_CTRL_ADV10G |
				     MDIO_AN_10GBT_CTRL_ADV5G |
				     MDIO_AN_10GBT_CTRL_ADV2_5G, adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;

	return changed;
}
EXPORT_SYMBOL_GPL(genphy_c45_an_config_aneg);

/**
 * genphy_c45_an_disable_aneg - disable auto-negotiation
 * @phydev: target phy_device struct
 *
 * Disable auto-negotiation in the Clause 45 PHY. The link parameters
 * are controlled through the PMA/PMD MMD registers.
 *
 * Returns zero on success, negative errno code on failure.
 */
int genphy_c45_an_disable_aneg(struct phy_device *phydev)
{
	u16 reg = MDIO_CTRL1;

	if (genphy_c45_baset1_able(phydev))
		reg = MDIO_AN_T1_CTRL;

	return phy_clear_bits_mmd(phydev, MDIO_MMD_AN, reg,
				  MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);
}
EXPORT_SYMBOL_GPL(genphy_c45_an_disable_aneg);

/**
 * genphy_c45_restart_aneg - Enable and restart auto-negotiation
 * @phydev: target phy_device struct
 *
 * This assumes that the auto-negotiation MMD is present.
 *
 * Enable and restart auto-negotiation.
 */
int genphy_c45_restart_aneg(struct phy_device *phydev)
{
	u16 reg = MDIO_CTRL1;

	if (genphy_c45_baset1_able(phydev))
		reg = MDIO_AN_T1_CTRL;

	return phy_set_bits_mmd(phydev, MDIO_MMD_AN, reg,
				MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);
}
EXPORT_SYMBOL_GPL(genphy_c45_restart_aneg);

/**
 * genphy_c45_check_and_restart_aneg - Enable and restart auto-negotiation
 * @phydev: target phy_device struct
 * @restart: whether aneg restart is requested
 *
 * This assumes that the auto-negotiation MMD is present.
 *
 * Check, and restart auto-negotiation if needed.
 */
int genphy_c45_check_and_restart_aneg(struct phy_device *phydev, bool restart)
{
	u16 reg = MDIO_CTRL1;
	int ret;

	if (genphy_c45_baset1_able(phydev))
		reg = MDIO_AN_T1_CTRL;

	if (!restart) {
		/* Configure and restart aneg if it wasn't set before */
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, reg);
		if (ret < 0)
			return ret;

		if (!(ret & MDIO_AN_CTRL1_ENABLE))
			restart = true;
	}

	if (restart)
		return genphy_c45_restart_aneg(phydev);

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_check_and_restart_aneg);

/**
 * genphy_c45_aneg_done - return auto-negotiation complete status
 * @phydev: target phy_device struct
 *
 * This assumes that the auto-negotiation MMD is present.
 *
 * Reads the status register from the auto-negotiation MMD, returning:
 * - positive if auto-negotiation is complete
 * - negative errno code on error
 * - zero otherwise
 */
int genphy_c45_aneg_done(struct phy_device *phydev)
{
	int reg = MDIO_STAT1;
	int val;

	if (genphy_c45_baset1_able(phydev))
		reg = MDIO_AN_T1_STAT;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, reg);

	return val < 0 ? val : val & MDIO_AN_STAT1_COMPLETE ? 1 : 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_aneg_done);

/**
 * genphy_c45_read_link - read the overall link status from the MMDs
 * @phydev: target phy_device struct
 *
 * Read the link status from the specified MMDs, and if they all indicate
 * that the link is up, set phydev->link to 1.  If an error is encountered,
 * a negative errno will be returned, otherwise zero.
 */
int genphy_c45_read_link(struct phy_device *phydev)
{
	u32 mmd_mask = MDIO_DEVS_PMAPMD;
	int val, devad;
	bool link = true;

	if (phydev->c45_ids.mmds_present & MDIO_DEVS_AN) {
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
		if (val < 0)
			return val;

		/* Autoneg is being started, therefore disregard current
		 * link status and report link as down.
		 */
		if (val & MDIO_AN_CTRL1_RESTART) {
			phydev->link = 0;
			return 0;
		}
	}

	while (mmd_mask && link) {
		devad = __ffs(mmd_mask);
		mmd_mask &= ~BIT(devad);

		/* The link state is latched low so that momentary link
		 * drops can be detected. Do not double-read the status
		 * in polling mode to detect such short link drops except
		 * the link was already down.
		 */
		if (!phy_polling_mode(phydev) || !phydev->link) {
			val = phy_read_mmd(phydev, devad, MDIO_STAT1);
			if (val < 0)
				return val;
			else if (val & MDIO_STAT1_LSTATUS)
				continue;
		}

		val = phy_read_mmd(phydev, devad, MDIO_STAT1);
		if (val < 0)
			return val;

		if (!(val & MDIO_STAT1_LSTATUS))
			link = false;
	}

	phydev->link = link;

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_link);

/* Read the Clause 45 defined BASE-T1 AN (7.513) status register to check
 * if autoneg is complete. If so read the BASE-T1 Autonegotiation
 * Advertisement registers filling in the link partner advertisement,
 * pause and asym_pause members in phydev.
 */
static int genphy_c45_baset1_read_lpa(struct phy_device *phydev)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_STAT);
	if (val < 0)
		return val;

	if (!(val & MDIO_AN_STAT1_COMPLETE)) {
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->lp_advertising);
		mii_t1_adv_l_mod_linkmode_t(phydev->lp_advertising, 0);
		mii_t1_adv_m_mod_linkmode_t(phydev->lp_advertising, 0);

		phydev->pause = 0;
		phydev->asym_pause = 0;

		return 0;
	}

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->lp_advertising, 1);

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_LP_L);
	if (val < 0)
		return val;

	mii_t1_adv_l_mod_linkmode_t(phydev->lp_advertising, val);
	phydev->pause = val & MDIO_AN_T1_ADV_L_PAUSE_CAP ? 1 : 0;
	phydev->asym_pause = val & MDIO_AN_T1_ADV_L_PAUSE_ASYM ? 1 : 0;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_LP_M);
	if (val < 0)
		return val;

	mii_t1_adv_m_mod_linkmode_t(phydev->lp_advertising, val);

	return 0;
}

/**
 * genphy_c45_read_lpa - read the link partner advertisement and pause
 * @phydev: target phy_device struct
 *
 * Read the Clause 45 defined base (7.19) and 10G (7.33) status registers,
 * filling in the link partner advertisement, pause and asym_pause members
 * in @phydev.  This assumes that the auto-negotiation MMD is present, and
 * the backplane bit (7.48.0) is clear.  Clause 45 PHY drivers are expected
 * to fill in the remainder of the link partner advert from vendor registers.
 */
int genphy_c45_read_lpa(struct phy_device *phydev)
{
	int val;

	if (genphy_c45_baset1_able(phydev))
		return genphy_c45_baset1_read_lpa(phydev);

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	if (!(val & MDIO_AN_STAT1_COMPLETE)) {
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				   phydev->lp_advertising);
		mii_10gbt_stat_mod_linkmode_lpa_t(phydev->lp_advertising, 0);
		mii_adv_mod_linkmode_adv_t(phydev->lp_advertising, 0);
		phydev->pause = 0;
		phydev->asym_pause = 0;

		return 0;
	}

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->lp_advertising,
			 val & MDIO_AN_STAT1_LPABLE);

	/* Read the link partner's base page advertisement */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_LPA);
	if (val < 0)
		return val;

	mii_adv_mod_linkmode_adv_t(phydev->lp_advertising, val);
	phydev->pause = val & LPA_PAUSE_CAP ? 1 : 0;
	phydev->asym_pause = val & LPA_PAUSE_ASYM ? 1 : 0;

	/* Read the link partner's 10G advertisement */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
	if (val < 0)
		return val;

	mii_10gbt_stat_mod_linkmode_lpa_t(phydev->lp_advertising, val);

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_lpa);

/**
 * genphy_c45_pma_baset1_read_master_slave - read forced master/slave
 * configuration
 * @phydev: target phy_device struct
 */
int genphy_c45_pma_baset1_read_master_slave(struct phy_device *phydev)
{
	int val;

	phydev->master_slave_state = MASTER_SLAVE_STATE_UNKNOWN;
	phydev->master_slave_get = MASTER_SLAVE_CFG_UNKNOWN;

	val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL);
	if (val < 0)
		return val;

	if (val & MDIO_PMA_PMD_BT1_CTRL_CFG_MST) {
		phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_FORCE;
		phydev->master_slave_state = MASTER_SLAVE_STATE_MASTER;
	} else {
		phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_FORCE;
		phydev->master_slave_state = MASTER_SLAVE_STATE_SLAVE;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_baset1_read_master_slave);

/**
 * genphy_c45_read_pma - read link speed etc from PMA
 * @phydev: target phy_device struct
 */
int genphy_c45_read_pma(struct phy_device *phydev)
{
	int val;

	linkmode_zero(phydev->lp_advertising);

	val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	if (val < 0)
		return val;

	switch (val & MDIO_CTRL1_SPEEDSEL) {
	case 0:
		phydev->speed = SPEED_10;
		break;
	case MDIO_PMA_CTRL1_SPEED100:
		phydev->speed = SPEED_100;
		break;
	case MDIO_PMA_CTRL1_SPEED1000:
		phydev->speed = SPEED_1000;
		break;
	case MDIO_CTRL1_SPEED2_5G:
		phydev->speed = SPEED_2500;
		break;
	case MDIO_CTRL1_SPEED5G:
		phydev->speed = SPEED_5000;
		break;
	case MDIO_CTRL1_SPEED10G:
		phydev->speed = SPEED_10000;
		break;
	default:
		phydev->speed = SPEED_UNKNOWN;
		break;
	}

	phydev->duplex = DUPLEX_FULL;

	if (genphy_c45_baset1_able(phydev)) {
		val = genphy_c45_pma_baset1_read_master_slave(phydev);
		if (val < 0)
			return val;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_pma);

/**
 * genphy_c45_read_mdix - read mdix status from PMA
 * @phydev: target phy_device struct
 */
int genphy_c45_read_mdix(struct phy_device *phydev)
{
	int val;

	if (phydev->speed == SPEED_10000) {
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD,
				   MDIO_PMA_10GBT_SWAPPOL);
		if (val < 0)
			return val;

		switch (val) {
		case MDIO_PMA_10GBT_SWAPPOL_ABNX | MDIO_PMA_10GBT_SWAPPOL_CDNX:
			phydev->mdix = ETH_TP_MDI;
			break;

		case 0:
			phydev->mdix = ETH_TP_MDI_X;
			break;

		default:
			phydev->mdix = ETH_TP_MDI_INVALID;
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_mdix);

/**
 * genphy_c45_write_eee_adv - write advertised EEE link modes
 * @phydev: target phy_device struct
 * @adv: the linkmode advertisement settings
 */
static int genphy_c45_write_eee_adv(struct phy_device *phydev,
				    unsigned long *adv)
{
	int val, changed = 0;

	if (linkmode_intersects(phydev->supported_eee, PHY_EEE_CAP1_FEATURES)) {
		val = linkmode_to_mii_eee_cap1_t(adv);

		/* IEEE 802.3-2018 45.2.7.13 EEE advertisement 1
		 * (Register 7.60)
		 */
		val = phy_modify_mmd_changed(phydev, MDIO_MMD_AN,
					     MDIO_AN_EEE_ADV,
					     MDIO_EEE_100TX | MDIO_EEE_1000T |
					     MDIO_EEE_10GT | MDIO_EEE_1000KX |
					     MDIO_EEE_10GKX4 | MDIO_EEE_10GKR,
					     val);
		if (val < 0)
			return val;
		if (val > 0)
			changed = 1;
	}

	if (linkmode_intersects(phydev->supported_eee, PHY_EEE_CAP2_FEATURES)) {
		val = linkmode_to_mii_eee_cap2_t(adv);

		/* IEEE 802.3-2022 45.2.7.16 EEE advertisement 2
		 * (Register 7.62)
		 */
		val = phy_modify_mmd_changed(phydev, MDIO_MMD_AN,
					     MDIO_AN_EEE_ADV2,
					     MDIO_EEE_2_5GT | MDIO_EEE_5GT,
					     val);
		if (val < 0)
			return val;
		if (val > 0)
			changed = 1;
	}

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			      phydev->supported_eee)) {
		val = linkmode_adv_to_mii_10base_t1_t(adv);
		/* IEEE 802.3cg-2019 45.2.7.25 10BASE-T1 AN control register
		 * (Register 7.526)
		 */
		val = phy_modify_mmd_changed(phydev, MDIO_MMD_AN,
					     MDIO_AN_10BT1_AN_CTRL,
					     MDIO_AN_10BT1_AN_CTRL_ADV_EEE_T1L,
					     val);
		if (val < 0)
			return val;
		if (val > 0)
			changed = 1;
	}

	return changed;
}

/**
 * genphy_c45_read_eee_adv - read advertised EEE link modes
 * @phydev: target phy_device struct
 * @adv: the linkmode advertisement status
 */
int genphy_c45_read_eee_adv(struct phy_device *phydev, unsigned long *adv)
{
	int val;

	if (linkmode_intersects(phydev->supported_eee, PHY_EEE_CAP1_FEATURES)) {
		/* IEEE 802.3-2018 45.2.7.13 EEE advertisement 1
		 * (Register 7.60)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
		if (val < 0)
			return val;

		mii_eee_cap1_mod_linkmode_t(adv, val);
	}

	if (linkmode_intersects(phydev->supported_eee, PHY_EEE_CAP2_FEATURES)) {
		/* IEEE 802.3-2022 45.2.7.16 EEE advertisement 2
		 * (Register 7.62)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2);
		if (val < 0)
			return val;

		mii_eee_cap2_mod_linkmode_adv_t(adv, val);
	}

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			      phydev->supported_eee)) {
		/* IEEE 802.3cg-2019 45.2.7.25 10BASE-T1 AN control register
		 * (Register 7.526)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10BT1_AN_CTRL);
		if (val < 0)
			return val;

		mii_10base_t1_adv_mod_linkmode_t(adv, val);
	}

	return 0;
}

/**
 * genphy_c45_read_eee_lpa - read advertised LP EEE link modes
 * @phydev: target phy_device struct
 * @lpa: the linkmode LP advertisement status
 */
static int genphy_c45_read_eee_lpa(struct phy_device *phydev,
				   unsigned long *lpa)
{
	int val;

	if (linkmode_intersects(phydev->supported_eee, PHY_EEE_CAP1_FEATURES)) {
		/* IEEE 802.3-2018 45.2.7.14 EEE link partner ability 1
		 * (Register 7.61)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);
		if (val < 0)
			return val;

		mii_eee_cap1_mod_linkmode_t(lpa, val);
	}

	if (linkmode_intersects(phydev->supported_eee, PHY_EEE_CAP2_FEATURES)) {
		/* IEEE 802.3-2022 45.2.7.17 EEE link partner ability 2
		 * (Register 7.63)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE2);
		if (val < 0)
			return val;

		mii_eee_cap2_mod_linkmode_adv_t(lpa, val);
	}

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			      phydev->supported_eee)) {
		/* IEEE 802.3cg-2019 45.2.7.26 10BASE-T1 AN status register
		 * (Register 7.527)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10BT1_AN_STAT);
		if (val < 0)
			return val;

		mii_10base_t1_adv_mod_linkmode_t(lpa, val);
	}

	return 0;
}

/**
 * genphy_c45_read_eee_cap1 - read supported EEE link modes from register 3.20
 * @phydev: target phy_device struct
 */
static int genphy_c45_read_eee_cap1(struct phy_device *phydev)
{
	int val;

	/* IEEE 802.3-2018 45.2.3.10 EEE control and capability 1
	 * (Register 3.20)
	 */
	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
	if (val < 0)
		return val;

	/* The 802.3 2018 standard says the top 2 bits are reserved and should
	 * read as 0. Also, it seems unlikely anybody will build a PHY which
	 * supports 100GBASE-R deep sleep all the way down to 100BASE-TX EEE.
	 * If MDIO_PCS_EEE_ABLE is 0xffff assume EEE is not supported.
	 */
	if (val == 0xffff)
		return 0;

	mii_eee_cap1_mod_linkmode_t(phydev->supported_eee, val);

	/* Some buggy devices indicate EEE link modes in MDIO_PCS_EEE_ABLE
	 * which they don't support as indicated by BMSR, ESTATUS etc.
	 */
	linkmode_and(phydev->supported_eee, phydev->supported_eee,
		     phydev->supported);

	return 0;
}

/**
 * genphy_c45_read_eee_cap2 - read supported EEE link modes from register 3.21
 * @phydev: target phy_device struct
 */
static int genphy_c45_read_eee_cap2(struct phy_device *phydev)
{
	int val;

	/* IEEE 802.3-2022 45.2.3.11 EEE control and capability 2
	 * (Register 3.21)
	 */
	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE2);
	if (val < 0)
		return val;

	/* IEEE 802.3-2022 45.2.3.11 says 9 bits are reserved. */
	if (val == 0xffff)
		return 0;

	mii_eee_cap2_mod_linkmode_sup_t(phydev->supported_eee, val);

	return 0;
}

/**
 * genphy_c45_read_eee_abilities - read supported EEE link modes
 * @phydev: target phy_device struct
 */
int genphy_c45_read_eee_abilities(struct phy_device *phydev)
{
	int val;

	/* There is not indicator whether optional register
	 * "EEE control and capability 1" (3.20) is supported. Read it only
	 * on devices with appropriate linkmodes.
	 */
	if (linkmode_intersects(phydev->supported, PHY_EEE_CAP1_FEATURES)) {
		val = genphy_c45_read_eee_cap1(phydev);
		if (val)
			return val;
	}

	/* Same for cap2 (3.21) */
	if (linkmode_intersects(phydev->supported, PHY_EEE_CAP2_FEATURES)) {
		val = genphy_c45_read_eee_cap2(phydev);
		if (val)
			return val;
	}

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			      phydev->supported)) {
		/* IEEE 802.3cg-2019 45.2.1.186b 10BASE-T1L PMA status register
		 * (Register 1.2295)
		 */
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_10T1L_STAT);
		if (val < 0)
			return val;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
				 phydev->supported_eee,
				 val & MDIO_PMA_10T1L_STAT_EEE);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_eee_abilities);

/**
 * genphy_c45_an_config_eee_aneg - configure EEE advertisement
 * @phydev: target phy_device struct
 */
int genphy_c45_an_config_eee_aneg(struct phy_device *phydev)
{
	if (!phydev->eee_cfg.eee_enabled) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(adv) = {};

		return genphy_c45_write_eee_adv(phydev, adv);
	}

	return genphy_c45_write_eee_adv(phydev, phydev->advertising_eee);
}
EXPORT_SYMBOL_GPL(genphy_c45_an_config_eee_aneg);

/**
 * genphy_c45_pma_baset1_read_abilities - read supported baset1 link modes from PMA
 * @phydev: target phy_device struct
 *
 * Read the supported link modes from the extended BASE-T1 ability register
 */
int genphy_c45_pma_baset1_read_abilities(struct phy_device *phydev)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1);
	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_PMD_BT1_B10L_ABLE);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT1_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_PMD_BT1_B100_ABLE);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT1_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_PMD_BT1_B1000_ABLE);

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_STAT);
	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			 phydev->supported,
			 val & MDIO_AN_STAT1_ABLE);

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_baset1_read_abilities);

/**
 * genphy_c45_pma_read_ext_abilities - read supported link modes from PMA
 * @phydev: target phy_device struct
 *
 * Read the supported link modes from the PMA/PMD extended ability register
 * (Register 1.11).
 */
int genphy_c45_pma_read_ext_abilities(struct phy_device *phydev)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_EXTABLE);
	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_10GBLRM);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_10GBT);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_10GBKX4);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_10GBKR);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_1000BT);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_1000BKX);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_100BTX);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_100BTX);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_10BT);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
			 phydev->supported,
			 val & MDIO_PMA_EXTABLE_10BT);

	if (val & MDIO_PMA_EXTABLE_NBT) {
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD,
				   MDIO_PMA_NG_EXTABLE);
		if (val < 0)
			return val;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				 phydev->supported,
				 val & MDIO_PMA_NG_EXTABLE_2_5GBT);

		linkmode_mod_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
				 phydev->supported,
				 val & MDIO_PMA_NG_EXTABLE_5GBT);
	}

	if (val & MDIO_PMA_EXTABLE_BT1) {
		val = genphy_c45_pma_baset1_read_abilities(phydev);
		if (val < 0)
			return val;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_read_ext_abilities);

/**
 * genphy_c45_pma_read_abilities - read supported link modes from PMA
 * @phydev: target phy_device struct
 *
 * Read the supported link modes from the PMA Status 2 (1.8) register. If bit
 * 1.8.9 is set, the list of supported modes is build using the values in the
 * PMA Extended Abilities (1.11) register, indicating 1000BASET an 10G related
 * modes. If bit 1.11.14 is set, then the list is also extended with the modes
 * in the 2.5G/5G PMA Extended register (1.21), indicating if 2.5GBASET and
 * 5GBASET are supported.
 */
int genphy_c45_pma_read_abilities(struct phy_device *phydev)
{
	int val;

	linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported);
	if (phydev->c45_ids.mmds_present & MDIO_DEVS_AN) {
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
		if (val < 0)
			return val;

		if (val & MDIO_AN_STAT1_ABLE)
			linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					 phydev->supported);
	}

	val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_STAT2);
	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_STAT2_10GBSR);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_STAT2_10GBLR);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseER_Full_BIT,
			 phydev->supported,
			 val & MDIO_PMA_STAT2_10GBER);

	if (val & MDIO_PMA_STAT2_EXTABLE) {
		val = genphy_c45_pma_read_ext_abilities(phydev);
		if (val < 0)
			return val;
	}

	/* This is optional functionality. If not supported, we may get an error
	 * which should be ignored.
	 */
	genphy_c45_read_eee_abilities(phydev);

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_read_abilities);

/* Read master/slave preference from registers.
 * The preference is read from the BIT(4) of BASE-T1 AN
 * advertisement register 7.515 and whether the preference
 * is forced or not, it is read from BASE-T1 AN advertisement
 * register 7.514.
 */
int genphy_c45_baset1_read_status(struct phy_device *phydev)
{
	int ret;
	int cfg;

	phydev->master_slave_get = MASTER_SLAVE_CFG_UNKNOWN;
	phydev->master_slave_state = MASTER_SLAVE_STATE_UNKNOWN;

	ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_ADV_L);
	if (ret < 0)
		return ret;

	cfg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_ADV_M);
	if (cfg < 0)
		return cfg;

	if (ret & MDIO_AN_T1_ADV_L_FORCE_MS) {
		if (cfg & MDIO_AN_T1_ADV_M_MST)
			phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_FORCE;
		else
			phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_FORCE;
	} else {
		if (cfg & MDIO_AN_T1_ADV_M_MST)
			phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_PREFERRED;
		else
			phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_PREFERRED;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_baset1_read_status);

/**
 * genphy_c45_read_status - read PHY status
 * @phydev: target phy_device struct
 *
 * Reads status from PHY and sets phy_device members accordingly.
 */
int genphy_c45_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_read_link(phydev);
	if (ret)
		return ret;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_c45_read_lpa(phydev);
		if (ret)
			return ret;

		if (genphy_c45_baset1_able(phydev)) {
			ret = genphy_c45_baset1_read_status(phydev);
			if (ret < 0)
				return ret;
		}

		phy_resolve_aneg_linkmode(phydev);
	} else {
		ret = genphy_c45_read_pma(phydev);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_status);

/**
 * genphy_c45_config_aneg - restart auto-negotiation or forced setup
 * @phydev: target phy_device struct
 *
 * Description: If auto-negotiation is enabled, we configure the
 *   advertising, and then restart auto-negotiation.  If it is not
 *   enabled, then we force a configuration.
 */
int genphy_c45_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	int ret;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}
EXPORT_SYMBOL_GPL(genphy_c45_config_aneg);

/* The gen10g_* functions are the old Clause 45 stub */

int gen10g_config_aneg(struct phy_device *phydev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_config_aneg);

int genphy_c45_loopback(struct phy_device *phydev, bool enable, int speed)
{
	if (enable && speed)
		return -EOPNOTSUPP;

	return phy_modify_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
			      MDIO_PCS_CTRL1_LOOPBACK,
			      enable ? MDIO_PCS_CTRL1_LOOPBACK : 0);
}
EXPORT_SYMBOL_GPL(genphy_c45_loopback);

/**
 * genphy_c45_fast_retrain - configure fast retrain registers
 * @phydev: target phy_device struct
 * @enable: enable fast retrain or not
 *
 * Description: If fast-retrain is enabled, we configure PHY as
 *   advertising fast retrain capable and THP Bypass Request, then
 *   enable fast retrain. If it is not enabled, we configure fast
 *   retrain disabled.
 */
int genphy_c45_fast_retrain(struct phy_device *phydev, bool enable)
{
	int ret;

	if (!enable)
		return phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_FSRT_CSR,
				MDIO_PMA_10GBR_FSRT_ENABLE);

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->supported)) {
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				MDIO_AN_10GBT_CTRL_ADVFSRT2_5G);
		if (ret)
			return ret;

		ret = phy_set_bits_mmd(phydev, MDIO_MMD_AN, MDIO_AN_CTRL2,
				MDIO_AN_THP_BP2_5GT);
		if (ret)
			return ret;
	}

	return phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_FSRT_CSR,
			MDIO_PMA_10GBR_FSRT_ENABLE);
}
EXPORT_SYMBOL_GPL(genphy_c45_fast_retrain);

/**
 * genphy_c45_plca_get_cfg - get PLCA configuration from standard registers
 * @phydev: target phy_device struct
 * @plca_cfg: output structure to store the PLCA configuration
 *
 * Description: if the PHY complies to the Open Alliance TC14 10BASE-T1S PLCA
 *   Management Registers specifications, this function can be used to retrieve
 *   the current PLCA configuration from the standard registers in MMD 31.
 */
int genphy_c45_plca_get_cfg(struct phy_device *phydev,
			    struct phy_plca_cfg *plca_cfg)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_IDVER);
	if (ret < 0)
		return ret;

	if ((ret & MDIO_OATC14_PLCA_IDM) != OATC14_IDM)
		return -ENODEV;

	plca_cfg->version = ret & ~MDIO_OATC14_PLCA_IDM;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_CTRL0);
	if (ret < 0)
		return ret;

	plca_cfg->enabled = !!(ret & MDIO_OATC14_PLCA_EN);

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_CTRL1);
	if (ret < 0)
		return ret;

	plca_cfg->node_cnt = (ret & MDIO_OATC14_PLCA_NCNT) >> 8;
	plca_cfg->node_id = (ret & MDIO_OATC14_PLCA_ID);

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_TOTMR);
	if (ret < 0)
		return ret;

	plca_cfg->to_tmr = ret & MDIO_OATC14_PLCA_TOT;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_BURST);
	if (ret < 0)
		return ret;

	plca_cfg->burst_cnt = (ret & MDIO_OATC14_PLCA_MAXBC) >> 8;
	plca_cfg->burst_tmr = (ret & MDIO_OATC14_PLCA_BTMR);

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_plca_get_cfg);

/**
 * genphy_c45_plca_set_cfg - set PLCA configuration using standard registers
 * @phydev: target phy_device struct
 * @plca_cfg: structure containing the PLCA configuration. Fields set to -1 are
 * not to be changed.
 *
 * Description: if the PHY complies to the Open Alliance TC14 10BASE-T1S PLCA
 *   Management Registers specifications, this function can be used to modify
 *   the PLCA configuration using the standard registers in MMD 31.
 */
int genphy_c45_plca_set_cfg(struct phy_device *phydev,
			    const struct phy_plca_cfg *plca_cfg)
{
	u16 val = 0;
	int ret;

	// PLCA IDVER is read-only
	if (plca_cfg->version >= 0)
		return -EINVAL;

	// first of all, disable PLCA if required
	if (plca_cfg->enabled == 0) {
		ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2,
					 MDIO_OATC14_PLCA_CTRL0,
					 MDIO_OATC14_PLCA_EN);

		if (ret < 0)
			return ret;
	}

	// check if we need to set the PLCA node count, node ID, or both
	if (plca_cfg->node_cnt >= 0 || plca_cfg->node_id >= 0) {
		/* if one between node count and node ID is -not- to be
		 * changed, read the register to later perform merge/purge of
		 * the configuration as appropriate
		 */
		if (plca_cfg->node_cnt < 0 || plca_cfg->node_id < 0) {
			ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
					   MDIO_OATC14_PLCA_CTRL1);

			if (ret < 0)
				return ret;

			val = ret;
		}

		if (plca_cfg->node_cnt >= 0)
			val = (val & ~MDIO_OATC14_PLCA_NCNT) |
			      (plca_cfg->node_cnt << 8);

		if (plca_cfg->node_id >= 0)
			val = (val & ~MDIO_OATC14_PLCA_ID) |
			      (plca_cfg->node_id);

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    MDIO_OATC14_PLCA_CTRL1, val);

		if (ret < 0)
			return ret;
	}

	if (plca_cfg->to_tmr >= 0) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    MDIO_OATC14_PLCA_TOTMR,
				    plca_cfg->to_tmr);

		if (ret < 0)
			return ret;
	}

	// check if we need to set the PLCA burst count, burst timer, or both
	if (plca_cfg->burst_cnt >= 0 || plca_cfg->burst_tmr >= 0) {
		/* if one between burst count and burst timer is -not- to be
		 * changed, read the register to later perform merge/purge of
		 * the configuration as appropriate
		 */
		if (plca_cfg->burst_cnt < 0 || plca_cfg->burst_tmr < 0) {
			ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
					   MDIO_OATC14_PLCA_BURST);

			if (ret < 0)
				return ret;

			val = ret;
		}

		if (plca_cfg->burst_cnt >= 0)
			val = (val & ~MDIO_OATC14_PLCA_MAXBC) |
			      (plca_cfg->burst_cnt << 8);

		if (plca_cfg->burst_tmr >= 0)
			val = (val & ~MDIO_OATC14_PLCA_BTMR) |
			      (plca_cfg->burst_tmr);

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    MDIO_OATC14_PLCA_BURST, val);

		if (ret < 0)
			return ret;
	}

	// if we need to enable PLCA, do it at the end
	if (plca_cfg->enabled > 0) {
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       MDIO_OATC14_PLCA_CTRL0,
				       MDIO_OATC14_PLCA_EN);

		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_plca_set_cfg);

/**
 * genphy_c45_plca_get_status - get PLCA status from standard registers
 * @phydev: target phy_device struct
 * @plca_st: output structure to store the PLCA status
 *
 * Description: if the PHY complies to the Open Alliance TC14 10BASE-T1S PLCA
 *   Management Registers specifications, this function can be used to retrieve
 *   the current PLCA status information from the standard registers in MMD 31.
 */
int genphy_c45_plca_get_status(struct phy_device *phydev,
			       struct phy_plca_status *plca_st)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_STATUS);
	if (ret < 0)
		return ret;

	plca_st->pst = !!(ret & MDIO_OATC14_PLCA_PST);
	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_plca_get_status);

/**
 * genphy_c45_eee_is_active - get EEE status
 * @phydev: target phy_device struct
 * @lp: variable to store LP advertised linkmodes
 *
 * Description: this function will read link partner PHY advertisement
 * and compare it to local advertisement to return current EEE state.
 */
int genphy_c45_eee_is_active(struct phy_device *phydev, unsigned long *lp)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(tmp_lp) = {};
	__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
	int ret;

	if (!phydev->eee_cfg.eee_enabled)
		return 0;

	ret = genphy_c45_read_eee_lpa(phydev, tmp_lp);
	if (ret)
		return ret;

	if (lp)
		linkmode_copy(lp, tmp_lp);

	linkmode_and(common, phydev->advertising_eee, tmp_lp);
	if (linkmode_empty(common))
		return 0;

	return phy_check_valid(phydev->speed, phydev->duplex, common);
}
EXPORT_SYMBOL(genphy_c45_eee_is_active);

/**
 * genphy_c45_ethtool_get_eee - get EEE supported and status
 * @phydev: target phy_device struct
 * @data: ethtool_keee data
 *
 * Description: it reports the Supported/Advertisement/LP Advertisement
 * capabilities.
 */
int genphy_c45_ethtool_get_eee(struct phy_device *phydev,
			       struct ethtool_keee *data)
{
	int ret;

	ret = genphy_c45_eee_is_active(phydev, data->lp_advertised);
	if (ret < 0)
		return ret;

	data->eee_active = phydev->eee_active;
	linkmode_andnot(data->supported, phydev->supported_eee,
			phydev->eee_disabled_modes);
	linkmode_copy(data->advertised, phydev->advertising_eee);
	return 0;
}
EXPORT_SYMBOL(genphy_c45_ethtool_get_eee);

/**
 * genphy_c45_ethtool_set_eee - set EEE supported and status
 * @phydev: target phy_device struct
 * @data: ethtool_keee data
 *
 * Description: sets the Supported/Advertisement/LP Advertisement
 * capabilities. If eee_enabled is false, no links modes are
 * advertised, but the previously advertised link modes are
 * retained. This allows EEE to be enabled/disabled in a
 * non-destructive way.
 * Returns either error code, 0 if there was no change, or positive
 * value if there was a change which triggered auto-neg.
 */
int genphy_c45_ethtool_set_eee(struct phy_device *phydev,
			       struct ethtool_keee *data)
{
	int ret;

	if (data->eee_enabled) {
		unsigned long *adv = data->advertised;

		if (!linkmode_empty(adv)) {
			__ETHTOOL_DECLARE_LINK_MODE_MASK(tmp);

			if (linkmode_andnot(tmp, adv, phydev->supported_eee)) {
				phydev_warn(phydev, "At least some EEE link modes are not supported.\n");
				return -EINVAL;
			}

			linkmode_andnot(phydev->advertising_eee, adv,
					phydev->eee_disabled_modes);
		} else if (linkmode_empty(phydev->advertising_eee)) {
			phy_advertise_eee_all(phydev);
		}
	}

	ret = genphy_c45_an_config_eee_aneg(phydev);
	if (ret > 0) {
		ret = phy_restart_aneg(phydev);
		if (ret < 0)
			return ret;

		/* explicitly return 1, otherwise (ret > 0) value will be
		 * overwritten by phy_restart_aneg().
		 */
		return 1;
	}

	return ret;
}
EXPORT_SYMBOL(genphy_c45_ethtool_set_eee);

struct phy_driver genphy_c45_driver = {
	.phy_id         = 0xffffffff,
	.phy_id_mask    = 0xffffffff,
	.name           = "Generic Clause 45 PHY",
	.read_status    = genphy_c45_read_status,
};
