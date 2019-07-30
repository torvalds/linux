// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Broadcom Corporation
 */

/* Broadcom Cygnus SoC internal transceivers support. */
#include "bcm-phy-lib.h"
#include <linux/brcmphy.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

/* Broadcom Cygnus Phy specific registers */
#define MII_BCM_CYGNUS_AFE_VDAC_ICTRL_0  0x91E5 /* VDAL Control register */

static int bcm_cygnus_afe_config(struct phy_device *phydev)
{
	int rc;

	/* ensure smdspclk is enabled */
	rc = phy_write(phydev, MII_BCM54XX_AUX_CTL, 0x0c30);
	if (rc < 0)
		return rc;

	/* AFE_VDAC_ICTRL_0 bit 7:4 Iq=1100 for 1g 10bt, normal modes */
	rc = bcm_phy_write_misc(phydev, 0x39, 0x01, 0xA7C8);
	if (rc < 0)
		return rc;

	/* AFE_HPF_TRIM_OTHERS bit11=1, short cascode enable for all modes*/
	rc = bcm_phy_write_misc(phydev, 0x3A, 0x00, 0x0803);
	if (rc < 0)
		return rc;

	/* AFE_TX_CONFIG_1 bit 7:4 Iq=1100 for test modes */
	rc = bcm_phy_write_misc(phydev, 0x3A, 0x01, 0xA740);
	if (rc < 0)
		return rc;

	/* AFE TEMPSEN_OTHERS rcal_HT, rcal_LT 10000 */
	rc = bcm_phy_write_misc(phydev, 0x3A, 0x03, 0x8400);
	if (rc < 0)
		return rc;

	/* AFE_FUTURE_RSV bit 2:0 rccal <2:0>=100 */
	rc = bcm_phy_write_misc(phydev, 0x3B, 0x00, 0x0004);
	if (rc < 0)
		return rc;

	/* Adjust bias current trim to overcome digital offSet */
	rc = phy_write(phydev, MII_BRCM_CORE_BASE1E, 0x02);
	if (rc < 0)
		return rc;

	/* make rcal=100, since rdb default is 000 */
	rc = bcm_phy_write_exp_sel(phydev, MII_BRCM_CORE_EXPB1, 0x10);
	if (rc < 0)
		return rc;

	/* CORE_EXPB0, Reset R_CAL/RC_CAL Engine */
	rc = bcm_phy_write_exp_sel(phydev, MII_BRCM_CORE_EXPB0, 0x10);
	if (rc < 0)
		return rc;

	/* CORE_EXPB0, Disable Reset R_CAL/RC_CAL Engine */
	rc = bcm_phy_write_exp_sel(phydev, MII_BRCM_CORE_EXPB0, 0x00);

	return 0;
}

static int bcm_cygnus_config_init(struct phy_device *phydev)
{
	int reg, rc;

	reg = phy_read(phydev, MII_BCM54XX_ECR);
	if (reg < 0)
		return reg;

	/* Mask interrupts globally. */
	reg |= MII_BCM54XX_ECR_IM;
	rc = phy_write(phydev, MII_BCM54XX_ECR, reg);
	if (rc)
		return rc;

	/* Unmask events of interest */
	reg = ~(MII_BCM54XX_INT_DUPLEX |
		MII_BCM54XX_INT_SPEED |
		MII_BCM54XX_INT_LINK);
	rc = phy_write(phydev, MII_BCM54XX_IMR, reg);
	if (rc)
		return rc;

	/* Apply AFE settings for the PHY */
	rc = bcm_cygnus_afe_config(phydev);
	if (rc)
		return rc;

	/* Advertise EEE */
	rc = bcm_phy_set_eee(phydev, true);
	if (rc)
		return rc;

	/* Enable APD */
	return bcm_phy_enable_apd(phydev, false);
}

static int bcm_cygnus_resume(struct phy_device *phydev)
{
	int rc;

	genphy_resume(phydev);

	/* Re-initialize the PHY to apply AFE work-arounds and
	 * configurations when coming out of suspend.
	 */
	rc = bcm_cygnus_config_init(phydev);
	if (rc)
		return rc;

	/* restart auto negotiation with the new settings */
	return genphy_config_aneg(phydev);
}

static struct phy_driver bcm_cygnus_phy_driver[] = {
{
	.phy_id        = PHY_ID_BCM_CYGNUS,
	.phy_id_mask   = 0xfffffff0,
	.name          = "Broadcom Cygnus PHY",
	.features      = PHY_GBIT_FEATURES,
	.config_init   = bcm_cygnus_config_init,
	.ack_interrupt = bcm_phy_ack_intr,
	.config_intr   = bcm_phy_config_intr,
	.suspend       = genphy_suspend,
	.resume        = bcm_cygnus_resume,
} };

static struct mdio_device_id __maybe_unused bcm_cygnus_phy_tbl[] = {
	{ PHY_ID_BCM_CYGNUS, 0xfffffff0, },
	{ }
};
MODULE_DEVICE_TABLE(mdio, bcm_cygnus_phy_tbl);

module_phy_driver(bcm_cygnus_phy_driver);

MODULE_DESCRIPTION("Broadcom Cygnus internal PHY driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom Corporation");
