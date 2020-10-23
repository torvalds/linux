/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

#ifndef __ODM_PRECOMP_H__
#define __ODM_PRECOMP_H__

#include "phydm_types.h"
#include "halrf/halrf_features.h"

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include "Precomp.h"		/* @We need to include mp_precomp.h due to batch file setting. */
#else
	#define		TEST_FALG___		1
#endif

/* @2 Config Flags and Structs - defined by each ODM type */

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#include "../8192cd_cfg.h"
	#include "../odm_inc.h"

	#include "../8192cd.h"
	#include "../8192cd_util.h"
	#include "../8192cd_hw.h"
	#ifdef _BIG_ENDIAN_
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_BIG
	#else
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_LITTLE
	#endif

	#include "../8192cd_headers.h"
	#include "../8192cd_debug.h"

	#if defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI)
		#define INIT_TIMER_EVENT_ENTRY(_entry, _func, _data) \
		do { \
			_rtw_init_listhead(&(_entry)->list); \
			(_entry)->data = (_data); \
			(_entry)->function = (_func); \
		} while (0)
	#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#ifdef DM_ODM_CE_MAC80211
		#include "../wifi.h"
		#include "rtl_phydm.h"
	#elif defined(DM_ODM_CE_MAC80211_V2)
		#include "../main.h"
		#include "../hw.h"
		#include "../fw.h"
	#endif
	#define __PACK
	#define __WLAN_ATTRIB_PACK__
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include "mp_precomp.h"
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_LITTLE
	#define __PACK
	#define __WLAN_ATTRIB_PACK__
#elif (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	#include <drv_types.h>
	#include <wifi.h>
	#define	ODM_ENDIAN_TYPE				ODM_ENDIAN_LITTLE
	#define __PACK
#endif

/* @2 OutSrc Header Files */

#include "phydm.h"
#include "phydm_hwconfig.h"
#include "phydm_phystatus.h"
#include "phydm_debug.h"
#include "phydm_regdefine11ac.h"
#include "phydm_regdefine11n.h"
#include "phydm_interface.h"
#include "phydm_reg.h"
#include "halrf/halrf_debug.h"

#ifndef RTL8188E_SUPPORT
	#define	RTL8188E_SUPPORT	0
#endif
#ifndef RTL8812A_SUPPORT
	#define	RTL8812A_SUPPORT	0
#endif
#ifndef RTL8821A_SUPPORT
	#define	RTL8821A_SUPPORT	0
#endif
#ifndef RTL8192E_SUPPORT
	#define	RTL8192E_SUPPORT	0
#endif
#ifndef RTL8723B_SUPPORT
	#define	RTL8723B_SUPPORT	0
#endif
#ifndef RTL8814A_SUPPORT
	#define	RTL8814A_SUPPORT	0
#endif
#ifndef RTL8881A_SUPPORT
	#define	RTL8881A_SUPPORT	0
#endif
#ifndef RTL8822B_SUPPORT
	#define	RTL8822B_SUPPORT	0
#endif
#ifndef RTL8703B_SUPPORT
	#define	RTL8703B_SUPPORT	0
#endif
#ifndef RTL8195A_SUPPORT
	#define	RTL8195A_SUPPORT	0
#endif
#ifndef RTL8188F_SUPPORT
	#define	RTL8188F_SUPPORT	0
#endif
#ifndef RTL8723D_SUPPORT
	#define	RTL8723D_SUPPORT	0
#endif
#ifndef RTL8197F_SUPPORT
	#define	RTL8197F_SUPPORT	0
#endif
#ifndef RTL8821C_SUPPORT
	#define	RTL8821C_SUPPORT	0
#endif
#ifndef RTL8814B_SUPPORT
	#define	RTL8814B_SUPPORT	0
#endif
#ifndef RTL8198F_SUPPORT
	#define	RTL8198F_SUPPORT	0
#endif
#ifndef RTL8710B_SUPPORT
	#define	RTL8710B_SUPPORT	0
#endif
#ifndef RTL8192F_SUPPORT
	#define	RTL8192F_SUPPORT	0
#endif
#ifndef RTL8822C_SUPPORT
	#define	RTL8822C_SUPPORT	0
