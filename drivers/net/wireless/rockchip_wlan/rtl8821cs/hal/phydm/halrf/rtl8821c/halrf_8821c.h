/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#ifndef __HALRF_8821C_H__
#define __HALRF_8821C_H__

#define AVG_THERMAL_NUM_8821C 4
#define RF_T_METER_8821C 0x42

void configure_txpower_track_8821c(struct txpwrtrack_cfg *config);

void odm_tx_pwr_track_set_pwr8821c(void *dm_void, enum pwrtrack_method method,
				   u8 rf_path, u8 channel_mapped_index);

void get_delta_swing_table_8821c(void *dm_void,
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
				 u8 **temperature_up_a, u8 **temperature_down_a,
				 u8 **temperature_up_b, u8 **temperature_down_b,
				 u8 **temperature_up_cck_a,
				 u8 **temperature_down_cck_a,
				 u8 **temperature_up_cck_b,
				 u8 **temperature_down_cck_b
#else
				 u8 **temperature_up_a, u8 **temperature_down_a,
				 u8 **temperature_up_b,
				 u8 **temperature_down_b
#endif
				 );

void phy_lc_calibrate_8821c(void *dm_void);

void halrf_rf_lna_setting_8821c(struct dm_struct *dm, enum halrf_lna_set type);

#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
void phy_set_rf_path_switch_8821c(struct dm_struct *dm,
#else
void phy_set_rf_path_switch_8821c(void *adapter,
#endif
				  boolean is_main);

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
boolean phy_query_rf_path_switch_8821c(struct dm_struct *dm
#else
boolean phy_query_rf_path_switch_8821c(void *adapter
#endif
				       );

#endif /*#ifndef __HALRF_8821C_H__*/
