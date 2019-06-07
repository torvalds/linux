// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Broadcom Corporation
 */

#ifndef _LINUX_BCM_PHY_LIB_H
#define _LINUX_BCM_PHY_LIB_H

#include <linux/brcmphy.h>
#include <linux/phy.h>

/* 28nm only register definitions */
#define MISC_ADDR(base, channel)	base, channel

#define DSP_TAP10			MISC_ADDR(0x0a, 0)
#define PLL_PLLCTRL_1			MISC_ADDR(0x32, 1)
#define PLL_PLLCTRL_2			MISC_ADDR(0x32, 2)
#define PLL_PLLCTRL_4			MISC_ADDR(0x33, 0)

#define AFE_RXCONFIG_0			MISC_ADDR(0x38, 0)
#define AFE_RXCONFIG_1			MISC_ADDR(0x38, 1)
#define AFE_RXCONFIG_2			MISC_ADDR(0x38, 2)
#define AFE_RX_LP_COUNTER		MISC_ADDR(0x38, 3)
#define AFE_TX_CONFIG			MISC_ADDR(0x39, 0)
#define AFE_VDCA_ICTRL_0		MISC_ADDR(0x39, 1)
#define AFE_VDAC_OTHERS_0		MISC_ADDR(0x39, 3)
#define AFE_HPF_TRIM_OTHERS		MISC_ADDR(0x3a, 0)


int bcm_phy_write_exp(struct phy_device *phydev, u16 reg, u16 val);
int bcm_phy_read_exp(struct phy_device *phydev, u16 reg);

static inline int bcm_phy_write_exp_sel(struct phy_device *phydev,
					u16 reg, u16 val)
{
	return bcm_phy_write_exp(phydev, reg | MII_BCM54XX_EXP_SEL_ER, val);
}

int bcm54xx_auxctl_write(struct phy_device *phydev, u16 regnum, u16 val);
int bcm54xx_auxctl_read(struct phy_device *phydev, u16 regnum);

int bcm_phy_write_misc(struct phy_device *phydev,
		       u16 reg, u16 chl, u16 value);
int bcm_phy_read_misc(struct phy_device *phydev,
		      u16 reg, u16 chl);

int bcm_phy_write_shadow(struct phy_device *phydev, u16 shadow,
			 u16 val);
int bcm_phy_read_shadow(struct phy_device *phydev, u16 shadow);

int bcm_phy_ack_intr(struct phy_device *phydev);
int bcm_phy_config_intr(struct phy_device *phydev);

int bcm_phy_enable_apd(struct phy_device *phydev, bool dll_pwr_down);

int bcm_phy_set_eee(struct phy_device *phydev, bool enable);

int bcm_phy_downshift_get(struct phy_device *phydev, u8 *count);

int bcm_phy_downshift_set(struct phy_device *phydev, u8 count);

int bcm_phy_get_sset_count(struct phy_device *phydev);
void bcm_phy_get_strings(struct phy_device *phydev, u8 *data);
void bcm_phy_get_stats(struct phy_device *phydev, u64 *shadow,
		       struct ethtool_stats *stats, u64 *data);
void bcm_phy_r_rc_cal_reset(struct phy_device *phydev);
int bcm_phy_28nm_a0b0_afe_config_init(struct phy_device *phydev);

#endif /* _LINUX_BCM_PHY_LIB_H */
