/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef __HAL_PHY_RF_8703B_H__
#define __HAL_PHY_RF_8703B_H__

/*--------------------------Define Parameters-------------------------------*/
#define	index_mapping_NUM_8703B		15
#define AVG_THERMAL_NUM_8703B		4
#define RF_T_METER_8703B		0x42

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	#include "../halphyrf_win.h"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	#include "../halphyrf_ce.h"
#endif

void configure_txpower_track_8703b(
	struct _TXPWRTRACK_CFG	*p_config
);

void do_iqk_8703b(
	void		*p_dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
);

void
odm_tx_pwr_track_set_pwr_8703b(
	void		*p_dm_void,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
);

void
odm_txxtaltrack_set_xtal_8703b(
	void		*p_dm_void
);


/* 1 7.	IQK */

void
phy_iq_calibrate_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*adapter,
#endif
	boolean	is_recovery);

boolean
odm_set_iqc_by_rfpath_8703b(
	struct PHY_DM_STRUCT		*p_dm_odm
);

/*
 * LC calibrate
 *   */
void
phy_lc_calibrate_8703b(
	void		*p_dm_void
);

/*
 * AP calibrate
 *   */
void
phy_ap_calibrate_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s8		delta);
void
phy_digital_predistortion_8703b(struct _ADAPTER	*p_adapter);


void
_phy_save_adda_registers_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
);

void
_phy_path_adda_on_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*adda_reg,
	boolean		is_path_a_on,
	boolean		is2T
);

void
_phy_mac_setting_calibration_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*mac_reg,
	u32		*mac_backup
);

#endif	/*  #ifndef __HAL_PHY_RF_8188E_H__ */
