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

#ifndef __HAL_PHY_RF_8188E_H__
#define __HAL_PHY_RF_8188E_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_88E		10		/* ms */
#define	index_mapping_NUM_88E	15
#define AVG_THERMAL_NUM_88E	4

#include "../halphyrf_ap.h"

void configure_txpower_track_8188e(
	struct _TXPWRTRACK_CFG	*p_config
);

void do_iqk_8188e(
	void		*p_dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
);

void
odm_tx_pwr_track_set_pwr88_e(
	struct PHY_DM_STRUCT			*p_dm_odm,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
);

/* 1 7.	IQK */

void
phy_iq_calibrate_8188e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*adapter,
#endif
	bool	is_recovery);


/*
 * LC calibrate
 *   */
void
phy_lc_calibrate_8188e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm
#else
	struct _ADAPTER	*p_adapter
#endif
);

/*
 * AP calibrate
 *   */
void
phy_ap_calibrate_8188e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s8		delta);
void
phy_digital_predistortion_8188e(struct _ADAPTER	*p_adapter);


void
_phy_save_adda_registers(
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
_phy_path_adda_on(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*adda_reg,
	bool		is_path_a_on,
	bool		is2T
);

void
_phy_mac_setting_calibration(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*mac_reg,
	u32		*mac_backup
);


void
_phy_path_a_stand_by(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm
#else
	struct _ADAPTER	*p_adapter
#endif
);


#endif	/*  #ifndef __HAL_PHY_RF_8188E_H__ */
