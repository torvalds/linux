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
#define _RTW_BEAMFORMING_C_

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_BEAMFORMING

#ifdef RTW_BEAMFORMING_VERSION_2

struct ndpa_sta_info {
	u16 aid:12;
	u16 feedback_type:1;
	u16 nc_index:3;
};

static void _get_txvector_parameter(PADAPTER adapter, struct sta_info *sta, u8 *g_id, u16 *p_aid)
{
	struct mlme_priv *mlme;
	u16 aid;
	u8 *bssid;
	u16 val16;
	u8 i;


	mlme = &adapter->mlmepriv;

	if (check_fwstate(mlme, WIFI_AP_STATE)) {
		/*
		 * Sent by an AP and addressed to a STA associated with that AP
		 * or sent by a DLS or TDLS STA in a direct path to
		 * a DLS or TDLS peer STA
		 */

		aid = sta->cmn.aid;
		bssid = adapter_mac_addr(adapter);
		RTW_INFO("%s: AID=0x%x BSSID=" MAC_FMT "\n",
			 __FUNCTION__, sta->cmn.aid, MAC_ARG(bssid));

		/* AID[0:8] */
		aid &= 0x1FF;
		/* BSSID[44:47] xor BSSID[40:43] */
		val16 = ((bssid[5] & 0xF0) >> 4) ^ (bssid[5] & 0xF);
		/* (dec(AID[0:8]) + dec(BSSID)*2^5) mod 2^9 */
		*p_aid = (aid + (val16 << 5)) & 0x1FF;
		*g_id = 63;
	} else if ((check_fwstate(mlme, WIFI_ADHOC_STATE) == _TRUE)
		   || (check_fwstate(mlme, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
		/*
		 * Otherwise, includes
		 * 1. Sent to an IBSS STA
		 * 2. Sent by an AP to a non associated STA
		 * 3. Sent to a STA for which it is not known
		 *    which condition is applicable
		 */
		*p_aid = 0;
		*g_id = 63;
	} else {
		/* Addressed to AP */
		bssid = sta->cmn.mac_addr;
		RTW_INFO("%s: BSSID=" MAC_FMT "\n", __FUNCTION__, MAC_ARG(bssid));

		/* BSSID[39:47] */
		*p_aid = (bssid[5] << 1) | (bssid[4] >> 7);
		*g_id = 0;
	}

	RTW_INFO("%s: GROUP_ID=0x%02x PARTIAL_AID=0x%04x\n",
		 __FUNCTION__, *g_id, *p_aid);
}

/*
 * Parameters
 *	adapter		struct _adapter*
 *	sta		struct sta_info*
 *	sta_bf_cap	beamforming capabe of sta
 *	sounding_dim	Number of Sounding Dimensions
 *	comp_steering	Compressed Steering Number of Beamformer Antennas Supported
 */
static void _get_sta_beamform_cap(PADAPTER adapter, struct sta_info *sta,
	u8 *sta_bf_cap, u8 *sounding_dim, u8 *comp_steering)
{
	struct beamforming_info *info;
	struct ht_priv *ht;
#ifdef CONFIG_80211AC_VHT
	struct vht_priv *vht;
#endif /* CONFIG_80211AC_VHT */
	u16 bf_cap;


	*sta_bf_cap = 0;
	*sounding_dim = 0;
	*comp_steering = 0;

	info = GET_BEAMFORM_INFO(adapter);
	ht = &adapter->mlmepriv.htpriv;
#ifdef CONFIG_80211AC_VHT
	vht = &adapter->mlmepriv.vhtpriv;
#endif /* CONFIG_80211AC_VHT */

	if (is_supported_ht(sta->wireless_mode) == _TRUE) {
		/* HT */
		bf_cap = ht->beamform_cap;

		if (TEST_FLAG(bf_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE)) {
			info->beamforming_cap |= BEAMFORMEE_CAP_HT_EXPLICIT;
			*sta_bf_cap |= BEAMFORMER_CAP_HT_EXPLICIT;
			*sounding_dim = (bf_cap & BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP) >> 6;
		}
		if (TEST_FLAG(bf_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE)) {
			info->beamforming_cap |= BEAMFORMER_CAP_HT_EXPLICIT;
			*sta_bf_cap |= BEAMFORMEE_CAP_HT_EXPLICIT;
			*comp_steering = (bf_cap & BEAMFORMING_HT_BEAMFORMER_STEER_NUM) >> 4;
		}
	}

#ifdef CONFIG_80211AC_VHT
	if (is_supported_vht(sta->wireless_mode) == _TRUE) {
		/* VHT */
		bf_cap = vht->beamform_cap;

		/* We are SU Beamformee because the STA is SU Beamformer */
		if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_BEAMFORMEE_ENABLE)) {
			info->beamforming_cap |= BEAMFORMEE_CAP_VHT_SU;
			*sta_bf_cap |= BEAMFORMER_CAP_VHT_SU;

			/* We are MU Beamformee because the STA is MU Beamformer */
			if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE)) {
				info->beamforming_cap |= BEAMFORMEE_CAP_VHT_MU;
				*sta_bf_cap |= BEAMFORMER_CAP_VHT_MU;
			}

			*sounding_dim = (bf_cap & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM) >> 12;
		}
		/* We are SU Beamformer because the STA is SU Beamformee */
		if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_BEAMFORMER_ENABLE)) {
			info->beamforming_cap |= BEAMFORMER_CAP_VHT_SU;
			*sta_bf_cap |= BEAMFORMEE_CAP_VHT_SU;

			/* We are MU Beamformer because the STA is MU Beamformee */
			if (TEST_FLAG(bf_cap, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE)) {
				info->beamforming_cap |= BEAMFORMER_CAP_VHT_MU;
				*sta_bf_cap |= BEAMFORMEE_CAP_VHT_MU;
			}

			*comp_steering = (bf_cap & BEAMFORMING_VHT_BEAMFORMER_STS_CAP) >> 8;
		}
	}
#endif /* CONFIG_80211AC_VHT */
}

static u8 _send_ht_ndpa_packet(PADAPTER adapter, u8 *ra, enum channel_width bw)
{
	/* General */
	struct xmit_priv		*pxmitpriv;
	struct mlme_ext_priv		*pmlmeext;
	struct mlme_ext_info		*pmlmeinfo;
	struct xmit_frame		*pmgntframe;
	/* Beamforming */
	struct beamforming_info		*info;
	struct beamformee_entry		*bfee;
	struct ndpa_sta_info		sta_info;
	u8 ActionHdr[4] = {ACT_CAT_VENDOR, 0x00, 0xE0, 0x4C};
	/* MISC */
	struct pkt_attrib		*attrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	enum MGN_RATE txrate;
	u8 *pframe;
	u16 duration = 0;
	u8 aSifsTime = 0;


	RTW_INFO("+%s: Send to " MAC_FMT "\n", __FUNCTION__, MAC_ARG(ra));

	pxmitpriv = &adapter->xmitpriv;
	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	bfee = rtw_bf_bfee_get_entry_by_addr(adapter, ra);
	if (!bfee) {
		RTW_ERR("%s: Cann't find beamformee entry!\n", __FUNCTION__);
		return _FALSE;
	}

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		RTW_ERR("%s: alloc mgnt frame fail!\n", __FUNCTION__);
		return _FALSE;
	}

	txrate = beamforming_get_htndp_tx_rate(GET_PDM_ODM(adapter), bfee->comp_steering_num_of_bfer);

	/* update attribute */
	attrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, attrib);
	/*attrib->type = WIFI_MGT_TYPE;*/ /* set in update_mgntframe_attrib() */
	attrib->subtype = WIFI_ACTION_NOACK;
	attrib->bwmode = bw;
	/*attrib->qsel = QSLT_MGNT;*/ /* set in update_mgntframe_attrib() */
	attrib->order = 1;
	attrib->rate = (u8)txrate;
	attrib->bf_pkt_type = 0;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	/* Frame control */
	pwlanhdr->frame_ctl = 0;
	set_frame_sub_type(pframe, attrib->subtype);
	set_order_bit(pframe);

	/* Duration */
	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;
	duration = 2 * aSifsTime + 40;
	if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;
	set_duration(pframe, duration);

	/* DA */
	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);
	/* SA */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);
	/* BSSID */
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

	/* HT control field */
	SET_HT_CTRL_CSI_STEERING(pframe + 24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe + 24, 1);

	/*
	 * Frame Body
	 * Category field: vender-specific value, 0x7F
	 * OUI: 0x00E04C
	 */
	_rtw_memcpy(pframe + 28, ActionHdr, 4);

	attrib->pktlen = 32;
	attrib->last_txcmdsz = attrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return _TRUE;
}