#endif
#ifndef RTL8195B_SUPPORT
	#define	RTL8195B_SUPPORT	0
#endif
#ifndef RTL8812F_SUPPORT
	#define	RTL8812F_SUPPORT	0
#endif
#ifndef RTL8197G_SUPPORT
	#define	RTL8197G_SUPPORT	0
#endif
#ifndef RTL8721D_SUPPORT
	#define	RTL8721D_SUPPORT	0
#endif
#ifndef RTL8710C_SUPPORT
	#define	RTL8710C_SUPPORT	0
#endif
#ifndef RTL8723F_SUPPORT
	#define	RTL8723F_SUPPORT	0
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && \
	(!defined(DM_ODM_CE_MAC80211) && !defined(DM_ODM_CE_MAC80211_V2))

void phy_set_tx_power_limit(
	struct dm_struct *dm,
	u8 *regulation,
	u8 *band,
	u8 *bandwidth,
	u8 *rate_section,
	u8 *rf_path,
	u8 *channel,
	u8 *power_limit);

void phy_set_tx_power_limit_ex(struct dm_struct *dm, u8 regulation, u8 band,
			       u8 bandwidth, u8 rate_section, u8 rf_path,
			       u8 channel, s8 power_limit);

enum hal_status
rtw_phydm_fw_iqk(
	struct dm_struct *dm,
	u8 clear,
	u8 segment);

enum hal_status
rtw_phydm_fw_dpk(
	struct dm_struct *dm);

enum hal_status
rtw_phydm_cfg_phy_para(
	struct dm_struct *dm,
	enum phydm_halmac_param config_type,
	u32 offset,
	u32 data,
	u32 mask,
	enum rf_path e_rf_path,
	u32 delay_time);

#endif

#if RTL8188E_SUPPORT == 1
	#define RTL8188E_T_SUPPORT 1
	#ifdef CONFIG_SFW_SUPPORTED
		#define RTL8188E_S_SUPPORT 1
	#else
		#define RTL8188E_S_SUPPORT 0
	#endif

	#include "rtl8188e/hal8188erateadaptive.h" /* @for  RA,Power training */
	#include "rtl8188e/halhwimg8188e_mac.h"
	#include "rtl8188e/halhwimg8188e_rf.h"
	#include "rtl8188e/halhwimg8188e_bb.h"
	#include "rtl8188e/phydm_regconfig8188e.h"
	#include "rtl8188e/phydm_rtl8188e.h"
	#include "rtl8188e/hal8188ereg.h"
	#include "rtl8188e/version_rtl8188e.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8188e_hal.h"
		#include "halrf/rtl8188e/halrf_8188e_ce.h"
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8188e/halrf_8188e_win.h"
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "halrf/rtl8188e/halrf_8188e_ap.h"
	#endif
#endif /* @88E END */

#if (RTL8192E_SUPPORT == 1)

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8192e/halrf_8192e_win.h" /*@FOR_8192E_IQK*/
	#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "halrf/rtl8192e/halrf_8192e_ap.h" /*@FOR_8192E_IQK*/
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "halrf/rtl8192e/halrf_8192e_ce.h" /*@FOR_8192E_IQK*/
	#endif

	#include "rtl8192e/phydm_rtl8192e.h" /* @FOR_8192E_IQK */
	#include "rtl8192e/version_rtl8192e.h"
	#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
		#include "rtl8192e/halhwimg8192e_bb.h"
		#include "rtl8192e/halhwimg8192e_mac.h"
		#include "rtl8192e/halhwimg8192e_rf.h"
		#include "rtl8192e/phydm_regconfig8192e.h"
		#include "rtl8192e/hal8192ereg.h"
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8192e_hal.h"
	#endif
#endif /* @92E END */

#if (RTL8812A_SUPPORT == 1)

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8812a/halrf_8812a_win.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "halrf/rtl8812a/halrf_8812a_ap.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "halrf/rtl8812a/halrf_8812a_ce.h"
	#endif

	#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
		#include "rtl8812a/halhwimg8812a_bb.h"
		#include "rtl8812a/halhwimg8812a_mac.h"
		#include "rtl8812a/halhwimg8812a_rf.h"
		#include "rtl8812a/phydm_regconfig8812a.h"
	#endif
	#include "rtl8812a/phydm_rtl8812a.h"

	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8812a_hal.h"
	#endif
	#include "rtl8812a/version_rtl8812a.h"

