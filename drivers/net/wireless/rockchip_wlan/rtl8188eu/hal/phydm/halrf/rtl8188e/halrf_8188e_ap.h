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

#ifndef __HALRF_8188E_H__
#define __HALRF_8188E_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_88E		10		/* ms */
#define	index_mapping_NUM_88E	15
#define AVG_THERMAL_NUM_88E	4

#include "../halphyrf_ap.h"

void configure_txpower_track_8188e(
	struct txpwrtrack_cfg	*config
);

void do_iqk_8188e(
	void		*dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
);

void
odm_tx_pwr_track_set_pwr88_e(
	struct dm_struct			*dm,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
);

/* 1 7.	IQK */

void
phy_iq_calibrate_8188e(
	struct dm_struct		*dm,
	boolean	is_recovery);


/*
 * LC calibrate
 *   */
void
phy_lc_calibrate_8188e(
	struct dm_struct		*dm
);

/*
 * AP calibrate
 *   */
void
phy_ap_calibrate_8188e(
	struct dm_struct		*dm,
	s8		delta);

void
_phy_save_adda_registers(
	struct dm_struct		*dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
);

void
_phy_path_adda_on(
	struct dm_struct		*dm,
	u32		*adda_reg,
	boolean		is_path_a_on,
	boolean		is2T
);

void
_phy_mac_setting_calibration(
	struct dm_struct		*dm,
	u32		*mac_reg,
	u32		*mac_backup
);


void
_phy_path_a_stand_by(
	struct dm_struct		*dm
);

void
halrf_rf_lna_setting_8188e(
	struct dm_struct	*dm,
	enum halrf_lna_set type
);

#endif	/*#ifndef __HALRF_8188E_H__*/