static u8 _send_vht_ndpa_packet(PADAPTER adapter, u8 *ra, u16 aid, enum channel_width bw)
{
	/* General */
	struct xmit_priv		*pxmitpriv;
	struct mlme_ext_priv		*pmlmeext;
	struct xmit_frame		*pmgntframe;
	/* Beamforming */
	struct beamforming_info		*info;
	struct beamformee_entry		*bfee;
	struct ndpa_sta_info		sta_info;
	/* MISC */
	struct pkt_attrib		*attrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u8 *pframe;
	enum MGN_RATE txrate;
	u16 duration = 0;
	u8 sequence = 0, aSifsTime = 0;


	RTW_INFO("+%s: Send to " MAC_FMT "\n", __FUNCTION__, MAC_ARG(ra));

	pxmitpriv = &adapter->xmitpriv;
	pmlmeext = &adapter->mlmeextpriv;
	info = GET_BEAMFORM_INFO(adapter);
	bfee = rtw_bf_bfee_get_entry_by_addr(adapter, ra);
	if (!bfee) {
		RTW_ERR("%s: Cann't find beamformee entry!\n", __FUNCTION__);
		return _FALSE;
	}

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		RTW_ERR("%s: alloc mgnt frame fail!\n", __FUNCTION__);
		return _FALSE;
	}

	txrate = beamforming_get_vht_ndp_tx_rate(GET_PDM_ODM(adapter), bfee->comp_steering_num_of_bfer);

	/* update attribute */
	attrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, attrib);
	/*pattrib->type = WIFI_MGT_TYPE;*/ /* set in update_mgntframe_attrib() */
	attrib->subtype = WIFI_NDPA;
	attrib->bwmode = bw;
	/*attrib->qsel = QSLT_MGNT;*/ /* set in update_mgntframe_attrib() */
	attrib->rate = (u8)txrate;
	attrib->bf_pkt_type = 0;

	_rtw_memset(pmgntframe->buf_addr, 0, TXDESC_OFFSET + WLANHDR_OFFSET);
	pframe = pmgntframe->buf_addr + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	/* Frame control */
	pwlanhdr->frame_ctl = 0;
	set_frame_sub_type(pframe, attrib->subtype);

	/* Duration */
	if (is_supported_5g(pmlmeext->cur_wireless_mode) || is_supported_ht(pmlmeext->cur_wireless_mode))
		aSifsTime = 16;
	else
		aSifsTime = 10;
	duration = 2 * aSifsTime + 44;
	if (bw == CHANNEL_WIDTH_80)
		duration += 40;
	else if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;
	set_duration(pframe, duration);

	/* RA */
	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);

	/* TA */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);

	/* Sounding Sequence, bit0~1 is reserved */
	sequence = info->sounding_sequence << 2;
	if (info->sounding_sequence >= 0x3f)
		info->sounding_sequence = 0;
	else
		info->sounding_sequence++;
	_rtw_memcpy(pframe + 16, &sequence, 1);

	/* STA Info */
	/*
	 * "AID12" Equal to 0 if the STA is an AP, mesh STA or
	 * STA that is a member of an IBSS
	 */
	if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _FALSE)
		aid = 0;
	sta_info.aid = aid;
	/* "Feedback Type" set to 0 for SU */
	sta_info.feedback_type = 0;
	/* "Nc Index" reserved if the Feedback Type field indicates SU */
	sta_info.nc_index = 0;
	_rtw_memcpy(pframe + 17, (u8 *)&sta_info, 2);

	attrib->pktlen = 19;
	attrib->last_txcmdsz = attrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return _TRUE;
}

static u8 _send_vht_mu_ndpa_packet(PADAPTER adapter, enum channel_width bw)
{
	/* General */
	struct xmit_priv		*pxmitpriv;
	struct mlme_ext_priv		*pmlmeext;
	struct xmit_frame		*pmgntframe;
	/* Beamforming */
	struct beamforming_info		*info;
	struct sounding_info		*sounding;
	struct beamformee_entry		*bfee;
	struct ndpa_sta_info		sta_info;
	/* MISC */
	struct pkt_attrib		*attrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	enum MGN_RATE txrate;
	u8 *pframe;
	u8 *ra = NULL;
	u16 duration = 0;
	u8 sequence = 0, aSifsTime = 0;
	u8 i;


	RTW_INFO("+%s\n", __FUNCTION__);

	pxmitpriv = &adapter->xmitpriv;
	pmlmeext = &adapter->mlmeextpriv;
	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	txrate = MGN_VHT2SS_MCS0;

	/*
	 * Fill the first MU BFee entry (STA1) MAC addr to destination address then
	 * HW will change A1 to broadcast addr.
	 * 2015.05.28. Suggested by SD1 Chunchu.
	 */
	bfee = &info->bfee_entry[sounding->mu_sounding_list[0]];
	ra = bfee->mac_addr;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		RTW_ERR("%s: alloc mgnt frame fail!\n", __FUNCTION__);
		return _FALSE;
	}

	/* update attribute */
	attrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, attrib);
	/*attrib->type = WIFI_MGT_TYPE;*/ /* set in update_mgntframe_attrib() */
	attrib->subtype = WIFI_NDPA;
	attrib->bwmode = bw;
	/*attrib->qsel = QSLT_MGNT;*/ /* set in update_mgntframe_attrib() */
	attrib->rate = (u8)txrate;
	/* Set TxBFPktType of Tx desc to unicast type if there is only one MU STA for HW design */
	if (info->sounding_info.candidate_mu_bfee_cnt > 1)
		attrib->bf_pkt_type = 1;
	else
		attrib->bf_pkt_type = 0;

	_rtw_memset(pmgntframe->buf_addr, 0, TXDESC_OFFSET + WLANHDR_OFFSET);
	pframe = pmgntframe->buf_addr + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	/* Frame control */
	pwlanhdr->frame_ctl = 0;
	set_frame_sub_type(pframe, attrib->subtype);

	/* Duration */
	if (is_supported_5g(pmlmeext->cur_wireless_mode) || is_supported_ht(pmlmeext->cur_wireless_mode))
		aSifsTime = 16;
	else
		aSifsTime = 10;
	duration = 2 * aSifsTime + 44;
	if (bw == CHANNEL_WIDTH_80)
		duration += 40;
	else if (bw == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;
	set_duration(pframe, duration);

	/* RA */
	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);

	/* TA */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);

	/* Sounding Sequence, bit0~1 is reserved */
	sequence = info->sounding_sequence << 2;
	if (info->sounding_sequence >= 0x3f)
		info->sounding_sequence = 0;
	else
		info->sounding_sequence++;
	_rtw_memcpy(pframe + 16, &sequence, 1);

	attrib->pktlen = 17;

	/*
	 * Construct STA info. for multiple STAs
	 * STA Info1, ..., STA Info n
	 */
	for (i = 0; i < sounding->candidate_mu_bfee_cnt; i++) {
		bfee = &info->bfee_entry[sounding->mu_sounding_list[i]];
		sta_info.aid = bfee->aid;
		sta_info.feedback_type = 1; /* 1'b1: MU */
		sta_info.nc_index = 0;
		_rtw_memcpy(pframe + attrib->pktlen, (u8 *)&sta_info, 2);
		attrib->pktlen += 2;
	}

	attrib->last_txcmdsz = attrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return _TRUE;
}

static u8 _send_bf_report_poll(PADAPTER adapter, u8 *ra, u8 bFinalPoll)
{
	/* General */
	struct xmit_priv *pxmitpriv;
	struct xmit_frame *pmgntframe;
	/* MISC */
	struct pkt_attrib *attrib;
	struct rtw_ieee80211_hdr *pwlanhdr;
	u8 *pframe;


	RTW_INFO("+%s: Send to " MAC_FMT "\n", __FUNCTION__, MAC_ARG(ra));

	pxmitpriv = &adapter->xmitpriv;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		RTW_ERR("%s: alloc mgnt frame fail!\n", __FUNCTION__);
		return _FALSE;
	}

	/* update attribute */
	attrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, attrib);
	/*attrib->type = WIFI_MGT_TYPE;*/ /* set in update_mgntframe_attrib() */
	attrib->subtype = WIFI_BF_REPORT_POLL;
	attrib->bwmode = CHANNEL_WIDTH_20;
	/*attrib->qsel = QSLT_MGNT;*/ /* set in update_mgntframe_attrib() */
	attrib->rate = MGN_6M;
	if (bFinalPoll)
		attrib->bf_pkt_type = 3;
	else
		attrib->bf_pkt_type = 2;

	_rtw_memset(pmgntframe->buf_addr, 0, TXDESC_OFFSET + WLANHDR_OFFSET);
	pframe = pmgntframe->buf_addr + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	/* Frame control */
	pwlanhdr->frame_ctl = 0;
	set_frame_sub_type(pframe, attrib->subtype);

	/* Duration */
	set_duration(pframe, 100);

	/* RA */
	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN);

	/* TA */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);

	/* Feedback Segment Retransmission Bitmap */
	pframe[16] = 0xFF;

	attrib->pktlen = 17;
	attrib->last_txcmdsz = attrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return _TRUE;
}

static void _sounding_update_min_period(PADAPTER adapter, u16 period, u8 leave)
{
	struct beamforming_info *info;
	struct beamformee_entry *bfee;
	u8 i = 0;
	u16 min_val = 0xFFFF;


	info = GET_BEAMFORM_INFO(adapter);

	if (_TRUE == leave) {
		/*
		 * When a BFee left,
		 * we need to find the latest min sounding period
		 * from the remaining BFees
		 */
		for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
			bfee = &info->bfee_entry[i];
			if ((bfee->used == _TRUE)
			    && (bfee->sound_period < min_val))
				min_val = bfee->sound_period;
		}

		if (min_val == 0xFFFF)
			info->sounding_info.min_sounding_period = 0;
		else
			info->sounding_info.min_sounding_period = min_val;
	} else {
		if ((info->sounding_info.min_sounding_period == 0)
		    || (period < info->sounding_info.min_sounding_period))
			info->sounding_info.min_sounding_period = period;
	}
}

