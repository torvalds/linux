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

#ifndef __HALPHYRF_H__
#define __HALPHYRF_H__

#if (RTL8814A_SUPPORT == 1)
	#include "halrf/rtl8814a/halrf_iqk_8814a.h"
#endif

#if (RTL8822B_SUPPORT == 1)
	#include "halrf/rtl8822b/halrf_iqk_8822b.h"
	#include "../mac/Halmac_type.h"
#endif
#include "halrf/halrf_powertracking_win.h"
#include "halrf/halrf_kfree.h"
#include "halrf/halrf_txgapcal.h"
#if (RTL8821C_SUPPORT == 1)
	#include "halrf/rtl8821c/halrf_iqk_8821c.h"
#endif

#if (RTL8195B_SUPPORT == 1)
//	#include "halrf/rtl8195b/halrf.h"
	#include "halrf/rtl8195b/halrf_iqk_8195b.h"
	#include "halrf/rtl8195b/halrf_txgapk_8195b.h"
	#include "halrf/rtl8195b/halrf_dpk_8195b.h"
#endif

#if (RTL8814B_SUPPORT == 1)
	#include "halrf/rtl8814b/halrf_iqk_8814b.h"
#endif

enum spur_cal_method {
	PLL_RESET,
	AFE_PHASE_SEL
};

enum pwrtrack_method {
	BBSWING,
	TXAGC,
	MIX_MODE,
	TSSI_MODE,
	MIX_2G_TSSI_5G_MODE,
	MIX_5G_TSSI_2G_MODE,
	CLEAN_MODE
};

typedef void(*func_set_pwr)(void *, enum pwrtrack_method, u8, u8);
typedef void(*func_iqk)(void *, u8, u8, u8);
typedef void(*func_lck)(void *);
typedef void(*func_tssi_dck)(void *, u8);
typedef void(*func_swing)(void *, u8 **, u8 **, u8 **, u8 **);
typedef void(*func_swing8814only)(void *, u8 **, u8 **, u8 **, u8 **);
typedef void (*func_swing_xtal)(void *, s8 **, s8 **);
typedef void (*func_set_xtal)(void *);
typedef void(*func_all_swing)(void *, u8 **, u8 **, u8 **, u8 **, u8 **, u8 **, u8 **, u8 **);

struct txpwrtrack_cfg {
	u8		swing_table_size_cck;
	u8		swing_table_size_ofdm;
	u8		threshold_iqk;
	u8		threshold_dpk;
	u8		average_thermal_num;
	u8		rf_path_count;
	u32		thermal_reg_addr;
	func_set_pwr	odm_tx_pwr_track_set_pwr;
	func_iqk	do_iqk;
	func_lck	phy_lc_calibrate;
	func_tssi_dck	do_tssi_dck;
	func_swing	get_delta_swing_table;
	func_swing8814only	get_delta_swing_table8814only;
	func_swing_xtal			get_delta_swing_xtal_table;
	func_set_xtal			odm_txxtaltrack_set_xtal;
	func_all_swing	get_delta_all_swing_table;
};

void
configure_txpower_track(
	struct dm_struct		*dm,
	struct txpwrtrack_cfg	*config
);


void
odm_clear_txpowertracking_state(
	struct dm_struct		*dm
);

void
odm_txpowertracking_callback_thermal_meter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct		*dm
#else
	void	*adapter
#endif
);

#if (RTL8822C_SUPPORT == 1 || RTL8814B_SUPPORT == 1)
void
odm_txpowertracking_new_callback_thermal_meter(void *dm_void);
#endif

#define ODM_TARGET_CHNL_NUM_2G_5G	59


void
odm_reset_iqk_result(
	struct dm_struct	*dm
);
u8
odm_get_right_chnl_place_for_iqk(
	u8 chnl
);

void odm_iq_calibrate(struct dm_struct	*dm);
void phydm_rf_init(struct dm_struct		*dm);
void phydm_rf_watchdog(struct dm_struct		*dm);

#endif	/*#ifndef __HALPHYRF_H__*/
