/*
 * Clause 45 PHY support
 */
#include <linux/ethtool.h>
#include <linux/export.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/phy.h>

/**
 * genphy_c45_setup_forced - configures a forced speed
 * @phydev: target phy_device struct
 */
int genphy_c45_pma_setup_forced(struct phy_device *phydev)
{
	int ctrl1, ctrl2, ret;

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

	return genphy_c45_an_disable_aneg(phydev);
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_setup_forced);

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
	int changed, ret;
	u32 adv;

	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);

	changed = genphy_config_eee_advert(phydev);

	adv = linkmode_adv_to_mii_adv_t(phydev->advertising);

	ret = phy_modify_mmd(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE,
			     ADVERTISE_ALL | ADVERTISE_100BASE4 |
			     ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM,
			     adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;

	adv = linkmode_adv_to_mii_10gbt_adv_t(phydev->advertising);

	ret = phy_modify_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
			     MDIO_AN_10GBT_CTRL_ADV10G |
			     MDIO_AN_10GBT_CTRL_ADV5G |
			     MDIO_AN_10GBT_CTRL_ADV2_5G,
			     adv);
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
 * parameters are controlled through the PMA/PMD MMD registers.
 *
 * Returns zero on success, negative errno code on failure.
 */
int genphy_c45_an_disable_aneg(struct phy_device *phydev)
{

	return phy_clear_bits_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1,
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
	return phy_set_bits_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1,
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
	int ret = 0;

	if (!restart) {
		/* Configure and restart aneg if it wasn't set before */
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
		if (ret < 0)
			return ret;

		if (!(ret & MDIO_AN_CTRL1_ENABLE))
			restart = true;
	}

	if (restart)
		ret = genphy_c45_restart_aneg(phydev);

	return ret;
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
	int val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);

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

	while (mmd_mask && link) {
		devad = __ffs(mmd_mask);
		mmd_mask &= ~BIT(devad);

		/* The link state is latched low so that momentary link
		 * drops can be detected. Do not double-read the status
		 * in polling mode to detect such short link drops.
		 */
		if (!phy_polling_mode(phydev)) {
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

	/* Read the link partner's base page advertisement */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_LPA);
	if (val < 0)
		return val;

	mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, val);
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
 * genphy_c45_read_pma - read link speed etc from PMA
 * @phydev: target phy_device struct
 */
int genphy_c45_read_pma(struct phy_device *phydev)
{
	int val;

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
	if (phydev->c45_ids.devices_in_package & MDIO_DEVS_AN) {
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
	}

	return 0;
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_read_abilities);

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

		phy_resolve_aneg_linkmode(phydev);
	} else {
		ret = genphy_c45_read_pma(phydev);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(genphy_c45_read_status);

/* The gen10g_* functions are the old Clause 45 stub */

int gen10g_config_aneg(struct phy_device *phydev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_config_aneg);

int gen10g_read_status(struct phy_device *phydev)
{
	/* For now just lie and say it's 10G all the time */
	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;

	return genphy_c45_read_link(phydev);
}
EXPORT_SYMBOL_GPL(gen10g_read_status);

int gen10g_no_soft_reset(struct phy_device *phydev)
{
	/* Do nothing for now */
	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_no_soft_reset);

int gen10g_config_init(struct phy_device *phydev)
{
	/* Temporarily just say we support everything */
	linkmode_zero(phydev->supported);

	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			 phydev->supported);
	linkmode_copy(phydev->advertising, phydev->supported);

	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_config_init);

int gen10g_suspend(struct phy_device *phydev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_suspend);

int gen10g_resume(struct phy_device *phydev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_resume);

struct phy_driver genphy_10g_driver = {
	.phy_id         = 0xffffffff,
	.phy_id_mask    = 0xffffffff,
	.name           = "Generic 10G PHY",
	.soft_reset	= gen10g_no_soft_reset,
	.config_init    = gen10g_config_init,
	.features       = PHY_10GBIT_FEATURES,
	.config_aneg    = gen10g_config_aneg,
	.read_status    = gen10g_read_status,
	.suspend        = gen10g_suspend,
	.resume         = gen10g_resume,
};
