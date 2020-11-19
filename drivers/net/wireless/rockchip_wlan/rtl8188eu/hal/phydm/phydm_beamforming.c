/* SPDX-License-Identifier: GPL-2.0 */
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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#if WPP_SOFTWARE_TRACE
		#include "phydm_beamforming.tmh"
	#endif
#endif

#ifdef PHYDM_BEAMFORMING_SUPPORT

void phydm_get_txbf_device_num(
	void *dm_void,
	u8 macid)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY)) /*@For BDC*/
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = dm->phydm_sta_info[macid];
	struct bf_cmn_info *bf = NULL;
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;
	u8 act_as_bfer = 0;
	u8 act_as_bfee = 0;

	if (is_sta_active(sta)) {
		bf = &(sta->bf_info);
	} else {
		PHYDM_DBG(dm, DBG_TXBF, "[Warning] %s invalid sta_info\n",
			  __func__);
		return;
	}

	if (sta->support_wireless_set & WIRELESS_VHT) {
		if (bf->vht_beamform_cap & BEAMFORMING_VHT_BEAMFORMEE_ENABLE)
			act_as_bfer = 1;

		if (bf->vht_beamform_cap & BEAMFORMING_VHT_BEAMFORMER_ENABLE)
			act_as_bfee = 1;

	} else if (sta->support_wireless_set & WIRELESS_HT) {
		if (bf->ht_beamform_cap & BEAMFORMING_HT_BEAMFORMEE_ENABLE)
			act_as_bfer = 1;

		if (bf->ht_beamform_cap & BEAMFORMING_HT_BEAMFORMER_ENABLE)
			act_as_bfee = 1;
	}

	if (act_as_bfer))
		{ /* Our Device act as BFer */
			dm_bdc_table->w_bfee_client[macid] = true;
			dm_bdc_table->num_txbfee_client++;
		}
	else
		dm_bdc_table->w_bfee_client[macid] = false;

	if (act_as_bfee))
		{ /* Our Device act as BFee */
			dm_bdc_table->w_bfer_client[macid] = true;
			dm_bdc_table->num_txbfer_client++;
		}
	else
		dm_bdc_table->w_bfer_client[macid] = false;

#endif
#endif
}

struct _RT_BEAMFORM_STAINFO *
phydm_sta_info_init(struct dm_struct *dm, u16 sta_idx, u8 *my_mac_addr)
{
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_BEAMFORM_STAINFO *entry = &beam_info->beamform_sta_info;
	struct cmn_sta_info *cmn_sta = dm->phydm_sta_info[sta_idx];
	//void					*adapter = dm->adapter;
	ADAPTER * adapter = dm->adapter;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO p_MgntInfo = &((adapter)->MgntInfo);
	PRT_HIGH_THROUGHPUT p_ht_info = GET_HT_INFO(p_MgntInfo);
	PRT_VERY_HIGH_THROUGHPUT p_vht_info = GET_VHT_INFO(p_MgntInfo);
#endif

	if (!is_sta_active(cmn_sta)) {
		PHYDM_DBG(dm, DBG_TXBF, "%s => sta_info(mac_id:%d) failed\n",
			  __func__, sta_idx);
		#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		rtw_warn_on(1);
		#endif

		return entry;
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/*odm_move_memory(dm, (PVOID)(entry->my_mac_addr),*/
	/*(PVOID)(adapter->CurrentAddress), 6);*/
	odm_move_memory(dm, entry->my_mac_addr, my_mac_addr, 6);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	/*odm_move_memory(dm, entry->my_mac_addr,*/
	/*adapter_mac_addr(sta->padapter), 6);*/
	odm_move_memory(dm, entry->my_mac_addr, my_mac_addr, 6);
#endif

	entry->aid = cmn_sta->aid;
	entry->ra = cmn_sta->mac_addr;
	entry->mac_id = cmn_sta->mac_id;
	entry->bw = cmn_sta->bw_mode;
	entry->cur_beamform = cmn_sta->bf_info.ht_beamform_cap;
	entry->ht_beamform_cap = cmn_sta->bf_info.ht_beamform_cap;

#if ODM_IC_11AC_SERIES_SUPPORT
	if (cmn_sta->support_wireless_set & WIRELESS_VHT) {
		entry->cur_beamform_vht = cmn_sta->bf_info.vht_beamform_cap;
		entry->vht_beamform_cap = cmn_sta->bf_info.vht_beamform_cap;
	}
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN) /*To Be Removed */
	entry->ht_beamform_cap = p_ht_info->HtBeamformCap; /*To Be Removed*/
	entry->vht_beamform_cap = p_vht_info->VhtBeamformCap; /*To Be Removed*/

	if (sta_idx == 0) { /*@client mode*/
		#if ODM_IC_11AC_SERIES_SUPPORT
		if (cmn_sta->support_wireless_set & WIRELESS_VHT)
			entry->cur_beamform_vht = p_vht_info->VhtCurBeamform;
		#endif
	}
#endif

	PHYDM_DBG(dm, DBG_TXBF, "wireless_set = 0x%x, staidx = %d\n",
		  cmn_sta->support_wireless_set, sta_idx);
	PHYDM_DBG(dm, DBG_TXBF,
		  "entry->cur_beamform = 0x%x, entry->cur_beamform_vht = 0x%x\n",
		  entry->cur_beamform, entry->cur_beamform_vht);
	return entry;
}
void phydm_sta_info_update(
	struct dm_struct *dm,
	u16 sta_idx,
	struct _RT_BEAMFORMEE_ENTRY *beamform_entry)
{
	struct cmn_sta_info *sta = dm->phydm_sta_info[sta_idx];

	if (!is_sta_active(sta))
		return;

	sta->bf_info.p_aid = beamform_entry->p_aid;
	sta->bf_info.g_id = beamform_entry->g_id;
}

struct _RT_BEAMFORMEE_ENTRY *
phydm_beamforming_get_bfee_entry_by_addr(
	void *dm_void,
	u8 *RA,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (beam_info->beamformee_entry[i].is_used && (eq_mac_addr(RA, beam_info->beamformee_entry[i].mac_addr))) {
			*idx = i;
			return &beam_info->beamformee_entry[i];
		}
	}

	return NULL;
}

struct _RT_BEAMFORMER_ENTRY *
phydm_beamforming_get_bfer_entry_by_addr(
	void *dm_void,
	u8 *TA,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	for (i = 0; i < BEAMFORMER_ENTRY_NUM; i++) {
		if (beam_info->beamformer_entry[i].is_used && (eq_mac_addr(TA, beam_info->beamformer_entry[i].mac_addr))) {
			*idx = i;
			return &beam_info->beamformer_entry[i];
		}
	}

	return NULL;
}

struct _RT_BEAMFORMEE_ENTRY *
phydm_beamforming_get_entry_by_mac_id(
	void *dm_void,
	u8 mac_id,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (beam_info->beamformee_entry[i].is_used && mac_id == beam_info->beamformee_entry[i].mac_id) {
			*idx = i;
			return &beam_info->beamformee_entry[i];
		}
	}

	return NULL;
}

enum beamforming_cap
phydm_beamforming_get_entry_beam_cap_by_mac_id(
	void *dm_void,
	u8 mac_id)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	enum beamforming_cap beamform_entry_cap = BEAMFORMING_CAP_NONE;

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (beam_info->beamformee_entry[i].is_used && mac_id == beam_info->beamformee_entry[i].mac_id) {
			beamform_entry_cap = beam_info->beamformee_entry[i].beamform_entry_cap;
			i = BEAMFORMEE_ENTRY_NUM;
		}
	}

	return beamform_entry_cap;
}

struct _RT_BEAMFORMEE_ENTRY *
phydm_beamforming_get_free_bfee_entry(
	void *dm_void,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (beam_info->beamformee_entry[i].is_used == false) {
			*idx = i;
			return &beam_info->beamformee_entry[i];
		}
	}
	return NULL;
}

struct _RT_BEAMFORMER_ENTRY *
phydm_beamforming_get_free_bfer_entry(
	void *dm_void,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	PHYDM_DBG(dm, DBG_TXBF, "%s ===>\n", __func__);

	for (i = 0; i < BEAMFORMER_ENTRY_NUM; i++) {
		if (beam_info->beamformer_entry[i].is_used == false) {
			*idx = i;
			return &beam_info->beamformer_entry[i];
		}
	}
	return NULL;
}

/*@
 * Description: Get the first entry index of MU Beamformee.
 *
 * Return value: index of the first MU sta.
 *
 * 2015.05.25. Created by tynli.
 *
 */
u8 phydm_beamforming_get_first_mu_bfee_entry_idx(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 idx = 0xFF;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	boolean is_found = false;

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		if (beam_info->beamformee_entry[idx].is_used && beam_info->beamformee_entry[idx].is_mu_sta) {
			PHYDM_DBG(dm, DBG_TXBF, "[%s] idx=%d!\n", __func__,
				  idx);
			is_found = true;
			break;
		}
	}

	if (!is_found)
		idx = 0xFF;

	return idx;
}

