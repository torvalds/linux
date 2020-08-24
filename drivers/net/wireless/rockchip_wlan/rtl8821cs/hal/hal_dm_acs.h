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
#ifndef __HAL_DM_ACS_H__
#define __HAL_DM_ACS_H__
#ifdef CONFIG_RTW_ACS
#define RTK_ACS_VERSION	3

#if (RTK_ACS_VERSION == 3)
enum NHM_PID {
	NHM_PID_ACS,
	NHM_PID_IEEE_11K_HIGH,
	NHM_PID_IEEE_11K_LOW,
};

#define init_clm_param(clm, app, lv, time) \
	do {\
		clm.clm_app = app;\
		clm.clm_lv = lv;\
		clm.mntr_time = time;\
	} while (0)

#define init_nhm_param(nhm, txon, cca, cnt_opt, app, lv, time) \
	do {\
		nhm.incld_txon = txon;\
		nhm.incld_cca = cca;\
		nhm.div_opt = cnt_opt;\
		nhm.nhm_app = app;\
		nhm.nhm_lv = lv;\
		nhm.mntr_time = time;\
	} while (0)

	
#define init_acs_clm(clm, time) \
	init_clm_param(clm, CLM_ACS, CLM_LV_2, time)

#define init_acs_nhm(nhm, time) \
	init_nhm_param(nhm, NHM_EXCLUDE_TXON, NHM_EXCLUDE_CCA, NHM_CNT_ALL, NHM_ACS, NHM_LV_2, time)

#define init_11K_high_nhm(nhm, time) \
	init_nhm_param(nhm, NHM_EXCLUDE_TXON, NHM_EXCLUDE_CCA, NHM_CNT_ALL, IEEE_11K_HIGH, NHM_LV_2, time)
	
#define init_11K_low_nhm(nhm, time) \
		init_nhm_param(nhm, NHM_EXCLUDE_TXON, NHM_EXCLUDE_CCA, NHM_CNT_ALL, IEEE_11K_LOW, NHM_LV_2, time)


#endif /*(RTK_ACS_VERSION == 3)*/
void rtw_acs_version_dump(void *sel, _adapter *adapter);
extern void phydm_ccx_monitor_trigger(void *p_dm_void, u16 monitor_time);
extern void phydm_ccx_monitor_result(void *p_dm_void);

#define GET_ACS_STATE(padapter)					(ATOMIC_READ(&GET_HAL_DATA(padapter)->acs.state))
#define SET_ACS_STATE(padapter, set_state)			(ATOMIC_SET(&GET_HAL_DATA(padapter)->acs.state, set_state))
#define IS_ACS_ENABLE(padapter)					((GET_ACS_STATE(padapter) == ACS_ENABLE) ? _TRUE : _FALSE)

enum ACS_STATE {
	ACS_DISABLE,
	ACS_ENABLE,
};

#define ACS_BW_20M	BIT(0)
#define ACS_BW_40M	BIT(1)
#define ACS_BW_80M	BIT(2)
#define ACS_BW_160M	BIT(3)

struct auto_chan_sel {
	ATOMIC_T state;
	u8 trigger_ch;
	bool triggered;
	u8 clm_ratio[MAX_CHANNEL_NUM];
	u8 nhm_ratio[MAX_CHANNEL_NUM];
	#if (RTK_ACS_VERSION == 3)
	u8 nhm[MAX_CHANNEL_NUM][NHM_RPT_NUM];
	#endif
	u8 bss_nums[MAX_CHANNEL_NUM];
	u8 interference_time[MAX_CHANNEL_NUM];
	u8 cur_ch_clm_ratio;
	u8 cur_ch_nhm_ratio;
	u8 best_chan_5g;
	u8 best_chan_24g;

	#if (RTK_ACS_VERSION == 3)
	u8 trig_rst;
	struct env_trig_rpt	trig_rpt;
	#endif

	#ifdef CONFIG_RTW_ACS_DBG
	RT_SCAN_TYPE scan_type;
	u16 scan_time;
	u8 igi;
	u8 bw;
	#endif
};

