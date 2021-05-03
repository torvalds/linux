// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2017 Broadcom
 */

#include "bcm-phy-lib.h"
#include <linux/bitfield.h>
#include <linux/brcmphy.h>
#include <linux/export.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>

#define MII_BCM_CHANNEL_WIDTH     0x2000
#define BCM_CL45VEN_EEE_ADV       0x3c

int __bcm_phy_write_exp(struct phy_device *phydev, u16 reg, u16 val)
{
	int rc;

	rc = __phy_write(phydev, MII_BCM54XX_EXP_SEL, reg);
	if (rc < 0)
		return rc;

	return __phy_write(phydev, MII_BCM54XX_EXP_DATA, val);
}
EXPORT_SYMBOL_GPL(__bcm_phy_write_exp);

int bcm_phy_write_exp(struct phy_device *phydev, u16 reg, u16 val)
{
	int rc;

	phy_lock_mdio_bus(phydev);
	rc = __bcm_phy_write_exp(phydev, reg, val);
	phy_unlock_mdio_bus(phydev);

	return rc;
}
EXPORT_SYMBOL_GPL(bcm_phy_write_exp);

int __bcm_phy_read_exp(struct phy_device *phydev, u16 reg)
{
	int val;

	val = __phy_write(phydev, MII_BCM54XX_EXP_SEL, reg);
	if (val < 0)
		return val;

	val = __phy_read(phydev, MII_BCM54XX_EXP_DATA);

	/* Restore default value.  It's O.K. if this write fails. */
	__phy_write(phydev, MII_BCM54XX_EXP_SEL, 0);

	return val;
}
EXPORT_SYMBOL_GPL(__bcm_phy_read_exp);

int bcm_phy_read_exp(struct phy_device *phydev, u16 reg)
{
	int rc;

	phy_lock_mdio_bus(phydev);
	rc = __bcm_phy_read_exp(phydev, reg);
	phy_unlock_mdio_bus(phydev);

	return rc;
}
EXPORT_SYMBOL_GPL(bcm_phy_read_exp);

int __bcm_phy_modify_exp(struct phy_device *phydev, u16 reg, u16 mask, u16 set)
{
	int new, ret;

	ret = __phy_write(phydev, MII_BCM54XX_EXP_SEL, reg);
	if (ret < 0)
		return ret;

	ret = __phy_read(phydev, MII_BCM54XX_EXP_DATA);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	return __phy_write(phydev, MII_BCM54XX_EXP_DATA, new);
}
EXPORT_SYMBOL_GPL(__bcm_phy_modify_exp);

