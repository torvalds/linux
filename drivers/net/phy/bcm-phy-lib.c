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
#include <linux/ethtool.h>

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

int bcm54xx_auxctl_read(struct phy_device *phydev, u16 regnum)
{
	/* The register must be written to both the Shadow Register Select and
	 * the Shadow Read Register Selector
	 */
	phy_write(phydev, MII_BCM54XX_AUX_CTL, regnum |
		  regnum << MII_BCM54XX_AUXCTL_SHDWSEL_READ_SHIFT);
	return phy_read(phydev, MII_BCM54XX_AUX_CTL);
}
EXPORT_SYMBOL_GPL(bcm54xx_auxctl_read);

int bcm54xx_auxctl_write(struct phy_device *phydev, u16 regnum, u16 val)
{
	return phy_write(phydev, MII_BCM54XX_AUX_CTL, regnum | val);
}
EXPORT_SYMBOL(bcm54xx_auxctl_write);

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

int bcm_phy_set_eee(struct phy_device *phydev, bool enable)
{
	int val;

	/* Enable EEE at PHY level */
	val = phy_read_mmd_indirect(phydev, BRCM_CL45VEN_EEE_CONTROL,
				    MDIO_MMD_AN);
	if (val < 0)
		return val;

	if (enable)
		val |= LPI_FEATURE_EN | LPI_FEATURE_EN_DIG1000X;
	else
		val &= ~(LPI_FEATURE_EN | LPI_FEATURE_EN_DIG1000X);

	phy_write_mmd_indirect(phydev, BRCM_CL45VEN_EEE_CONTROL,
			       MDIO_MMD_AN, (u32)val);

	/* Advertise EEE */
	val = phy_read_mmd_indirect(phydev, BCM_CL45VEN_EEE_ADV,
				    MDIO_MMD_AN);
	if (val < 0)
		return val;

	if (enable)
		val |= (MDIO_AN_EEE_ADV_100TX | MDIO_AN_EEE_ADV_1000T);
	else
		val &= ~(MDIO_AN_EEE_ADV_100TX | MDIO_AN_EEE_ADV_1000T);

	phy_write_mmd_indirect(phydev, BCM_CL45VEN_EEE_ADV,
			       MDIO_MMD_AN, (u32)val);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_phy_set_eee);

int bcm_phy_downshift_get(struct phy_device *phydev, u8 *count)
{
	int val;

	val = bcm54xx_auxctl_read(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_MISC);
	if (val < 0)
		return val;

	/* Check if wirespeed is enabled or not */
	if (!(val & MII_BCM54XX_AUXCTL_SHDWSEL_MISC_WIRESPEED_EN)) {
		*count = DOWNSHIFT_DEV_DISABLE;
		return 0;
	}

	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_SCR2);
	if (val < 0)
		return val;

	/* Downgrade after one link attempt */
	if (val & BCM54XX_SHD_SCR2_WSPD_RTRY_DIS) {
		*count = 1;
	} else {
		/* Downgrade after configured retry count */
		val >>= BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_SHIFT;
		val &= BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_MASK;
		*count = val + BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_OFFSET;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_phy_downshift_get);

int bcm_phy_downshift_set(struct phy_device *phydev, u8 count)
{
	int val = 0, ret = 0;

	/* Range check the number given */
	if (count - BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_OFFSET >
	    BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_MASK &&
	    count != DOWNSHIFT_DEV_DEFAULT_COUNT) {
		return -ERANGE;
	}

	val = bcm54xx_auxctl_read(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_MISC);
	if (val < 0)
		return val;

	/* Se the write enable bit */
	val |= MII_BCM54XX_AUXCTL_MISC_WREN;

	if (count == DOWNSHIFT_DEV_DISABLE) {
		val &= ~MII_BCM54XX_AUXCTL_SHDWSEL_MISC_WIRESPEED_EN;
		return bcm54xx_auxctl_write(phydev,
					    MII_BCM54XX_AUXCTL_SHDWSEL_MISC,
					    val);
	} else {
		val |= MII_BCM54XX_AUXCTL_SHDWSEL_MISC_WIRESPEED_EN;
		ret = bcm54xx_auxctl_write(phydev,
					   MII_BCM54XX_AUXCTL_SHDWSEL_MISC,
					   val);
		if (ret < 0)
			return ret;
	}

	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_SCR2);
	val &= ~(BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_MASK <<
		 BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_SHIFT |
		 BCM54XX_SHD_SCR2_WSPD_RTRY_DIS);

	switch (count) {
	case 1:
		val |= BCM54XX_SHD_SCR2_WSPD_RTRY_DIS;
		break;
	case DOWNSHIFT_DEV_DEFAULT_COUNT:
		val |= 1 << BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_SHIFT;
		break;
	default:
		val |= (count - BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_OFFSET) <<
			BCM54XX_SHD_SCR2_WSPD_RTRY_LMT_SHIFT;
		break;
	}

	return bcm_phy_write_shadow(phydev, BCM54XX_SHD_SCR2, val);
}
EXPORT_SYMBOL_GPL(bcm_phy_downshift_set);

struct bcm_phy_hw_stat {
	const char *string;
	u8 reg;
	u8 shift;
	u8 bits;
};

/* Counters freeze at either 0xffff or 0xff, better than nothing */
static const struct bcm_phy_hw_stat bcm_phy_hw_stats[] = {
	{ "phy_receive_errors", MII_BRCM_CORE_BASE12, 0, 16 },
	{ "phy_serdes_ber_errors", MII_BRCM_CORE_BASE13, 8, 8 },
	{ "phy_false_carrier_sense_errors", MII_BRCM_CORE_BASE13, 0, 8 },
	{ "phy_local_rcvr_nok", MII_BRCM_CORE_BASE14, 8, 8 },
	{ "phy_remote_rcv_nok", MII_BRCM_CORE_BASE14, 0, 8 },
};

int bcm_phy_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(bcm_phy_hw_stats);
}
EXPORT_SYMBOL_GPL(bcm_phy_get_sset_count);

void bcm_phy_get_strings(struct phy_device *phydev, u8 *data)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bcm_phy_hw_stats); i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       bcm_phy_hw_stats[i].string, ETH_GSTRING_LEN);
}
EXPORT_SYMBOL_GPL(bcm_phy_get_strings);

#ifndef UINT64_MAX
#define UINT64_MAX              (u64)(~((u64)0))
#endif

/* Caller is supposed to provide appropriate storage for the library code to
 * access the shadow copy
 */
static u64 bcm_phy_get_stat(struct phy_device *phydev, u64 *shadow,
			    unsigned int i)
{
	struct bcm_phy_hw_stat stat = bcm_phy_hw_stats[i];
	int val;
	u64 ret;

	val = phy_read(phydev, stat.reg);
	if (val < 0) {
		ret = UINT64_MAX;
	} else {
		val >>= stat.shift;
		val = val & ((1 << stat.bits) - 1);
		shadow[i] += val;
		ret = shadow[i];
	}

	return ret;
}

void bcm_phy_get_stats(struct phy_device *phydev, u64 *shadow,
		       struct ethtool_stats *stats, u64 *data)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bcm_phy_hw_stats); i++)
		data[i] = bcm_phy_get_stat(phydev, shadow, i);
}
EXPORT_SYMBOL_GPL(bcm_phy_get_stats);

MODULE_DESCRIPTION("Broadcom PHY Library");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom Corporation");
