/******************************************************************************
 *
 * Copyright(c) 2014 - 2017 Realtek Corporation.
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
#include <drv_types.h>
#include <hal_data.h>


#if defined(CONFIG_RTW_ACS) || defined(CONFIG_BACKGROUND_NOISE_MONITOR)
static void _rtw_bss_nums_count(_adapter *adapter, u8 *pbss_nums)
{
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	_queue *queue = &(pmlmepriv->scanned_queue);
	struct wlan_network *pnetwork = NULL;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	_list	*plist, *phead;
	_irqL irqL;
	int chan_idx = -1;

	if (pbss_nums == NULL) {
		RTW_ERR("%s pbss_nums is null pointer\n", __func__);
		return;
	}
	_rtw_memset(pbss_nums, 0, MAX_CHANNEL_NUM);

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	phead = get_list_head(queue);
	plist = get_next(phead);
	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		if (!pnetwork)
			break;
		chan_idx = rtw_chset_search_ch(adapter_to_chset(adapter), pnetwork->network.Configuration.DSConfig);
		if ((chan_idx == -1) || (chan_idx >= MAX_CHANNEL_NUM)) {
			RTW_ERR("%s can't get chan_idx(CH:%d)\n",
				__func__, pnetwork->network.Configuration.DSConfig);
			chan_idx = 0;
		}
		/*if (pnetwork->network.Reserved[0] != BSS_TYPE_PROB_REQ)*/

		pbss_nums[chan_idx]++;

		plist = get_next(plist);
	}
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
}

u8 rtw_get_ch_num_by_idx(_adapter *adapter, u8 idx)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *pch_set = rfctl->channel_set;
	u8 max_chan_nums = rfctl->max_chan_nums;

	if (idx >= max_chan_nums)
		return 0;
	return pch_set[idx].ChannelNum;
}
#endif /*defined(CONFIG_RTW_ACS) || defined(CONFIG_BACKGROUND_NOISE_MONITOR)*/


#ifdef CONFIG_RTW_ACS
void rtw_acs_version_dump(void *sel, _adapter *adapter)
{
	_RTW_PRINT_SEL(sel, "RTK_ACS VER_%d\n", RTK_ACS_VERSION);
}
u8 rtw_phydm_clm_ratio(_adapter *adapter)
{
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);

	return phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CLM_RATIO);
}
u8 rtw_phydm_nhm_ratio(_adapter *adapter)
{
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);

	return phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_NHM_RATIO);
}
void rtw_acs_reset(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct auto_chan_sel *pacs = &hal_data->acs;

	_rtw_memset(pacs, 0, sizeof(struct auto_chan_sel));
}

void rtw_acs_trigger(_adapter *adapter, u16 scan_time_ms, u8 scan_chan)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);
	u16 sample_times = 0;

	hal_data->acs.trigger_ch = scan_chan;
	/*scan_time - ms ,1ms can sample 250 times*/
	sample_times = scan_time_ms * 250;
	phydm_ccx_monitor_trigger(phydm, sample_times);

	#ifdef CONFIG_RTW_ACS_DBG
	RTW_INFO("[ACS] Trigger CH:%d, Times:%d\n", hal_data->acs.trigger_ch, sample_times);
	#endif
}
void rtw_acs_get_rst(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);
	int chan_idx = -1;
	u8 cur_chan = hal_data->acs.trigger_ch;

	if (cur_chan == 0)
		return;

	chan_idx = rtw_chset_search_ch(adapter_to_chset(adapter), cur_chan);
	if ((chan_idx == -1) || (chan_idx >= MAX_CHANNEL_NUM)) {
		RTW_ERR("[ACS] %s can't get chan_idx(CH:%d)\n", __func__, cur_chan);
		return;
	}

	phydm_ccx_monitor_result(phydm);

	hal_data->acs.clm_ratio[chan_idx] = rtw_phydm_clm_ratio(adapter);
	hal_data->acs.nhm_ratio[chan_idx] = rtw_phydm_nhm_ratio(adapter);

	#ifdef CONFIG_RTW_ACS_DBG
	RTW_INFO("[ACS] Result CH:%d, CLM:%d NHM:%d\n",
		cur_chan, hal_data->acs.clm_ratio[chan_idx], hal_data->acs.nhm_ratio[chan_idx]);
	#endif
}