int bcm_phy_modify_exp(struct phy_device *phydev, u16 reg, u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __bcm_phy_modify_exp(phydev, reg, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(bcm_phy_modify_exp);

int bcm54xx_auxctl_read(struct phy_device *phydev, u16 regnum)
{
	/* The register must be written to both the Shadow Register Select and
	 * the Shadow Read Register Selector
	 */
	phy_write(phydev, MII_BCM54XX_AUX_CTL, MII_BCM54XX_AUXCTL_SHDWSEL_MASK |
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

int __bcm_phy_read_rdb(struct phy_device *phydev, u16 rdb)
{
	int val;

	val = __phy_write(phydev, MII_BCM54XX_RDB_ADDR, rdb);
	if (val < 0)
		return val;

	return __phy_read(phydev, MII_BCM54XX_RDB_DATA);
}
EXPORT_SYMBOL_GPL(__bcm_phy_read_rdb);

int bcm_phy_read_rdb(struct phy_device *phydev, u16 rdb)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __bcm_phy_read_rdb(phydev, rdb);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(bcm_phy_read_rdb);

int __bcm_phy_write_rdb(struct phy_device *phydev, u16 rdb, u16 val)
{
	int ret;

	ret = __phy_write(phydev, MII_BCM54XX_RDB_ADDR, rdb);
	if (ret < 0)
		return ret;

	return __phy_write(phydev, MII_BCM54XX_RDB_DATA, val);
}
EXPORT_SYMBOL_GPL(__bcm_phy_write_rdb);

int bcm_phy_write_rdb(struct phy_device *phydev, u16 rdb, u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __bcm_phy_write_rdb(phydev, rdb, val);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(bcm_phy_write_rdb);

int __bcm_phy_modify_rdb(struct phy_device *phydev, u16 rdb, u16 mask, u16 set)
{
	int new, ret;

	ret = __phy_write(phydev, MII_BCM54XX_RDB_ADDR, rdb);
	if (ret < 0)
		return ret;

	ret = __phy_read(phydev, MII_BCM54XX_RDB_DATA);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	return __phy_write(phydev, MII_BCM54XX_RDB_DATA, new);
}
EXPORT_SYMBOL_GPL(__bcm_phy_modify_rdb);

int bcm_phy_modify_rdb(struct phy_device *phydev, u16 rdb, u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __bcm_phy_modify_rdb(phydev, rdb, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(bcm_phy_modify_rdb);

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
	int val, mask = 0;

	/* Enable EEE at PHY level */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, BRCM_CL45VEN_EEE_CONTROL);
	if (val < 0)
		return val;

	if (enable)
		val |= LPI_FEATURE_EN | LPI_FEATURE_EN_DIG1000X;
	else
		val &= ~(LPI_FEATURE_EN | LPI_FEATURE_EN_DIG1000X);

	phy_write_mmd(phydev, MDIO_MMD_AN, BRCM_CL45VEN_EEE_CONTROL, (u32)val);

	/* Advertise EEE */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, BCM_CL45VEN_EEE_ADV);
	if (val < 0)
		return val;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			      phydev->supported))
		mask |= MDIO_EEE_1000T;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
			      phydev->supported))
		mask |= MDIO_EEE_100TX;

	if (enable)
		val |= mask;
	else
		val &= ~mask;

	phy_write_mmd(phydev, MDIO_MMD_AN, BCM_CL45VEN_EEE_ADV, (u32)val);

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
		strlcpy(data + i * ETH_GSTRING_LEN,
			bcm_phy_hw_stats[i].string, ETH_GSTRING_LEN);
}
EXPORT_SYMBOL_GPL(bcm_phy_get_strings);

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
		ret = U64_MAX;
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

void bcm_phy_r_rc_cal_reset(struct phy_device *phydev)
{
	/* Reset R_CAL/RC_CAL Engine */
	bcm_phy_write_exp_sel(phydev, 0x00b0, 0x0010);

	/* Disable Reset R_AL/RC_CAL Engine */
	bcm_phy_write_exp_sel(phydev, 0x00b0, 0x0000);
}
EXPORT_SYMBOL_GPL(bcm_phy_r_rc_cal_reset);

int bcm_phy_28nm_a0b0_afe_config_init(struct phy_device *phydev)
{
	/* Increase VCO range to prevent unlocking problem of PLL at low
	 * temp
	 */
	bcm_phy_write_misc(phydev, PLL_PLLCTRL_1, 0x0048);

	/* Change Ki to 011 */
	bcm_phy_write_misc(phydev, PLL_PLLCTRL_2, 0x021b);

	/* Disable loading of TVCO buffer to bandgap, set bandgap trim
	 * to 111
	 */
	bcm_phy_write_misc(phydev, PLL_PLLCTRL_4, 0x0e20);

	/* Adjust bias current trim by -3 */
	bcm_phy_write_misc(phydev, DSP_TAP10, 0x690b);

	/* Switch to CORE_BASE1E */
	phy_write(phydev, MII_BRCM_CORE_BASE1E, 0xd);

	bcm_phy_r_rc_cal_reset(phydev);

	/* write AFE_RXCONFIG_0 */
	bcm_phy_write_misc(phydev, AFE_RXCONFIG_0, 0xeb19);

	/* write AFE_RXCONFIG_1 */
	bcm_phy_write_misc(phydev, AFE_RXCONFIG_1, 0x9a3f);

	/* write AFE_RX_LP_COUNTER */
	bcm_phy_write_misc(phydev, AFE_RX_LP_COUNTER, 0x7fc0);

	/* write AFE_HPF_TRIM_OTHERS */
	bcm_phy_write_misc(phydev, AFE_HPF_TRIM_OTHERS, 0x000b);

	/* write AFTE_TX_CONFIG */
	bcm_phy_write_misc(phydev, AFE_TX_CONFIG, 0x0800);

	return 0;
}
EXPORT_SYMBOL_GPL(bcm_phy_28nm_a0b0_afe_config_init);

