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

#include <drv_types.h>
#include <hal_data.h>

#ifndef RTW_WNM_DBG
	#define RTW_WNM_DBG	0
#endif
#if RTW_WNM_DBG
	#define RTW_WNM_INFO(fmt, arg...)	\
		RTW_INFO(fmt, arg)
	#define RTW_WNM_DUMP(str, data, len)	\
		RTW_INFO_DUMP(str, data, len)
#else
	#define RTW_WNM_INFO(fmt, arg...) do {} while (0)
	#define RTW_WNM_DUMP(str, data, len) do {} while (0)
#endif

#ifdef CONFIG_RTW_WNM

static u32 wnm_defualt_validity_time = 6000;
static u32 wnm_default_disassoc_time = 5000;
static u32 wnm_disassoc_wait_time = 500;

/* for wifi test, need more validity time to wait scan done */
static u32 wnm_ext_validity_time = 4000;

static void rtw_wmn_btm_cache_update(_adapter *padapter, struct btm_req_hdr *phdr)
{
	struct btm_rpt_cache *pcache = &(padapter->mlmepriv.nb_info.btm_cache);

	pcache->dialog_token = phdr->dialog_token;
	pcache->req_mode = phdr->req_mode;
	pcache->disassoc_timer = le16_to_cpu(phdr->disassoc_timer);

	if (phdr->validity_interval  > 0)
		pcache->validity_interval = phdr->validity_interval;

	pcache->term_duration.id = phdr->term_duration.id;
	pcache->term_duration.len = phdr->term_duration.len;
	pcache->term_duration.tsf = le64_to_cpu(phdr->term_duration.tsf);
	pcache->term_duration.duration =  le16_to_cpu(phdr->term_duration.duration);

	RTW_WNM_INFO("%s: req_mode(0x%02x), disassoc_timer(0x%04x), "
		"validity_interval(0x%02x %s), tsf(0x%llx), duration(0x%02x)\n",
		__func__, pcache->req_mode, pcache->disassoc_timer,
		pcache->validity_interval, (!phdr->validity_interval)?"default":"",
		pcache->term_duration.tsf,
		pcache->term_duration.duration);

	if (pcache->validity_interval > 0) {
		pcache->validity_time = pcache->validity_interval * 100;
	#ifdef CONFIG_RTW_MBO
		if (rtw_mbo_wifi_logo_test(padapter))
			pcache->validity_time += wnm_ext_validity_time;
	#endif
	}

	if (pcache->disassoc_timer > 0) {
		pcache->disassoc_time= pcache->disassoc_timer * 100;
	#ifdef CONFIG_RTW_MBO
		if (rtw_mbo_wifi_logo_test(padapter))
			pcache->disassoc_time += wnm_ext_validity_time;
	#endif
	}

	pcache->req_stime = rtw_get_current_time();

	RTW_WNM_INFO("%s: validity_time=%u, disassoc_time=%u\n",
		__func__, pcache->validity_time, pcache->disassoc_time);
}

static u8 rtw_wnm_btm_candidate_validity(struct btm_rpt_cache *pcache, u8 flag)
{
	u8 is_validity =_TRUE;
	u32 req_validity_time = rtw_get_passing_time_ms(pcache->req_stime);

	if ((flag & BIT(0)) && (req_validity_time > pcache->validity_time))
		is_validity = _FALSE;

	if ((flag & BIT(1)) && (req_validity_time > pcache->disassoc_time))
		is_validity = _FALSE;

	RTW_WNM_INFO("%s : validity=%u, rtime=%u, vtime=%u. dtime=%u\n",
			__func__, is_validity, req_validity_time,
			pcache->validity_time, pcache->disassoc_time);
	return is_validity;
}

u8 rtw_wmn_btm_rsp_reason_decision(_adapter *padapter, u8* req_mode)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 reason = 0;

	if (!rtw_wnm_btm_diff_bss(padapter)) {
		/* Reject - No suitable BSS transition candidates */
		reason = 7;
		goto candidate_remove;
	}

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_chk_flags(padapter, RTW_FT_BTM_ROAM)) {
		/* Accept */
		reason = 0;
		goto under_survey;
	}	
#endif

	if (((*req_mode) & DISASSOC_IMMINENT) == 0) {
		/* Reject - Unspecified reject reason */
		reason = 1;
		goto candidate_remove;
	}	

	if (precvpriv->signal_strength_data.avg_val >= pmlmepriv->roam_rssi_threshold) {
		reason = 1;
		RTW_WNM_INFO("%s : Reject - under high roam rssi(%u, %u) \n",
			__func__, precvpriv->signal_strength_data.avg_val,
			pmlmepriv->roam_rssi_threshold);
		goto candidate_remove;
	}

#ifdef CONFIG_RTW_80211R
under_survey:	
	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY)) {
		RTW_WNM_INFO("%s reject due to _FW_UNDER_SURVEY\n", __func__);
		reason = 1;
	}
#endif

candidate_remove:
	if (reason !=0)
		rtw_wnm_reset_btm_candidate(&pmlmepriv->nb_info);

	return reason;
}