static void _sounding_init(struct sounding_info *sounding)
{
	_rtw_memset(sounding->su_sounding_list, 0xFF, MAX_NUM_BEAMFORMEE_SU);
	_rtw_memset(sounding->mu_sounding_list, 0xFF, MAX_NUM_BEAMFORMEE_MU);
	sounding->state = SOUNDING_STATE_NONE;
	sounding->su_bfee_curidx = 0xFF;
	sounding->candidate_mu_bfee_cnt = 0;
	sounding->min_sounding_period = 0;
	sounding->sound_remain_cnt_per_period = 0;
}

static void _sounding_reset_vars(PADAPTER adapter)
{
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	u8 idx;


	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	_rtw_memset(sounding->su_sounding_list, 0xFF, MAX_NUM_BEAMFORMEE_SU);
	_rtw_memset(sounding->mu_sounding_list, 0xFF, MAX_NUM_BEAMFORMEE_MU);
	sounding->su_bfee_curidx = 0xFF;
	sounding->candidate_mu_bfee_cnt = 0;

	/* Clear bSound flag for the new period */
	for (idx = 0; idx < MAX_BEAMFORMEE_ENTRY_NUM; idx++) {
		if ((info->bfee_entry[idx].used == _TRUE)
		    && (info->bfee_entry[idx].sounding == _TRUE)) {
			info->bfee_entry[idx].sounding = _FALSE;
			info->bfee_entry[idx].bCandidateSoundingPeer = _FALSE;
		}
	}
}

/*
 * Return
 *	0	Prepare sounding list OK
 *	-1	Fail to prepare sounding list, because no beamformee need to souding
 *	-2	Fail to prepare sounding list, because beamformee state not ready
 *
 */
static int _sounding_get_list(PADAPTER adapter)
{
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	struct beamformee_entry *bfee;
	u8 i, mu_idx = 0, su_idx = 0, not_ready = 0;
	int ret = 0;


	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	/* Add MU BFee list first because MU priority is higher than SU */
	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (bfee->used == _FALSE)
			continue;

		if (bfee->state != BEAMFORM_ENTRY_HW_STATE_ADDED) {
			RTW_ERR("%s: Invalid BFee idx(%d) Hw state=%d\n", __FUNCTION__, i, bfee->state);
			not_ready++;
			continue;
		}

		/*
		 * Decrease BFee's SoundCnt per period
		 * If the remain count is 0,
		 * then it can be sounded at this time
		 */
		if (bfee->SoundCnt) {
			bfee->SoundCnt--;
			if (bfee->SoundCnt)
				continue;
		}

		/*
		 * <tynli_Note>
		 *	If the STA supports MU BFee capability then we add it to MUSoundingList directly
		 *	because we can only sound one STA by unicast NDPA with MU cap enabled to get correct channel info.
		 *	Suggested by BB team Luke Lee. 2015.11.25.
		 */
		if (bfee->cap & BEAMFORMEE_CAP_VHT_MU) {
			/* MU BFee */
			if (mu_idx >= MAX_NUM_BEAMFORMEE_MU) {
				RTW_ERR("%s: Too much MU bfee entry(Limit:%d)\n", __FUNCTION__, MAX_NUM_BEAMFORMEE_MU);
				continue;
			}

			if (bfee->bApplySounding == _TRUE) {
				bfee->bCandidateSoundingPeer = _TRUE;
				bfee->SoundCnt = GetInitSoundCnt(bfee->sound_period, sounding->min_sounding_period);
				sounding->mu_sounding_list[mu_idx] = i;
				mu_idx++;
			}
		} else if (bfee->cap & (BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
			/* SU BFee (HT/VHT) */
			if (su_idx >= MAX_NUM_BEAMFORMEE_SU) {
				RTW_ERR("%s: Too much SU bfee entry(Limit:%d)\n", __FUNCTION__, MAX_NUM_BEAMFORMEE_SU);
				continue;
			}

			if (bfee->bDeleteSounding == _TRUE) {
				sounding->su_sounding_list[su_idx] = i;
				su_idx++;
			} else if ((bfee->bApplySounding == _TRUE)
			    && (bfee->bSuspendSUCap == _FALSE)) {
				bfee->bCandidateSoundingPeer = _TRUE;
				bfee->SoundCnt = GetInitSoundCnt(bfee->sound_period, sounding->min_sounding_period);
				sounding->su_sounding_list[su_idx] = i;
				su_idx++;
			}
		}
	}

	sounding->candidate_mu_bfee_cnt = mu_idx;

	if (su_idx + mu_idx == 0) {
		ret = -1;
		if (not_ready)
			ret = -2;
	}

	RTW_INFO("-%s: There are %d SU and %d MU BFees in this sounding period\n", __FUNCTION__, su_idx, mu_idx);

	return ret;
}

static void _sounding_handler(PADAPTER adapter)
{
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	struct beamformee_entry *bfee;
	u8 su_idx, i;
	u32 timeout_period = 0;
	u8 set_timer = _FALSE;
	int ret = 0;
	static u16 wait_cnt = 0;


	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	RTW_DBG("+%s: state=%d\n", __FUNCTION__, sounding->state);
	if ((sounding->state != SOUNDING_STATE_INIT)
	    && (sounding->state != SOUNDING_STATE_SU_SOUNDDOWN)
	    && (sounding->state != SOUNDING_STATE_MU_SOUNDDOWN)
	    && (sounding->state != SOUNDING_STATE_SOUNDING_TIMEOUT)) {
		RTW_WARN("%s: Invalid State(%d) and return!\n", __FUNCTION__, sounding->state);
		return;
	}

	if (sounding->state == SOUNDING_STATE_INIT) {
		RTW_INFO("%s: Sounding start\n", __FUNCTION__);

		/* Init Var */
		_sounding_reset_vars(adapter);

		/* Get the sounding list of this sounding period */
		ret = _sounding_get_list(adapter);
		if (ret == -1) {
			wait_cnt = 0;
			sounding->state = SOUNDING_STATE_NONE;
			RTW_ERR("%s: No BFees found, set to SOUNDING_STATE_NONE\n", __FUNCTION__);
			info->sounding_running--;
			return;
		}
		if (ret == -2) {
			RTW_WARN("%s: Temporarily cann't find BFee to sounding\n", __FUNCTION__);
			if (wait_cnt < 5) {
				wait_cnt++;
			} else {
				wait_cnt = 0;
				sounding->state = SOUNDING_STATE_NONE;
				RTW_ERR("%s: Wait changing state timeout!! Set to SOUNDING_STATE_NONE\n", __FUNCTION__);
			}
			info->sounding_running--;
			return;
		}
		if (ret != 0) {
			wait_cnt = 0;
			RTW_ERR("%s: Unkown state(%d)!\n", __FUNCTION__, ret);
			info->sounding_running--;
			return;

		}

		wait_cnt = 0;

		if (check_fwstate(&adapter->mlmepriv, WIFI_UNDER_SURVEY) == _TRUE) {
			RTW_INFO("%s: Sounding abort! scanning APs...\n", __FUNCTION__);
			info->sounding_running--;
			return;
		}

		rtw_ps_deny(adapter, PS_DENY_BEAMFORMING);
		LeaveAllPowerSaveModeDirect(adapter);
	}

	/* Get non-sound SU BFee index */
	for (i = 0; i < MAX_NUM_BEAMFORMEE_SU; i++) {
		su_idx = sounding->su_sounding_list[i];
		if (su_idx >= MAX_BEAMFORMEE_ENTRY_NUM)
			continue;
		bfee = &info->bfee_entry[su_idx];
		if (_FALSE == bfee->sounding)
			break;
	}
	if (i < MAX_NUM_BEAMFORMEE_SU) {
		sounding->su_bfee_curidx = su_idx;
		/* Set to sounding start state */
		sounding->state = SOUNDING_STATE_SU_START;
		RTW_DBG("%s: Set to SOUNDING_STATE_SU_START\n", __FUNCTION__);

		bfee->sounding = _TRUE;
		/* Reset sounding timeout flag for the new sounding */
		bfee->bSoundingTimeout = _FALSE;

		if (_TRUE == bfee->bDeleteSounding) {
			u8 res = _FALSE;
			rtw_bf_cmd(adapter, BEAMFORMING_CTRL_END_PERIOD, &res, 1, 0);
			return;
		}

		/* Start SU sounding */
		if (bfee->cap & BEAMFORMEE_CAP_VHT_SU)
			_send_vht_ndpa_packet(adapter, bfee->mac_addr, bfee->aid, bfee->sound_bw);
		else if (bfee->cap & BEAMFORMEE_CAP_HT_EXPLICIT)
			_send_ht_ndpa_packet(adapter, bfee->mac_addr, bfee->sound_bw);

		/* Set sounding timeout timer */
		_set_timer(&info->sounding_timeout_timer, SU_SOUNDING_TIMEOUT);
		return;
	}

	if (sounding->candidate_mu_bfee_cnt > 0) {
		/*
		 * If there is no SU BFee then find MU BFee and perform MU sounding
		 *
		 * <tynli_note> Need to check the MU starting condition. 2015.12.15.
		 */
		sounding->state = SOUNDING_STATE_MU_START;
		RTW_DBG("%s: Set to SOUNDING_STATE_MU_START\n", __FUNCTION__);

		/* Update MU BFee info */
		for (i = 0; i < sounding->candidate_mu_bfee_cnt; i++) {
			bfee = &info->bfee_entry[sounding->mu_sounding_list[i]];
			bfee->sounding = _TRUE;
		}

		/* Send MU NDPA */
		bfee = &info->bfee_entry[sounding->mu_sounding_list[0]];
		_send_vht_mu_ndpa_packet(adapter, bfee->sound_bw);

		/* Send BF report poll if more than 1 MU STA */
		for (i = 1; i < sounding->candidate_mu_bfee_cnt; i++) {
			bfee = &info->bfee_entry[sounding->mu_sounding_list[i]];

			if (i == (sounding->candidate_mu_bfee_cnt - 1))/* The last STA*/
				_send_bf_report_poll(adapter, bfee->mac_addr, _TRUE);
			else
				_send_bf_report_poll(adapter, bfee->mac_addr, _FALSE);
		}

		sounding->candidate_mu_bfee_cnt = 0;

		/* Set sounding timeout timer */
		_set_timer(&info->sounding_timeout_timer, MU_SOUNDING_TIMEOUT);
		return;
	}

	info->sounding_running--;
	sounding->state = SOUNDING_STATE_INIT;
	RTW_INFO("%s: Sounding finished!\n", __FUNCTION__);
	rtw_ps_deny_cancel(adapter, PS_DENY_BEAMFORMING);
}

