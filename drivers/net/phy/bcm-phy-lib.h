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

#ifndef _LINUX_BCM_PHY_LIB_H
#define _LINUX_BCM_PHY_LIB_H

#include <linux/brcmphy.h>
#include <linux/phy.h>

int bcm_phy_write_exp(struct phy_device *phydev, u16 reg, u16 val);
int bcm_phy_read_exp(struct phy_device *phydev, u16 reg);

static inline int bcm_phy_write_exp_sel(struct phy_device *phydev,
					u16 reg, u16 val)
{
	return bcm_phy_write_exp(phydev, reg | MII_BCM54XX_EXP_SEL_ER, val);
}

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

int bcm_phy_enable_eee(struct phy_device *phydev);
#endif /* _LINUX_BCM_PHY_LIB_H */