void _rtw_phydm_acs_select_best_chan(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	u8 ch_idx;
	u8 ch_idx_24g = 0xFF, ch_idx_5g = 0xFF;
	u8 min_itf_24g = 0xFF,  min_itf_5g = 0xFF;
	u8 *pbss_nums = hal_data->acs.bss_nums;
	u8 *pclm_ratio = hal_data->acs.clm_ratio;
	u8 *pnhm_ratio = hal_data->acs.nhm_ratio;
	u8 *pinterference_time = hal_data->acs.interference_time;
	u8 max_chan_nums = rfctl->max_chan_nums;

	for (ch_idx = 0; ch_idx < max_chan_nums; ch_idx++) {
		if (pbss_nums[ch_idx])
			pinterference_time[ch_idx] = (pclm_ratio[ch_idx] / 2) + pnhm_ratio[ch_idx];
		else
			pinterference_time[ch_idx] = pclm_ratio[ch_idx] + pnhm_ratio[ch_idx];

		if (rtw_get_ch_num_by_idx(adapter, ch_idx) < 14) {
			if (pinterference_time[ch_idx] < min_itf_24g) {
				min_itf_24g = pinterference_time[ch_idx];
				ch_idx_24g = ch_idx;
			}
		} else {
			if (pinterference_time[ch_idx] < min_itf_5g) {
				min_itf_5g = pinterference_time[ch_idx];
				ch_idx_5g = ch_idx;
			}
		}
	}
	if (ch_idx_24g != 0xFF)
		hal_data->acs.best_chan_24g = rtw_get_ch_num_by_idx(adapter, ch_idx_24g);

	if (ch_idx_5g != 0xFF)
		hal_data->acs.best_chan_5g = rtw_get_ch_num_by_idx(adapter, ch_idx_5g);

	hal_data->acs.trigger_ch = 0;
}

void rtw_acs_info_dump(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	u8 max_chan_nums = rfctl->max_chan_nums;
	u8 ch_idx, ch_num;

	_RTW_PRINT_SEL(sel, "========== ACS (VER-%d) ==========\n", RTK_ACS_VERSION);
	_RTW_PRINT_SEL(sel, "Best 24G Channel:%d\n", hal_data->acs.best_chan_24g);
	_RTW_PRINT_SEL(sel, "Best 5G Channel:%d\n\n", hal_data->acs.best_chan_5g);

	#ifdef CONFIG_RTW_ACS_DBG
	_RTW_PRINT_SEL(sel, "BW  20MHz\n");
	_RTW_PRINT_SEL(sel, "%5s  %3s  %3s  %3s(%%)  %3s(%%)  %3s\n",
						"Index", "CH", "BSS", "CLM", "NHM", "ITF");

	for (ch_idx = 0; ch_idx < max_chan_nums; ch_idx++) {
		ch_num = rtw_get_ch_num_by_idx(adapter, ch_idx);
		_RTW_PRINT_SEL(sel, "%5d  %3d  %3d  %6d  %6d  %3d\n",
						ch_idx, ch_num, hal_data->acs.bss_nums[ch_idx],
						hal_data->acs.clm_ratio[ch_idx],
						hal_data->acs.nhm_ratio[ch_idx],
						hal_data->acs.interference_time[ch_idx]);
	}
	#endif
}
void rtw_acs_select_best_chan(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	_rtw_bss_nums_count(adapter, hal_data->acs.bss_nums);
	_rtw_phydm_acs_select_best_chan(adapter);
	rtw_acs_info_dump(RTW_DBGDUMP, adapter);
}

void rtw_acs_start(_adapter *adapter)
{
	rtw_acs_reset(adapter);
	if (GET_ACS_STATE(adapter) != ACS_ENABLE)
		SET_ACS_STATE(adapter, ACS_ENABLE);
}
void rtw_acs_stop(_adapter *adapter)
{
	SET_ACS_STATE(adapter, ACS_DISABLE);
}