static void _sounding_force_stop(PADAPTER adapter)
{
	struct beamforming_info	*info;
	struct sounding_info *sounding;

	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	if ((sounding->state == SOUNDING_STATE_SU_START)
	    || (sounding->state == SOUNDING_STATE_MU_START)) {
		u8 res = _FALSE;
		_cancel_timer_ex(&info->sounding_timeout_timer);
		rtw_bf_cmd(adapter, BEAMFORMING_CTRL_END_PERIOD, &res, 1, 1);
		return;
	}

	info->sounding_running--;
	sounding->state = SOUNDING_STATE_INIT;
	RTW_INFO("%s: Sounding finished!\n", __FUNCTION__);
	rtw_ps_deny_cancel(adapter, PS_DENY_BEAMFORMING);
}

static void _sounding_timer_handler(void *FunctionContext)
{
	PADAPTER adapter;
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	static u8 delay = 0;


	RTW_DBG("+%s\n", __FUNCTION__);

	adapter = (PADAPTER)FunctionContext;
	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	if (SOUNDING_STATE_NONE == sounding->state) {
		RTW_INFO("%s: Stop!\n", __FUNCTION__);
		if (info->sounding_running)
			RTW_WARN("%s: souding_running=%d when thread stop!\n",
				 __FUNCTION__, info->sounding_running);
		return;
	}

	_set_timer(&info->sounding_timer, sounding->min_sounding_period);

	if (!info->sounding_running) {
		if (SOUNDING_STATE_INIT != sounding->state) {
			RTW_WARN("%s: state(%d) != SOUNDING_STATE_INIT!!\n", __FUNCTION__, sounding->state);
			sounding->state = SOUNDING_STATE_INIT;
		}
		delay = 0;
		info->sounding_running++;
		rtw_bf_cmd(adapter, BEAMFORMING_CTRL_START_PERIOD, NULL, 0, 1);
	} else {
		if (delay != 0xFF)
			delay++;
		RTW_WARN("%s: souding is still processing...(state:%d, running:%d, delay:%d)\n",
			 __FUNCTION__, sounding->state, info->sounding_running, delay);
		if (delay > 3) {
			RTW_WARN("%s: Stop sounding!!\n", __FUNCTION__);
			_sounding_force_stop(adapter);
		}
	}
}

static void _sounding_timeout_timer_handler(void *FunctionContext)
{
	PADAPTER adapter;
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	struct beamformee_entry *bfee;


	RTW_WARN("+%s\n", __FUNCTION__);

	adapter = (PADAPTER)FunctionContext;
	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	if (SOUNDING_STATE_SU_START == sounding->state) {
		sounding->state = SOUNDING_STATE_SOUNDING_TIMEOUT;
		RTW_ERR("%s: Set to SU SOUNDING_STATE_SOUNDING_TIMEOUT\n", __FUNCTION__);
		/* SU BFee */
		bfee = &info->bfee_entry[sounding->su_bfee_curidx];
		bfee->bSoundingTimeout = _TRUE;
		RTW_WARN("%s: The BFee entry[%d] is Sounding Timeout!\n", __FUNCTION__, sounding->su_bfee_curidx);
	} else if (SOUNDING_STATE_MU_START == sounding->state) {
		sounding->state = SOUNDING_STATE_SOUNDING_TIMEOUT;
		RTW_ERR("%s: Set to MU SOUNDING_STATE_SOUNDING_TIMEOUT\n", __FUNCTION__);
	} else {
		RTW_WARN("%s: unexpected sounding state:0x%02x\n", __FUNCTION__, sounding->state);
		return;
	}

	rtw_bf_cmd(adapter, BEAMFORMING_CTRL_START_PERIOD, NULL, 0, 1);
}

static struct beamformer_entry *_bfer_get_free_entry(PADAPTER adapter)
{
	u8 i = 0;
	struct beamforming_info *info;
	struct beamformer_entry *bfer;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMER_ENTRY_NUM; i++) {
		bfer = &info->bfer_entry[i];
		if (bfer->used == _FALSE)
			return bfer;
	}

	return NULL;
}

static struct beamformer_entry *_bfer_get_entry_by_addr(PADAPTER adapter, u8 *ra)
{
	u8 i = 0;
	struct beamforming_info *info;
	struct beamformer_entry *bfer;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMER_ENTRY_NUM; i++) {
		bfer = &info->bfer_entry[i];
		if (bfer->used == _FALSE)
			continue;
		if (_rtw_memcmp(ra, bfer->mac_addr, ETH_ALEN) == _TRUE)
			return bfer;
	}

	return NULL;
}

static struct beamformer_entry *_bfer_add_entry(PADAPTER adapter,
	struct sta_info *sta, u8 bf_cap, u8 sounding_dim, u8 comp_steering)
{
	struct mlme_priv *mlme;
	struct beamforming_info *info;
	struct beamformer_entry *bfer;
	u8 *bssid;
	u16 val16;
	u8 i;


	mlme = &adapter->mlmepriv;
	info = GET_BEAMFORM_INFO(adapter);

	bfer = _bfer_get_entry_by_addr(adapter, sta->cmn.mac_addr);
	if (!bfer) {
		bfer = _bfer_get_free_entry(adapter);
		if (!bfer)
			return NULL;
	}

	bfer->used = _TRUE;
	_get_txvector_parameter(adapter, sta, &bfer->g_id, &bfer->p_aid);
	_rtw_memcpy(bfer->mac_addr, sta->cmn.mac_addr, ETH_ALEN);
	bfer->cap = bf_cap;
	bfer->state = BEAMFORM_ENTRY_HW_STATE_ADD_INIT;
	bfer->NumofSoundingDim = sounding_dim;

	if (TEST_FLAG(bf_cap, BEAMFORMER_CAP_VHT_MU)) {
		info->beamformer_mu_cnt += 1;
		bfer->aid = sta->cmn.aid;
	} else if (TEST_FLAG(bf_cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT)) {
		info->beamformer_su_cnt += 1;

		/* Record HW idx info */
		for (i = 0; i < MAX_NUM_BEAMFORMER_SU; i++) {
			if ((info->beamformer_su_reg_maping & BIT(i)) == 0) {
				info->beamformer_su_reg_maping |= BIT(i);
				bfer->su_reg_index = i;
				break;
			}
		}
		RTW_INFO("%s: Add BFer entry beamformer_su_reg_maping=%#x, su_reg_index=%d\n",
			 __FUNCTION__, info->beamformer_su_reg_maping, bfer->su_reg_index);
	}

	return bfer;
}

static void _bfer_remove_entry(PADAPTER adapter, struct beamformer_entry *entry)
{
	struct beamforming_info *info;


	info = GET_BEAMFORM_INFO(adapter);

	entry->state = BEAMFORM_ENTRY_HW_STATE_DELETE_INIT;

	if (TEST_FLAG(entry->cap, BEAMFORMER_CAP_VHT_MU)) {
		info->beamformer_mu_cnt -= 1;
		_rtw_memset(entry->gid_valid, 0, 8);
		_rtw_memset(entry->user_position, 0, 16);
	} else if (TEST_FLAG(entry->cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT)) {
		info->beamformer_su_cnt -= 1;
	}

	if (info->beamformer_mu_cnt == 0)
		info->beamforming_cap &= ~BEAMFORMEE_CAP_VHT_MU;
	if (info->beamformer_su_cnt == 0)
		info->beamforming_cap &= ~(BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT);
}

static u8 _bfer_set_entry_gid(PADAPTER adapter, u8 *addr, u8 *gid, u8 *position)
{
	struct beamformer_entry bfer;

	memset(&bfer, 0, sizeof(bfer));
	memcpy(bfer.mac_addr, addr, ETH_ALEN);

	/* Parsing Membership Status Array */
	memcpy(bfer.gid_valid, gid, 8);

	/* Parsing User Position Array */
	memcpy(bfer.user_position, position, 16);

	/* Config HW GID table */
	rtw_bf_cmd(adapter, BEAMFORMING_CTRL_SET_GID_TABLE, (u8 *) &bfer,
			sizeof(bfer), 1);

	return _SUCCESS;
}

