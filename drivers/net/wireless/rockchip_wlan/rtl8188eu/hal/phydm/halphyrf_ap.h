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

#ifndef __HAL_PHY_RF_H__
#define __HAL_PHY_RF_H__

#include "phydm_powertracking_ap.h"
#if (RTL8814A_SUPPORT == 1)
	#include "rtl8814a/phydm_iqk_8814a.h"
#endif

#if (RTL8822B_SUPPORT == 1)
	#include "rtl8822b/phydm_iqk_8822b.h"
#endif

#if (RTL8821C_SUPPORT == 1)
	#include "rtl8822b/phydm_iqk_8821c.h"
#endif

enum pwrtrack_method {
	BBSWING,
	TXAGC,
	MIX_MODE,
	TSSI_MODE
};

typedef void	(*func_set_pwr)(void *, enum pwrtrack_method, u8, u8);
typedef void(*func_iqk)(void *, u8, u8, u8);
typedef void	(*func_lck)(void *);
/* refine by YuChen for 8814A */
typedef void	(*func_swing)(void *, u8 **, u8 **, u8 **, u8 **);
typedef void	(*func_swing8814only)(void *, u8 **, u8 **, u8 **, u8 **);
typedef void	(*func_all_swing)(void *, u8 **, u8 **, u8 **, u8 **, u8 **, u8 **, u8 **, u8 **);


struct _TXPWRTRACK_CFG {
	u8		swing_table_size_cck;
	u8		swing_table_size_ofdm;
	u8		threshold_iqk;
	u8		threshold_dpk;
	u8		average_thermal_num;
	u8		rf_path_count;
	u32		thermal_reg_addr;
	func_set_pwr	odm_tx_pwr_track_set_pwr;
	func_iqk	do_iqk;
	func_lck		phy_lc_calibrate;
	func_swing	get_delta_swing_table;
	func_swing8814only	get_delta_swing_table8814only;
	func_all_swing	get_delta_all_swing_table;
};

void
configure_txpower_track(
	void		*p_dm_void,
	struct _TXPWRTRACK_CFG	*p_config
);


void
odm_txpowertracking_callback_thermal_meter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
);

#if (RTL8192E_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_92e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
);
#endif

#if (RTL8814A_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series2(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
);

#elif ODM_IC_11AC_SERIES_SUPPORT
void
odm_txpowertracking_callback_thermal_meter_jaguar_series(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
);

#elif (RTL8197F_SUPPORT == 1 || RTL8822B_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series3(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
);

#endif

#define IS_CCK_RATE(_rate)				(ODM_MGN_1M == _rate || _rate == ODM_MGN_2M || _rate == ODM_MGN_5_5M || _rate == ODM_MGN_11M)


#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#define MAX_TOLERANCE          5
#define IQK_DELAY_TIME         1               /* ms */

/*
* BB/MAC/RF other monitor API
*   */

void	phy_set_monitor_mode8192c(struct _ADAPTER	*p_adapter,
				  bool		is_enable_monitor_mode);

/*
 * IQ calibrate
 *   */
void
phy_iq_calibrate_8192c(struct _ADAPTER	*p_adapter,
		       bool	is_recovery);

/*
 * LC calibrate
 *   */
void
phy_lc_calibrate_8192c(struct _ADAPTER	*p_adapter);

/*
 * AP calibrate
 *   */
void
phy_ap_calibrate_8192c(struct _ADAPTER	*p_adapter,
		       s8		delta);
#endif

#define ODM_TARGET_CHNL_NUM_2G_5G	59


void
odm_reset_iqk_result(
	void		*p_dm_void
);
u8
odm_get_right_chnl_place_for_iqk(
	u8 chnl
);

void phydm_rf_init(void		*p_dm_void);
void phydm_rf_watchdog(void		*p_dm_void);

#endif	/*  #ifndef __HAL_PHY_RF_H__ */