static u32 rtw_wnm_btm_candidates_offset_get(u8* pframe)
{
	u32 offset = 0;

	if (!pframe)
		return 0;

	offset += 7;

	/* BSS Termination Duration check */
	if (wnm_btm_bss_term_inc(pframe))
		offset += 12;

	/* Session Information URL check*/
	if (wnm_btm_ess_disassoc_im(pframe)) {
		/*URL length field + URL variable length*/
		offset = 1 + *(pframe + offset);
	}

	RTW_WNM_INFO("%s : hdr offset=%u\n", __func__, offset);
	return offset;
}

static void rtw_wnm_btm_req_hdr_parsing(u8* pframe, struct btm_req_hdr *phdr)
{
	u8 *pos;
	u32 offset = 0;

	if (!pframe || !phdr)
		return;

	_rtw_memset(phdr, 0, sizeof(struct btm_req_hdr));
	phdr->dialog_token = wnm_btm_dialog_token(pframe);
	phdr->req_mode  = wnm_btm_req_mode(pframe);
	phdr->disassoc_timer = wnm_btm_disassoc_timer(pframe);
	phdr->validity_interval = wnm_btm_valid_interval(pframe);
	if (wnm_btm_bss_term_inc(pframe)) {
		pos = wnm_btm_term_duration_offset(pframe);
		if (*pos == WNM_BTM_TERM_DUR_SUBEID) {
			phdr->term_duration.id = *pos;
			phdr->term_duration.len = *(pos + 1);
			phdr->term_duration.tsf = *((u64*)(pos + 2));
			phdr->term_duration.duration= *((u16*)(pos + 10));
		} else
			RTW_WNM_INFO("%s : invaild BSS Termination Duration content!\n", __func__);
	}

	RTW_WNM_INFO("WNM: req_mode(0x%02x), disassoc_timer(0x%04x), validity_interval(0x%02x)\n",
		phdr->req_mode, phdr->disassoc_timer, phdr->validity_interval);
	if (wnm_btm_bss_term_inc(pframe))
		RTW_WNM_INFO("WNM: tsf(0x%llx), duration(0x%4x)\n",
			phdr->term_duration.tsf, phdr->term_duration.duration);
}

u8 rtw_wnm_btm_reassoc_req(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(pmlmepriv->nb_info);
	u8 breassoc = _FALSE;

	if (_rtw_memcmp(get_my_bssid(&(pmlmeinfo->network)),
		pnb->roam_target_addr, ETH_ALEN)) {
		RTW_WNM_INFO("%s : bss "MAC_FMT" found in roam_target "MAC_FMT"\n",
			__func__, MAC_ARG(get_my_bssid(&(pmlmeinfo->network))),
			MAC_ARG(pnb->roam_target_addr));

		breassoc = _TRUE;
	}

	return breassoc;
}

void rtw_wnm_roam_scan_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (rtw_is_scan_deny(padapter)) 
		RTW_WNM_INFO("%s: roam scan would abort by scan_deny!\n", __func__);

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_chk_flags(padapter, RTW_FT_BTM_ROAM)) {
		pmlmepriv->need_to_roam = _TRUE;
		rtw_set_to_roam(padapter, padapter->registrypriv.max_roaming_times);
		RTW_WNM_INFO("%s : enable roaming\n", __func__);
	}

	rtw_drv_scan_by_self(padapter, RTW_AUTO_SCAN_REASON_ROAM);
#endif
}

static void rtw_wnm_roam_scan(_adapter *padapter)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);

	if (rtw_is_scan_deny(padapter)) {
		_cancel_timer_ex(&pnb->roam_scan_timer);
		_set_timer(&pnb->roam_scan_timer, 1000);
	} else
		rtw_wnm_roam_scan_hdl((void *)padapter);
}

void rtw_wnm_disassoc_chk_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(pmlmepriv->nb_info);

	RTW_WNM_INFO("%s : expired\n", __func__);
	if (pnb->disassoc_waiting <= 0 ) {
		RTW_WNM_INFO("%s : btm roam is interrupted by disassoc\n", __func__);
		return;
	}

	pnb->disassoc_waiting = _FALSE;
	rtw_wnm_roam_scan(padapter);
}

u8 rtw_wnm_try_btm_roam_imnt(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(pmlmepriv->nb_info);
	struct btm_rpt_cache *pcache = &(pnb->btm_cache);
	u8 reason = 0, flag = 0;

	if (!rtw_wnm_btm_preference_cap(padapter)) {
		RTW_WNM_INFO("%s : no btm candidate can be used!\n", __func__);
		return 1;
	}

	flag = BIT(0) | BIT(1);
	if (!rtw_wnm_btm_candidate_validity(pcache, flag))
		return 1;

#ifdef CONFIG_RTW_MBO
	if (!rtw_mbo_wifi_logo_test(padapter)
		&& !(pcache->req_mode & DISASSOC_IMMINENT)) {
		RTW_WNM_INFO("%s : non-disassoc imminet req\n",  __func__);
		return 1;
	}
#endif

	RTW_WNM_INFO("%s : disassoc_waiting(%d)\n", __func__, pnb->disassoc_waiting);
	if (pnb->disassoc_waiting) {
		_cancel_timer_ex(&pnb->disassoc_chk_timer);
		pnb->disassoc_waiting = _FALSE;
		rtw_wnm_roam_scan_hdl((void *)padapter);
	} else if (!pnb->disassoc_waiting)
		RTW_WNM_INFO("%s : waiting for btm roaming start/finish\n", __func__);
	else
		reason = 1;

	return reason;
}