/*@Add SU BFee and MU BFee*/
struct _RT_BEAMFORMEE_ENTRY *
beamforming_add_bfee_entry(
	void *dm_void,
	struct _RT_BEAMFORM_STAINFO *sta,
	enum beamforming_cap beamform_cap,
	u8 num_of_sounding_dim,
	u8 comp_steering_num_of_bfer,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMEE_ENTRY *entry = phydm_beamforming_get_free_bfee_entry(dm, idx);

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (entry != NULL) {
		entry->is_used = true;
		entry->aid = sta->aid;
		entry->mac_id = sta->mac_id;
		entry->sound_bw = sta->bw;
		odm_move_memory(dm, entry->my_mac_addr, sta->my_mac_addr, 6);

		if (phydm_acting_determine(dm, phydm_acting_as_ap)) {
			/*@BSSID[44:47] xor BSSID[40:43]*/
			u16 bssid = ((sta->my_mac_addr[5] & 0xf0) >> 4) ^ (sta->my_mac_addr[5] & 0xf);
			/*@(dec(A) + dec(B)*32) mod 512*/
			entry->p_aid = (sta->aid + bssid * 32) & 0x1ff;
			entry->g_id = 63;
			PHYDM_DBG(dm, DBG_TXBF,
				  "%s: BFee P_AID addressed to STA=%d\n",
				  __func__, entry->p_aid);
		} else if (phydm_acting_determine(dm, phydm_acting_as_ibss)) {
			/*@ad hoc mode*/
			entry->p_aid = 0;
			entry->g_id = 63;
			PHYDM_DBG(dm, DBG_TXBF, "%s: BFee P_AID as IBSS=%d\n",
				  __func__, entry->p_aid);
		} else {
			/*@client mode*/
			entry->p_aid = sta->ra[5];
			/*@BSSID[39:47]*/
			entry->p_aid = (entry->p_aid << 1) | (sta->ra[4] >> 7);
			entry->g_id = 0;
			PHYDM_DBG(dm, DBG_TXBF,
				  "%s: BFee P_AID addressed to AP=0x%X\n",
				  __func__, entry->p_aid);
		}
		cp_mac_addr(entry->mac_addr, sta->ra);
		entry->is_txbf = false;
		entry->is_sound = false;
		entry->sound_period = 400;
		entry->beamform_entry_cap = beamform_cap;
		entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;

		/*		@entry->log_seq = 0xff;				Move to beamforming_add_bfer_entry*/
		/*		@entry->log_retry_cnt = 0;			Move to beamforming_add_bfer_entry*/
		/*		@entry->LogSuccessCnt = 0;		Move to beamforming_add_bfer_entry*/

		entry->log_status_fail_cnt = 0;

		entry->num_of_sounding_dim = num_of_sounding_dim;
		entry->comp_steering_num_of_bfer = comp_steering_num_of_bfer;

		if (beamform_cap & BEAMFORMER_CAP_VHT_MU) {
			dm->beamforming_info.beamformee_mu_cnt += 1;
			entry->is_mu_sta = true;
			dm->beamforming_info.first_mu_bfee_index = phydm_beamforming_get_first_mu_bfee_entry_idx(dm);
		} else if (beamform_cap & (BEAMFORMER_CAP_VHT_SU | BEAMFORMER_CAP_HT_EXPLICIT)) {
			dm->beamforming_info.beamformee_su_cnt += 1;
			entry->is_mu_sta = false;
		}

		return entry;
	} else
		return NULL;
}

/*@Add SU BFee and MU BFer*/
struct _RT_BEAMFORMER_ENTRY *
beamforming_add_bfer_entry(
	void *dm_void,
	struct _RT_BEAMFORM_STAINFO *sta,
	enum beamforming_cap beamform_cap,
	u8 num_of_sounding_dim,
	u8 *idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMER_ENTRY *entry = phydm_beamforming_get_free_bfer_entry(dm, idx);

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (entry != NULL) {
		entry->is_used = true;
		odm_move_memory(dm, entry->my_mac_addr, sta->my_mac_addr, 6);
		if (phydm_acting_determine(dm, phydm_acting_as_ap)) {
			/*@BSSID[44:47] xor BSSID[40:43]*/
			u16 bssid = ((sta->my_mac_addr[5] & 0xf0) >> 4) ^ (sta->my_mac_addr[5] & 0xf);

			entry->p_aid = (sta->aid + bssid * 32) & 0x1ff;
			entry->g_id = 63;
			/*@(dec(A) + dec(B)*32) mod 512*/
		} else if (phydm_acting_determine(dm, phydm_acting_as_ibss)) {
			entry->p_aid = 0;
			entry->g_id = 63;
		} else {
			entry->p_aid = sta->ra[5];
			/*@BSSID[39:47]*/
			entry->p_aid = (entry->p_aid << 1) | (sta->ra[4] >> 7);
			entry->g_id = 0;
			PHYDM_DBG(dm, DBG_TXBF,
				  "%s: P_AID addressed to AP=0x%X\n", __func__,
				  entry->p_aid);
		}

		cp_mac_addr(entry->mac_addr, sta->ra);
		entry->beamform_entry_cap = beamform_cap;

		entry->pre_log_seq = 0; /*@Modified by Jeffery @2015-04-13*/
		entry->log_seq = 0; /*@Modified by Jeffery @2014-10-29*/
		entry->log_retry_cnt = 0; /*@Modified by Jeffery @2014-10-29*/
		entry->log_success = 0; /*@log_success is NOT needed to be accumulated, so  LogSuccessCnt->log_success, 2015-04-13, Jeffery*/
		entry->clock_reset_times = 0; /*@Modified by Jeffery @2015-04-13*/

		entry->num_of_sounding_dim = num_of_sounding_dim;

		if (beamform_cap & BEAMFORMEE_CAP_VHT_MU) {
			dm->beamforming_info.beamformer_mu_cnt += 1;
			entry->is_mu_ap = true;
			entry->aid = sta->aid;
		} else if (beamform_cap & (BEAMFORMEE_CAP_VHT_SU | BEAMFORMEE_CAP_HT_EXPLICIT)) {
			dm->beamforming_info.beamformer_su_cnt += 1;
			entry->is_mu_ap = false;
		}

		return entry;
	} else
		return NULL;
}

#if 0
boolean
beamforming_remove_entry(
	void			*adapter,
	u8		*RA,
	u8		*idx
)
{
	HAL_DATA_TYPE			*hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct				*dm = &hal_data->DM_OutSrc;

	struct _RT_BEAMFORMER_ENTRY	*bfer_entry = phydm_beamforming_get_bfer_entry_by_addr(dm, RA, idx);
	struct _RT_BEAMFORMEE_ENTRY	*entry = phydm_beamforming_get_bfee_entry_by_addr(dm, RA, idx);
	boolean ret = false;

	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s Start!\n", __func__));
	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s, bfer_entry=0x%x\n", __func__, bfer_entry));
	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s, entry=0x%x\n", __func__, entry));

	if (entry != NULL) {
		entry->is_used = false;
		entry->beamform_entry_cap = BEAMFORMING_CAP_NONE;
		/*@entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;*/
		entry->is_beamforming_in_progress = false;
		ret = true;
	}
	if (bfer_entry != NULL) {
		bfer_entry->is_used = false;
		bfer_entry->beamform_entry_cap = BEAMFORMING_CAP_NONE;
		ret = true;
	}
	return ret;
}
#endif

/* Used for beamforming_start_v1 */
void phydm_beamforming_ndpa_rate(
	void *dm_void,
	enum channel_width BW,
	u8 rate)
{
	u16 ndpa_rate = rate;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (ndpa_rate == 0) {
		if (dm->rssi_min > 30) /* @link RSSI > 30% */
			ndpa_rate = ODM_RATE24M;
		else
			ndpa_rate = ODM_RATE6M;
	}

	if (ndpa_rate < ODM_RATEMCS0)
		BW = (enum channel_width)CHANNEL_WIDTH_20;

	ndpa_rate = (ndpa_rate << 8) | BW;
	hal_com_txbf_set(dm, TXBF_SET_SOUNDING_RATE, (u8 *)&ndpa_rate);
}

/* Used for beamforming_start_sw and  beamforming_start_fw */
void phydm_beamforming_dym_ndpa_rate(
	void *dm_void)
{
	u16 ndpa_rate = ODM_RATE6M, BW;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	ndpa_rate = ODM_RATE6M;
	BW = CHANNEL_WIDTH_20;

	ndpa_rate = ndpa_rate << 8 | BW;
	hal_com_txbf_set(dm, TXBF_SET_SOUNDING_RATE, (u8 *)&ndpa_rate);
	PHYDM_DBG(dm, DBG_TXBF, "%s End, NDPA rate = 0x%X\n", __func__,
		  ndpa_rate);
}

