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

	return phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL2, ctrl2);
}
EXPORT_SYMBOL_GPL(genphy_c45_pma_setup_forced);

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
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
	if (val < 0)
		return val;

	val &= ~(MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART);

	return phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1, val);
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
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
	if (val < 0)
		return val;

	val |= MDIO_AN_CTRL1_ENABLE | MDIO_AN_CTRL1_RESTART;

	return phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1, val);
}
EXPORT_SYMBOL_GPL(genphy_c45_restart_aneg);

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
 * @mmd_mask: MMDs to read status from
 *
 * Read the link status from the specified MMDs, and if they all indicate
 * that the link is up, return positive.  If an error is encountered,
 * a negative errno will be returned, otherwise zero.
 */
int genphy_c45_read_link(struct phy_device *phydev, u32 mmd_mask)
{
	int val, devad;
	bool link = true;

	while (mmd_mask) {
		devad = __ffs(mmd_mask);
		mmd_mask &= ~BIT(devad);

		/* The link state is latched low so that momentary link
		 * drops can be detected.  Do not double-read the status
		 * register if the link is down.
		 */
		val = phy_read_mmd(phydev, devad, MDIO_STAT1);
		if (val < 0)
			return val;

		if (!(val & MDIO_STAT1_LSTATUS))
			link = false;
	}

	return link;
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

	phydev->lp_advertising = mii_lpa_to_ethtool_lpa_t(val);
	phydev->pause = val & LPA_PAUSE_CAP ? 1 : 0;
	phydev->asym_pause = val & LPA_PAUSE_ASYM ? 1 : 0;

	/* Read the link partner's 10G advertisement */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
	if (val < 0)
		return val;

	if (val & MDIO_AN_10GBT_STAT_LP10G)
		phydev->lp_advertising |= ADVERTISED_10000baseT_Full;

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

/* The gen10g_* functions are the old Clause 45 stub */

int gen10g_config_aneg(struct phy_device *phydev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(gen10g_config_aneg);

int gen10g_read_status(struct phy_device *phydev)
{
	u32 mmd_mask = phydev->c45_ids.devices_in_package;
	int ret;

	/* For now just lie and say it's 10G all the time */
	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;

	/* Avoid reading the vendor MMDs */
	mmd_mask &= ~(BIT(MDIO_MMD_VEND1) | BIT(MDIO_MMD_VEND2));

	ret = genphy_c45_read_link(phydev, mmd_mask);

	phydev->link = ret > 0 ? 1 : 0;

	return 0;
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
	phydev->supported = SUPPORTED_10000baseT_Full;
	phydev->advertising = SUPPORTED_10000baseT_Full;

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
	.features       = 0,
	.config_aneg    = gen10g_config_aneg,
	.read_status    = gen10g_read_status,
	.suspend        = gen10g_suspend,
	.resume         = gen10g_resume,
};