void rtw_wnm_process_btm_req(_adapter *padapter, u8* pframe, u32 frame_len)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);
	struct btm_req_hdr req_hdr;
	u8 *ptr, reason;
	u32 elem_len, offset;

	rtw_wnm_btm_req_hdr_parsing(pframe, &req_hdr);
	offset = rtw_wnm_btm_candidates_offset_get(pframe);
	if (offset == 0)
		return;

	if ((frame_len - offset) <= 15) {
		RTW_INFO("WNM : Reject - no suitable BSS transition candidates!\n");
		rtw_wnm_issue_action(padapter, 
			RTW_WLAN_ACTION_WNM_BTM_RSP, 7, req_hdr.dialog_token);
		return;
	}

	rtw_wmn_btm_cache_update(padapter, &req_hdr);

	ptr = (pframe + offset);
	elem_len = (frame_len - offset);
	rtw_wnm_btm_candidates_survey(padapter, ptr, elem_len, _TRUE);
	reason = rtw_wmn_btm_rsp_reason_decision(padapter, &pframe[3]);

#ifdef CONFIG_RTW_MBO
	/* for wifi-test; AP2 could power-off when BTM-req received */
	if ((reason > 0) && (rtw_mbo_wifi_logo_test(padapter))) {
		_rtw_memcpy(pnb->roam_target_addr, pnb->nb_rpt[0].bssid, ETH_ALEN);
		RTW_WNM_INFO("%s : used report 0 as roam_target_addr(reason=%u)\n",
			__func__, reason);
		reason = 0;
		pnb->preference_en = _TRUE;
		pnb->nb_rpt_valid = _FALSE;
	}
#endif

	rtw_wnm_issue_action(padapter, 
		RTW_WLAN_ACTION_WNM_BTM_RSP, reason, req_hdr.dialog_token);

	if (reason == 0) {
		pnb->disassoc_waiting = _TRUE;
		_set_timer(&pnb->disassoc_chk_timer, wnm_disassoc_wait_time);
	}

}

void rtw_wnm_reset_btm_candidate(struct roam_nb_info *pnb)
{
	pnb->preference_en = _FALSE;
	_rtw_memset(pnb->roam_target_addr, 0, ETH_ALEN);
}

void rtw_wnm_reset_btm_cache(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(pmlmepriv->nb_info);
	struct btm_rpt_cache *pcache = &(pnb->btm_cache);
	u8 flag = 0;

	flag |= BIT(0);
	if (rtw_wnm_btm_candidate_validity(pcache, flag))
		return;

	rtw_wnm_reset_btm_candidate(pnb);
	_rtw_memset(pcache, 0, sizeof(struct btm_rpt_cache));
	pcache->validity_time = wnm_defualt_validity_time;
	pcache->disassoc_time= wnm_default_disassoc_time;

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_chk_flags(padapter, RTW_FT_BTM_ROAM)) {
		pmlmepriv->need_to_roam = _FALSE;
		rtw_set_to_roam(padapter, 0);
		RTW_WNM_INFO("%s : disabled roaming\n", __func__);
	}
#endif
}

void rtw_wnm_reset_btm_state(_adapter *padapter)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);

	pnb->last_nb_rpt_entries = 0;
	pnb->nb_rpt_is_same = _TRUE;
	pnb->nb_rpt_valid = _FALSE;
	pnb->nb_rpt_ch_list_num = 0;
	pnb->disassoc_waiting = -1;
	_rtw_memset(&pnb->nb_rpt, 0, sizeof(pnb->nb_rpt));
	_rtw_memset(&pnb->nb_rpt_ch_list, 0, sizeof(pnb->nb_rpt_ch_list));
	rtw_wnm_reset_btm_cache(padapter);
}

u32 rtw_wnm_btm_rsp_candidates_sz_get(
	_adapter *padapter, u8* pframe, u32 frame_len)
{
	u32 num = 0, sz = 0;
	u8 status;
	u8 *ptr;

	if (!pframe || (frame_len <= 5))
		goto exit;

	status = wnm_btm_rsp_status(pframe);
	if (((status != 0) && (status != 6)) || (frame_len < 23))
		goto exit;

	if (status == 0)
		num = (frame_len - 5 - ETH_ALEN)/18;
	else
		num = (frame_len - 5)/18;
	sz = sizeof(struct wnm_btm_cant) * num;
exit:
	RTW_WNM_INFO("WNM: %u candidates(sz=%u) in BTM rsp\n", num, sz);
	return sz;
}