/*@
*	SW Sounding : SW Timer unit 1ms
*				 HW Timer unit (1/32000) s  32k is clock.
*	FW Sounding : FW Timer unit 10ms
*/
void beamforming_dym_period(
	void *dm_void,
	u8 status)
{
	u8 idx;
	boolean is_change_period = false;
	u16 sound_period_sw, sound_period_fw;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	struct _RT_BEAMFORMEE_ENTRY *beamform_entry;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_SOUNDING_INFO *sound_info = &beam_info->sounding_info;

	struct _RT_BEAMFORMEE_ENTRY *entry = &beam_info->beamformee_entry[beam_info->beamformee_cur_idx];

	PHYDM_DBG(dm, DBG_TXBF, "[%s] Start!\n", __func__);

	/* @3 TODO  per-client throughput caculation. */

	if ((*dm->current_tx_tp + *dm->current_rx_tp > 2) && (entry->log_status_fail_cnt <= 20 || status)) {
		sound_period_sw = 40; /* @40ms */
		sound_period_fw = 40; /* @From  H2C cmd, unit = 10ms */
	} else {
		sound_period_sw = 4000; /* @4s */
		sound_period_fw = 400;
	}
	PHYDM_DBG(dm, DBG_TXBF, "[%s]sound_period_sw=%d, sound_period_fw=%d\n",
		  __func__, sound_period_sw, sound_period_fw);

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		beamform_entry = beam_info->beamformee_entry + idx;

		if (beamform_entry->default_csi_cnt > 20) {
			/*@Modified by David*/
			sound_period_sw = 4000;
			sound_period_fw = 400;
		}

		PHYDM_DBG(dm, DBG_TXBF, "[%s] period = %d\n", __func__,
			  sound_period_sw);
		if ((beamform_entry->beamform_entry_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU)) == 0)
			continue;

		if (sound_info->sound_mode == SOUNDING_FW_VHT_TIMER || sound_info->sound_mode == SOUNDING_FW_HT_TIMER) {
			if (beamform_entry->sound_period != sound_period_fw) {
				beamform_entry->sound_period = sound_period_fw;
				is_change_period = true; /*Only FW sounding need to send H2C packet to change sound period. */
			}
		} else if (beamform_entry->sound_period != sound_period_sw)
			beamform_entry->sound_period = sound_period_sw;
	}

	if (is_change_period)
		hal_com_txbf_set(dm, TXBF_SET_SOUNDING_FW_NDPA, (u8 *)&idx);
}

boolean
beamforming_send_ht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	enum channel_width BW,
	u8 q_idx)
{
	boolean ret = true;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (q_idx == BEACON_QUEUE)
		ret = send_fw_ht_ndpa_packet(dm, RA, BW);
	else
		ret = send_sw_ht_ndpa_packet(dm, RA, BW);

	return ret;
}

boolean
beamforming_send_vht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	u16 AID,
	enum channel_width BW,
	u8 q_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	boolean ret = true;

	hal_com_txbf_set(dm, TXBF_SET_GET_TX_RATE, NULL);

	if (beam_info->tx_bf_data_rate >= ODM_RATEVHTSS3MCS7 && beam_info->tx_bf_data_rate <= ODM_RATEVHTSS3MCS9 && !beam_info->snding3ss)
		PHYDM_DBG(dm, DBG_TXBF, "@%s: 3SS VHT 789 don't sounding\n",
			  __func__);

	else {
		if (q_idx == BEACON_QUEUE) /* Send to reserved page => FW NDPA */
			ret = send_fw_vht_ndpa_packet(dm, RA, AID, BW);
		else {
#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
			beam_info->is_mu_sounding = true;
			ret = send_sw_vht_mu_ndpa_packet(dm, BW);
#else
			beam_info->is_mu_sounding = false;
			ret = send_sw_vht_ndpa_packet(dm, RA, AID, BW);
#endif
#else
			beam_info->is_mu_sounding = false;
			ret = send_sw_vht_ndpa_packet(dm, RA, AID, BW);
#endif
		}
	}
	return ret;
}

enum beamforming_notify_state
phydm_beamfomring_is_sounding(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info,
	u8 *idx)
{
	enum beamforming_notify_state is_sounding = BEAMFORMING_NOTIFY_NONE;
	struct _RT_BEAMFORMING_OID_INFO beam_oid_info = beam_info->beamforming_oid_info;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	/*@if(( Beamforming_GetBeamCap(beam_info) & BEAMFORMER_CAP) == 0)*/
	/*@is_sounding = BEAMFORMING_NOTIFY_RESET;*/
	if (beam_oid_info.sound_oid_mode == sounding_stop_all_timer) {
		is_sounding = BEAMFORMING_NOTIFY_RESET;
		goto out;
	}

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		PHYDM_DBG(dm, DBG_TXBF,
			  "@%s: BFee Entry %d is_used=%d, is_sound=%d\n",
			  __func__, i, beam_info->beamformee_entry[i].is_used,
			  beam_info->beamformee_entry[i].is_sound);
		if (beam_info->beamformee_entry[i].is_used && !beam_info->beamformee_entry[i].is_sound) {
			PHYDM_DBG(dm, DBG_TXBF, "%s: Add BFee entry %d\n",
				  __func__, i);
			*idx = i;
			if (beam_info->beamformee_entry[i].is_mu_sta)
				is_sounding = BEAMFORMEE_NOTIFY_ADD_MU;
			else
				is_sounding = BEAMFORMEE_NOTIFY_ADD_SU;
		}

		if (!beam_info->beamformee_entry[i].is_used && beam_info->beamformee_entry[i].is_sound) {
			PHYDM_DBG(dm, DBG_TXBF, "%s: Delete BFee entry %d\n",
				  __func__, i);
			*idx = i;
			if (beam_info->beamformee_entry[i].is_mu_sta)
				is_sounding = BEAMFORMEE_NOTIFY_DELETE_MU;
			else
				is_sounding = BEAMFORMEE_NOTIFY_DELETE_SU;
		}
	}

out:
	PHYDM_DBG(dm, DBG_TXBF, "%s End, is_sounding = %d\n", __func__,
		  is_sounding);
	return is_sounding;
}

/* This function is unused */
u8 phydm_beamforming_sounding_idx(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info)
{
	u8 idx = 0;
	struct _RT_BEAMFORMING_OID_INFO beam_oid_info = beam_info->beamforming_oid_info;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (beam_oid_info.sound_oid_mode == SOUNDING_SW_HT_TIMER || beam_oid_info.sound_oid_mode == SOUNDING_SW_VHT_TIMER ||
	    beam_oid_info.sound_oid_mode == SOUNDING_HW_HT_TIMER || beam_oid_info.sound_oid_mode == SOUNDING_HW_VHT_TIMER)
		idx = beam_oid_info.sound_oid_idx;
	else {
		u8 i;
		for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
			if (beam_info->beamformee_entry[i].is_used && !beam_info->beamformee_entry[i].is_sound) {
				idx = i;
				break;
			}
		}
	}

	return idx;
}

enum sounding_mode
phydm_beamforming_sounding_mode(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info,
	u8 idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 support_interface = dm->support_interface;

	struct _RT_BEAMFORMEE_ENTRY beam_entry = beam_info->beamformee_entry[idx];
	struct _RT_BEAMFORMING_OID_INFO beam_oid_info = beam_info->beamforming_oid_info;
	enum sounding_mode mode = beam_oid_info.sound_oid_mode;

	if (beam_oid_info.sound_oid_mode == SOUNDING_SW_VHT_TIMER || beam_oid_info.sound_oid_mode == SOUNDING_HW_VHT_TIMER) {
		if (beam_entry.beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)
			mode = beam_oid_info.sound_oid_mode;
		else
			mode = sounding_stop_all_timer;
	} else if (beam_oid_info.sound_oid_mode == SOUNDING_SW_HT_TIMER || beam_oid_info.sound_oid_mode == SOUNDING_HW_HT_TIMER) {
		if (beam_entry.beamform_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)
			mode = beam_oid_info.sound_oid_mode;
		else
			mode = sounding_stop_all_timer;
	} else if (beam_entry.beamform_entry_cap & BEAMFORMER_CAP_VHT_SU) {
		if (support_interface == ODM_ITRF_USB && !(dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B)))
			mode = SOUNDING_FW_VHT_TIMER;
		else
			mode = SOUNDING_SW_VHT_TIMER;
	} else if (beam_entry.beamform_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT) {
		if (support_interface == ODM_ITRF_USB && !(dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B)))
			mode = SOUNDING_FW_HT_TIMER;
		else
			mode = SOUNDING_SW_HT_TIMER;
	} else
		mode = sounding_stop_all_timer;

	PHYDM_DBG(dm, DBG_TXBF, "[%s] support_interface=%d, mode=%d\n",
		  __func__, support_interface, mode);

	return mode;
}

u16 phydm_beamforming_sounding_time(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info,
	enum sounding_mode mode,
	u8 idx)
{
	u16 sounding_time = 0xffff;
	struct _RT_BEAMFORMEE_ENTRY beam_entry = beam_info->beamformee_entry[idx];
	struct _RT_BEAMFORMING_OID_INFO beam_oid_info = beam_info->beamforming_oid_info;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (mode == SOUNDING_HW_HT_TIMER || mode == SOUNDING_HW_VHT_TIMER)
		sounding_time = beam_oid_info.sound_oid_period * 32;
	else if (mode == SOUNDING_SW_HT_TIMER || mode == SOUNDING_SW_VHT_TIMER)
		/*@Modified by David*/
		sounding_time = beam_entry.sound_period; /*@beam_oid_info.sound_oid_period;*/
	else
		sounding_time = beam_entry.sound_period;

	return sounding_time;
}

enum channel_width
phydm_beamforming_sounding_bw(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info,
	enum sounding_mode mode,
	u8 idx)
{
	enum channel_width sounding_bw = CHANNEL_WIDTH_20;
	struct _RT_BEAMFORMEE_ENTRY beam_entry = beam_info->beamformee_entry[idx];
	struct _RT_BEAMFORMING_OID_INFO beam_oid_info = beam_info->beamforming_oid_info;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (mode == SOUNDING_HW_HT_TIMER || mode == SOUNDING_HW_VHT_TIMER)
		sounding_bw = beam_oid_info.sound_oid_bw;
	else if (mode == SOUNDING_SW_HT_TIMER || mode == SOUNDING_SW_VHT_TIMER)
		/*@Modified by David*/
		sounding_bw = beam_entry.sound_bw; /*@beam_oid_info.sound_oid_bw;*/
	else
		sounding_bw = beam_entry.sound_bw;

	PHYDM_DBG(dm, DBG_TXBF, "%s, sounding_bw=0x%X\n", __func__,
		  sounding_bw);

	return sounding_bw;
}