int bcm_phy_enable_jumbo(struct phy_device *phydev)
{
	int ret;

	ret = bcm54xx_auxctl_read(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL);
	if (ret < 0)
		return ret;

	/* Enable extended length packet reception */
	ret = bcm54xx_auxctl_write(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
				   ret | MII_BCM54XX_AUXCTL_ACTL_EXT_PKT_LEN);
	if (ret < 0)
		return ret;

	/* Enable the elastic FIFO for raising the transmission limit from
	 * 4.5KB to 10KB, at the expense of an additional 16 ns in propagation
	 * latency.
	 */
	return phy_set_bits(phydev, MII_BCM54XX_ECR, MII_BCM54XX_ECR_FIFOE);
}
EXPORT_SYMBOL_GPL(bcm_phy_enable_jumbo);

static int __bcm_phy_enable_rdb_access(struct phy_device *phydev)
{
	return __bcm_phy_write_exp(phydev, BCM54XX_EXP_REG7E, 0);
}

static int __bcm_phy_enable_legacy_access(struct phy_device *phydev)
{
	return __bcm_phy_write_rdb(phydev, BCM54XX_RDB_REG0087,
				   BCM54XX_ACCESS_MODE_LEGACY_EN);
}

static int _bcm_phy_cable_test_start(struct phy_device *phydev, bool is_rdb)
{
	u16 mask, set;
	int ret;

	/* Auto-negotiation must be enabled for cable diagnostics to work, but
	 * don't advertise any capabilities.
	 */
	phy_write(phydev, MII_BMCR, BMCR_ANENABLE);
	phy_write(phydev, MII_ADVERTISE, ADVERTISE_CSMA);
	phy_write(phydev, MII_CTRL1000, 0);

	phy_lock_mdio_bus(phydev);
	if (is_rdb) {
		ret = __bcm_phy_enable_legacy_access(phydev);
		if (ret)
			goto out;
	}

	mask = BCM54XX_ECD_CTRL_CROSS_SHORT_DIS | BCM54XX_ECD_CTRL_UNIT_MASK;
	set = BCM54XX_ECD_CTRL_RUN | BCM54XX_ECD_CTRL_BREAK_LINK |
	      FIELD_PREP(BCM54XX_ECD_CTRL_UNIT_MASK,
			 BCM54XX_ECD_CTRL_UNIT_CM);

	ret = __bcm_phy_modify_exp(phydev, BCM54XX_EXP_ECD_CTRL, mask, set);

out:
	/* re-enable the RDB access even if there was an error */
	if (is_rdb)
		ret = __bcm_phy_enable_rdb_access(phydev) ? : ret;

	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int bcm_phy_cable_test_report_trans(int result)
{
	switch (result) {
	case BCM54XX_ECD_FAULT_TYPE_OK:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case BCM54XX_ECD_FAULT_TYPE_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case BCM54XX_ECD_FAULT_TYPE_SAME_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case BCM54XX_ECD_FAULT_TYPE_CROSS_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_CROSS_SHORT;
	case BCM54XX_ECD_FAULT_TYPE_INVALID:
	case BCM54XX_ECD_FAULT_TYPE_BUSY:
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static bool bcm_phy_distance_valid(int result)
{
	switch (result) {
	case BCM54XX_ECD_FAULT_TYPE_OPEN:
	case BCM54XX_ECD_FAULT_TYPE_SAME_SHORT:
	case BCM54XX_ECD_FAULT_TYPE_CROSS_SHORT:
		return true;
	}
	return false;
}

static int bcm_phy_report_length(struct phy_device *phydev, int pair)
{
	int val;

	val = __bcm_phy_read_exp(phydev,
				 BCM54XX_EXP_ECD_PAIR_A_LENGTH_RESULTS + pair);
	if (val < 0)
		return val;

	if (val == BCM54XX_ECD_LENGTH_RESULTS_INVALID)
		return 0;

	ethnl_cable_test_fault_length(phydev, pair, val);

	return 0;
}

static int _bcm_phy_cable_test_get_status(struct phy_device *phydev,
					  bool *finished, bool is_rdb)
{
	int pair_a, pair_b, pair_c, pair_d, ret;

	*finished = false;

	phy_lock_mdio_bus(phydev);

	if (is_rdb) {
		ret = __bcm_phy_enable_legacy_access(phydev);
		if (ret)
			goto out;
	}

	ret = __bcm_phy_read_exp(phydev, BCM54XX_EXP_ECD_CTRL);
	if (ret < 0)
		goto out;

	if (ret & BCM54XX_ECD_CTRL_IN_PROGRESS) {
		ret = 0;
		goto out;
	}

	ret = __bcm_phy_read_exp(phydev, BCM54XX_EXP_ECD_FAULT_TYPE);
	if (ret < 0)
		goto out;

	pair_a = FIELD_GET(BCM54XX_ECD_FAULT_TYPE_PAIR_A_MASK, ret);
	pair_b = FIELD_GET(BCM54XX_ECD_FAULT_TYPE_PAIR_B_MASK, ret);
	pair_c = FIELD_GET(BCM54XX_ECD_FAULT_TYPE_PAIR_C_MASK, ret);
	pair_d = FIELD_GET(BCM54XX_ECD_FAULT_TYPE_PAIR_D_MASK, ret);

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				bcm_phy_cable_test_report_trans(pair_a));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_B,
				bcm_phy_cable_test_report_trans(pair_b));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_C,
				bcm_phy_cable_test_report_trans(pair_c));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_D,
				bcm_phy_cable_test_report_trans(pair_d));

	if (bcm_phy_distance_valid(pair_a))
		bcm_phy_report_length(phydev, 0);
	if (bcm_phy_distance_valid(pair_b))
		bcm_phy_report_length(phydev, 1);
	if (bcm_phy_distance_valid(pair_c))
		bcm_phy_report_length(phydev, 2);
	if (bcm_phy_distance_valid(pair_d))
		bcm_phy_report_length(phydev, 3);

	ret = 0;
	*finished = true;