#endif /* @8812 END */

#if (RTL8814A_SUPPORT == 1)

	#include "rtl8814a/halhwimg8814a_mac.h"
	#include "rtl8814a/halhwimg8814a_bb.h"
	#include "rtl8814a/version_rtl8814a.h"
	#include "rtl8814a/phydm_rtl8814a.h"
	#include "halrf/rtl8814a/halhwimg8814a_rf.h"
	#include "halrf/rtl8814a/version_rtl8814a_rf.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8814a/halrf_8814a_win.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "halrf/rtl8814a/halrf_8814a_ce.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "halrf/rtl8814a/halrf_8814a_ap.h"
	#endif
	#include "rtl8814a/phydm_regconfig8814a.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8814a_hal.h"
		#include "halrf/rtl8814a/halrf_iqk_8814a.h"
	#endif
#endif /* @8814 END */

#if (RTL8881A_SUPPORT == 1)/* @FOR_8881_IQK */
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8821a/halrf_iqk_8821a_win.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "halrf/rtl8821a/halrf_iqk_8821a_ce.h"
	#else
		#include "halrf/rtl8821a/halrf_iqk_8821a_ap.h"
	#endif
#endif

#if (RTL8723B_SUPPORT == 1)
	#include "rtl8723b/halhwimg8723b_mac.h"
	#include "rtl8723b/halhwimg8723b_rf.h"
	#include "rtl8723b/halhwimg8723b_bb.h"
	#include "rtl8723b/phydm_regconfig8723b.h"
	#include "rtl8723b/phydm_rtl8723b.h"
	#include "rtl8723b/hal8723breg.h"
	#include "rtl8723b/version_rtl8723b.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8723b/halrf_8723b_win.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "halrf/rtl8723b/halrf_8723b_ce.h"
		#include "rtl8723b/halhwimg8723b_mp.h"
		#include "rtl8723b_hal.h"
	#else
		#include "halrf/rtl8723b/halrf_8723b_ap.h"
	#endif
#endif

#if (RTL8821A_SUPPORT == 1)
	#include "rtl8821a/halhwimg8821a_mac.h"
	#include "rtl8821a/halhwimg8821a_rf.h"
	#include "rtl8821a/halhwimg8821a_bb.h"
	#include "rtl8821a/phydm_regconfig8821a.h"
	#include "rtl8821a/phydm_rtl8821a.h"
	#include "rtl8821a/version_rtl8821a.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		#include "halrf/rtl8821a/halrf_8821a_win.h"
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "halrf/rtl8821a/halrf_8821a_ce.h"
		#include "halrf/rtl8821a/halrf_iqk_8821a_ce.h"/*@for IQK*/
		#include "halrf/rtl8812a/halrf_8812a_ce.h"/*@for IQK,LCK,Power-tracking*/
		#include "rtl8812a_hal.h"
	#else
	#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)
#include "../halmac/halmac_reg2.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#include "../halmac/halmac_reg2.h"
#endif


#if (RTL8822B_SUPPORT == 1)
	#include "rtl8822b/halhwimg8822b_mac.h"
	#include "rtl8822b/halhwimg8822b_bb.h"
	#include "rtl8822b/phydm_regconfig8822b.h"
	#include "halrf/rtl8822b/halrf_8822b.h"
	#include "halrf/rtl8822b/halhwimg8822b_rf.h"
	#include "halrf/rtl8822b/version_rtl8822b_rf.h"
	#include "rtl8822b/phydm_rtl8822b.h"
	#include "rtl8822b/phydm_hal_api8822b.h"
	#include "rtl8822b/version_rtl8822b.h"

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#ifdef DM_ODM_CE_MAC80211
			#include "../halmac/halmac_reg_8822b.h"
		#elif defined(DM_ODM_CE_MAC80211_V2)
			#include "../halmac/halmac_reg_8822b.h"
		#else
			#include <hal_data.h>		/* @struct HAL_DATA_TYPE */
			#include <rtl8822b_hal.h>	/* @RX_SMOOTH_FACTOR, reg definition and etc.*/
		#endif
	#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#endif