boolean
phydm_beamforming_select_beam_entry(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info)
{
	struct _RT_SOUNDING_INFO *sound_info = &beam_info->sounding_info;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/*@entry.is_sound is different between first and latter NDPA, and should not be used as BFee entry selection*/
	/*@BTW, latter modification should sync to the selection mechanism of AP/ADSL instead of the fixed sound_idx.*/
	sound_info->sound_idx = phydm_beamforming_sounding_idx(dm, beam_info);
	/*sound_info->sound_idx = 0;*/

	if (sound_info->sound_idx < BEAMFORMEE_ENTRY_NUM)
		sound_info->sound_mode = phydm_beamforming_sounding_mode(dm, beam_info, sound_info->sound_idx);
	else
		sound_info->sound_mode = sounding_stop_all_timer;

	if (sounding_stop_all_timer == sound_info->sound_mode) {
		PHYDM_DBG(dm, DBG_TXBF,
			  "[%s] Return because of sounding_stop_all_timer\n",
			  __func__);
		return false;
	} else {
		sound_info->sound_bw = phydm_beamforming_sounding_bw(dm, beam_info, sound_info->sound_mode, sound_info->sound_idx);
		sound_info->sound_period = phydm_beamforming_sounding_time(dm, beam_info, sound_info->sound_mode, sound_info->sound_idx);
		return true;
	}
}

/*SU BFee Entry Only*/
boolean
phydm_beamforming_start_period(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = true;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_SOUNDING_INFO *sound_info = &beam_info->sounding_info;

	phydm_beamforming_dym_ndpa_rate(dm);

	phydm_beamforming_select_beam_entry(dm, beam_info); /* @Modified */

	if (sound_info->sound_mode == SOUNDING_SW_VHT_TIMER || sound_info->sound_mode == SOUNDING_SW_HT_TIMER)
		odm_set_timer(dm, &beam_info->beamforming_timer, sound_info->sound_period);
	else if (sound_info->sound_mode == SOUNDING_HW_VHT_TIMER || sound_info->sound_mode == SOUNDING_HW_HT_TIMER ||
		 sound_info->sound_mode == SOUNDING_AUTO_VHT_TIMER || sound_info->sound_mode == SOUNDING_AUTO_HT_TIMER) {
		HAL_HW_TIMER_TYPE timer_type = HAL_TIMER_TXBF;
		u32 val = (sound_info->sound_period | (timer_type << 16));

		/* @HW timer stop: All IC has the same setting */
		phydm_set_hw_reg_handler_interface(dm, HW_VAR_HW_REG_TIMER_STOP, (u8 *)(&timer_type));
		/* odm_write_1byte(dm, 0x15F, 0); */
		/* @HW timer init: All IC has the same setting, but 92E & 8812A only write 2 bytes */
		phydm_set_hw_reg_handler_interface(dm, HW_VAR_HW_REG_TIMER_INIT, (u8 *)(&val));
		/* odm_write_1byte(dm, 0x164, 1); */
		/* odm_write_4byte(dm, 0x15C, val); */
		/* @HW timer start: All IC has the same setting */
		phydm_set_hw_reg_handler_interface(dm, HW_VAR_HW_REG_TIMER_START, (u8 *)(&timer_type));
		/* odm_write_1byte(dm, 0x15F, 0x5); */
	} else if (sound_info->sound_mode == SOUNDING_FW_VHT_TIMER || sound_info->sound_mode == SOUNDING_FW_HT_TIMER)
		ret = beamforming_start_fw(dm, sound_info->sound_idx);
	else
		ret = false;

	PHYDM_DBG(dm, DBG_TXBF,
		  "[%s] sound_idx=%d, sound_mode=%d, sound_bw=%d, sound_period=%d\n",
		  __func__, sound_info->sound_idx, sound_info->sound_mode,
		  sound_info->sound_bw, sound_info->sound_period);

	return ret;
}

/* Used after beamforming_leave, and will clear the setting of the "already deleted" entry
 *SU BFee Entry Only*/
void phydm_beamforming_end_period_sw(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	/*void					*adapter = dm->adapter;*/
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_SOUNDING_INFO *sound_info = &beam_info->sounding_info;

	HAL_HW_TIMER_TYPE timer_type = HAL_TIMER_TXBF;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (sound_info->sound_mode == SOUNDING_SW_VHT_TIMER || sound_info->sound_mode == SOUNDING_SW_HT_TIMER)
		odm_cancel_timer(dm, &beam_info->beamforming_timer);
	else if (sound_info->sound_mode == SOUNDING_HW_VHT_TIMER || sound_info->sound_mode == SOUNDING_HW_HT_TIMER ||
		 sound_info->sound_mode == SOUNDING_AUTO_VHT_TIMER || sound_info->sound_mode == SOUNDING_AUTO_HT_TIMER)
		/*@HW timer stop: All IC has the same setting*/
		phydm_set_hw_reg_handler_interface(dm, HW_VAR_HW_REG_TIMER_STOP, (u8 *)(&timer_type));
	/*odm_write_1byte(dm, 0x15F, 0);*/
}

void phydm_beamforming_end_period_fw(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 idx = 0;

	hal_com_txbf_set(dm, TXBF_SET_SOUNDING_FW_NDPA, (u8 *)&idx);
	PHYDM_DBG(dm, DBG_TXBF, "[%s]\n", __func__);
}

/*SU BFee Entry Only*/
void phydm_beamforming_clear_entry_sw(
	void *dm_void,
	boolean is_delete,
	u8 delete_idx)
{
	u8 idx = 0;
	struct _RT_BEAMFORMEE_ENTRY *beamform_entry = NULL;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	if (is_delete) {
		if (delete_idx < BEAMFORMEE_ENTRY_NUM) {
			beamform_entry = beam_info->beamformee_entry + delete_idx;
			if (!(!beamform_entry->is_used && beamform_entry->is_sound)) {
				PHYDM_DBG(dm, DBG_TXBF,
					  "[%s] SW delete_idx is wrong!!!!!\n",
					  __func__);
				return;
			}
		}

		PHYDM_DBG(dm, DBG_TXBF, "[%s] SW delete BFee entry %d\n",
			  __func__, delete_idx);
		if (beamform_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING) {
			beamform_entry->is_beamforming_in_progress = false;
			beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		} else if (beamform_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
			hal_com_txbf_set(dm, TXBF_SET_SOUNDING_STATUS, (u8 *)&delete_idx);
		}
		beamform_entry->is_sound = false;
		return;
	}

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		beamform_entry = beam_info->beamformee_entry + idx;

		/*Used after is_sounding=RESET, and will clear the setting of "ever sounded" entry, which is not necessarily be deleted.*/
		/*This function is mainly used in case "beam_oid_info.sound_oid_mode == sounding_stop_all_timer".*/
		/*@However, setting oid doesn't delete entries (is_used is still true), new entries may fail to be added in.*/

		if (!beamform_entry->is_sound)
			continue;

		PHYDM_DBG(dm, DBG_TXBF, "[%s] SW reset BFee entry %d\n",
			  __func__, idx);
		/*@
		*	If End procedure is
		*	1. Between (Send NDPA, C2H packet return), reset state to initialized.
		*	After C2H packet return , status bit will be set to zero.
		*
		*	2. After C2H packet, then reset state to initialized and clear status bit.
		*/

		if (beamform_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING)
			phydm_beamforming_end_sw(dm, 0);
		else if (beamform_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;
			hal_com_txbf_set(dm, TXBF_SET_SOUNDING_STATUS, (u8 *)&idx);
		}

		beamform_entry->is_sound = false;
	}
}

void phydm_beamforming_clear_entry_fw(
	void *dm_void,
	boolean is_delete,
	u8 delete_idx)
{
	u8 idx = 0;
	struct _RT_BEAMFORMEE_ENTRY *beamform_entry = NULL;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	if (is_delete) {
		if (delete_idx < BEAMFORMEE_ENTRY_NUM) {
			beamform_entry = beam_info->beamformee_entry + delete_idx;

			if (!(!beamform_entry->is_used && beamform_entry->is_sound)) {
				PHYDM_DBG(dm, DBG_TXBF,
					  "[%s] FW delete_idx is wrong!!!!!\n",
					  __func__);
				return;
			}
		}
		PHYDM_DBG(dm, DBG_TXBF, "%s: FW delete BFee entry %d\n",
			  __func__, delete_idx);
		beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		beamform_entry->is_sound = false;
	} else {
		for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
			beamform_entry = beam_info->beamformee_entry + idx;

			/*Used after is_sounding=RESET, and will clear the setting of "ever sounded" entry, which is not necessarily be deleted.*/
			/*This function is mainly used in case "beam_oid_info.sound_oid_mode == sounding_stop_all_timer".*/
			/*@However, setting oid doesn't delete entries (is_used is still true), new entries may fail to be added in.*/

			if (beamform_entry->is_sound) {
				PHYDM_DBG(dm, DBG_TXBF,
					  "[%s]FW reset BFee entry %d\n",
					  __func__, idx);
				/*@
				*	If End procedure is
				*	1. Between (Send NDPA, C2H packet return), reset state to initialized.
				*	After C2H packet return , status bit will be set to zero.
				*
				*	2. After C2H packet, then reset state to initialized and clear status bit.
				*/

				beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;
				beamform_entry->is_sound = false;
			}
		}
	}
}

