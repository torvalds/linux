/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "bcm-phy-lib.h"
#include <linux/brcmphy.h>
#include <linux/export.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/phy.h>

#define MII_BCM_CHANNEL_WIDTH     0x2000
#define BCM_CL45VEN_EEE_ADV       0x3c

int bcm_phy_write_exp(struct phy_device *phydev, u16 reg, u16 val)
{
	int rc;

	rc = phy_write(phydev, MII_BCM54XX_EXP_SEL, reg);
	if (rc < 0)
		return rc;

	return phy_write(phydev, MII_BCM54XX_EXP_DATA, val);
}
EXPORT_SYMBOL_GPL(bcm_phy_write_exp);

int bcm_phy_read_exp(struct phy_device *phydev, u16 reg)
{
	int val;

	val = phy_write(phydev, MII_BCM54XX_EXP_SEL, reg);
	if (val < 0)
		return val;

	val = phy_read(phydev, MII_BCM54XX_EXP_DATA);

	/* Restore default value.  It's O.K. if this write fails. */
	phy_write(phydev, MII_BCM54XX_EXP_SEL, 0);

	return val;
}
EXPORT_SYMBOL_GPL(bcm_phy_read_exp);

int bcm_phy_write_misc(struct phy_device *phydev,
		       u16 reg, u16 chl, u16 val)
{
	int rc;
	int tmp;

	rc = phy_write(phydev, MII_BCM54XX_AUX_CTL,
		       MII_BCM54XX_AUXCTL_SHDWSEL_MISC);
	if (rc < 0)
		return rc;

	tmp = phy_read(phydev, MII_BCM54XX_AUX_CTL);
	tmp |= MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA;
	rc = phy_write(phydev, MII_BCM54XX_AUX_CTL, tmp);
	if (rc < 0)
		return rc;

	tmp = (chl * MII_BCM_CHANNEL_WIDTH) | reg;
	rc = bcm_phy_write_exp(phydev, tmp, val);

	return rc;
}
EXPORT_SYMBOL_GPL(bcm_phy_write_misc);

int bcm_phy_read_misc(struct phy_device *phydev,
		      u16 reg, u16 chl)
{
	int rc;
	int tmp;

	rc = phy_write(phydev, MII_BCM54XX_AUX_CTL,
		       MII_BCM54XX_AUXCTL_SHDWSEL_MISC);
	if (rc < 0)
		return rc;

	tmp = phy_read(phydev, MII_BCM54XX_AUX_CTL);
	tmp |= MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA;
	rc = phy_write(phydev, MII_BCM54XX_AUX_CTL, tmp);
	if (rc < 0)
		return rc;

	tmp = (chl * MII_BCM_CHANNEL_WIDTH) | reg;
	rc = bcm_phy_read_exp(phydev, tmp);

	return rc;
}
EXPORT_SYMBOL_GPL(bcm_phy_read_misc);

int bcm_phy_ack_intr(struct phy_device *phydev)
{
	int reg;

	/* Clear pending interrupts.  */
	reg = phy_read(phydev, MII_BCM54XX_ISR);
	if (reg < 0)
		return reg;

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_phy_ack_intr);

int bcm_phy_config_intr(struct phy_device *phydev)
{
	int reg;

	reg = phy_read(phydev, MII_BCM54XX_ECR);
	if (reg < 0)
		return reg;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		reg &= ~MII_BCM54XX_ECR_IM;
	else
		reg |= MII_BCM54XX_ECR_IM;

	return phy_write(phydev, MII_BCM54XX_ECR, reg);
}
EXPORT_SYMBOL_GPL(bcm_phy_config_intr);

int bcm_phy_read_shadow(struct phy_device *phydev, u16 shadow)
{
	phy_write(phydev, MII_BCM54XX_SHD, MII_BCM54XX_SHD_VAL(shadow));
	return MII_BCM54XX_SHD_DATA(phy_read(phydev, MII_BCM54XX_SHD));
}
EXPORT_SYMBOL_GPL(bcm_phy_read_shadow);

int bcm_phy_write_shadow(struct phy_device *phydev, u16 shadow,
			 u16 val)
{
	return phy_write(phydev, MII_BCM54XX_SHD,
			 MII_BCM54XX_SHD_WRITE |
			 MII_BCM54XX_SHD_VAL(shadow) |
			 MII_BCM54XX_SHD_DATA(val));
}
EXPORT_SYMBOL_GPL(bcm_phy_write_shadow);

int bcm_phy_enable_apd(struct phy_device *phydev, bool dll_pwr_down)
{
	int val;

	if (dll_pwr_down) {
		val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_SCR3);
		if (val < 0)
			return val;

		val |= BCM54XX_SHD_SCR3_DLLAPD_DIS;
		bcm_phy_write_shadow(phydev, BCM54XX_SHD_SCR3, val);
	}

	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_APD);
	if (val < 0)
		return val;

	/* Clear APD bits */
	val &= BCM_APD_CLR_MASK;

	if (phydev->autoneg == AUTONEG_ENABLE)
		val |= BCM54XX_SHD_APD_EN;
	else
		val |= BCM_NO_ANEG_APD_EN;

	/* Enable energy detect single link pulse for easy wakeup */
	val |= BCM_APD_SINGLELP_EN;

	/* Enable Auto Power-Down (APD) for the PHY */
	return bcm_phy_write_shadow(phydev, BCM54XX_SHD_APD, val);
}
EXPORT_SYMBOL_GPL(bcm_phy_enable_apd);

int bcm_phy_enable_eee(struct phy_device *phydev)
{
	int val;

	/* Enable EEE at PHY level */
	val = phy_read_mmd_indirect(phydev, BRCM_CL45VEN_EEE_CONTROL,
				    MDIO_MMD_AN);
	if (val < 0)
		return val;

	val |= LPI_FEATURE_EN | LPI_FEATURE_EN_DIG1000X;

	phy_write_mmd_indirect(phydev, BRCM_CL45VEN_EEE_CONTROL,
			       MDIO_MMD_AN, (u32)val);

	/* Advertise EEE */
	val = phy_read_mmd_indirect(phydev, BCM_CL45VEN_EEE_ADV,
				    MDIO_MMD_AN);
	if (val < 0)
		return val;

	val |= (MDIO_AN_EEE_ADV_100TX | MDIO_AN_EEE_ADV_1000T);

	phy_write_mmd_indirect(phydev, BCM_CL45VEN_EEE_ADV,
			       MDIO_MMD_AN, (u32)val);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_phy_enable_eee);

MODULE_DESCRIPTION("Broadcom PHY Library");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom Corporation");