#endif

#if (RTL8703B_SUPPORT == 1)
	#include "rtl8703b/phydm_rtl8703b.h"
	#include "rtl8703b/phydm_regconfig8703b.h"
	#include "rtl8703b/halhwimg8703b_mac.h"
	#include "rtl8703b/halhwimg8703b_rf.h"
	#include "rtl8703b/halhwimg8703b_bb.h"
	#include "halrf/rtl8703b/halrf_8703b.h"
	#include "rtl8703b/version_rtl8703b.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8703b_hal.h"
	#endif
#endif

#if (RTL8188F_SUPPORT == 1)
	#include "rtl8188f/halhwimg8188f_mac.h"
	#include "rtl8188f/halhwimg8188f_rf.h"
	#include "rtl8188f/halhwimg8188f_bb.h"
	#include "rtl8188f/hal8188freg.h"
	#include "rtl8188f/phydm_rtl8188f.h"
	#include "rtl8188f/phydm_regconfig8188f.h"
	#include "halrf/rtl8188f/halrf_8188f.h" /*@for IQK,LCK,Power-tracking*/
	#include "rtl8188f/version_rtl8188f.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8188f_hal.h"
	#endif
#endif

#if (RTL8723D_SUPPORT == 1)
	#if (DM_ODM_SUPPORT_TYPE != ODM_AP)

		#include "rtl8723d/halhwimg8723d_bb.h"
		#include "rtl8723d/halhwimg8723d_mac.h"
		#include "rtl8723d/halhwimg8723d_rf.h"
		#include "rtl8723d/phydm_regconfig8723d.h"
		#include "rtl8723d/hal8723dreg.h"
		#include "rtl8723d/phydm_rtl8723d.h"
		#include "halrf/rtl8723d/halrf_8723d.h"
		#include "rtl8723d/version_rtl8723d.h"
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#ifdef DM_ODM_CE_MAC80211
		#else
		#include "rtl8723d_hal.h"
		#endif
	#endif
#endif /* @8723D End */

#if (RTL8710B_SUPPORT == 1)
	#if (DM_ODM_SUPPORT_TYPE != ODM_AP)

		#include "rtl8710b/halhwimg8710b_bb.h"
		#include "rtl8710b/halhwimg8710b_mac.h"
		#include "rtl8710b/phydm_regconfig8710b.h"
		#include "rtl8710b/hal8710breg.h"
		#include "rtl8710b/phydm_rtl8710b.h"
		#include "halrf/rtl8710b/halrf_8710b.h"
		#include "halrf/rtl8710b/halhwimg8710b_rf.h"
		#include "halrf/rtl8710b/version_rtl8710b_rf.h"
		#include "rtl8710b/version_rtl8710b.h"
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8710b_hal.h"
	#endif
#endif /* @8710B End */

#if (RTL8197F_SUPPORT == 1)
	#include "rtl8197f/halhwimg8197f_mac.h"
	#include "rtl8197f/halhwimg8197f_bb.h"
	#include "rtl8197f/phydm_hal_api8197f.h"
	#include "rtl8197f/version_rtl8197f.h"
	#include "rtl8197f/phydm_rtl8197f.h"
	#include "rtl8197f/phydm_regconfig8197f.h"
	#include "halrf/rtl8197f/halrf_8197f.h"
	#include "halrf/rtl8197f/halrf_iqk_8197f.h"
	#include "halrf/rtl8197f/halrf_dpk_8197f.h"
	#include "halrf/rtl8197f/halhwimg8197f_rf.h"
	#include "halrf/rtl8197f/version_rtl8197f_rf.h"
#endif

#if (RTL8821C_SUPPORT == 1)
	#include "rtl8821c/phydm_hal_api8821c.h"
	#include "rtl8821c/halhwimg8821c_mac.h"
	#include "rtl8821c/halhwimg8821c_bb.h"
	#include "rtl8821c/phydm_regconfig8821c.h"
	#include "rtl8821c/phydm_rtl8821c.h"
	#include "halrf/rtl8821c/halrf_8821c.h"
	#include "halrf/rtl8821c/halhwimg8821c_rf.h"
	#include "halrf/rtl8821c/version_rtl8821c_rf.h"
	#include "rtl8821c/version_rtl8821c.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#ifdef DM_ODM_CE_MAC80211
		#include "../halmac/halmac_reg_8821c.h"
		#else
		#include "rtl8821c_hal.h"
		#endif
	#endif