/*@
*	Called :
*	1. Add and delete entry : beamforming_enter/beamforming_leave
*	2. FW trigger :  Beamforming_SetTxBFen
*	3. Set OID_RT_BEAMFORMING_PERIOD : beamforming_control_v2
*/
void phydm_beamforming_notify(
	void *dm_void)
{
	u8 idx = BEAMFORMEE_ENTRY_NUM;
	enum beamforming_notify_state is_sounding = BEAMFORMING_NOTIFY_NONE;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_SOUNDING_INFO *sound_info = &beam_info->sounding_info;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	is_sounding = phydm_beamfomring_is_sounding(dm, beam_info, &idx);

	PHYDM_DBG(dm, DBG_TXBF, "%s, Before notify, is_sounding=%d, idx=%d\n",
		  __func__, is_sounding, idx);
	PHYDM_DBG(dm, DBG_TXBF, "%s: beam_info->beamformee_su_cnt = %d\n",
		  __func__, beam_info->beamformee_su_cnt);

	switch (is_sounding) {
	case BEAMFORMEE_NOTIFY_ADD_SU:
		PHYDM_DBG(dm, DBG_TXBF, "%s: BEAMFORMEE_NOTIFY_ADD_SU\n",
			  __func__);
		phydm_beamforming_start_period(dm);
		break;

	case BEAMFORMEE_NOTIFY_DELETE_SU:
		PHYDM_DBG(dm, DBG_TXBF, "%s: BEAMFORMEE_NOTIFY_DELETE_SU\n",
			  __func__);
		if (sound_info->sound_mode == SOUNDING_FW_HT_TIMER || sound_info->sound_mode == SOUNDING_FW_VHT_TIMER) {
			phydm_beamforming_clear_entry_fw(dm, true, idx);
			if (beam_info->beamformee_su_cnt == 0) { /* @For 2->1 entry, we should not cancel SW timer */
				phydm_beamforming_end_period_fw(dm);
				PHYDM_DBG(dm, DBG_TXBF, "%s: No BFee left\n",
					  __func__);
			}
		} else {
			phydm_beamforming_clear_entry_sw(dm, true, idx);
			if (beam_info->beamformee_su_cnt == 0) { /* @For 2->1 entry, we should not cancel SW timer */
				phydm_beamforming_end_period_sw(dm);
				PHYDM_DBG(dm, DBG_TXBF, "%s: No BFee left\n",
					  __func__);
			}
		}
		break;

	case BEAMFORMEE_NOTIFY_ADD_MU:
		PHYDM_DBG(dm, DBG_TXBF, "%s: BEAMFORMEE_NOTIFY_ADD_MU\n",
			  __func__);
		if (beam_info->beamformee_mu_cnt == 2) {
			/*@if (sound_info->sound_mode == SOUNDING_SW_VHT_TIMER || sound_info->sound_mode == SOUNDING_SW_HT_TIMER)
				odm_set_timer(dm, &beam_info->beamforming_timer, sound_info->sound_period);*/
			odm_set_timer(dm, &beam_info->beamforming_timer, 1000); /*@Do MU sounding every 1sec*/
		} else
			PHYDM_DBG(dm, DBG_TXBF,
				  "%s: Less or larger than 2 MU STAs, not to set timer\n",
				  __func__);
		break;

	case BEAMFORMEE_NOTIFY_DELETE_MU:
		PHYDM_DBG(dm, DBG_TXBF, "%s: BEAMFORMEE_NOTIFY_DELETE_MU\n",
			  __func__);
		if (beam_info->beamformee_mu_cnt == 1) {
			/*@if (sound_info->sound_mode == SOUNDING_SW_VHT_TIMER || sound_info->sound_mode == SOUNDING_SW_HT_TIMER)*/ {
				odm_cancel_timer(dm, &beam_info->beamforming_timer);
				PHYDM_DBG(dm, DBG_TXBF,
					  "%s: Less than 2 MU STAs, stop sounding\n",
					  __func__);
			}
		}
		break;

	case BEAMFORMING_NOTIFY_RESET:
		if (sound_info->sound_mode == SOUNDING_FW_HT_TIMER || sound_info->sound_mode == SOUNDING_FW_VHT_TIMER) {
			phydm_beamforming_clear_entry_fw(dm, false, idx);
			phydm_beamforming_end_period_fw(dm);
		} else {
			phydm_beamforming_clear_entry_sw(dm, false, idx);
			phydm_beamforming_end_period_sw(dm);
		}

		break;

	default:
		break;
	}
}

boolean
beamforming_init_entry(void *dm_void, u16 sta_idx, u8 *bfer_bfee_idx,
		       u8 *my_mac_addr)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *cmn_sta = dm->phydm_sta_info[sta_idx];
	struct _RT_BEAMFORMEE_ENTRY *beamform_entry = NULL;
	struct _RT_BEAMFORMER_ENTRY *beamformer_entry = NULL;
	struct _RT_BEAMFORM_STAINFO *sta = NULL;
	enum beamforming_cap beamform_cap = BEAMFORMING_CAP_NONE;
	u8 bfer_idx = 0xF, bfee_idx = 0xF;
	u8 num_of_sounding_dim = 0, comp_steering_num_of_bfer = 0;

	if (!is_sta_active(cmn_sta)) {
		PHYDM_DBG(dm, DBG_TXBF, "%s => sta_info(mac_id:%d) failed\n",
			  __func__, sta_idx);
		#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		rtw_warn_on(1);
		#endif
		return false;
	}

	sta = phydm_sta_info_init(dm, sta_idx, my_mac_addr);
	/*The current setting does not support Beaforming*/
	if (BEAMFORMING_CAP_NONE == sta->ht_beamform_cap && BEAMFORMING_CAP_NONE == sta->vht_beamform_cap) {
		PHYDM_DBG(dm, DBG_TXBF,
			  "The configuration disabled Beamforming! Skip...\n");
		return false;
	}

	if (!(cmn_sta->support_wireless_set & (WIRELESS_VHT | WIRELESS_HT)))
		return false;
	else {
		if (cmn_sta->support_wireless_set & WIRELESS_HT) { /*@HT*/
			if (TEST_FLAG(sta->cur_beamform, BEAMFORMING_HT_BEAMFORMER_ENABLE)) { /*We are Beamformee because the STA is Beamformer*/
				beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMEE_CAP_HT_EXPLICIT);
				num_of_sounding_dim = (sta->cur_beamform & BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP) >> 6;
			}
			/*We are Beamformer because the STA is Beamformee*/
			if (TEST_FLAG(sta->cur_beamform, BEAMFORMING_HT_BEAMFORMEE_ENABLE) ||
			    TEST_FLAG(sta->ht_beamform_cap, BEAMFORMING_HT_BEAMFORMER_TEST)) {
				beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMER_CAP_HT_EXPLICIT);
				comp_steering_num_of_bfer = (sta->cur_beamform & BEAMFORMING_HT_BEAMFORMER_STEER_NUM) >> 4;
			}
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s] HT cur_beamform=0x%X, beamform_cap=0x%X\n",
				  __func__, sta->cur_beamform, beamform_cap);
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s] HT num_of_sounding_dim=%d, comp_steering_num_of_bfer=%d\n",
				  __func__, num_of_sounding_dim,
				  comp_steering_num_of_bfer);
		}
#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
		if (cmn_sta->support_wireless_set & WIRELESS_VHT) { /*VHT*/

			/* We are Beamformee because the STA is SU Beamformer*/
			if (TEST_FLAG(sta->cur_beamform_vht, BEAMFORMING_VHT_BEAMFORMER_ENABLE)) {
				beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMEE_CAP_VHT_SU);
				num_of_sounding_dim = (sta->cur_beamform_vht & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM) >> 12;
			}
			/* We are Beamformer because the STA is SU Beamformee*/
			if (TEST_FLAG(sta->cur_beamform_vht, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) ||
			    TEST_FLAG(sta->vht_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_TEST)) {
				beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMER_CAP_VHT_SU);
				comp_steering_num_of_bfer = (sta->cur_beamform_vht & BEAMFORMING_VHT_BEAMFORMER_STS_CAP) >> 8;
			}
			/* We are Beamformee because the STA is MU Beamformer*/
			if (TEST_FLAG(sta->cur_beamform_vht, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE)) {
				beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMEE_CAP_VHT_MU);
				num_of_sounding_dim = (sta->cur_beamform_vht & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM) >> 12;
			}
			/* We are Beamformer because the STA is MU Beamformee*/
			if (phydm_acting_determine(dm, phydm_acting_as_ap)) { /* Only AP mode supports to act an MU beamformer */
				if (TEST_FLAG(sta->cur_beamform_vht, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE) ||
				    TEST_FLAG(sta->vht_beamform_cap, BEAMFORMING_VHT_BEAMFORMER_TEST)) {
					beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMER_CAP_VHT_MU);
					comp_steering_num_of_bfer = (sta->cur_beamform_vht & BEAMFORMING_VHT_BEAMFORMER_STS_CAP) >> 8;
				}
			}
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s]VHT cur_beamform_vht=0x%X, beamform_cap=0x%X\n",
				  __func__, sta->cur_beamform_vht,
				  beamform_cap);
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s]VHT num_of_sounding_dim=0x%X, comp_steering_num_of_bfer=0x%X\n",
				  __func__, num_of_sounding_dim,
				  comp_steering_num_of_bfer);
		}
