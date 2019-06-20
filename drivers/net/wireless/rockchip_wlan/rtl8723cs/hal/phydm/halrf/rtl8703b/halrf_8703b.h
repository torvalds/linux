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

#ifndef __HALRF_8703B_H__
#define __HALRF_8703B_H__

/*--------------------------Define Parameters-------------------------------*/
#define index_mapping_NUM_8703B 15
#define AVG_THERMAL_NUM_8703B 4
#define RF_T_METER_8703B 0x42

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if RT_PLATFORM == PLATFORM_MACOSX
#include "halphyrf_win.h"
#else
#include "../halrf/halphyrf_win.h"
#endif
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#include "../halphyrf_ce.h"
#endif

void configure_txpower_track_8703b(struct txpwrtrack_cfg *config);

void do_iqk_8703b(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold);

void odm_tx_pwr_track_set_pwr_8703b(void *dm_void, enum pwrtrack_method method,
				    u8 rf_path, u8 channel_mapped_index);

void odm_txxtaltrack_set_xtal_8703b(void *dm_void);

/* 1 7.	IQK */

void phy_iq_calibrate_8703b(void *dm_void, boolean is_recovery);

boolean
odm_set_iqc_by_rfpath_8703b(struct dm_struct *dm);

/*
 * LC calibrate
 */
void phy_lc_calibrate_8703b(void *dm_void);

#if 0
/*
 * AP calibrate
 *   */
void
phy_ap_calibrate_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct		*dm,
#else
	void	*adapter,
#endif
	s8		delta);
void
phy_digital_predistortion_8703b(void	*adapter);
#endif

void _phy_save_adda_registers_8703b(
	struct dm_struct *dm,
	u32 *adda_reg,
	u32 *adda_backup,
	u32 register_num);

void _phy_path_adda_on_8703b(
	struct dm_struct *dm,
	u32 *adda_reg,
	boolean is_path_a_on,
	boolean is2T);

void _phy_mac_setting_calibration_8703b(
	struct dm_struct *dm,
	u32 *mac_reg,
	u32 *mac_backup);

#endif /*#ifndef __HALRF_8703B_H__*/