void rtw_wnm_process_btm_rsp(_adapter *padapter,
	u8* pframe, u32 frame_len, struct btm_rsp_hdr *prsp)
{
	prsp->dialog_token = wnm_btm_dialog_token(pframe);
	prsp->status = wnm_btm_rsp_status(pframe);
	prsp->termination_delay = wnm_btm_rsp_term_delay(pframe);

	if ((pframe == NULL) || (frame_len == 0))
		return;

	prsp->status = *(pframe + 3);
	prsp->termination_delay = *(pframe + 4);

	/* no Target BSSID & Candidate in frame */
	if (frame_len <= 5)
		return;

	/* accept */
	if ((prsp->status == 0) && (frame_len >= 11))
		_rtw_memcpy(prsp->bssid, (pframe + 5), ETH_ALEN);

	/* STA BSS Transition Candidate List provided,
		and at least one NB report exist */
	if (((prsp->status == 0) || (prsp->status == 6)) && (frame_len >= 23)) {
		struct wnm_btm_cant cant;
		u8 *ptr, *pend;
		u32 idx = 0;

		ptr = pframe + 5;
		if (prsp->status == 0)
			ptr += ETH_ALEN;

		pend = ptr + frame_len;
		prsp->candidates_num = 0;
		while (ptr < pend) {
			if (*ptr != RTW_WLAN_ACTION_WNM_NB_RPT_ELEM)
				break;
			_rtw_memset(&cant, 0, sizeof(cant));
			cant.nb_rpt.id = *ptr;
			cant.nb_rpt.len = *(ptr + 1);
			_rtw_memcpy(cant.nb_rpt.bssid, (ptr + 2), ETH_ALEN);
			cant.nb_rpt.bss_info = *((u32 *)(ptr + 8));
			cant.nb_rpt.reg_class = *(ptr + 12);
			cant.nb_rpt.ch_num = *(ptr + 13);
			cant.nb_rpt.phy_type= *(ptr + 14);

			if (*(ptr + 15) == WNM_BTM_CAND_PREF_SUBEID)
				cant.preference = *(ptr + 17);
			ptr = ptr + cant.nb_rpt.len + 2;
			if (prsp->pcandidates) {
				prsp->candidates_num++;
				_rtw_memcpy((prsp->pcandidates + sizeof(cant) * idx), &cant, sizeof(cant));
			}

			idx++;
			RTW_WNM_INFO("WNM: btm rsp candidate bssid("MAC_FMT
				") ,bss_info(0x%04X), reg_class(0x%02X), ch(%d),"
				" phy_type(0x%02X), preference(0x%02X)\n",
				MAC_ARG(cant.nb_rpt.bssid), cant.nb_rpt.bss_info,
				cant.nb_rpt.reg_class, cant.nb_rpt.ch_num,
				cant.nb_rpt.phy_type, cant.preference);
			if ((prsp->pcandidates) && (prsp->candidates_num > 0))
				RTW_WNM_DUMP("WNM candidates: ", prsp->pcandidates,
						(sizeof(struct wnm_btm_cant) * prsp->candidates_num));
		}
	}

}

void rtw_wnm_hdr_init(_adapter *padapter,
	struct xmit_frame *pactionframe, u8 *pmac,
	u8 action, u8 **pcontent)
{
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib *pattrib;
	struct rtw_ieee80211_hdr *pwlanhdr;
	u16 *pfctrl;
	u8 category;

	pattrib = &(pactionframe->attrib);
	update_mgntframe_attrib(padapter, pattrib);
	_rtw_memset(pactionframe->buf_addr, 0, (WLANHDR_OFFSET + TXDESC_OFFSET));

	*pcontent = (u8 *)(pactionframe->buf_addr + TXDESC_OFFSET);
	pwlanhdr = (struct rtw_ieee80211_hdr *)(*pcontent);
	pfctrl = &(pwlanhdr->frame_ctl);
	*(pfctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, pmac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(*pcontent, WIFI_ACTION);

	*pcontent += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	category = RTW_WLAN_CATEGORY_WNM;
	*pcontent = rtw_set_fixed_ie(*pcontent, 1, &(category), &(pattrib->pktlen));
	*pcontent = rtw_set_fixed_ie(*pcontent, 1, &(action), &(pattrib->pktlen));
}

void rtw_wnm_build_btm_req_ies(_adapter *padapter,
	u8 **pframe, struct pkt_attrib *pattrib,
	struct btm_req_hdr *phdr, u8 *purl, u32 url_len,
	u8 *pcandidates, u8 candidate_cnt)
{
	int i;

	*pframe = rtw_set_fixed_ie(*pframe, 1,
				&phdr->dialog_token, &(pattrib->pktlen));
	*pframe = rtw_set_fixed_ie(*pframe, 1,
				&phdr->req_mode, &(pattrib->pktlen));
	*pframe = rtw_set_fixed_ie(*pframe, 2,
				(u8 *)&phdr->disassoc_timer, &(pattrib->pktlen));
	*pframe = rtw_set_fixed_ie(*pframe, 1,
				&phdr->validity_interval, &(pattrib->pktlen));

	if (phdr->req_mode & BSS_TERMINATION_INCLUDED) {
		*pframe = rtw_set_fixed_ie(*pframe, 1,
					&phdr->term_duration.id, &(pattrib->pktlen));
		*pframe = rtw_set_fixed_ie(*pframe, 1,
					&phdr->term_duration.len, &(pattrib->pktlen));
		*pframe = rtw_set_fixed_ie(*pframe, 8,
					(u8 *)&phdr->term_duration.tsf, &(pattrib->pktlen));
		*pframe = rtw_set_fixed_ie(*pframe, 2,
					(u8 *)&phdr->term_duration.duration, &(pattrib->pktlen));
	}

	if ((purl != NULL) && (url_len > 0) &&
		(phdr->req_mode & ESS_DISASSOC_IMMINENT)) {
		*pframe = rtw_set_fixed_ie(*pframe, 1,
					(u8 *)&url_len, &(pattrib->pktlen));
		*pframe = rtw_set_fixed_ie(*pframe,
					url_len, purl, &(pattrib->pktlen));
	}

	if ((pcandidates != NULL) && (candidate_cnt > 0)) {
		for (i=0; i<candidate_cnt; i++) {
			struct wnm_btm_cant *pcandidate = \
				((struct wnm_btm_cant *)pcandidates) + i;
			struct nb_rpt_hdr *prpt = &(pcandidate->nb_rpt);

			*pframe = rtw_set_fixed_ie(*pframe, 1,
						&pcandidate->nb_rpt.id, &(pattrib->pktlen));
			*pframe = rtw_set_fixed_ie(*pframe, 1,
						&pcandidate->nb_rpt.len, &(pattrib->pktlen));
			*pframe = rtw_set_fixed_ie(*pframe, ETH_ALEN,
						pcandidate->nb_rpt.bssid, &(pattrib->pktlen));
			*pframe = rtw_set_fixed_ie(*pframe, 4,
						(u8 *)&pcandidate->nb_rpt.bss_info, &(pattrib->pktlen));
			*pframe = rtw_set_fixed_ie(*pframe, 1,
						&pcandidate->nb_rpt.reg_class, &(pattrib->pktlen));
			*pframe = rtw_set_fixed_ie(*pframe, 1,
						&pcandidate->nb_rpt.ch_num, &(pattrib->pktlen));
			*pframe = rtw_set_fixed_ie(*pframe, 1,
						&pcandidate->nb_rpt.phy_type, &(pattrib->pktlen));
			*pframe = rtw_set_ie(*pframe, WNM_BTM_CAND_PREF_SUBEID, 1,
					(u8 *)&pcandidate->preference, &(pattrib->pktlen));
		}
	}

}

void rtw_wnm_issue_btm_req(_adapter *padapter,
	u8 *pmac, struct btm_req_hdr *phdr, u8 *purl, u32 url_len,
	u8 *pcandidates, u8 candidate_cnt)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 action, *pframe, dialog_token = 0;

	if (!pmac || is_zero_mac_addr(pmac)
		|| is_broadcast_mac_addr(pmac))
		return ;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		return ;

	rtw_wnm_hdr_init(padapter, pmgntframe, pmac,
		RTW_WLAN_ACTION_WNM_BTM_REQ, &pframe);

	pattrib = &(pmgntframe->attrib);
	rtw_wnm_build_btm_req_ies(padapter, &pframe, pattrib,
		phdr, purl, url_len, pcandidates, candidate_cnt);

	if (0) {
		u8 *__p =  (u8 *)(pmgntframe->buf_addr + TXDESC_OFFSET);
		RTW_WNM_DUMP("WNM BTM REQ :", __p, pattrib->pktlen);
	}

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);
	RTW_INFO("WNM: BSS Transition Management Request sent\n");
}