#endif
	}

	if (beamform_cap == BEAMFORMING_CAP_NONE)
		return false;

	PHYDM_DBG(dm, DBG_TXBF, "[%s] Self BF Entry Cap = 0x%02X\n", __func__,
		  beamform_cap);

	/*We are BFee, so the entry is BFer*/
	if (beamform_cap & (BEAMFORMEE_CAP_VHT_MU | BEAMFORMEE_CAP_VHT_SU | BEAMFORMEE_CAP_HT_EXPLICIT)) {
		beamformer_entry = phydm_beamforming_get_bfer_entry_by_addr(dm, sta->ra, &bfer_idx);

		if (beamformer_entry == NULL) {
			beamformer_entry = beamforming_add_bfer_entry(dm, sta, beamform_cap, num_of_sounding_dim, &bfer_idx);
			if (beamformer_entry == NULL)
				PHYDM_DBG(dm, DBG_TXBF,
					  "[%s]Not enough BFer entry!!!!!\n",
					  __func__);
		}
	}

	/*We are BFer, so the entry is BFee*/
	if (beamform_cap & (BEAMFORMER_CAP_VHT_MU | BEAMFORMER_CAP_VHT_SU | BEAMFORMER_CAP_HT_EXPLICIT)) {
		beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(dm, sta->ra, &bfee_idx);

		/*@if BFeeIdx = 0xF, that represent for no matched MACID among all linked entrys */
		PHYDM_DBG(dm, DBG_TXBF, "[%s] Get BFee entry 0x%X by address\n",
			  __func__, bfee_idx);
		if (beamform_entry == NULL) {
			beamform_entry = beamforming_add_bfee_entry(dm, sta, beamform_cap, num_of_sounding_dim, comp_steering_num_of_bfer, &bfee_idx);
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s]: sta->AID=%d, sta->mac_id=%d\n",
				  __func__, sta->aid, sta->mac_id);

			PHYDM_DBG(dm, DBG_TXBF, "[%s]: Add BFee entry %d\n",
				  __func__, bfee_idx);

			if (beamform_entry == NULL)
				return false;
			else
				beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		} else {
			/*@Entry has been created. If entry is initialing or progressing then errors occur.*/
			if (beamform_entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_INITIALIZED &&
			    beamform_entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_PROGRESSED)
				return false;
			else
				beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		}
		beamform_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		phydm_sta_info_update(dm, sta_idx, beamform_entry);
	}

	*bfer_bfee_idx = (bfer_idx << 4) | bfee_idx;
	PHYDM_DBG(dm, DBG_TXBF,
		  "[%s] End: bfer_idx=0x%X, bfee_idx=0x%X, bfer_bfee_idx=0x%X\n",
		  __func__, bfer_idx, bfee_idx, *bfer_bfee_idx);

	return true;
}

void beamforming_deinit_entry(
	void *dm_void,
	u8 *RA)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 idx = 0;

	struct _RT_BEAMFORMER_ENTRY *bfer_entry = phydm_beamforming_get_bfer_entry_by_addr(dm, RA, &idx);
	struct _RT_BEAMFORMEE_ENTRY *bfee_entry = phydm_beamforming_get_bfee_entry_by_addr(dm, RA, &idx);
	boolean ret = false;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (bfee_entry != NULL) {
		PHYDM_DBG(dm, DBG_TXBF, "%s, bfee_entry\n", __func__);
		bfee_entry->is_used = false;
		bfee_entry->beamform_entry_cap = BEAMFORMING_CAP_NONE;
		bfee_entry->is_beamforming_in_progress = false;
		if (bfee_entry->is_mu_sta) {
			dm->beamforming_info.beamformee_mu_cnt -= 1;
			dm->beamforming_info.first_mu_bfee_index = phydm_beamforming_get_first_mu_bfee_entry_idx(dm);
		} else
			dm->beamforming_info.beamformee_su_cnt -= 1;
		ret = true;
	}

	if (bfer_entry != NULL) {
		PHYDM_DBG(dm, DBG_TXBF, "%s, bfer_entry\n", __func__);
		bfer_entry->is_used = false;
		bfer_entry->beamform_entry_cap = BEAMFORMING_CAP_NONE;
		if (bfer_entry->is_mu_ap)
			dm->beamforming_info.beamformer_mu_cnt -= 1;
		else
			dm->beamforming_info.beamformer_su_cnt -= 1;
		ret = true;
	}

	if (ret == true)
		hal_com_txbf_set(dm, TXBF_SET_SOUNDING_LEAVE, (u8 *)&idx);

	PHYDM_DBG(dm, DBG_TXBF, "%s End, idx = 0x%X\n", __func__, idx);
}

boolean
beamforming_start_v1(
	void *dm_void,
	u8 *RA,
	boolean mode,
	enum channel_width BW,
	u8 rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 idx = 0;
	struct _RT_BEAMFORMEE_ENTRY *entry;
	boolean ret = true;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	entry = phydm_beamforming_get_bfee_entry_by_addr(dm, RA, &idx);

	if (entry->is_used == false) {
		entry->is_beamforming_in_progress = false;
		return false;
	} else {
		if (entry->is_beamforming_in_progress)
			return false;

		entry->is_beamforming_in_progress = true;

		if (mode == 1) {
			if (!(entry->beamform_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)) {
				entry->is_beamforming_in_progress = false;
				return false;
			}
		} else if (mode == 0) {
			if (!(entry->beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)) {
				entry->is_beamforming_in_progress = false;
				return false;
			}
		}

		if (entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_INITIALIZED && entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			entry->is_beamforming_in_progress = false;
			return false;
		} else {
			entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSING;
			entry->is_sound = true;
		}
	}

	entry->sound_bw = BW;
	beam_info->beamformee_cur_idx = idx;
	phydm_beamforming_ndpa_rate(dm, BW, rate);
	hal_com_txbf_set(dm, TXBF_SET_SOUNDING_STATUS, (u8 *)&idx);

	if (mode == 1)
		ret = beamforming_send_ht_ndpa_packet(dm, RA, BW, NORMAL_QUEUE);
	else
		ret = beamforming_send_vht_ndpa_packet(dm, RA, entry->aid, BW, NORMAL_QUEUE);

	if (ret == false) {
		beamforming_leave(dm, RA);
		entry->is_beamforming_in_progress = false;
		return false;
	}

	PHYDM_DBG(dm, DBG_TXBF, "%s  idx %d\n", __func__, idx);
	return true;
}

boolean
beamforming_start_sw(
	void *dm_void,
	u8 idx,
	u8 mode,
	enum channel_width BW)
{
	u8 *ra = NULL;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMEE_ENTRY *entry;
	boolean ret = true;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
	u8 i, poll_sta_cnt = 0;
	boolean is_get_first_bfee = false;
#endif
#endif

	if (beam_info->is_mu_sounding) {
		beam_info->is_mu_sounding_in_progress = true;
		entry = &beam_info->beamformee_entry[idx];
		ra = entry->mac_addr;

	} else {
		entry = &beam_info->beamformee_entry[idx];

		if (entry->is_used == false) {
			PHYDM_DBG(dm, DBG_TXBF,
				  "Skip Beamforming, no entry for idx =%d\n",
				  idx);
			entry->is_beamforming_in_progress = false;
			return false;
		}

		if (entry->is_beamforming_in_progress) {
			PHYDM_DBG(dm, DBG_TXBF,
				  "is_beamforming_in_progress, skip...\n");
			return false;
		}

		entry->is_beamforming_in_progress = true;
		ra = entry->mac_addr;

		if (mode == SOUNDING_SW_HT_TIMER || mode == SOUNDING_HW_HT_TIMER || mode == SOUNDING_AUTO_HT_TIMER) {
			if (!(entry->beamform_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)) {
				entry->is_beamforming_in_progress = false;
				PHYDM_DBG(dm, DBG_TXBF,
					  "%s Return by not support BEAMFORMER_CAP_HT_EXPLICIT <==\n",
					  __func__);
				return false;
			}
		} else if (mode == SOUNDING_SW_VHT_TIMER || mode == SOUNDING_HW_VHT_TIMER || mode == SOUNDING_AUTO_VHT_TIMER) {
			if (!(entry->beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)) {
				entry->is_beamforming_in_progress = false;
				PHYDM_DBG(dm, DBG_TXBF,
					  "%s Return by not support BEAMFORMER_CAP_VHT_SU <==\n",
					  __func__);
				return false;
			}
		}
		if (entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_INITIALIZED && entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			entry->is_beamforming_in_progress = false;
			PHYDM_DBG(dm, DBG_TXBF,
				  "%s Return by incorrect beamform_entry_state(%d) <==\n",
				  __func__, entry->beamform_entry_state);
			return false;
		} else {
			entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSING;
			entry->is_sound = true;
		}

		beam_info->beamformee_cur_idx = idx;
	}

	/*@2014.12.22 Luke: Need to be checked*/
	/*@GET_TXBF_INFO(adapter)->fTxbfSet(adapter, TXBF_SET_SOUNDING_STATUS, (u8*)&idx);*/

