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

#ifndef __HALRF_8188F_H__
#define __HALRF_8188F_H__

/*--------------------------Define Parameters-------------------------------*/
#define IQK_DELAY_TIME_8188F 25 /* ms */
#define IQK_DEFERRED_TIME_8188F 4
#define index_mapping_NUM_8188F 15
#define AVG_THERMAL_NUM_8188F 4
#define RF_T_METER_8188F 0x42

void configure_txpower_track_8188f(struct txpwrtrack_cfg *config);

void do_iqk_8188f(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold);

void odm_tx_pwr_track_set_pwr_8188f(void *dm_void, enum pwrtrack_method method,
				    u8 rf_path, u8 channel_mapped_index);

/* 1 7.	IQK */

void phy_iq_calibrate_8188f(void *dm_void, boolean is_recovery);

/*
 * LC calibrate
 */
void phy_lc_calibrate_8188f(void *dm_void);

void _phy_save_adda_registers_8188f(
	struct dm_struct *dm,
	u32 *adda_reg,
	u32 *adda_backup,
	u32 register_num);

void _phy_path_adda_on_8188f(
	struct dm_struct *dm,
	u32 *adda_reg,
	boolean is_path_a_on,
	boolean is2T);

void _phy_mac_setting_calibration_8188f(
	struct dm_struct *dm,
	u32 *mac_reg,
	u32 *mac_backup);

void _phy_path_a_stand_by_8188f(
	struct dm_struct *dm);

void phy_set_rf_path_switch_8188f(
#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
				  struct dm_struct *dm,
#else
				  void *adapter,
#endif
				  boolean is_main);

void phy_active_large_power_detection_8188f(struct dm_struct *dm);

#endif /*#ifndef __HALRF_8188F_H__*/