void rtw_wnm_issue_action(_adapter *padapter,
	u8 action, u8 reason, u8 dialog)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct xmit_frame *pmgntframe;
	struct rtw_ieee80211_hdr *pwlanhdr;
	struct pkt_attrib *pattrib;
	u8 category, termination_delay, *pframe, dialog_token = 0;
#ifdef CONFIG_RTW_MBO
	u8 mbo_trans_rej_res = 1;  /* Unspecified reason */
	u8 mbo_notif_req_type ;
#endif
	u16 *fctrl;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		return ;
	
	pattrib = &(pmgntframe->attrib);
	update_mgntframe_attrib(padapter, pattrib);
	_rtw_memset(pmgntframe->buf_addr, 0, (WLANHDR_OFFSET + TXDESC_OFFSET));

	pframe = (u8 *)(pmgntframe->buf_addr + TXDESC_OFFSET);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	category = RTW_WLAN_CATEGORY_WNM;
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	switch (action) {
		case RTW_WLAN_ACTION_WNM_BTM_QUERY:
			dialog_token++;
			pframe = rtw_set_fixed_ie(pframe, 1, &(dialog_token), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 1, &(reason), &(pattrib->pktlen));
			RTW_INFO("WNM: BSS Transition Management Query sent\n");
			break;
		case RTW_WLAN_ACTION_WNM_BTM_RSP:
			dialog_token = dialog;
			termination_delay = 0;
			pframe = rtw_set_fixed_ie(pframe, 1, &(dialog_token), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 1, &(reason), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 1, &(termination_delay), &(pattrib->pktlen));
			if (!reason && !is_zero_mac_addr(pmlmepriv->nb_info.roam_target_addr)) {
				pframe = rtw_set_fixed_ie(pframe, 6, 
					pmlmepriv->nb_info.roam_target_addr, &(pattrib->pktlen));
			}

#ifdef CONFIG_RTW_MBO
			rtw_mbo_build_trans_reject_reason_attr(padapter, 
				&pframe, pattrib, &mbo_trans_rej_res);
#endif

			RTW_INFO("WNM: BSS Transition Management Response sent(reason:%d)\n", reason);			
			break;
		case RTW_WLAN_ACTION_WNM_NOTIF_REQ:
#ifdef CONFIG_RTW_MBO			
			dialog_token++;
			mbo_notif_req_type = WLAN_EID_VENDOR_SPECIFIC;
			pframe = rtw_set_fixed_ie(pframe, 1, &(dialog_token), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 1, &(mbo_notif_req_type), &(pattrib->pktlen));
			rtw_mbo_build_wnm_notification(padapter, &pframe, pattrib);
			RTW_INFO("WNM: Notification request sent\n");
#endif
			break;
		default:
			goto exit;
	}	
	
	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