	if (mode == SOUNDING_SW_HT_TIMER || mode == SOUNDING_HW_HT_TIMER || mode == SOUNDING_AUTO_HT_TIMER)
		ret = beamforming_send_ht_ndpa_packet(dm, ra, BW, NORMAL_QUEUE);
	else
		ret = beamforming_send_vht_ndpa_packet(dm, ra, entry->aid, BW, NORMAL_QUEUE);

	if (ret == false) {
		beamforming_leave(dm, ra);
		entry->is_beamforming_in_progress = false;
		return false;
	}

/*@--------------------------
	 * Send BF Report Poll for MU BF
	--------------------------*/
#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
	if (beam_info->beamformee_mu_cnt <= 1)
		goto out;

	/* @More than 1 MU STA*/
	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		entry = &beam_info->beamformee_entry[i];
		if (!entry->is_mu_sta)
			continue;

		if (!is_get_first_bfee) {
			is_get_first_bfee = true;
			continue;
		}

		poll_sta_cnt++;
		if (poll_sta_cnt == (beam_info->beamformee_mu_cnt - 1)) /* The last STA*/
			send_sw_vht_bf_report_poll(dm, entry->mac_addr, true);
		else
			send_sw_vht_bf_report_poll(dm, entry->mac_addr, false);
	}
out:
#endif
#endif
	return true;
}

boolean
beamforming_start_fw(
	void *dm_void,
	u8 idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMEE_ENTRY *entry;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	entry = &beam_info->beamformee_entry[idx];
	if (entry->is_used == false) {
		PHYDM_DBG(dm, DBG_TXBF,
			  "Skip Beamforming, no entry for idx =%d\n", idx);
		return false;
	}

	entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSING;
	entry->is_sound = true;
	hal_com_txbf_set(dm, TXBF_SET_SOUNDING_FW_NDPA, (u8 *)&idx);

	PHYDM_DBG(dm, DBG_TXBF, "[%s] End, idx=0x%X\n", __func__, idx);
	return true;
}

void beamforming_check_sounding_success(
	void *dm_void,
	boolean status)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY *entry = &beam_info->beamformee_entry[beam_info->beamformee_cur_idx];

	PHYDM_DBG(dm, DBG_TXBF, "[David]@%s Start!\n", __func__);

	if (status == 1) {
		if (entry->log_status_fail_cnt == 21)
			beamforming_dym_period(dm, status);
		entry->log_status_fail_cnt = 0;
	} else if (entry->log_status_fail_cnt <= 20) {
		entry->log_status_fail_cnt++;
		PHYDM_DBG(dm, DBG_TXBF, "%s log_status_fail_cnt %d\n", __func__,
			  entry->log_status_fail_cnt);
	}
	if (entry->log_status_fail_cnt > 20) {
		entry->log_status_fail_cnt = 21;
		PHYDM_DBG(dm, DBG_TXBF,
			  "%s log_status_fail_cnt > 20, Stop SOUNDING\n",
			  __func__);
		beamforming_dym_period(dm, status);
	}
}

void phydm_beamforming_end_sw(
	void *dm_void,
	boolean status)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY *entry = &beam_info->beamformee_entry[beam_info->beamformee_cur_idx];

	if (beam_info->is_mu_sounding) {
		PHYDM_DBG(dm, DBG_TXBF, "%s: MU sounding done\n", __func__);
		beam_info->is_mu_sounding_in_progress = false;
		hal_com_txbf_set(dm, TXBF_SET_SOUNDING_STATUS,
				 (u8 *)&beam_info->beamformee_cur_idx);
	} else {
		if (entry->beamform_entry_state != BEAMFORMING_ENTRY_STATE_PROGRESSING) {
			PHYDM_DBG(dm, DBG_TXBF, "[%s] BeamformStatus %d\n",
				  __func__, entry->beamform_entry_state);
			return;
		}

		if (beam_info->tx_bf_data_rate >= ODM_RATEVHTSS3MCS7 && beam_info->tx_bf_data_rate <= ODM_RATEVHTSS3MCS9 && !beam_info->snding3ss) {
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s] VHT3SS 7,8,9, do not apply V matrix.\n",
				  __func__);
			entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;
			hal_com_txbf_set(dm, TXBF_SET_SOUNDING_STATUS,
					 (u8 *)&beam_info->beamformee_cur_idx);
		} else if (status == 1) {
			entry->log_status_fail_cnt = 0;
			entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSED;
			hal_com_txbf_set(dm, TXBF_SET_SOUNDING_STATUS,
					 (u8 *)&beam_info->beamformee_cur_idx);
		} else {
			entry->log_status_fail_cnt++;
			entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_INITIALIZED;
			hal_com_txbf_set(dm, TXBF_SET_TX_PATH_RESET,
					 (u8 *)&beam_info->beamformee_cur_idx);
			PHYDM_DBG(dm, DBG_TXBF, "[%s] log_status_fail_cnt %d\n",
				  __func__, entry->log_status_fail_cnt);
		}

		if (entry->log_status_fail_cnt > 50) {
			PHYDM_DBG(dm, DBG_TXBF,
				  "%s log_status_fail_cnt > 50, Stop SOUNDING\n",
				  __func__);
			entry->is_sound = false;
			beamforming_deinit_entry(dm, entry->mac_addr);

			/*@Modified by David - Every action of deleting entry should follow by Notify*/
			phydm_beamforming_notify(dm);
		}

		entry->is_beamforming_in_progress = false;
	}
	PHYDM_DBG(dm, DBG_TXBF, "%s: status=%d\n", __func__, status);
}

void beamforming_timer_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *dm_void
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	void *context
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	void *adapter = (void *)context;
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->odmpriv;
#endif
	boolean ret = false;
	struct _RT_BEAMFORMING_INFO *beam_info = &(dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY *entry = &(beam_info->beamformee_entry[beam_info->beamformee_cur_idx]);
	struct _RT_SOUNDING_INFO *sound_info = &(beam_info->sounding_info);
	boolean is_beamforming_in_progress;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (beam_info->is_mu_sounding)
		is_beamforming_in_progress = beam_info->is_mu_sounding_in_progress;
	else
		is_beamforming_in_progress = entry->is_beamforming_in_progress;

	if (is_beamforming_in_progress) {
		PHYDM_DBG(dm, DBG_TXBF,
			  "is_beamforming_in_progress, reset it\n");
		phydm_beamforming_end_sw(dm, 0);
	}

	ret = phydm_beamforming_select_beam_entry(dm, beam_info);
#if (SUPPORT_MU_BF == 1)
	if (ret && beam_info->beamformee_mu_cnt > 1)
		ret = 1;
	else
		ret = 0;
#endif
	if (ret)
		ret = beamforming_start_sw(dm, sound_info->sound_idx, sound_info->sound_mode, sound_info->sound_bw);
	else
		PHYDM_DBG(dm, DBG_TXBF,
			  "%s, Error value return from BeamformingStart_V2\n",
			  __func__);

	if (beam_info->beamformee_su_cnt != 0 || beam_info->beamformee_mu_cnt > 1) {
		if (sound_info->sound_mode == SOUNDING_SW_VHT_TIMER || sound_info->sound_mode == SOUNDING_SW_HT_TIMER)
			odm_set_timer(dm, &beam_info->beamforming_timer, sound_info->sound_period);
		else {
			u32 val = (sound_info->sound_period << 16) | HAL_TIMER_TXBF;
			phydm_set_hw_reg_handler_interface(dm, HW_VAR_HW_REG_TIMER_RESTART, (u8 *)(&val));
		}
	}
}

void beamforming_sw_timer_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct phydm_timer_list *timer
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	void *function_context
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *adapter = (void *)timer->Adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;

	PHYDM_DBG(dm, DBG_TXBF, "[%s] Start!\n", __func__);
	beamforming_timer_callback(dm);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct dm_struct *dm = (struct dm_struct *)function_context;
	void *adapter = dm->adapter;

	if (*dm->is_net_closed == true)
		return;
	phydm_run_in_thread_cmd(dm, beamforming_timer_callback, adapter);
#endif
}

void phydm_beamforming_init(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_BEAMFORMING_OID_INFO *beam_oid_info = &beam_info->beamforming_oid_info;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *adapter = dm->adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));

#ifdef BEAMFORMING_VERSION_1
	if (hal_data->beamforming_version != BEAMFORMING_VERSION_1) {
		return;
	}
#endif
#endif

	beam_oid_info->sound_oid_mode = SOUNDING_STOP_OID_TIMER;
	PHYDM_DBG(dm, DBG_TXBF, "%s mode (%d)\n", __func__,
		  beam_oid_info->sound_oid_mode);

	beam_info->beamformee_su_cnt = 0;
	beam_info->beamformer_su_cnt = 0;
	beam_info->beamformee_mu_cnt = 0;
	beam_info->beamformer_mu_cnt = 0;
	beam_info->beamformee_mu_reg_maping = 0;
	beam_info->mu_ap_index = 0;
	beam_info->is_mu_sounding = false;
	beam_info->first_mu_bfee_index = 0xFF;
	beam_info->apply_v_matrix = true;
	beam_info->snding3ss = false;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	beam_info->source_adapter = dm->adapter;
#endif
	hal_com_txbf_beamform_init(dm);
}

