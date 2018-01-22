/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __INC_ODM_REGCONFIG_H_8822B
#define __INC_ODM_REGCONFIG_H_8822B

void odm_config_rf_reg_8822b(struct phy_dm_struct *dm, u32 addr, u32 data,
			     enum odm_rf_radio_path RF_PATH, u32 reg_addr);

void odm_config_rf_radio_a_8822b(struct phy_dm_struct *dm, u32 addr, u32 data);

void odm_config_rf_radio_b_8822b(struct phy_dm_struct *dm, u32 addr, u32 data);

void odm_config_mac_8822b(struct phy_dm_struct *dm, u32 addr, u8 data);

void odm_update_agc_big_jump_lmt_8822b(struct phy_dm_struct *dm, u32 addr,
				       u32 data);

void odm_config_bb_agc_8822b(struct phy_dm_struct *dm, u32 addr, u32 bitmask,
			     u32 data);

void odm_config_bb_phy_reg_pg_8822b(struct phy_dm_struct *dm, u32 band,
				    u32 rf_path, u32 tx_num, u32 addr,
				    u32 bitmask, u32 data);

void odm_config_bb_phy_8822b(struct phy_dm_struct *dm, u32 addr, u32 bitmask,
			     u32 data);

void odm_config_bb_txpwr_lmt_8822b(struct phy_dm_struct *dm, u8 *regulation,
				   u8 *band, u8 *bandwidth, u8 *rate_section,
				   u8 *rf_path, u8 *channel, u8 *power_limit);

#endif /* RTL8822B_SUPPORT == 1*/
