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

#ifndef __HAL_PHY_RF_8822B_H__
#define __HAL_PHY_RF_8822B_H__

#define AVG_THERMAL_NUM_8822B 4
#define RF_T_METER_8822B 0x42

void configure_txpower_track_8822b(struct txpwrtrack_cfg *config);

void odm_tx_pwr_track_set_pwr8822b(void *dm_void, enum pwrtrack_method method,
				   u8 rf_path, u8 channel_mapped_index);

void get_delta_swing_table_8822b(void *dm_void, u8 **temperature_up_a,
				 u8 **temperature_down_a, u8 **temperature_up_b,
				 u8 **temperature_down_b);

void phy_lc_calibrate_8822b(void *dm_void);

void phy_set_rf_path_switch_8822b(struct phy_dm_struct *dm, bool is_main);

#endif /* #ifndef __HAL_PHY_RF_8822B_H__ */