static struct beamformee_entry *_bfee_get_free_entry(PADAPTER adapter)
{
	u8 i = 0;
	struct beamforming_info *info;
	struct beamformee_entry *bfee;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (bfee->used == _FALSE)
			return bfee;
	}

	return NULL;
}

static struct beamformee_entry *_bfee_get_entry_by_addr(PADAPTER adapter, u8 *ra)
{
	u8 i = 0;
	struct beamforming_info *info;
	struct beamformee_entry *bfee;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (bfee->used == _FALSE)
			continue;
		if (_rtw_memcmp(ra, bfee->mac_addr, ETH_ALEN) == _TRUE)
			return bfee;
	}

	return NULL;
}

static u8 _bfee_get_first_su_entry_idx(PADAPTER adapter, struct beamformee_entry *ignore)
{
	struct beamforming_info *info;
	struct beamformee_entry *bfee;
	u8 i;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (ignore && (bfee == ignore))
			continue;
		if (bfee->used == _FALSE)
			continue;
		if ((!TEST_FLAG(bfee->cap, BEAMFORMEE_CAP_VHT_MU))
		    && TEST_FLAG(bfee->cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT))
			return i;
	}

	return 0xFF;
}

/*
 * Description:
 *	Get the first entry index of MU Beamformee.
 *
 * Return Value:
 *	Index of the first MU sta, or 0xFF for invalid index.
 *
 * 2015.05.25. Created by tynli.
 *
 */
static u8 _bfee_get_first_mu_entry_idx(PADAPTER adapter, struct beamformee_entry *ignore)
{
	struct beamforming_info *info;
	struct beamformee_entry *bfee;
	u8 i;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (ignore && (bfee == ignore))
			continue;
		if (bfee->used == _FALSE)
			continue;
		if (TEST_FLAG(bfee->cap, BEAMFORMEE_CAP_VHT_MU))
			return i;
	}

	return 0xFF;
}

static struct beamformee_entry *_bfee_add_entry(PADAPTER adapter,
	struct sta_info *sta, u8 bf_cap, u8 sounding_dim, u8 comp_steering)
{
	struct mlme_priv *mlme;
	struct beamforming_info *info;
	struct beamformee_entry *bfee;
	u8 *bssid;
	u16 val16;
	u8 i;


	mlme = &adapter->mlmepriv;
	info = GET_BEAMFORM_INFO(adapter);

	bfee = _bfee_get_entry_by_addr(adapter, sta->cmn.mac_addr);
	if (!bfee) {
		bfee = _bfee_get_free_entry(adapter);
		if (!bfee)
			return NULL;
	}

	bfee->used = _TRUE;
	bfee->aid = sta->cmn.aid;
	bfee->mac_id = sta->cmn.mac_id;
	bfee->sound_bw = sta->cmn.bw_mode;

	_get_txvector_parameter(adapter, sta, &bfee->g_id, &bfee->p_aid);
	sta->cmn.bf_info.g_id = bfee->g_id;
	sta->cmn.bf_info.p_aid = bfee->p_aid;

	_rtw_memcpy(bfee->mac_addr, sta->cmn.mac_addr, ETH_ALEN);
	bfee->txbf = _FALSE;
	bfee->sounding = _FALSE;
	bfee->sound_period = 40;
	_sounding_update_min_period(adapter, bfee->sound_period, _FALSE);
	bfee->SoundCnt = GetInitSoundCnt(bfee->sound_period, info->sounding_info.min_sounding_period);
	bfee->cap = bf_cap;
	bfee->state = BEAMFORM_ENTRY_HW_STATE_ADD_INIT;

	bfee->bCandidateSoundingPeer = _FALSE;
	bfee->bSoundingTimeout = _FALSE;
	bfee->bDeleteSounding = _FALSE;
	bfee->bApplySounding = _TRUE;

	bfee->tx_timestamp = 0;
	bfee->tx_bytes = 0;

	bfee->LogStatusFailCnt = 0;
	bfee->NumofSoundingDim = sounding_dim;
	bfee->comp_steering_num_of_bfer = comp_steering;
	bfee->bSuspendSUCap = _FALSE;

	if (TEST_FLAG(bf_cap, BEAMFORMEE_CAP_VHT_MU)) {
		info->beamformee_mu_cnt += 1;
		info->first_mu_bfee_index = _bfee_get_first_mu_entry_idx(adapter, NULL);

		if (_TRUE == info->bEnableSUTxBFWorkAround) {
			/* When the first MU BFee added, discard SU BFee bfee's capability */
			if ((info->beamformee_mu_cnt == 1) && (info->beamformee_su_cnt > 0)) {
				if (info->TargetSUBFee) {
					info->TargetSUBFee->bSuspendSUCap = _TRUE;
					info->TargetSUBFee->bDeleteSounding = _TRUE;
				} else {
					RTW_ERR("%s: UNEXPECTED!! info->TargetSUBFee is NULL!", __FUNCTION__);
				}
				info->TargetSUBFee = NULL;
				_rtw_memset(&info->TargetCSIInfo, 0, sizeof(struct _RT_CSI_INFO));
				rtw_bf_cmd(adapter, BEAMFORMING_CTRL_SET_CSI_REPORT, (u8*)&info->TargetCSIInfo, sizeof(struct _RT_CSI_INFO), 0);
			}
		}

		/* Record HW idx info */
		for (i = 0; i < MAX_NUM_BEAMFORMEE_MU; i++) {
			if ((info->beamformee_mu_reg_maping & BIT(i)) == 0) {
				info->beamformee_mu_reg_maping |= BIT(i);
				bfee->mu_reg_index = i;
				break;
			}
		}
		RTW_INFO("%s: Add BFee entry beamformee_mu_reg_maping=%#x, mu_reg_index=%d\n",
			 __FUNCTION__, info->beamformee_mu_reg_maping, bfee->mu_reg_index);

	} else if (TEST_FLAG(bf_cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
		info->beamformee_su_cnt += 1;

		if (_TRUE == info->bEnableSUTxBFWorkAround) {
			/* Record the first SU BFee index. We only allow the first SU BFee to be sound */
			if ((info->beamformee_su_cnt == 1) && (info->beamformee_mu_cnt == 0)) {
				info->TargetSUBFee = bfee;
				_rtw_memset(&info->TargetCSIInfo, 0, sizeof(struct _RT_CSI_INFO));
				bfee->bSuspendSUCap = _FALSE;
			} else {
				bfee->bSuspendSUCap = _TRUE;
			}
		}

		/* Record HW idx info */
		for (i = 0; i < MAX_NUM_BEAMFORMEE_SU; i++) {
			if ((info->beamformee_su_reg_maping & BIT(i)) == 0) {
				info->beamformee_su_reg_maping |= BIT(i);
				bfee->su_reg_index = i;
				break;
			}
		}
		RTW_INFO("%s: Add BFee entry beamformee_su_reg_maping=%#x, su_reg_index=%d\n",
			 __FUNCTION__, info->beamformee_su_reg_maping, bfee->su_reg_index);
	}

	return bfee;
}

static void _bfee_remove_entry(PADAPTER adapter, struct beamformee_entry *entry)
{
	struct beamforming_info *info;
	u8 idx;


	info = GET_BEAMFORM_INFO(adapter);

	entry->state = BEAMFORM_ENTRY_HW_STATE_DELETE_INIT;

	if (TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_MU)) {
		info->beamformee_mu_cnt -= 1;
		info->first_mu_bfee_index = _bfee_get_first_mu_entry_idx(adapter, entry);

		if (_TRUE == info->bEnableSUTxBFWorkAround) {
			if ((info->beamformee_mu_cnt == 0) && (info->beamformee_su_cnt > 0)) {
				idx = _bfee_get_first_su_entry_idx(adapter, NULL);
				info->TargetSUBFee = &info->bfee_entry[idx];
				_rtw_memset(&info->TargetCSIInfo, 0, sizeof(struct _RT_CSI_INFO));
				info->TargetSUBFee->bSuspendSUCap = _FALSE;
			}
		}
	} else if (TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
		info->beamformee_su_cnt -= 1;

		/* When the target SU BFee leaves, disable workaround */
		if ((_TRUE == info->bEnableSUTxBFWorkAround)
		    && (entry == info->TargetSUBFee)) {
			entry->bSuspendSUCap = _TRUE;
			info->TargetSUBFee = NULL;
			_rtw_memset(&info->TargetCSIInfo, 0, sizeof(struct _RT_CSI_INFO));
			rtw_bf_cmd(adapter, BEAMFORMING_CTRL_SET_CSI_REPORT, (u8*)&info->TargetCSIInfo, sizeof(struct _RT_CSI_INFO), 0);
		}
	}

	if (info->beamformee_mu_cnt == 0)
		info->beamforming_cap &= ~BEAMFORMER_CAP_VHT_MU;
	if (info->beamformee_su_cnt == 0)
		info->beamforming_cap &= ~(BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT);

	_sounding_update_min_period(adapter, 0, _TRUE);
}

