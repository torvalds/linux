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
#ifndef __HAL_DM_H__
#define __HAL_DM_H__

#define adapter_to_phydm(adapter) (&(GET_HAL_DATA(adapter)->odmpriv))
#define dvobj_to_phydm(dvobj) adapter_to_phydm(dvobj_get_primary_adapter(dvobj))
#ifdef CONFIG_TDMADIG
void rtw_phydm_tdmadig(_adapter *adapter, u8 state);
#endif
void rtw_phydm_priv_init(_adapter *adapter);
void Init_ODM_ComInfo(_adapter *adapter);
void rtw_phydm_init(_adapter *adapter);

void rtw_hal_turbo_edca(_adapter *adapter);
u8 rtw_phydm_is_iqk_in_progress(_adapter *adapter);

void GetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	void						*pValue1,
	void						*pValue2);
void SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	void						*pValue1,
	BOOLEAN					bSet);

void rtw_phydm_ra_registed(_adapter *adapter, struct sta_info *psta);

#ifdef CONFIG_DYNAMIC_SOML
void rtw_dyn_soml_byte_update(_adapter *adapter, u8 data_rate, u32 size);
void rtw_dyn_soml_para_set(_adapter *adapter, u8 train_num, u8 intvl,
			u8 period, u8 delay);
void rtw_dyn_soml_config(_adapter *adapter);
#endif
void rtw_phydm_set_rrsr(_adapter *adapter, u32 rrsr_value, bool write_rrsr);
void rtw_phydm_dyn_rrsr_en(_adapter *adapter, bool en_rrsr);
void rtw_phydm_watchdog(_adapter *adapter, bool in_lps);

void rtw_hal_update_iqk_fw_offload_cap(_adapter *adapter);
void dump_sta_info(void *sel, struct sta_info *psta);
void dump_sta_traffic(void *sel, _adapter *adapter, struct sta_info *psta);

void rtw_hal_phydm_cal_trigger(_adapter *adapter);
#ifdef CONFIG_DBG_RF_CAL
void rtw_hal_iqk_test(_adapter *adapter, bool recovery, bool clear, bool segment);
void rtw_hal_lck_test(_adapter *adapter);
#endif

s8 rtw_dm_get_min_rssi(_adapter *adapter);
s8 rtw_phydm_get_min_rssi(_adapter *adapter);
u8 rtw_phydm_get_cur_igi(_adapter *adapter);
bool rtw_phydm_get_edcca_flag(_adapter *adapter);


#ifdef CONFIG_LPS_LCLK_WD_TIMER
extern void phydm_rssi_monitor_check(void *p_dm_void);

void rtw_phydm_wd_lps_lclk_hdl(_adapter *adapter);
void rtw_phydm_watchdog_in_lps_lclk(_adapter *adapter);
#endif
#ifdef CONFIG_TDMADIG
enum rtw_tdmadig_state{
	TDMADIG_INIT,
	TDMADIG_NON_INIT,
};
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
#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8814B_SUPPORT == 1) || (RTL8822C_SUPPORT == 1) \
	|| (RTL8723F_SUPPORT == 1))
void rtw_phydm_iqk_trigger(_adapter *adapter);
#endif
void rtw_phydm_read_efuse(_adapter *adapter);
bool rtw_phydm_set_crystal_cap(_adapter *adapter, u8 crystal_cap);

#ifdef CONFIG_SUPPORT_DYNAMIC_TXPWR
void rtw_phydm_set_dyntxpwr(_adapter *adapter, u8 *desc, u8 mac_id);
#endif

#ifdef CONFIG_LPS_PG
void rtw_phydm_lps_pg_hdl(_adapter *adapter, struct sta_info *sta, bool in_lpspg);
#endif
#ifdef CONFIG_LPS_PWR_TRACKING
void rtw_phydm_pwr_tracking_directly(_adapter *adapter);
#endif

#ifdef CONFIG_CTRL_TXSS_BY_TP
void rtw_phydm_trx_cfg(_adapter *adapter, bool tx_1ss);
#endif
u8 rtw_hal_runtime_trx_path_decision(_adapter *adapter);
bool rtw_phydm_rfe_ctrl_gpio(_adapter *adapter, u8 gpio_num);
#endif /* __HAL_DM_H__ */