u8 rtw_acs_get_clm_ratio_by_ch_num(_adapter *adapter, u8 chan)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int chan_idx = -1;

	chan_idx = rtw_chset_search_ch(adapter_to_chset(adapter), chan);
	if ((chan_idx == -1) || (chan_idx >= MAX_CHANNEL_NUM)) {
		RTW_ERR("[ACS] Get CLM fail, can't get chan_idx(CH:%d)\n", chan);
		return 0;
	}

	return hal_data->acs.clm_ratio[chan_idx];
}
u8 rtw_acs_get_clm_ratio_by_ch_idx(_adapter *adapter, u8 ch_idx)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (ch_idx >= MAX_CHANNEL_NUM) {
		RTW_ERR("%s [ACS] ch_idx(%d) is invalid\n", __func__, ch_idx);
		return 0;
	}

	return hal_data->acs.clm_ratio[ch_idx];
}
u8 rtw_acs_get_nhm_ratio_by_ch_num(_adapter *adapter, u8 chan)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int chan_idx = -1;

	chan_idx = rtw_chset_search_ch(adapter_to_chset(adapter), chan);
	if ((chan_idx == -1) || (chan_idx >= MAX_CHANNEL_NUM)) {
		RTW_ERR("[ACS] Get NHM fail, can't get chan_idx(CH:%d)\n", chan);
		return 0;
	}

	return hal_data->acs.nhm_ratio[chan_idx];
}
u8 rtw_acs_get_num_ratio_by_ch_idx(_adapter *adapter, u8 ch_idx)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (ch_idx >= MAX_CHANNEL_NUM) {
		RTW_ERR("%s [ACS] ch_idx(%d) is invalid\n", __func__, ch_idx);
		return 0;
	}

	return hal_data->acs.nhm_ratio[ch_idx];
}
void rtw_acs_chan_info_dump(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	u8 max_chan_nums = rfctl->max_chan_nums;
	u8 ch_idx, ch_num;
	u8 utilization;

	_RTW_PRINT_SEL(sel, "BW  20MHz\n");
	_RTW_PRINT_SEL(sel, "%5s  %3s  %7s(%%)  %12s(%%)  %11s(%%)  %9s(%%)  %8s(%%)\n",
						"Index", "CH", "Quality", "Availability", "Utilization",
						"WIFI Util", "Interference Util");

	for (ch_idx = 0; ch_idx < max_chan_nums; ch_idx++) {
		ch_num = rtw_get_ch_num_by_idx(adapter, ch_idx);
		utilization = hal_data->acs.clm_ratio[ch_idx] + hal_data->acs.nhm_ratio[ch_idx];
		_RTW_PRINT_SEL(sel, "%5d  %3d  %7d   %12d   %12d   %12d   %12d\n",
						ch_idx, ch_num,
						(100-hal_data->acs.interference_time[ch_idx]),
						(100-utilization),
						utilization,
						hal_data->acs.clm_ratio[ch_idx],
						hal_data->acs.nhm_ratio[ch_idx]);
	}
}
void rtw_acs_current_info_dump(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 ch, cen_ch, bw, offset;

	_RTW_PRINT_SEL(sel, "========== ACS (VER-%d) ==========\n", RTK_ACS_VERSION);

	ch = rtw_get_oper_ch(adapter);
	bw = rtw_get_oper_bw(adapter);
	offset = rtw_get_oper_choffset(adapter);

	_RTW_PRINT_SEL(sel, "Current Channel:%d\n", ch);
	if ((bw == CHANNEL_WIDTH_80) ||(bw == CHANNEL_WIDTH_40)) {
		cen_ch = rtw_get_center_ch(ch, bw, offset);
		_RTW_PRINT_SEL(sel, "Center Channel:%d\n", cen_ch);
	}

	_RTW_PRINT_SEL(sel, "Current BW %s\n", ch_width_str(bw));
	if (0)
		_RTW_PRINT_SEL(sel, "Current IGI 0x%02x\n", rtw_phydm_get_cur_igi(adapter));
	_RTW_PRINT_SEL(sel, "CLM:%d, NHM:%d\n\n",
		hal_data->acs.cur_ch_clm_ratio, hal_data->acs.cur_ch_nhm_ratio);
}

void rtw_acs_update_current_info(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	hal_data->acs.cur_ch_clm_ratio = rtw_phydm_clm_ratio(adapter);
	hal_data->acs.cur_ch_nhm_ratio = rtw_phydm_nhm_ratio(adapter);

	#ifdef CONFIG_RTW_ACS_DBG
	rtw_acs_current_info_dump(RTW_DBGDUMP, adapter);
	#endif
}
#endif /*CONFIG_RTW_ACS*/

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
void rtw_noise_monitor_version_dump(void *sel, _adapter *adapter)
{
	_RTW_PRINT_SEL(sel, "RTK_NOISE_MONITOR VER_%d\n", RTK_NOISE_MONITOR_VERSION);
}
void rtw_nm_enable(_adapter *adapter)
{
	SET_NM_STATE(adapter, NM_ENABLE);
}
void rtw_nm_disable(_adapter *adapter)
{
	SET_NM_STATE(adapter, NM_DISABLE);
}
void rtw_noise_info_dump(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	u8 max_chan_nums = rfctl->max_chan_nums;
	u8 ch_idx, ch_num;

	_RTW_PRINT_SEL(sel, "========== NM (VER-%d) ==========\n", RTK_NOISE_MONITOR_VERSION);

	_RTW_PRINT_SEL(sel, "%5s  %3s  %3s  %10s", "Index", "CH", "BSS", "Noise(dBm)\n");

	_rtw_bss_nums_count(adapter, hal_data->nm.bss_nums);

	for (ch_idx = 0; ch_idx < max_chan_nums; ch_idx++) {
		ch_num = rtw_get_ch_num_by_idx(adapter, ch_idx);
		_RTW_PRINT_SEL(sel, "%5d  %3d  %3d  %10d\n",
						ch_idx, ch_num, hal_data->nm.bss_nums[ch_idx],
						hal_data->nm.noise[ch_idx]);
	}
}