static enum beamforming_cap _bfee_get_entry_cap_by_macid(PADAPTER adapter, u8 macid)
{
	struct beamforming_info *info;
	struct beamformee_entry *bfee;
	u8 i;


	info = GET_BEAMFORM_INFO(adapter);

	for (i = 0; i < MAX_BEAMFORMER_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (bfee->used == _FALSE)
			continue;
		if (bfee->mac_id == macid)
			return bfee->cap;
	}

	return BEAMFORMING_CAP_NONE;
}

static void _beamforming_enter(PADAPTER adapter, void *p)
{
	struct mlme_priv *mlme;
	struct ht_priv *htpriv;
#ifdef CONFIG_80211AC_VHT
	struct vht_priv *vhtpriv;
#endif
	struct mlme_ext_priv *mlme_ext;
	struct sta_info *sta, *sta_copy;
	struct beamforming_info *info;
	struct beamformer_entry *bfer = NULL;
	struct beamformee_entry *bfee = NULL;
	u8 wireless_mode;
	u8 sta_bf_cap;
	u8 sounding_dim = 0; /* number of sounding dimensions */
	u8 comp_steering_num = 0; /* compressed steering number */


	mlme = &adapter->mlmepriv;
	htpriv = &mlme->htpriv;
#ifdef CONFIG_80211AC_VHT
	vhtpriv = &mlme->vhtpriv;
#endif
	mlme_ext = &adapter->mlmeextpriv;
	info = GET_BEAMFORM_INFO(adapter);

	sta_copy = (struct sta_info *)p;
	sta = rtw_get_stainfo(&adapter->stapriv, sta_copy->cmn.mac_addr);
	if (!sta) {
		RTW_ERR("%s: Cann't find STA info for " MAC_FMT "\n",
			__FUNCTION__, MAC_ARG(sta_copy->cmn.mac_addr));
		return;
	}
	if (sta != sta_copy) {
		RTW_WARN("%s: Origin sta(fake)=%p realsta=%p for " MAC_FMT "\n",
		__FUNCTION__, sta_copy, sta, MAC_ARG(sta_copy->cmn.mac_addr));
	}

	/* The current setting does not support Beaforming */
	wireless_mode = sta->wireless_mode;
	if ((is_supported_ht(wireless_mode) == _FALSE)
	    && (is_supported_vht(wireless_mode) == _FALSE)) {
		RTW_WARN("%s: Not support HT or VHT mode\n", __FUNCTION__);
		return;
	}

	if ((0 == htpriv->beamform_cap)
#ifdef CONFIG_80211AC_VHT
	    && (0 == vhtpriv->beamform_cap)
#endif
	   ) {
		RTW_INFO("The configuration disabled Beamforming! Skip...\n");
		return;
	}

	_get_sta_beamform_cap(adapter, sta,
			      &sta_bf_cap, &sounding_dim, &comp_steering_num);
	RTW_INFO("STA Beamforming Capability=0x%02X\n", sta_bf_cap);
	if (sta_bf_cap == BEAMFORMING_CAP_NONE)
		return;
	if ((sta_bf_cap & BEAMFORMEE_CAP_HT_EXPLICIT)
	    || (sta_bf_cap & BEAMFORMEE_CAP_VHT_SU)
	    || (sta_bf_cap & BEAMFORMEE_CAP_VHT_MU))
		sta_bf_cap |= BEAMFORMEE_CAP;
	if ((sta_bf_cap & BEAMFORMER_CAP_HT_EXPLICIT)
	    || (sta_bf_cap & BEAMFORMER_CAP_VHT_SU)
	    || (sta_bf_cap & BEAMFORMER_CAP_VHT_MU))
		sta_bf_cap |= BEAMFORMER_CAP;

	if (sta_bf_cap & BEAMFORMER_CAP) {
		/* The other side is beamformer */
		bfer = _bfer_add_entry(adapter, sta, sta_bf_cap, sounding_dim, comp_steering_num);
		if (!bfer)
			RTW_ERR("%s: Fail to allocate bfer entry!\n", __FUNCTION__);
	}
	if (sta_bf_cap & BEAMFORMEE_CAP) {
		/* The other side is beamformee */
		bfee = _bfee_add_entry(adapter, sta, sta_bf_cap, sounding_dim, comp_steering_num);
		if (!bfee)
			RTW_ERR("%s: Fail to allocate bfee entry!\n", __FUNCTION__);
	}
	if (!bfer && !bfee)
		return;

	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_ENTER, (u8*)sta);

	/* Perform sounding if there is BFee */
	if ((info->beamformee_su_cnt != 0)
	    || (info->beamformee_mu_cnt != 0)) {
		if (SOUNDING_STATE_NONE == info->sounding_info.state) {
			info->sounding_info.state = SOUNDING_STATE_INIT;
			/* Start sounding after 2 sec */
			_set_timer(&info->sounding_timer, 2000);
		}
	}
}

static void _beamforming_reset(PADAPTER adapter)
{
	RTW_ERR("%s: Not ready!!\n", __FUNCTION__);
}

static void _beamforming_leave(PADAPTER adapter, u8 *ra)
{
	struct beamforming_info *info;
	struct beamformer_entry *bfer = NULL;
	struct beamformee_entry *bfee = NULL;
	u8 bHwStateAddInit = _FALSE;


	RTW_INFO("+%s\n", __FUNCTION__);

	info = GET_BEAMFORM_INFO(adapter);
	bfer = _bfer_get_entry_by_addr(adapter, ra);
	bfee = _bfee_get_entry_by_addr(adapter, ra);

	if (!bfer && !bfee) {
		RTW_WARN("%s: " MAC_FMT " is neither beamforming ee or er!!\n",
			__FUNCTION__, MAC_ARG(ra));
		return;
	}

	if (bfer)
		_bfer_remove_entry(adapter, bfer);

	if (bfee)
		_bfee_remove_entry(adapter, bfee);

	rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_LEAVE, ra);

	/* Stop sounding if there is no any BFee */
	if ((info->beamformee_su_cnt == 0)
	    && (info->beamformee_mu_cnt == 0)) {
		_cancel_timer_ex(&info->sounding_timer);
		_sounding_init(&info->sounding_info);
	}

	RTW_INFO("-%s\n", __FUNCTION__);
}

static void _beamforming_sounding_down(PADAPTER adapter, u8 status)
{
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	struct beamformee_entry *bfee;


	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	RTW_INFO("+%s: sounding=%d, status=0x%02x\n", __FUNCTION__, sounding->state, status);

	if (sounding->state == SOUNDING_STATE_MU_START) {
		RTW_INFO("%s: MU sounding done\n", __FUNCTION__);
		sounding->state = SOUNDING_STATE_MU_SOUNDDOWN;
		RTW_INFO("%s: Set to SOUNDING_STATE_MU_SOUNDDOWN\n", __FUNCTION__);
		info->SetHalSoundownOnDemandCnt++;
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_STATUS, &status);
	} else if (sounding->state == SOUNDING_STATE_SU_START) {
		RTW_INFO("%s: SU entry[%d] sounding down\n", __FUNCTION__, sounding->su_bfee_curidx);
		bfee = &info->bfee_entry[sounding->su_bfee_curidx];
		sounding->state = SOUNDING_STATE_SU_SOUNDDOWN;
		RTW_INFO("%s: Set to SOUNDING_STATE_SU_SOUNDDOWN\n", __FUNCTION__);

		/*
		 * <tynli_note>
		 *	bfee->bSoundingTimeout this flag still cannot avoid
		 *	old sound down event happens in the new sounding period.
		 *	2015.12.10
		 */
		if (_TRUE == bfee->bSoundingTimeout) {
			RTW_WARN("%s: The entry[%d] is bSoundingTimeout!\n", __FUNCTION__, sounding->su_bfee_curidx);
			bfee->bSoundingTimeout = _FALSE;
			return;
		}

		if (_TRUE == status) {
			/* success */
			bfee->LogStatusFailCnt = 0;
			info->SetHalSoundownOnDemandCnt++;
			rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_STATUS, &status);
		} else if (_TRUE == bfee->bDeleteSounding) {
			RTW_WARN("%s: Delete entry[%d] sounding info!\n", __FUNCTION__, sounding->su_bfee_curidx);
			rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_STATUS, &status);
			bfee->bDeleteSounding = _FALSE;
		} else {
			bfee->LogStatusFailCnt++;
			RTW_WARN("%s: LogStatusFailCnt=%d\n", __FUNCTION__, bfee->LogStatusFailCnt);
			if (bfee->LogStatusFailCnt > 30) {
				RTW_ERR("%s: LogStatusFailCnt > 30, Stop SOUNDING!!\n", __FUNCTION__);
				rtw_bf_cmd(adapter, BEAMFORMING_CTRL_LEAVE, bfee->mac_addr, ETH_ALEN, 1);
			}
		}
	} else {
		RTW_WARN("%s: unexpected sounding state:0x%02x\n", __FUNCTION__, sounding->state);
		return;
	}

	rtw_bf_cmd(adapter, BEAMFORMING_CTRL_START_PERIOD, NULL, 0, 0);
}