#define rtw_acs_get_best_chan_24g(adapter)		(GET_HAL_DATA(adapter)->acs.best_chan_24g)
#define rtw_acs_get_best_chan_5g(adapter)		(GET_HAL_DATA(adapter)->acs.best_chan_5g)

#ifdef CONFIG_RTW_ACS_DBG
#define rtw_is_acs_passiv_scan(adapter)	(((GET_HAL_DATA(adapter)->acs.scan_type) == SCAN_PASSIVE) ? _TRUE : _FALSE)

#define rtw_acs_get_adv_st(adapter)	(GET_HAL_DATA(adapter)->acs.scan_time)
#define rtw_is_acs_st_valid(adapter)	((GET_HAL_DATA(adapter)->acs.scan_time) ? _TRUE : _FALSE)

#define rtw_acs_get_adv_igi(adapter)	(GET_HAL_DATA(adapter)->acs.igi)
u8 rtw_is_acs_igi_valid(_adapter *adapter);

#define rtw_acs_get_adv_bw(adapter)	(GET_HAL_DATA(adapter)->acs.bw)

void rtw_acs_adv_setting(_adapter *adapter, RT_SCAN_TYPE scan_type, u16 scan_time, u8 igi, u8 bw);
void rtw_acs_adv_reset(_adapter *adapter);
#endif

u8 rtw_acs_get_clm_ratio_by_ch_num(_adapter *adapter, u8 chan);
u8 rtw_acs_get_clm_ratio_by_ch_idx(_adapter *adapter, u8 ch_idx);
u8 rtw_acs_get_nhm_ratio_by_ch_num(_adapter *adapter, u8 chan);
u8 rtw_acs_get_num_ratio_by_ch_idx(_adapter *adapter, u8 ch_idx);

void rtw_acs_reset(_adapter *adapter);
void rtw_acs_trigger(_adapter *adapter, u16 scan_time_ms, u8 scan_chan, enum NHM_PID pid);
void rtw_acs_get_rst(_adapter *adapter);
void rtw_acs_select_best_chan(_adapter *adapter);
void rtw_acs_info_dump(void *sel, _adapter *adapter);
void rtw_acs_update_current_info(_adapter *adapter);
void rtw_acs_chan_info_dump(void *sel, _adapter *adapter);
void rtw_acs_current_info_dump(void *sel, _adapter *adapter);

void rtw_acs_start(_adapter *adapter);
void rtw_acs_stop(_adapter *adapter);

#endif /*CONFIG_RTW_ACS*/

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
#define RTK_NOISE_MONITOR_VERSION	3
#define GET_NM_STATE(padapter)					(ATOMIC_READ(&GET_HAL_DATA(padapter)->nm.state))
#define SET_NM_STATE(padapter, set_state)			(ATOMIC_SET(&GET_HAL_DATA(padapter)->nm.state, set_state))
#define IS_NM_ENABLE(padapter)					((GET_NM_STATE(padapter) == NM_ENABLE) ? _TRUE : _FALSE)

enum NM_STATE {
	NM_DISABLE,
	NM_ENABLE,
};

struct noise_monitor {
	ATOMIC_T state;
	s16 noise[MAX_CHANNEL_NUM];
	u8 bss_nums[MAX_CHANNEL_NUM];
};
void rtw_nm_enable(_adapter *adapter);
void rtw_nm_disable(_adapter *adapter);
void rtw_noise_measure(_adapter *adapter, u8 chan, u8 is_pause_dig, u8 igi_value, u32 max_time);
s16 rtw_noise_query_by_chan_num(_adapter *adapter, u8 chan);
s16 rtw_noise_query_by_chan_idx(_adapter *adapter, u8 ch_idx);
s16 rtw_noise_measure_curchan(_adapter *padapter);
void rtw_noise_info_dump(void *sel, _adapter *adapter);
#endif
#endif /* __HAL_DM_ACS_H__ */