exit:	
	return;
}

/* argument req_ie@cfg80211_roamed()/cfg80211_connect_result()
	is association request IEs format. if driver used reassoc-req format,
	RSN IE could not be parsed @supplicant process */
void rtw_wnm_update_reassoc_req_ie(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u32 dup_len, offset;
	u8 *pdup;

	if (!pmlmepriv->assoc_req || !pmlmepriv->assoc_req_len)
		return;

	/* total len is assoc req len without Current AP Field*/
	dup_len = pmlmepriv->assoc_req_len - ETH_ALEN;

	/* offset is a len of 80211 header +  capability(2B) + listen interval(2B) */
	offset =  sizeof(struct rtw_ieee80211_hdr_3addr) + 4;

	pdup = rtw_zmalloc(dup_len);
	if (pdup) {
		/* remove Current AP Field @reassoc req IE */
		_rtw_memcpy(pdup, pmlmepriv->assoc_req, offset);
		_rtw_memcpy(pdup + offset, pmlmepriv->assoc_req + offset + ETH_ALEN,
				pmlmepriv->assoc_req_len - offset);
		rtw_buf_update(&pmlmepriv->assoc_req,
			&pmlmepriv->assoc_req_len, pdup, dup_len);
		rtw_mfree(pdup, dup_len);
	}
}
#endif /* CONFIG_RTW_WNM */

#if defined(CONFIG_RTW_WNM) || defined(CONFIG_RTW_80211K)
void rtw_roam_nb_info_init(_adapter *padapter)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);
	struct btm_rpt_cache *pcache = &(pnb->btm_cache);
	
	_rtw_memset(&pnb->nb_rpt, 0, sizeof(pnb->nb_rpt));
	_rtw_memset(&pnb->nb_rpt_ch_list, 0, sizeof(pnb->nb_rpt_ch_list));
	_rtw_memset(&pnb->roam_target_addr, 0, ETH_ALEN);
	pnb->nb_rpt_valid = _FALSE;
	pnb->nb_rpt_ch_list_num = 0;
	pnb->preference_en = _FALSE;
	pnb->nb_rpt_is_same = _TRUE;
	pnb->last_nb_rpt_entries = 0;
	pnb->disassoc_waiting = -1;
#ifdef CONFIG_RTW_WNM
	pnb->features = 0;
	/* pnb->features |= RTW_WNM_FEATURE_BTM_REQ_EN; */

#ifdef CONFIG_PLATFORM_CMAP_INTFS
	pnb->features |= RTW_WNM_FEATURE_BTM_REQ_EN;
#endif

	rtw_init_timer(&pnb->roam_scan_timer, 
		padapter, rtw_wnm_roam_scan_hdl, 
		padapter);
	rtw_init_timer(&pnb->disassoc_chk_timer,
		padapter, rtw_wnm_disassoc_chk_hdl,
		padapter);

	_rtw_memset(pcache, 0, sizeof(struct btm_rpt_cache));
	pcache->validity_time = wnm_defualt_validity_time;
	pcache->disassoc_time= wnm_default_disassoc_time ;
#endif
}

u8 rtw_roam_nb_scan_list_set(
	_adapter *padapter, struct sitesurvey_parm *pparm)
{
	u8 ret = _FALSE;
	u32 i;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(pmlmepriv->nb_info);

#ifdef CONFIG_RTW_80211R
	if (!rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE)
		&& !rtw_ft_chk_flags(padapter, RTW_FT_BTM_ROAM))
		return ret;
#endif

	if (!pmlmepriv->need_to_roam)
		return ret;

	if ((!pmlmepriv->nb_info.nb_rpt_valid) || (!pnb->nb_rpt_ch_list_num))
		return ret;

	if (!pparm)
		return ret;

	rtw_init_sitesurvey_parm(padapter, pparm);
	if (rtw_roam_busy_scan(padapter, pnb)) {
		pparm->ch_num = 1;
		pparm->ch[pmlmepriv->ch_cnt].hw_value = 
			pnb->nb_rpt_ch_list[pmlmepriv->ch_cnt].hw_value;
		pmlmepriv->ch_cnt++;
		ret = _TRUE;

		RTW_WNM_INFO("%s: ch_cnt=%u, (%u)hw_value=%u\n",
			__func__, pparm->ch_num, pmlmepriv->ch_cnt,
			pparm->ch[pmlmepriv->ch_cnt].hw_value);

		if (pmlmepriv->ch_cnt == pnb->nb_rpt_ch_list_num) {
			pmlmepriv->nb_info.nb_rpt_valid = _FALSE;
			pmlmepriv->ch_cnt = 0;
		}
		goto set_bssid_list;
	}

	pparm->ch_num = (pnb->nb_rpt_ch_list_num > RTW_CHANNEL_SCAN_AMOUNT)?
		(RTW_CHANNEL_SCAN_AMOUNT):(pnb->nb_rpt_ch_list_num);
	for (i=0; i<pparm->ch_num; i++) {
		pparm->ch[i].hw_value = pnb->nb_rpt_ch_list[i].hw_value;
		pparm->ch[i].flags = RTW_IEEE80211_CHAN_PASSIVE_SCAN;
	}

	pmlmepriv->nb_info.nb_rpt_valid = _FALSE;
	pmlmepriv->ch_cnt = 0;		
	ret = _TRUE;