static void _c2h_snd_txbf(PADAPTER adapter, u8 *buf, u8 buf_len)
{
	struct beamforming_info	*info;
	u8 res;

	info = GET_BEAMFORM_INFO(adapter);

	_cancel_timer_ex(&info->sounding_timeout_timer);

	res = C2H_SND_TXBF_GET_SND_RESULT(buf) ? _TRUE : _FALSE;
	RTW_INFO("+%s: %s\n", __FUNCTION__, res==_TRUE?"Success":"Fail!");

	rtw_bf_cmd(adapter, BEAMFORMING_CTRL_END_PERIOD, &res, 1, 1);
}

/*
 * Description:
 *	This function is for phydm only
 */
enum beamforming_cap rtw_bf_bfee_get_entry_cap_by_macid(void *mlme, u8 macid)
{
	PADAPTER adapter;
	enum beamforming_cap cap = BEAMFORMING_CAP_NONE;


	adapter = mlme_to_adapter((struct mlme_priv *)mlme);
	cap = _bfee_get_entry_cap_by_macid(adapter, macid);

	return cap;
}

struct beamformer_entry *rtw_bf_bfer_get_entry_by_addr(PADAPTER adapter, u8 *ra)
{
	return _bfer_get_entry_by_addr(adapter, ra);
}

struct beamformee_entry *rtw_bf_bfee_get_entry_by_addr(PADAPTER adapter, u8 *ra)
{
	return _bfee_get_entry_by_addr(adapter, ra);
}

void rtw_bf_get_ndpa_packet(PADAPTER adapter, union recv_frame *precv_frame)
{
	RTW_DBG("+%s\n", __FUNCTION__);
}

u32 rtw_bf_get_report_packet(PADAPTER adapter, union recv_frame *precv_frame)
{
	u32 ret = _SUCCESS;
	struct beamforming_info *info;
	struct beamformee_entry *bfee = NULL;
	u8 *pframe;
	u32 frame_len;
	u8 *ta;
	u8 *frame_body;
	u8 category, action;
	u8 *pMIMOCtrlField, *pCSIMatrix;
	u8 Nc = 0, Nr = 0, CH_W = 0, Ng = 0, CodeBook = 0;
	u16 CSIMatrixLen = 0;


	RTW_INFO("+%s\n", __FUNCTION__);

	info = GET_BEAMFORM_INFO(adapter);
	pframe = precv_frame->u.hdr.rx_data;
	frame_len = precv_frame->u.hdr.len;

	/* Memory comparison to see if CSI report is the same with previous one */
	ta = get_addr2_ptr(pframe);
	bfee = _bfee_get_entry_by_addr(adapter, ta);
	if (!bfee)
		return _FAIL;

	frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	category = frame_body[0];
	action = frame_body[1];

	if ((category == RTW_WLAN_CATEGORY_VHT)
	    && (action == RTW_WLAN_ACTION_VHT_COMPRESSED_BEAMFORMING)) {
		pMIMOCtrlField = pframe + 26;
		Nc = (*pMIMOCtrlField) & 0x7;
		Nr = ((*pMIMOCtrlField) & 0x38) >> 3;
		CH_W =  (((*pMIMOCtrlField) & 0xC0) >> 6);
		Ng = (*(pMIMOCtrlField+1)) & 0x3;
		CodeBook = ((*(pMIMOCtrlField+1)) & 0x4) >> 2;
		/*
		 * 24+(1+1+3)+2
		 * ==> MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)
		 */
		pCSIMatrix = pMIMOCtrlField + 3 + Nc;
		CSIMatrixLen = frame_len - 26 - 3 - Nc;
		info->TargetCSIInfo.bVHT = _TRUE;
	} else if ((category == RTW_WLAN_CATEGORY_HT)
		   && (action == RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING)) {
		pMIMOCtrlField = pframe + 26;
		Nc = (*pMIMOCtrlField) & 0x3;
		Nr = ((*pMIMOCtrlField) & 0xC) >> 2;
		CH_W = ((*pMIMOCtrlField) & 0x10) >> 4;
		Ng = ((*pMIMOCtrlField) & 0x60) >> 5;
		CodeBook = ((*(pMIMOCtrlField+1)) & 0x6) >> 1;
		/*
		 * 24+(1+1+6)+2
		 * ==> MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)
		 */
		pCSIMatrix = pMIMOCtrlField + 6 + Nr;
		CSIMatrixLen = frame_len  - 26 - 6 - Nr;
		info->TargetCSIInfo.bVHT = _FALSE;
	}

	/* Update current CSI report info */
	if ((_TRUE == info->bEnableSUTxBFWorkAround)
	    && (info->TargetSUBFee == bfee)) {
		if ((info->TargetCSIInfo.Nc != Nc) || (info->TargetCSIInfo.Nr != Nr) ||
			(info->TargetCSIInfo.ChnlWidth != CH_W) || (info->TargetCSIInfo.Ng != Ng) ||
			(info->TargetCSIInfo.CodeBook != CodeBook)) {
			info->TargetCSIInfo.Nc = Nc;
			info->TargetCSIInfo.Nr = Nr;
			info->TargetCSIInfo.ChnlWidth = CH_W;
			info->TargetCSIInfo.Ng = Ng;
			info->TargetCSIInfo.CodeBook = CodeBook;

			rtw_bf_cmd(adapter, BEAMFORMING_CTRL_SET_CSI_REPORT, (u8*)&info->TargetCSIInfo, sizeof(struct _RT_CSI_INFO), 1);
		}
	}

	RTW_INFO("%s: pkt type=%d-%d, Nc=%d, Nr=%d, CH_W=%d, Ng=%d, CodeBook=%d\n",
		 __FUNCTION__, category, action, Nc, Nr, CH_W, Ng, CodeBook);

	return ret;
}

u8 rtw_bf_send_vht_gid_mgnt_packet(PADAPTER adapter, u8 *ra, u8 *gid, u8 *position)
{
	/* General */
	struct xmit_priv *xmitpriv;
	struct mlme_priv *mlmepriv;
	struct xmit_frame *pmgntframe;
	/* MISC */
	struct pkt_attrib *attrib;
	struct rtw_ieee80211_hdr *wlanhdr;
	u8 *pframe, *ptr;


	xmitpriv = &adapter->xmitpriv;
	mlmepriv = &adapter->mlmepriv;

	pmgntframe = alloc_mgtxmitframe(xmitpriv);
	if (!pmgntframe)
		return _FALSE;

	/* update attribute */
	attrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, attrib);
	attrib->rate = MGN_6M;
	attrib->bwmode = CHANNEL_WIDTH_20;
	attrib->subtype = WIFI_ACTION;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)pmgntframe->buf_addr + TXDESC_OFFSET;
	wlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	wlanhdr->frame_ctl = 0;
	set_frame_sub_type(pframe, attrib->subtype);
	set_duration(pframe, 0);
	SetFragNum(pframe, 0);
	SetSeqNum(pframe, 0);

	_rtw_memcpy(wlanhdr->addr1, ra, ETH_ALEN);
	_rtw_memcpy(wlanhdr->addr2, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memcpy(wlanhdr->addr3, get_bssid(mlmepriv), ETH_ALEN);

	pframe[24] = RTW_WLAN_CATEGORY_VHT;
	pframe[25] = RTW_WLAN_ACTION_VHT_GROUPID_MANAGEMENT;
	/* Set Membership Status Array */
	ptr = pframe + 26;
	_rtw_memcpy(ptr, gid, 8);
	/* Set User Position Array */
	ptr = pframe + 34;
	_rtw_memcpy(ptr, position, 16);

	attrib->pktlen = 54;
	attrib->last_txcmdsz = attrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return _TRUE;
}

/*
 * Description:
 *	On VHT GID management frame by an MU beamformee.
 */
void rtw_bf_get_vht_gid_mgnt_packet(PADAPTER adapter, union recv_frame *precv_frame)
{
	u8 *pframe;
	u8 *ta, *gid, *position;


	RTW_DBG("+%s\n", __FUNCTION__);

	pframe = precv_frame->u.hdr.rx_data;

	/* Get address by Addr2 */
	ta = get_addr2_ptr(pframe);
	/* Remove signaling TA */
	ta[0] &= 0xFE;

	/* Membership Status Array */
	gid = pframe + 26;
	/* User Position Array */
	position= pframe + 34;

	_bfer_set_entry_gid(adapter, ta, gid, position);
}

void rtw_bf_init(PADAPTER adapter)
{
	struct beamforming_info	*info;


	info = GET_BEAMFORM_INFO(adapter);
	info->beamforming_cap = BEAMFORMING_CAP_NONE;
	info->beamforming_state = BEAMFORMING_STATE_IDLE;
/*
	info->bfee_entry[MAX_BEAMFORMEE_ENTRY_NUM];
	info->bfer_entry[MAX_BEAMFORMER_ENTRY_NUM];
*/
	info->sounding_sequence = 0;
	info->beamformee_su_cnt = 0;
	info->beamformer_su_cnt = 0;
	info->beamformee_su_reg_maping = 0;
	info->beamformer_su_reg_maping = 0;
	info->beamformee_mu_cnt = 0;
	info->beamformer_mu_cnt = 0;
	info->beamformee_mu_reg_maping = 0;
	info->first_mu_bfee_index = 0xFF;
	info->mu_bfer_curidx = 0xFF;
	info->cur_csi_rpt_rate = HALMAC_OFDM24;

	_sounding_init(&info->sounding_info);
	rtw_init_timer(&info->sounding_timer, adapter, _sounding_timer_handler, adapter);
	rtw_init_timer(&info->sounding_timeout_timer, adapter, _sounding_timeout_timer_handler, adapter);

	info->SetHalBFEnterOnDemandCnt = 0;
	info->SetHalBFLeaveOnDemandCnt = 0;
	info->SetHalSoundownOnDemandCnt = 0;

	info->bEnableSUTxBFWorkAround = _TRUE;
	info->TargetSUBFee = NULL;

	info->sounding_running = 0;
}

