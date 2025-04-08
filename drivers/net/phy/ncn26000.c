// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 *  Driver for the onsemi 10BASE-T1S NCN26000 PHYs family.
 *
 * Copyright 2022 onsemi
 */
#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>

#include "mdio-open-alliance.h"

#define PHY_ID_NCN26000			0x180FF5A1

#define NCN26000_REG_IRQ_CTL            16
#define NCN26000_REG_IRQ_STATUS         17

// the NCN26000 maps link_ctrl to BMCR_ANENABLE
#define NCN26000_BCMR_LINK_CTRL_BIT	BMCR_ANENABLE

// the NCN26000 maps link_status to BMSR_ANEGCOMPLETE
#define NCN26000_BMSR_LINK_STATUS_BIT	BMSR_ANEGCOMPLETE

#define NCN26000_IRQ_LINKST_BIT		BIT(0)
#define NCN26000_IRQ_PLCAST_BIT		BIT(1)
#define NCN26000_IRQ_LJABBER_BIT	BIT(2)
#define NCN26000_IRQ_RJABBER_BIT	BIT(3)
#define NCN26000_IRQ_PLCAREC_BIT	BIT(4)
#define NCN26000_IRQ_PHYSCOL_BIT	BIT(5)
#define NCN26000_IRQ_RESET_BIT		BIT(15)

#define TO_TMR_DEFAULT			32

static int ncn26000_config_init(struct phy_device *phydev)
{
	/* HW bug workaround: the default value of the PLCA TO_TIMER should be
	 * 32, where the current version of NCN26000 reports 24. This will be
	 * fixed in future PHY versions. For the time being, we force the
	 * correct default here.
	 */
	return phy_write_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_PLCA_TOTMR,
			     TO_TMR_DEFAULT);
}

static int ncn26000_config_aneg(struct phy_device *phydev)
{
	/* Note: the NCN26000 supports only P2MP link mode. Therefore, AN is not
	 * supported. However, this function is invoked by phylib to enable the
	 * PHY, regardless of the AN support.
	 */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
	phydev->mdix = ETH_TP_MDI;

	// bring up the link
	return phy_write(phydev, MII_BMCR, NCN26000_BCMR_LINK_CTRL_BIT);
}

static int ncn26000_read_status(struct phy_device *phydev)
{
	/* The NCN26000 reports NCN26000_LINK_STATUS_BIT if the link status of
	 * the PHY is up. It further reports the logical AND of the link status
	 * and the PLCA status in the BMSR_LSTATUS bit.
	 */
	int ret;

	/* The link state is latched low so that momentary link
	 * drops can be detected. Do not double-read the status
	 * in polling mode to detect such short link drops except
	 * the link was already down.
	 */
	if (!phy_polling_mode(phydev) || !phydev->link) {
		ret = phy_read(phydev, MII_BMSR);
		if (ret < 0)
			return ret;
		else if (ret & NCN26000_BMSR_LINK_STATUS_BIT)
			goto upd_link;
	}

	ret = phy_read(phydev, MII_BMSR);
	if (ret < 0)
		return ret;

upd_link:
	// update link status
	if (ret & NCN26000_BMSR_LINK_STATUS_BIT) {
		phydev->link = 1;
		phydev->pause = 0;
		phydev->duplex = DUPLEX_HALF;
		phydev->speed = SPEED_10;
	} else {
		phydev->link = 0;
		phydev->duplex = DUPLEX_UNKNOWN;
		phydev->speed = SPEED_UNKNOWN;
	}

	return 0;
}

static irqreturn_t ncn26000_handle_interrupt(struct phy_device *phydev)
{
	int ret;

	// read and aknowledge the IRQ status register
	ret = phy_read(phydev, NCN26000_REG_IRQ_STATUS);

	// check only link status changes
	if (ret < 0 || (ret & NCN26000_REG_IRQ_STATUS) == 0)
		return IRQ_NONE;

	phy_trigger_machine(phydev);
	return IRQ_HANDLED;
}

static int ncn26000_config_intr(struct phy_device *phydev)
{
	int ret;
	u16 irqe;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		// acknowledge IRQs
		ret = phy_read(phydev, NCN26000_REG_IRQ_STATUS);
		if (ret < 0)
			return ret;

		// get link status notifications
		irqe = NCN26000_IRQ_LINKST_BIT;
	} else {
		// disable all IRQs
		irqe = 0;
	}

	ret = phy_write(phydev, NCN26000_REG_IRQ_CTL, irqe);
	if (ret != 0)
		return ret;

	return 0;
}

static struct phy_driver ncn26000_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_NCN26000),
		.name			= "NCN26000",
		.features		= PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init            = ncn26000_config_init,
		.config_intr            = ncn26000_config_intr,
		.config_aneg		= ncn26000_config_aneg,
		.read_status		= ncn26000_read_status,
		.handle_interrupt       = ncn26000_handle_interrupt,
		.get_plca_cfg		= genphy_c45_plca_get_cfg,
		.set_plca_cfg		= genphy_c45_plca_set_cfg,
		.get_plca_status	= genphy_c45_plca_get_status,
		.soft_reset             = genphy_soft_reset,
	},
};

module_phy_driver(ncn26000_driver);

static const struct mdio_device_id __maybe_unused ncn26000_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_NCN26000) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, ncn26000_tbl);

MODULE_AUTHOR("Piergiorgio Beruto");
MODULE_DESCRIPTION("onsemi 10BASE-T1S PHY driver");
MODULE_LICENSE("Dual BSD/GPL");