set_bssid_list:
	rtw_set_802_11_bssid_list_scan(padapter, pparm);
	return ret;
}

static u8 rtw_wnm_nb_elem_parsing(
	u8* pdata, u32 data_len, u8 from_btm, 
	u32 *nb_rpt_num, u8 *nb_rpt_is_same,
	struct roam_nb_info *pnb, struct wnm_btm_cant *pcandidates)
{
	u8 bfound = _FALSE, ret = _SUCCESS;
	u8 *ptr, *pend, *op;
	u32 elem_len, subelem_len, op_len;
	u32 i, nb_rpt_entries = 0;
	struct nb_rpt_hdr *pie;
	struct wnm_btm_cant *pcandidate;

	if ((!pdata) || (!pnb))
		return _FAIL;

	if ((from_btm) && (!pcandidates))
		return _FAIL;

	ptr = pdata;
	pend = ptr + data_len;
	elem_len = data_len;
	subelem_len = (u32)*(pdata+1);

	for (i=0; i < RTW_MAX_NB_RPT_NUM; i++) {
		if (((ptr + 7) > pend) || (elem_len < subelem_len)) 
			break;

		if (*ptr != RTW_WLAN_ACTION_WNM_NB_RPT_ELEM) {
			RTW_WNM_INFO("WNM: end of data(0x%2x)!\n", *ptr);
			break;
		}

		pie = (struct nb_rpt_hdr *)ptr;		
		if (from_btm) {
			op = rtw_get_ie((u8 *)(ptr+15), 
				WNM_BTM_CAND_PREF_SUBEID, 
				&op_len, (subelem_len - 15));
		}

		ptr = (u8 *)(ptr + subelem_len + 2);
		elem_len -= (subelem_len +2);
		subelem_len = *(ptr+1);
		if (from_btm) {
			pcandidate = (pcandidates + i);
			_rtw_memcpy(&pcandidate->nb_rpt, pie, sizeof(struct nb_rpt_hdr));
			if (op && (op_len !=0)) {
				pcandidate->preference = *(op + 2);
				bfound = _TRUE;
			} else
				pcandidate->preference = 0;

			RTW_WNM_INFO("WNM: preference check bssid("MAC_FMT
				") ,bss_info(0x%04X), reg_class(0x%02X), ch(%d),"
				" phy_type(0x%02X), preference(0x%02X)\n",
				MAC_ARG(pcandidate->nb_rpt.bssid), pcandidate->nb_rpt.bss_info, 
				pcandidate->nb_rpt.reg_class, pcandidate->nb_rpt.ch_num, 
				pcandidate->nb_rpt.phy_type, pcandidate->preference);
		} else {
			if (_rtw_memcmp(&pnb->nb_rpt[i], pie, sizeof(struct nb_rpt_hdr)) == _FALSE)
				*nb_rpt_is_same = _FALSE;
			_rtw_memcpy(&pnb->nb_rpt[i], pie, sizeof(struct nb_rpt_hdr));
		}
		nb_rpt_entries++;			
	} 

	if (from_btm) 
		pnb->preference_en = (bfound)?_TRUE:_FALSE; 

	*nb_rpt_num = nb_rpt_entries;
	return ret;
}	

/* selection sorting based on preference value
 * IN : 		nb_rpt_entries - candidate num
 * IN/OUT :	pcandidates	- candidate list
 * return : TRUE - means pcandidates is updated.  
 */
static u8 rtw_wnm_candidates_sorting(
	u32 nb_rpt_entries, struct wnm_btm_cant *pcandidates)
{
	u8 updated = _FALSE;
	u32 i, j, pos;
	struct wnm_btm_cant swap;
	struct wnm_btm_cant *pcant_1, *pcant_2;

	if ((!nb_rpt_entries) || (!pcandidates))
		return updated;

	for (i=0; i < (nb_rpt_entries - 1); i++) {
		pos = i;
		for (j=(i + 1); j < nb_rpt_entries; j++) {
			pcant_1 = pcandidates+pos;
			pcant_2 = pcandidates+j;
			if ((pcant_1->preference) < (pcant_2->preference))
				pos = j;
		}

		if (pos != i) {
			updated = _TRUE;
			_rtw_memcpy(&swap, (pcandidates+i), sizeof(struct wnm_btm_cant));
			_rtw_memcpy((pcandidates+i), (pcandidates+pos), sizeof(struct wnm_btm_cant));
			_rtw_memcpy((pcandidates+pos), &swap, sizeof(struct wnm_btm_cant));
		}
	}	
	return updated;
}	