void rtw_noise_measure(_adapter *adapter, u8 chan, u8 is_pause_dig, u8 igi_value, u32 max_time)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT *phydm = &hal_data->odmpriv;
	int chan_idx = -1;
	s16 noise = 0;

	#ifdef DBG_NOISE_MONITOR
	RTW_INFO("[NM] chan(%d)-PauseDIG:%s,  IGIValue:0x%02x, max_time:%d (ms)\n",
		chan, (is_pause_dig) ? "Y" : "N", igi_value, max_time);
	#endif

	chan_idx = rtw_chset_search_ch(adapter_to_chset(adapter), chan);
	if ((chan_idx == -1) || (chan_idx >= MAX_CHANNEL_NUM)) {
		RTW_ERR("[NM] Get noise fail, can't get chan_idx(CH:%d)\n", chan);
		return;
	}
	noise = odm_inband_noise_monitor(phydm, is_pause_dig, igi_value, max_time); /*dBm*/

	hal_data->nm.noise[chan_idx] = noise;

	#ifdef DBG_NOISE_MONITOR
	RTW_INFO("[NM] %s chan_%d, noise = %d (dBm)\n", __func__, chan, hal_data->nm.noise[chan_idx]);

	RTW_INFO("[NM] noise_a = %d, noise_b = %d  noise_all:%d\n",
			 phydm->noise_level.noise[RF_PATH_A],
			 phydm->noise_level.noise[RF_PATH_B],
			 phydm->noise_level.noise_all);
	#endif /*DBG_NOISE_MONITOR*/
}

s16 rtw_noise_query_by_chan_num(_adapter *adapter, u8 chan)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	s16 noise = 0;
	int chan_idx = -1;

	chan_idx = rtw_chset_search_ch(adapter_to_chset(adapter), chan);
	if ((chan_idx == -1) || (chan_idx >= MAX_CHANNEL_NUM)) {
		RTW_ERR("[NM] Get noise fail, can't get chan_idx(CH:%d)\n", chan);
		return noise;
	}
	noise = hal_data->nm.noise[chan_idx];

	#ifdef DBG_NOISE_MONITOR
	RTW_INFO("[NM] %s chan_%d, noise = %d (dBm)\n", __func__, chan, noise);
	#endif/*DBG_NOISE_MONITOR*/
	return noise;
}
s16 rtw_noise_query_by_chan_idx(_adapter *adapter, u8 ch_idx)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	s16 noise = 0;

	if (ch_idx >= MAX_CHANNEL_NUM) {
		RTW_ERR("[NM] %s ch_idx(%d) is invalid\n", __func__, ch_idx);
		return noise;
	}
	noise = hal_data->nm.noise[ch_idx];

	#ifdef DBG_NOISE_MONITOR
	RTW_INFO("[NM] %s ch_idx %d, noise = %d (dBm)\n", __func__, ch_idx, noise);
	#endif/*DBG_NOISE_MONITOR*/
	return noise;
}

s16 rtw_noise_measure_curchan(_adapter *padapter)
{
	s16 noise = 0;
	u8 igi_value = 0x1E;
	u32 max_time = 100;/* ms */
	u8 is_pause_dig = _TRUE;
	u8 cur_chan = rtw_get_oper_ch(padapter);

	if (rtw_linked_check(padapter) == _FALSE)
		return noise;

	rtw_ps_deny(padapter, PS_DENY_IOCTL);
	LeaveAllPowerSaveModeDirect(padapter);
	rtw_noise_measure(padapter, cur_chan, is_pause_dig, igi_value, max_time);
	noise = rtw_noise_query_by_chan_num(padapter, cur_chan);
	rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);

	return noise;
}
#endif /*CONFIG_BACKGROUND_NOISE_MONITOR*/