out:
	/* re-enable the RDB access even if there was an error */
	if (is_rdb)
		ret = __bcm_phy_enable_rdb_access(phydev) ? : ret;

	phy_unlock_mdio_bus(phydev);

	return ret;
}

int bcm_phy_cable_test_start(struct phy_device *phydev)
{
	return _bcm_phy_cable_test_start(phydev, false);
}
EXPORT_SYMBOL_GPL(bcm_phy_cable_test_start);

int bcm_phy_cable_test_get_status(struct phy_device *phydev, bool *finished)
{
	return _bcm_phy_cable_test_get_status(phydev, finished, false);
}
EXPORT_SYMBOL_GPL(bcm_phy_cable_test_get_status);

/* We assume that all PHYs which support RDB access can be switched to legacy
 * mode. If, in the future, this is not true anymore, we have to re-implement
 * this with RDB access.
 */
int bcm_phy_cable_test_start_rdb(struct phy_device *phydev)
{
	return _bcm_phy_cable_test_start(phydev, true);
}
EXPORT_SYMBOL_GPL(bcm_phy_cable_test_start_rdb);

int bcm_phy_cable_test_get_status_rdb(struct phy_device *phydev,
				      bool *finished)
{
	return _bcm_phy_cable_test_get_status(phydev, finished, true);
}
EXPORT_SYMBOL_GPL(bcm_phy_cable_test_get_status_rdb);

MODULE_DESCRIPTION("Broadcom PHY Library");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom Corporation");