static void rtw_wnm_nb_info_update(
	u32 nb_rpt_entries, u8 from_btm, 
	struct roam_nb_info *pnb, struct wnm_btm_cant *pcandidates, 
	u8 *nb_rpt_is_same)
{
	u8 is_found;
	u32 i, j;
	struct wnm_btm_cant *pcand;

	if (!pnb)
		return;

	pnb->nb_rpt_ch_list_num = 0;
	for (i=0; i<nb_rpt_entries; i++) {
		is_found = _FALSE;
		if (from_btm) {
			pcand = (pcandidates+i);
			if (_rtw_memcmp(&pnb->nb_rpt[i], &pcand->nb_rpt,
					sizeof(struct nb_rpt_hdr)) == _FALSE)
				*nb_rpt_is_same = _FALSE;
			_rtw_memcpy(&pnb->nb_rpt[i], &pcand->nb_rpt, sizeof(struct nb_rpt_hdr));
		}

		RTW_WNM_INFO("WNM: bssid(" MAC_FMT
			") , bss_info(0x%04X), reg_class(0x%02X), ch_num(%d), phy_type(0x%02X)\n",
			MAC_ARG(pnb->nb_rpt[i].bssid), pnb->nb_rpt[i].bss_info, 
			pnb->nb_rpt[i].reg_class, pnb->nb_rpt[i].ch_num, 
			pnb->nb_rpt[i].phy_type);

		if (pnb->nb_rpt[i].ch_num == 0)
			continue;

		for (j=0; j<nb_rpt_entries; j++) {
			if (pnb->nb_rpt[i].ch_num == pnb->nb_rpt_ch_list[j].hw_value) {
				is_found = _TRUE;
				break;
			}
		}
							
		if (!is_found) {
			pnb->nb_rpt_ch_list[pnb->nb_rpt_ch_list_num].hw_value = pnb->nb_rpt[i].ch_num;
				pnb->nb_rpt_ch_list_num++;
		}
	}
}

static void rtw_wnm_btm_candidate_select(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);
	struct wlan_network *pnetwork;
	u8 bfound = _FALSE;
	u8 ignore_currrent = _FALSE;
	u32 i;

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_chk_flags(padapter, RTW_FT_BTM_ROAM))
		ignore_currrent = _TRUE;
#endif

	for (i = 0; i < pnb->last_nb_rpt_entries; i++) {
		if (ignore_currrent &&
			(_rtw_memcmp(pnb->nb_rpt[i].bssid,\
				padapter->mlmepriv.cur_network.network.MacAddress, ETH_ALEN))) {
			RTW_WNM_INFO("WNM : ignore candidate "MAC_FMT" for it's connected(%u)!\n",
					MAC_ARG(pnb->nb_rpt[i].bssid), i);	
			continue;
		}
		
		pnetwork = rtw_find_network(
				&(pmlmepriv->scanned_queue), 
				pnb->nb_rpt[i].bssid);

		if (pnetwork) {
			bfound = _TRUE;
			break;
		}
	}

	if (bfound) {
		_rtw_memcpy(pnb->roam_target_addr, pnb->nb_rpt[i].bssid, ETH_ALEN);
		RTW_INFO("WNM : select btm entry(%d) - %s("MAC_FMT", ch:%u) rssi:%d\n"
			, i
			, pnetwork->network.Ssid.Ssid
			, MAC_ARG(pnetwork->network.MacAddress)
			, pnetwork->network.Configuration.DSConfig
			, (int)pnetwork->network.Rssi);
	} else 
		_rtw_memset(pnb->roam_target_addr,0, ETH_ALEN);
}

u32 rtw_wnm_btm_candidates_survey(
	_adapter *padapter, u8* pframe, u32 elem_len, u8 from_btm)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);
	struct wnm_btm_cant *pcandidate_list = NULL;
	u8 nb_rpt_is_same = _TRUE;
	u32	ret = _FAIL;
	u32 nb_rpt_entries = 0;	

	if (from_btm) {
		u32 mlen = sizeof(struct wnm_btm_cant) * RTW_MAX_NB_RPT_NUM;
		pcandidate_list = (struct wnm_btm_cant *)rtw_malloc(mlen);
		if (pcandidate_list == NULL) 
			goto exit;				
	}

	/*clean the status set last time*/
	_rtw_memset(&pnb->nb_rpt_ch_list, 0, sizeof(pnb->nb_rpt_ch_list));
	pnb->nb_rpt_valid = _FALSE;
	if (!rtw_wnm_nb_elem_parsing(
			pframe, elem_len, from_btm, 
			&nb_rpt_entries, &nb_rpt_is_same,
			pnb, pcandidate_list))
		goto exit;

	if (nb_rpt_entries != 0) {
		if ((from_btm) && (rtw_wnm_btm_preference_cap(padapter)))
			rtw_wnm_candidates_sorting(nb_rpt_entries, pcandidate_list);

		rtw_wnm_nb_info_update(
			nb_rpt_entries, from_btm, 
			pnb, pcandidate_list, &nb_rpt_is_same);
	}

	RTW_WNM_INFO("nb_rpt_is_same = %d, nb_rpt_entries = %d, last_nb_rpt_entries = %d\n",
		nb_rpt_is_same, nb_rpt_entries, pnb->last_nb_rpt_entries);
	if ((nb_rpt_is_same == _TRUE) && (nb_rpt_entries == pnb->last_nb_rpt_entries))
		pnb->nb_rpt_is_same = _TRUE;
	else {
		pnb->nb_rpt_is_same = _FALSE;
		pnb->last_nb_rpt_entries = nb_rpt_entries;
	}

	if ((from_btm) && (nb_rpt_entries != 0))
		rtw_wnm_btm_candidate_select(padapter);
	
	pnb->nb_rpt_valid = _TRUE;
	ret = _SUCCESS;

exit:
	if (from_btm && pcandidate_list)
		rtw_mfree((u8 *)pcandidate_list, sizeof(struct wnm_btm_cant) * RTW_MAX_NB_RPT_NUM);
	
	return ret;
}

#endif /*defined(CONFIG_RTW_WNM) || defined(CONFIG_RTW_80211K) */

