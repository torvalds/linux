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
#ifndef __HAL_DM_H__
#define __HAL_DM_H__

#define adapter_to_phydm(adapter) (&(GET_HAL_DATA(adapter)->odmpriv))

void Init_ODM_ComInfo(_adapter *adapter);
void rtw_hal_turbo_edca(_adapter *adapter);
u8 rtw_phydm_is_iqk_in_progress(_adapter *adapter);

void GetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2);
void SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet);

void rtw_phydm_ra_registed(_adapter *adapter, struct sta_info *psta);

void rtw_phydm_watchdog(_adapter *adapter);

void rtw_hal_update_iqk_fw_offload_cap(_adapter *adapter);
void dump_sta_info(void *sel, struct sta_info *psta);
void dump_sta_traffic(void *sel, _adapter *adapter, struct sta_info *psta);

#ifdef CONFIG_DBG_RF_CAL
void rtw_hal_iqk_test(_adapter *adapter, bool recovery, bool clear, bool segment);
void rtw_hal_lck_test(_adapter *adapter);
#endif

s8 rtw_phydm_get_min_rssi(_adapter *adapter);
u8 rtw_phydm_get_cur_igi(_adapter *adapter);


#ifdef CONFIG_LPS_LCLK_WD_TIMER
extern void phydm_rssi_monitor_check(void *p_dm_void);

void rtw_phydm_wd_lps_lclk_hdl(_adapter *adapter);
void rtw_phydm_watchdog_in_lps_lclk(_adapter *adapter);
#endif

enum phy_cnt {
	FA_OFDM,
	FA_CCK,
	FA_TOTAL,
	CCA_OFDM,
	CCA_CCK,
	CCA_ALL,
	CRC32_OK_VHT,
	CRC32_OK_HT,
	CRC32_OK_LEGACY,
	CRC32_OK_CCK,
	CRC32_ERROR_VHT,
	CRC32_ERROR_HT,
	CRC32_ERROR_LEGACY,
	CRC32_ERROR_CCK,
};
u32 rtw_phydm_get_phy_cnt(_adapter *adapter, enum phy_cnt cnt);
#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8814B_SUPPORT == 1))
void rtw_phydm_iqk_trigger(_adapter *adapter);
#endif

#endif /* __HAL_DM_H__ */