#endif

#if (RTL8192F_SUPPORT == 1)
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8192f_hal.h"/*need to before rf.h*/
	#endif
	#include "rtl8192f/halhwimg8192f_mac.h"
	#include "rtl8192f/halhwimg8192f_bb.h"
	#include "rtl8192f/phydm_hal_api8192f.h"
	#include "rtl8192f/version_rtl8192f.h"
	#include "rtl8192f/phydm_rtl8192f.h"
	#include "rtl8192f/phydm_regconfig8192f.h"
	#include "halrf/rtl8192f/halrf_8192f.h"
	#include "halrf/rtl8192f/halhwimg8192f_rf.h"
	#include "halrf/rtl8192f/version_rtl8192f_rf.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "halrf/rtl8192f/halrf_dpk_8192f.h"
	#endif
#endif

#if (RTL8721D_SUPPORT == 1)
	#include "halrf/rtl8721d/halrf_btiqk_8721d.h"
	#include "halrf/rtl8721d/halrf_rfk_init_8721d.h"
	#include "halrf/rtl8721d/halrf_dpk_8721d.h"
	#include "halrf/rtl8721d/halrf_8721d.h"
	#include "halrf/rtl8721d/halhwimg8721d_rf.h"
	#include "halrf/rtl8721d/version_rtl8721d_rf.h"
	#include "rtl8721d/phydm_hal_api8721d.h"
	#include "rtl8721d/phydm_regconfig8721d.h"
	#include "rtl8721d/halhwimg8721d_mac.h"
	#include "rtl8721d/halhwimg8721d_bb.h"
	#include "rtl8721d/version_rtl8721d.h"
	#include "rtl8721d/phydm_rtl8721d.h"
	#include "rtl8721d/hal8721dreg.h"
	#include <hal_data.h>
	#if 0
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8721d_hal.h"
	#endif
	#endif
#endif

#if (RTL8710C_SUPPORT == 1)
	#include "halrf/rtl8710c/halrf_8710c.h"
	#include "halrf/rtl8710c/halhwimg8710c_rf.h"
	//#include "halrf/rtl8710c/version_rtl8710c_rf.h"
	#include "rtl8710c/phydm_hal_api8710c.h"
	#include "rtl8710c/phydm_regconfig8710c.h"
	#include "rtl8710c/halhwimg8710c_mac.h"
	#include "rtl8710c/halhwimg8710c_bb.h"
	#include "rtl8710c/version_rtl8710c.h"
	#include "rtl8710c/phydm_rtl8710c.h"
	//#include "rtl8710c/hal87100creg.h"
	#include <hal_data.h> /*@HAL_DATA_TYPE*/
	#if 0
	#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		#include "halrf/rtl8710c/halrf_dpk_8710c.h"
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include "rtl8710c_hal.h"
	#endif
	#endif
#endif

#if (RTL8195B_SUPPORT == 1)
	#include "halrf/rtl8195b/halrf_8195b.h"
	#include "halrf/rtl8195b/halhwimg8195b_rf.h"
	#include "halrf/rtl8195b/version_rtl8195b_rf.h"
	#include "rtl8195b/phydm_hal_api8195b.h"
	#include "rtl8195b/phydm_regconfig8195b.h"
	#include "rtl8195b/halhwimg8195b_mac.h"
	#include "rtl8195b/halhwimg8195b_bb.h"
	#include "rtl8195b/version_rtl8195b.h"
	#include <hal_data.h> /*@HAL_DATA_TYPE*/
#endif

#if (RTL8198F_SUPPORT == 1)
	#include "rtl8198f/phydm_regconfig8198f.h"
	#include "rtl8198f/phydm_hal_api8198f.h"
	#include "rtl8198f/halhwimg8198f_mac.h"
	#include "rtl8198f/halhwimg8198f_bb.h"
	#include "rtl8198f/version_rtl8198f.h"
	#include "halrf/rtl8198f/halrf_8198f.h"
	#include "halrf/rtl8198f/halrf_iqk_8198f.h"
	#include "halrf/rtl8198f/halhwimg8198f_rf.h"
	#include "halrf/rtl8198f/version_rtl8198f_rf.h"