void rtw_bf_cmd_hdl(PADAPTER adapter, u8 type, u8 *pbuf)
{
	switch (type) {
	case BEAMFORMING_CTRL_ENTER:
		_beamforming_enter(adapter, pbuf);
		break;

	case BEAMFORMING_CTRL_LEAVE:
		if (pbuf == NULL)
			_beamforming_reset(adapter);
		else
			_beamforming_leave(adapter, pbuf);
		break;

	case BEAMFORMING_CTRL_START_PERIOD:
		_sounding_handler(adapter);
		break;

	case BEAMFORMING_CTRL_END_PERIOD:
		_beamforming_sounding_down(adapter, *pbuf);
		break;

	case BEAMFORMING_CTRL_SET_GID_TABLE:
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_SET_GID_TABLE, pbuf);
		break;

	case BEAMFORMING_CTRL_SET_CSI_REPORT:
		rtw_hal_set_hwreg(adapter, HW_VAR_SOUNDING_CSI_REPORT, pbuf);
		break;

	default:
		break;
	}
}

u8 rtw_bf_cmd(PADAPTER adapter, s32 type, u8 *pbuf, s32 size, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &adapter->cmdpriv;
	u8 *wk_buf;
	u8 res = _SUCCESS;


	if (!enqueue) {
		rtw_bf_cmd_hdl(adapter, type, pbuf);
		goto exit;
	}

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	if (pbuf != NULL) {
		wk_buf = rtw_zmalloc(size);
		if (wk_buf == NULL) {
			rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
			rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
			res = _FAIL;
			goto exit;
		}

		_rtw_memcpy(wk_buf, pbuf, size);
	} else {
		wk_buf = NULL;
		size = 0;
	}

	pdrvextra_cmd_parm->ec_id = BEAMFORMING_WK_CID;
	pdrvextra_cmd_parm->type = type;
	pdrvextra_cmd_parm->size = size;
	pdrvextra_cmd_parm->pbuf = wk_buf;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

void rtw_bf_update_attrib(PADAPTER adapter, struct pkt_attrib *attrib, struct sta_info *sta)
{
	if (sta) {
		attrib->txbf_g_id = sta->cmn.bf_info.g_id;
		attrib->txbf_p_aid = sta->cmn.bf_info.p_aid;
	}
}

void rtw_bf_c2h_handler(PADAPTER adapter, u8 id, u8 *buf, u8 buf_len)
{
	switch (id) {
	case CMD_ID_C2H_SND_TXBF:
		_c2h_snd_txbf(adapter, buf, buf_len);
		break;
	}
}

#define toMbps(bytes, secs)	(rtw_division64(bytes >> 17, secs))
void rtw_bf_update_traffic(PADAPTER adapter)
{
	struct beamforming_info	*info;
	struct sounding_info *sounding;
	struct beamformee_entry *bfee;
	struct sta_info *sta;
	u8 bfee_cnt, sounding_idx, i;
	u16 tp[MAX_BEAMFORMEE_ENTRY_NUM] = {0};
	u8 tx_rate[MAX_BEAMFORMEE_ENTRY_NUM] = {0};
	u64 tx_bytes, last_bytes;
	u32 time;
	systime last_timestamp;
	u8 set_timer = _FALSE;


	info = GET_BEAMFORM_INFO(adapter);
	sounding = &info->sounding_info;

	/* Check any bfee exist? */
	bfee_cnt = info->beamformee_su_cnt + info->beamformee_mu_cnt;
	if (bfee_cnt == 0)
		return;

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (_FALSE == bfee->used)
			continue;

		sta = rtw_get_stainfo(&adapter->stapriv, bfee->mac_addr);
		if (!sta) {
			RTW_ERR("%s: Cann't find sta_info for " MAC_FMT "!\n", __FUNCTION__, MAC_ARG(bfee->mac_addr));
			continue;
		}

		last_timestamp = bfee->tx_timestamp;
		last_bytes = bfee->tx_bytes;
		bfee->tx_timestamp = rtw_get_current_time();
		bfee->tx_bytes = sta->sta_stats.tx_bytes;
		if (last_timestamp) {
			if (bfee->tx_bytes >= last_bytes)
				tx_bytes = bfee->tx_bytes - last_bytes;
			else
				tx_bytes = bfee->tx_bytes + (~last_bytes);
			time = rtw_get_time_interval_ms(last_timestamp, bfee->tx_timestamp);
			time = (time > 1000) ? time/1000 : 1;
			tp[i] = toMbps(tx_bytes, time);
			tx_rate[i] = rtw_get_current_tx_rate(adapter, sta);
			RTW_INFO("%s: BFee idx(%d), MadId(%d), TxTP=%lld bytes (%d Mbps), txrate=%d\n",
				 __FUNCTION__, i, bfee->mac_id, tx_bytes, tp[i], tx_rate[i]);
		}
	}

	sounding_idx = phydm_get_beamforming_sounding_info(GET_PDM_ODM(adapter), tp, MAX_BEAMFORMEE_ENTRY_NUM, tx_rate);

	for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
		bfee = &info->bfee_entry[i];
		if (_FALSE == bfee->used) {
			if (sounding_idx & BIT(i))
				RTW_WARN("%s: bfee(%d) not in used but need sounding?!\n", __FUNCTION__, i);
			continue;
		}

		if (sounding_idx & BIT(i)) {
			if (_FALSE == bfee->bApplySounding) {
				bfee->bApplySounding = _TRUE;
				bfee->SoundCnt = 0;
				set_timer = _TRUE;
			}
		} else {
			if (_TRUE == bfee->bApplySounding) {
				bfee->bApplySounding = _FALSE;
				bfee->bDeleteSounding = _TRUE;
				bfee->SoundCnt = 0;
				set_timer = _TRUE;
			}
		}
	}

	if (_TRUE == set_timer) {
		if (SOUNDING_STATE_NONE == info->sounding_info.state) {
			info->sounding_info.state = SOUNDING_STATE_INIT;
			_set_timer(&info->sounding_timer, 0);
		}
	}
}

#else /* !RTW_BEAMFORMING_VERSION_2 */

/*PHYDM_BF - (BEAMFORMING_SUPPORT == 1)*/
u32	rtw_beamforming_get_report_frame(PADAPTER	 Adapter, union recv_frame *precv_frame)
{
	u32	ret = _SUCCESS;

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_struct		*pDM_Odm = &(pHalData->odmpriv);

	ret = beamforming_get_report_frame(pDM_Odm, precv_frame);
	return ret;
}

void	rtw_beamforming_get_ndpa_frame(PADAPTER	 Adapter, union recv_frame *precv_frame)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_struct		*pDM_Odm = &(pHalData->odmpriv);

	beamforming_get_ndpa_frame(pDM_Odm, precv_frame);
}

void	beamforming_wk_hdl(_adapter *padapter, u8 type, u8 *pbuf)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	struct dm_struct		*pDM_Odm = &(pHalData->odmpriv);

	/*(BEAMFORMING_SUPPORT == 1)- for PHYDM beamfoming*/
	switch (type) {
	case BEAMFORMING_CTRL_ENTER: {
		struct sta_info	*psta = (void *)pbuf;
		u16			staIdx = psta->cmn.mac_id;

		beamforming_enter(pDM_Odm, staIdx, adapter_mac_addr(psta->padapter));
		break;
	}
	case BEAMFORMING_CTRL_LEAVE:
		beamforming_leave(pDM_Odm, pbuf);
		break;
	default:
		break;

	}
}

u8	beamforming_wk_cmd(_adapter *padapter, s32 type, u8 *pbuf, s32 size, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	res = _SUCCESS;

	/*20170214 ad_hoc mode and mp_mode not support BF*/
	if ((padapter->registrypriv.mp_mode == 1)
		|| (pmlmeinfo->state == WIFI_FW_ADHOC_STATE))
		return res;

	if (enqueue) {
		u8	*wk_buf;

		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		if (pbuf != NULL) {
			wk_buf = rtw_zmalloc(size);
			if (wk_buf == NULL) {
				rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
				rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
				res = _FAIL;
				goto exit;
			}

			_rtw_memcpy(wk_buf, pbuf, size);
		} else {
			wk_buf = NULL;
			size = 0;
		}

		pdrvextra_cmd_parm->ec_id = BEAMFORMING_WK_CID;
		pdrvextra_cmd_parm->type = type;
		pdrvextra_cmd_parm->size = size;
		pdrvextra_cmd_parm->pbuf = wk_buf;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else
		beamforming_wk_hdl(padapter, type, pbuf);

exit:


	return res;
}

void update_attrib_txbf_info(_adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta)
{
	if (psta) {
		pattrib->txbf_g_id = psta->cmn.bf_info.g_id;
		pattrib->txbf_p_aid = psta->cmn.bf_info.p_aid;
	}
}
#endif /* !RTW_BEAMFORMING_VERSION_2 */

#endif /* CONFIG_BEAMFORMING */