boolean
phydm_acting_determine(
	void *dm_void,
	enum phydm_acting_type type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = false;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *adapter = dm->beamforming_info.source_adapter;
#else
	struct _ADAPTER *adapter = dm->adapter;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (type == phydm_acting_as_ap)
		ret = ACTING_AS_AP(adapter);
	else if (type == phydm_acting_as_ibss)
		ret = ACTING_AS_IBSS(((PADAPTER)(adapter)));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (type == phydm_acting_as_ap)
		ret = check_fwstate(pmlmepriv, WIFI_AP_STATE);
	else if (type == phydm_acting_as_ibss)
		ret = check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
#endif

	return ret;
}

void beamforming_enter(void *dm_void, u16 sta_idx, u8 *my_mac_addr)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 bfer_bfee_idx = 0xff;

	if (beamforming_init_entry(dm, sta_idx, &bfer_bfee_idx, my_mac_addr))
		hal_com_txbf_set(dm, TXBF_SET_SOUNDING_ENTER, (u8 *)&bfer_bfee_idx);

	PHYDM_DBG(dm, DBG_TXBF, "[%s] End!\n", __func__);
}

void beamforming_leave(
	void *dm_void,
	u8 *RA)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (RA != NULL) {
		beamforming_deinit_entry(dm, RA);
		phydm_beamforming_notify(dm);
	}

	PHYDM_DBG(dm, DBG_TXBF, "[%s] End!!\n", __func__);
}

#if 0
/* Nobody calls this function */
void
phydm_beamforming_set_txbf_en(
	void		*dm_void,
	u8			mac_id,
	boolean			is_txbf
)
{
	struct dm_struct				*dm = (struct dm_struct *)dm_void;
	u8					idx = 0;
	struct _RT_BEAMFORMEE_ENTRY	*entry;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	entry = phydm_beamforming_get_entry_by_mac_id(dm, mac_id, &idx);

	if (entry == NULL)
		return;
	else
		entry->is_txbf = is_txbf;

	PHYDM_DBG(dm, DBG_TXBF, "%s mac_id %d TxBF %d\n", __func__,
		  entry->mac_id, entry->is_txbf);

	phydm_beamforming_notify(dm);
}
#endif

enum beamforming_cap
phydm_beamforming_get_beam_cap(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info)
{
	u8 i;
	boolean is_self_beamformer = false;
	boolean is_self_beamformee = false;
	struct _RT_BEAMFORMEE_ENTRY beamformee_entry;
	struct _RT_BEAMFORMER_ENTRY beamformer_entry;
	enum beamforming_cap beamform_cap = BEAMFORMING_CAP_NONE;
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_TXBF, "[%s] Start!\n", __func__);

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		beamformee_entry = beam_info->beamformee_entry[i];

		if (beamformee_entry.is_used) {
			is_self_beamformer = true;
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s] BFee entry %d is_used=true\n", __func__,
				  i);
			break;
		}
	}

	for (i = 0; i < BEAMFORMER_ENTRY_NUM; i++) {
		beamformer_entry = beam_info->beamformer_entry[i];

		if (beamformer_entry.is_used) {
			is_self_beamformee = true;
			PHYDM_DBG(dm, DBG_TXBF,
				  "[%s]: BFer entry %d is_used=true\n",
				  __func__, i);
			break;
		}
	}

	if (is_self_beamformer)
		beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMER_CAP);
	if (is_self_beamformee)
		beamform_cap = (enum beamforming_cap)(beamform_cap | BEAMFORMEE_CAP);

	return beamform_cap;
}

boolean
beamforming_control_v1(
	void *dm_void,
	u8 *RA,
	u8 AID,
	u8 mode,
	enum channel_width BW,
	u8 rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = true;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	PHYDM_DBG(dm, DBG_TXBF, "AID (%d), mode (%d), BW (%d)\n", AID, mode,
		  BW);

	switch (mode) {
	case 0:
		ret = beamforming_start_v1(dm, RA, 0, BW, rate);
		break;
	case 1:
		ret = beamforming_start_v1(dm, RA, 1, BW, rate);
		break;
	case 2:
		phydm_beamforming_ndpa_rate(dm, BW, rate);
		ret = beamforming_send_vht_ndpa_packet(dm, RA, AID, BW, NORMAL_QUEUE);
		break;
	case 3:
		phydm_beamforming_ndpa_rate(dm, BW, rate);
		ret = beamforming_send_ht_ndpa_packet(dm, RA, BW, NORMAL_QUEUE);
		break;
	}
	return ret;
}

/*Only OID uses this function*/
boolean
phydm_beamforming_control_v2(
	void *dm_void,
	u8 idx,
	u8 mode,
	enum channel_width BW,
	u16 period)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	struct _RT_BEAMFORMING_OID_INFO *beam_oid_info = &beam_info->beamforming_oid_info;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);
	PHYDM_DBG(dm, DBG_TXBF, "idx (%d), mode (%d), BW (%d), period (%d)\n",
		  idx, mode, BW, period);

	beam_oid_info->sound_oid_idx = idx;
	beam_oid_info->sound_oid_mode = (enum sounding_mode)mode;
	beam_oid_info->sound_oid_bw = BW;
	beam_oid_info->sound_oid_period = period;

	phydm_beamforming_notify(dm);

	return true;
}

void phydm_beamforming_watchdog(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;

	PHYDM_DBG(dm, DBG_TXBF, "%s Start!\n", __func__);

	if (beam_info->beamformee_su_cnt == 0)
		return;

	beamforming_dym_period(dm, 0);
}
enum beamforming_cap
phydm_get_beamform_cap(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = NULL;
	struct bf_cmn_info *bf_info = NULL;
	struct _RT_BEAMFORMING_INFO *beam_info = &dm->beamforming_info;
	void *adapter = dm->adapter;
	enum beamforming_cap beamform_cap = BEAMFORMING_CAP_NONE;
	u8 macid;
	u8 ht_curbeamformcap = 0;
	u16 vht_curbeamformcap = 0;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO p_MgntInfo = &(((PADAPTER)(adapter))->MgntInfo);
	PRT_VERY_HIGH_THROUGHPUT p_vht_info = GET_VHT_INFO(p_MgntInfo);
	PRT_HIGH_THROUGHPUT p_ht_info = GET_HT_INFO(p_MgntInfo);

	ht_curbeamformcap = p_ht_info->HtCurBeamform;
	vht_curbeamformcap = p_vht_info->VhtCurBeamform;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[%s] WIN ht_curcap = %d ; vht_curcap = %d\n", __func__,
		  ht_curbeamformcap, vht_curbeamformcap);

	if (TEST_FLAG(ht_curbeamformcap, BEAMFORMING_HT_BEAMFORMER_ENABLE)) /*We are Beamformee because the STA is Beamformer*/
		beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMEE_CAP_HT_EXPLICIT | BEAMFORMEE_CAP));

	/*We are Beamformer because the STA is Beamformee*/
	if (TEST_FLAG(ht_curbeamformcap, BEAMFORMING_HT_BEAMFORMEE_ENABLE))
		beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP));

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)

	/* We are Beamformee because the STA is SU Beamformer*/
	if (TEST_FLAG(vht_curbeamformcap, BEAMFORMING_VHT_BEAMFORMER_ENABLE))
		beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMEE_CAP_VHT_SU | BEAMFORMEE_CAP));

	/* We are Beamformer because the STA is SU Beamformee*/
	if (TEST_FLAG(vht_curbeamformcap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE))
		beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMER_CAP_VHT_SU | BEAMFORMER_CAP));

	/* We are Beamformee because the STA is MU Beamformer*/
	if (TEST_FLAG(vht_curbeamformcap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE))
		beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMEE_CAP_VHT_MU | BEAMFORMEE_CAP));
#endif
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
		sta = dm->phydm_sta_info[macid];

		if (!is_sta_active(sta))
			continue;

		bf_info = &sta->bf_info;
		vht_curbeamformcap = bf_info->vht_beamform_cap;
		ht_curbeamformcap = bf_info->ht_beamform_cap;

		if (TEST_FLAG(ht_curbeamformcap, BEAMFORMING_HT_BEAMFORMER_ENABLE)) /*We are Beamformee because the STA is Beamformer*/
			beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMEE_CAP_HT_EXPLICIT | BEAMFORMEE_CAP));

		/*We are Beamformer because the STA is Beamformee*/
		if (TEST_FLAG(ht_curbeamformcap, BEAMFORMING_HT_BEAMFORMEE_ENABLE))
			beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP));

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
		/* We are Beamformee because the STA is SU Beamformer*/
		if (TEST_FLAG(vht_curbeamformcap, BEAMFORMING_VHT_BEAMFORMER_ENABLE))
			beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMEE_CAP_VHT_SU | BEAMFORMEE_CAP));

		/* We are Beamformer because the STA is SU Beamformee*/
		if (TEST_FLAG(vht_curbeamformcap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE))
			beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMER_CAP_VHT_SU | BEAMFORMER_CAP));

		/* We are Beamformee because the STA is MU Beamformer*/
		if (TEST_FLAG(vht_curbeamformcap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE))
			beamform_cap = (enum beamforming_cap)(beamform_cap | (BEAMFORMEE_CAP_VHT_MU | BEAMFORMEE_CAP));
#endif
	}
	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s] CE ht_curcap = %d ; vht_curcap = %d\n",
		  __func__, ht_curbeamformcap, vht_curbeamformcap);

#endif

	return beamform_cap;
}

#endif