#endif

#if (RTL8822C_SUPPORT)
	#include "rtl8822c/halhwimg8822c_bb.h"
	#include "rtl8822c/phydm_regconfig8822c.h"
	#include "rtl8822c/phydm_hal_api8822c.h"
	#include "rtl8822c/version_rtl8822c.h"
	#include "rtl8822c/phydm_rtl8822c.h"
	#include "halrf/rtl8822c/halrf_8822c.h"
	#include "halrf/rtl8822c/halhwimg8822c_rf.h"
	#include "halrf/rtl8822c/version_rtl8822c_rf.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	/* @struct HAL_DATA_TYPE */
	#include <hal_data.h>
	/* @RX_SMOOTH_FACTOR, reg definition and etc.*/
	#include <rtl8822c_hal.h>
	#endif
#endif
#if (RTL8814B_SUPPORT == 1)
	#include "rtl8814b/halhwimg8814b_bb.h"
	#include "rtl8814b/phydm_regconfig8814b.h"
	#include "halrf/rtl8814b/halrf_8814b.h"
	#include "halrf/rtl8814b/halhwimg8814b_rf.h"
	#include "halrf/rtl8814b/version_rtl8814b_rf.h"
	#include "rtl8814b/phydm_hal_api8814b.h"
	#include "rtl8814b/version_rtl8814b.h"
	#include "rtl8814b/phydm_extraagc8814b.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		#include <hal_data.h>		/* @struct HAL_DATA_TYPE */
		#include <rtl8814b_hal.h>	/* @RX_SMOOTH_FACTOR, reg definition and etc.*/
	#endif
#endif
#if (RTL8812F_SUPPORT)
	#include "rtl8812f/halhwimg8812f_bb.h"
	#include "rtl8812f/phydm_regconfig8812f.h"
	#include "halrf/rtl8812f/halrf_8812f.h"
	#include "halrf/rtl8812f/halhwimg8812f_rf.h"
	#include "halrf/rtl8812f/version_rtl8812f_rf.h"
	#include "rtl8812f/phydm_hal_api8812f.h"
	#include "rtl8812f/version_rtl8812f.h"
	#include "rtl8812f/phydm_rtl8812f.h"
#endif
#if (RTL8197G_SUPPORT)
	#include "rtl8197g/halhwimg8197g_bb.h"
	#include "rtl8197g/halhwimg8197g_mac.h"
	#include "rtl8197g/phydm_regconfig8197g.h"
	#include "halrf/rtl8197g/halrf_8197g.h"
	#include "halrf/rtl8197g/halhwimg8197g_rf.h"
	#include "halrf/rtl8197g/version_rtl8197g_rf.h"
	#include "rtl8197g/phydm_hal_api8197g.h"
	#include "rtl8197g/version_rtl8197g.h"
	#include "rtl8197g/phydm_rtl8197g.h"
#endif
#if (RTL8723F_SUPPORT)
	#include "rtl8723f/halhwimg8723f_bb.h"
	#include "rtl8723f/halhwimg8723f_mac.h"
	#include "rtl8723f/phydm_regconfig8723f.h"
	#include "halrf/rtl8723f/halrf_8723f.h"
	#include "halrf/rtl8723f/halhwimg8723f_rf.h"
	#include "halrf/rtl8723f/version_rtl8723f_rf.h"
	#include "halrf/rtl8723f/halrf_iqk_8723f.h"
	#include "halrf/rtl8723f/halrf_dpk_8723f.h"
	#include "halrf/rtl8723f/halrf_tssi_8723f.h"
	#include "halrf/rtl8723f/halrf_rfk_init_8723f.h"
	#include "rtl8723f/phydm_hal_api8723f.h"
	#include "rtl8723f/version_rtl8723f.h"
	#include "rtl8723f/phydm_rtl8723f.h"
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	/* @struct HAL_DATA_TYPE */
	#include <hal_data.h>
	/* @RX_SMOOTH_FACTOR, reg definition and etc.*/
	#include <rtl8723f_hal.h>
	#endif
#endif
#endif /* @__ODM_PRECOMP_H__ */
