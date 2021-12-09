// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include <rtw_debug.h>
#include <asm/unaligned.h>

void init_mlme_ap_info(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	spin_lock_init(&pmlmepriv->bcn_update_lock);

	/* for ACL */
	INIT_LIST_HEAD(&pacl_list->acl_node_q.queue);
	spin_lock_init(&pacl_list->acl_node_q.lock);

	/* pmlmeext->bstart_bss = false; */

	start_ap_mode(padapter);
}

void free_mlme_ap_info(struct adapter *padapter)
{
	struct sta_info *psta = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	/* stop_ap_mode(padapter); */

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	rtw_sta_flush(padapter);

	pmlmeinfo->state = _HW_STATE_NOLINK_;

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo(padapter);

	/* free bc/mc sta_info */
	psta = rtw_get_bcmc_stainfo(padapter);
	rtw_free_stainfo(padapter, psta);
}

static void update_BCNTIM(struct adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork_mlmeext = &pmlmeinfo->network;
	unsigned char *pie = pnetwork_mlmeext->ies;

	/* update TIM IE */
	u8 *p, *dst_ie, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
	__le16 tim_bitmap_le;
	uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

	tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);

	p = rtw_get_ie(pie + _FIXED_IE_LENGTH_,
		       WLAN_EID_TIM,
		       &tim_ielen,
		       pnetwork_mlmeext->ie_length - _FIXED_IE_LENGTH_
	);
	if (p && tim_ielen > 0) {
		tim_ielen += 2;

		premainder_ie = p + tim_ielen;

		tim_ie_offset = (signed int)(p - pie);

		remainder_ielen = pnetwork_mlmeext->ie_length - tim_ie_offset - tim_ielen;

		/* append TIM IE from dst_ie offset */
		dst_ie = p;
	} else {
		tim_ielen = 0;

		/* calculate head_len */
		offset = _FIXED_IE_LENGTH_;

		/* get ssid_ie len */
		p = rtw_get_ie(pie + _BEACON_IE_OFFSET_,
			       WLAN_EID_SSID,
			       &tmp_len,
			       (pnetwork_mlmeext->ie_length - _BEACON_IE_OFFSET_)
		);
		if (p)
			offset += tmp_len + 2;

		/*  get supported rates len */
		p = rtw_get_ie(pie + _BEACON_IE_OFFSET_,
			       WLAN_EID_SUPP_RATES, &tmp_len,
			       (pnetwork_mlmeext->ie_length - _BEACON_IE_OFFSET_)
		);
		if (p)
			offset += tmp_len + 2;

		/* DS Parameter Set IE, len =3 */
		offset += 3;

		premainder_ie = pie + offset;

		remainder_ielen = pnetwork_mlmeext->ie_length - offset - tim_ielen;

		/* append TIM IE from offset */
		dst_ie = pie + offset;
	}

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie && premainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	*dst_ie++ = WLAN_EID_TIM;

	if ((pstapriv->tim_bitmap & 0xff00) && (pstapriv->tim_bitmap & 0x00fe))
		tim_ielen = 5;
	else
		tim_ielen = 4;

	*dst_ie++ = tim_ielen;

	*dst_ie++ = 0;/* DTIM count */
	*dst_ie++ = 1;/* DTIM period */

	if (pstapriv->tim_bitmap & BIT(0))/* for bc/mc frames */
		*dst_ie++ = BIT(0);/* bitmap ctrl */
	else
		*dst_ie++ = 0;

	if (tim_ielen == 4) {
		__le16 pvb;

		if (pstapriv->tim_bitmap & 0xff00)
			pvb = cpu_to_le16(pstapriv->tim_bitmap >> 8);
		else
			pvb = tim_bitmap_le;

		*dst_ie++ = le16_to_cpu(pvb);

	} else if (tim_ielen == 5) {
		memcpy(dst_ie, &tim_bitmap_le, 2);
		dst_ie += 2;
	}

	/* copy remainder IE */
	if (pbackup_remainder_ie) {
		memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		kfree(pbackup_remainder_ie);
	}

	offset =  (uint)(dst_ie - pie);
	pnetwork_mlmeext->ie_length = offset + remainder_ielen;
}

static u8 chk_sta_is_alive(struct sta_info *psta)
{
	sta_update_last_rx_pkts(psta);

	return true;
}

void expire_timeout_chk(struct adapter *padapter)
{
	struct list_head *phead, *plist, *tmp;
	u8 updated = false;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	spin_lock_bh(&pstapriv->auth_list_lock);

	phead = &pstapriv->auth_list;
	/* check auth_queue */
	list_for_each_safe(plist, tmp, phead) {
		psta = list_entry(plist, struct sta_info, auth_list);

		if (psta->expire_to > 0) {
			psta->expire_to--;
			if (psta->expire_to == 0) {
				list_del_init(&psta->auth_list);
				pstapriv->auth_list_cnt--;

				spin_unlock_bh(&pstapriv->auth_list_lock);

				rtw_free_stainfo(padapter, psta);

				spin_lock_bh(&pstapriv->auth_list_lock);
			}
		}
	}

	spin_unlock_bh(&pstapriv->auth_list_lock);
	psta = NULL;

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;
	/* check asoc_queue */
	list_for_each_safe(plist, tmp, phead) {
		psta = list_entry(plist, struct sta_info, asoc_list);
		if (chk_sta_is_alive(psta) || !psta->expire_to) {
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
			psta->under_exist_checking = 0;
		} else {
			if (psta->expire_to > 0)
				psta->expire_to--;
		}

		if (psta->expire_to == 0) {
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			if (padapter->registrypriv.wifi_spec == 1) {
				psta->expire_to = pstapriv->expire_to;
				continue;
			}

			if (psta->state & WIFI_SLEEP_STATE) {
				if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
					/* to check if alive by another methods */
					/* if station is at ps mode. */
					psta->expire_to = pstapriv->expire_to;
					psta->state |= WIFI_STA_ALIVE_CHK_STATE;

					/* to update bcn with tim_bitmap for this station */
					pstapriv->tim_bitmap |= BIT(psta->aid);
					update_beacon(padapter, WLAN_EID_TIM, NULL, true);

					if (!pmlmeext->active_keep_alive_check)
						continue;
				}
			}
			if (pmlmeext->active_keep_alive_check) {
				int stainfo_offset;

				stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
				if (stainfo_offset_valid(stainfo_offset))
					chk_alive_list[chk_alive_num++] = stainfo_offset;

				continue;
			}
			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta(padapter, psta, false, WLAN_REASON_DEAUTH_LEAVING);
		} else {
			/* TODO: Aging mechanism to digest frames in sleep_q to */
			/* avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME / pstapriv->asoc_list_cnt)
				&& padapter->xmitpriv.free_xmitframe_cnt < ((
					NR_XMITFRAME / pstapriv->asoc_list_cnt
				) / 2)
			)
				wakeup_sta_to_xmit(padapter, psta);
		}
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);

	if (chk_alive_num) {
		u8 backup_oper_channel = 0;
		struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

		/* switch to correct channel of current network  before issue keep-alive frames */
		if (rtw_get_oper_ch(padapter) != pmlmeext->cur_channel) {
			backup_oper_channel = rtw_get_oper_ch(padapter);
			SelectChannel(padapter, pmlmeext->cur_channel);
		}

		/* issue null data to check sta alive*/
		for (i = 0; i < chk_alive_num; i++) {
			int ret = _FAIL;

			psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);
			if (!(psta->state & _FW_LINKED))
				continue;

			if (psta->state & WIFI_SLEEP_STATE)
				ret = issue_nulldata(padapter, psta->hwaddr, 0, 1, 50);
			else
				ret = issue_nulldata(padapter, psta->hwaddr, 0, 3, 50);

			psta->keep_alive_trycnt++;
			if (ret == _SUCCESS) {
				psta->expire_to = pstapriv->expire_to;
				psta->keep_alive_trycnt = 0;
				continue;
			} else if (psta->keep_alive_trycnt <= 3) {
				psta->expire_to = 1;
				continue;
			}

			psta->keep_alive_trycnt = 0;
			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (list_empty(&psta->asoc_list) == false) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, false,
						      WLAN_REASON_DEAUTH_LEAVING);
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);
		}

		if (backup_oper_channel > 0) /* back to the original operation channel */
			SelectChannel(padapter, backup_oper_channel);
	}

	associated_clients_update(padapter, updated);
}

void add_RATid(struct adapter *padapter, struct sta_info *psta, u8 rssi_level)
{
	unsigned char sta_band = 0, shortGIrate = false;
	unsigned int tx_ra_bitmap = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex
		*pcur_network = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;

	if (!psta)
		return;

	if (!(psta->state & _FW_LINKED))
		return;

	rtw_hal_update_sta_rate_mask(padapter, psta);
	tx_ra_bitmap = psta->ra_mask;

	shortGIrate = query_ra_short_GI(psta);

	if (pcur_network->configuration.ds_config > 14) {
		sta_band |= WIRELESS_INVALID;
	} else {
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_24N;

		if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11G;

		if (tx_ra_bitmap & 0x0f)
			sta_band |= WIRELESS_11B;
	}

	psta->wireless_mode = sta_band;
	psta->raid = networktype_to_raid_ex(padapter, psta);

	if (psta->aid < NUM_STA) {
		u8 arg[4] = {0};

		arg[0] = psta->mac_id;
		arg[1] = psta->raid;
		arg[2] = shortGIrate;
		arg[3] = psta->init_rate;

		rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, rssi_level);
	}
}

void update_bmc_sta(struct adapter *padapter)
{
	unsigned char network_type;
	int supportRateNum = 0;
	unsigned int tx_ra_bitmap = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex
		*pcur_network = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	struct sta_info *psta = rtw_get_bcmc_stainfo(padapter);

	if (psta) {
		psta->aid = 0;/* default set to 0 */
		/* psta->mac_id = psta->aid+4; */
		psta->mac_id = psta->aid + 1;/* mac_id = 1 for bc/mc stainfo */

		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

		psta->qos_option = 0;
		psta->htpriv.ht_option = false;

		psta->ieee8021x_blocked = 0;

		memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		/* psta->dot118021XPrivacy = _NO_PRIVACY_;//!!! remove it, because it has been set before this. */

		/* prepare for add_RATid */
		supportRateNum = rtw_get_rateset_len((u8 *)&pcur_network->supported_rates);
		network_type = rtw_check_network_type((u8 *)&pcur_network->supported_rates,
						      supportRateNum,
						      pcur_network->configuration.ds_config
		);
		if (is_supported_tx_cck(network_type)) {
			network_type = WIRELESS_11B;
		} else if (network_type == WIRELESS_INVALID) { /*  error handling */

			if (pcur_network->configuration.ds_config > 14)
				network_type = WIRELESS_INVALID;
			else
				network_type = WIRELESS_11B;
		}
		update_sta_basic_rate(psta, network_type);
		psta->wireless_mode = network_type;

		rtw_hal_update_sta_rate_mask(padapter, psta);
		tx_ra_bitmap = psta->ra_mask;

		psta->raid = networktype_to_raid_ex(padapter, psta);

		/* ap mode */
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, true);

		/* if (pHalData->fw_ractrl == true) */
		{
			u8 arg[4] = {0};

			arg[0] = psta->mac_id;
			arg[1] = psta->raid;
			arg[2] = 0;
			arg[3] = psta->init_rate;

			rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, 0);
		}

		rtw_sta_media_status_rpt(padapter, psta, 1);

		spin_lock_bh(&psta->lock);
		psta->state = _FW_LINKED;
		spin_unlock_bh(&psta->lock);

	}
}

/* notes: */
/* AID: 1~MAX for sta and 0 for bc/mc in ap/adhoc mode */
/* MAC_ID = AID+1 for sta in ap/adhoc mode */
/* MAC_ID = 1 for bc/mc for sta/ap/adhoc */
/* MAC_ID = 0 for bssid for sta/ap/adhoc */
/* CAM_ID = 0~3 for default key, cmd_id =macid + 3, macid =aid+1; */

void update_sta_info_apmode(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct ht_priv *phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv *phtpriv_sta = &psta->htpriv;
	u8 cur_ldpc_cap = 0, cur_stbc_cap = 0, cur_beamform_cap = 0;
	/* set intf_tag to if1 */
	/* psta->intf_tag = 0; */

	/* psta->mac_id = psta->aid+4; */
	/* psta->mac_id = psta->aid+1;//alloc macid when call rtw_alloc_stainfo(), */
	/* release macid when call rtw_free_stainfo() */

	/* ap mode */
	rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, true);

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->ieee8021x_blocked = true;
	else
		psta->ieee8021x_blocked = false;

	/* update sta's cap */

	/* ERP */
	VCS_update(padapter, psta);

	/* HT related cap */
	if (phtpriv_sta->ht_option) {
		/* check if sta supports rx ampdu */
		phtpriv_sta->ampdu_enable = phtpriv_ap->ampdu_enable;

		phtpriv_sta->rx_ampdu_min_spacing = (
			phtpriv_sta->ht_cap.ampdu_params_info & IEEE80211_HT_CAP_AMPDU_DENSITY
		) >> 2;

		/*  bwmode */
		if ((
			phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info
		) & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH))
			psta->bw_mode = CHANNEL_WIDTH_40;
		else
			psta->bw_mode = CHANNEL_WIDTH_20;

		if (pmlmeext->cur_bwmode < psta->bw_mode)
			psta->bw_mode = pmlmeext->cur_bwmode;

		phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;

		/* check if sta support s Short GI 20M */
		if ((
			phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info
		) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
			phtpriv_sta->sgi_20m = true;

		/* check if sta support s Short GI 40M */
		if ((
			phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info
		) & cpu_to_le16(IEEE80211_HT_CAP_SGI_40)) {
			if (psta->bw_mode == CHANNEL_WIDTH_40) /* according to psta->bw_mode */
				phtpriv_sta->sgi_40m = true;
			else
				phtpriv_sta->sgi_40m = false;
		}

		psta->qos_option = true;

		/*  B0 Config LDPC Coding Capability */
		if (TEST_FLAG(phtpriv_ap->ldpc_cap, LDPC_HT_ENABLE_TX) &&
			      GET_HT_CAPABILITY_ELE_LDPC_CAP((u8 *)(&phtpriv_sta->ht_cap)))
			SET_FLAG(cur_ldpc_cap, (LDPC_HT_ENABLE_TX | LDPC_HT_CAP_TX));

		/*  B7 B8 B9 Config STBC setting */
		if (TEST_FLAG(phtpriv_ap->stbc_cap, STBC_HT_ENABLE_TX) &&
			      GET_HT_CAPABILITY_ELE_RX_STBC((u8 *)(&phtpriv_sta->ht_cap)))
			SET_FLAG(cur_stbc_cap, (STBC_HT_ENABLE_TX | STBC_HT_CAP_TX));
	} else {
		phtpriv_sta->ampdu_enable = false;

		phtpriv_sta->sgi_20m = false;
		phtpriv_sta->sgi_40m = false;
		psta->bw_mode = CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	phtpriv_sta->ldpc_cap = cur_ldpc_cap;
	phtpriv_sta->stbc_cap = cur_stbc_cap;
	phtpriv_sta->beamform_cap = cur_beamform_cap;

	/* Rx AMPDU */
	send_delba(padapter, 0, psta->hwaddr);/*  recipient */

	/* TX AMPDU */
	send_delba(padapter, 1, psta->hwaddr);/* originator */
	phtpriv_sta->agg_enable_bitmap = 0x0;/* reset */
	phtpriv_sta->candidate_tid_bitmap = 0x0;/* reset */

	update_ldpc_stbc_cap(psta);

	/* todo: init other variables */

	memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

	/* add ratid */
	/* add_RATid(padapter, psta);//move to ap_sta_info_defer_update() */

	spin_lock_bh(&psta->lock);
	psta->state |= _FW_LINKED;
	spin_unlock_bh(&psta->lock);
}

static void update_ap_info(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex
		*pnetwork = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct ht_priv *phtpriv_ap = &pmlmepriv->htpriv;

	psta->wireless_mode = pmlmeext->cur_wireless_mode;

	psta->bssratelen = rtw_get_rateset_len(pnetwork->supported_rates);
	memcpy(psta->bssrateset, pnetwork->supported_rates, psta->bssratelen);

	/* HT related cap */
	if (phtpriv_ap->ht_option) {
		/* check if sta supports rx ampdu */
		/* phtpriv_ap->ampdu_enable = phtpriv_ap->ampdu_enable; */

		/* check if sta support s Short GI 20M */
		if ((phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
			phtpriv_ap->sgi_20m = true;

		/* check if sta support s Short GI 40M */
		if ((phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_40))
			phtpriv_ap->sgi_40m = true;

		psta->qos_option = true;
	} else {
		phtpriv_ap->ampdu_enable = false;

		phtpriv_ap->sgi_20m = false;
		phtpriv_ap->sgi_40m = false;
	}

	psta->bw_mode = pmlmeext->cur_bwmode;
	phtpriv_ap->ch_offset = pmlmeext->cur_ch_offset;

	phtpriv_ap->agg_enable_bitmap = 0x0;/* reset */
	phtpriv_ap->candidate_tid_bitmap = 0x0;/* reset */

	memcpy(&psta->htpriv, &pmlmepriv->htpriv, sizeof(struct ht_priv));
}

static void update_hw_ht_param(struct adapter *padapter)
{
	unsigned char max_AMPDU_len;
	unsigned char min_MPDU_spacing;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	/* handle A-MPDU parameter field
	 *
	 *	AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
	 *	AMPDU_para [4:2]:Min MPDU Start Spacing
	 */
	max_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;

	min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) >> 2;

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MIN_SPACE, (u8 *)(&min_MPDU_spacing));

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));

	/*  */
	/*  Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS = (le16_to_cpu(
		pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info
	) & 0x0C) >> 2;

	/*  */
	/*  Config current HT Protection mode. */
	/*  */
	/* pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3; */
}

void start_bss_network(struct adapter *padapter)
{
	u8 *p;
	u8 val8, cur_channel, cur_bwmode, cur_ch_offset;
	u16 bcn_interval;
	u32 acparm;
	int	ie_len;
	struct registry_priv  *pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct wlan_bssid_ex
		*pnetwork = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork_mlmeext = &(pmlmeinfo->network);
	struct HT_info_element *pht_info = NULL;
	u8 cbw40_enable = 0;

	bcn_interval = (u16)pnetwork->configuration.beacon_period;
	cur_channel = pnetwork->configuration.ds_config;
	cur_bwmode = CHANNEL_WIDTH_20;
	cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	/* check if there is wps ie, */
	/* if there is wpsie in beacon, the hostapd will update beacon twice when stating hostapd, */
	/* and at first time the security ie (RSN/WPA IE) will not include in beacon. */
	if (!rtw_get_wps_ie(pnetwork->ies + _FIXED_IE_LENGTH_,
			    pnetwork->ie_length - _FIXED_IE_LENGTH_, NULL, NULL))
		pmlmeext->bstart_bss = true;

	/* todo: update wmm, ht cap */
	/* pmlmeinfo->WMM_enable; */
	/* pmlmeinfo->HT_enable; */
	if (pmlmepriv->qospriv.qos_option)
		pmlmeinfo->WMM_enable = true;
	if (pmlmepriv->htpriv.ht_option) {
		pmlmeinfo->WMM_enable = true;
		pmlmeinfo->HT_enable = true;
		/* pmlmeinfo->HT_info_enable = true; */
		/* pmlmeinfo->HT_caps_enable = true; */

		update_hw_ht_param(padapter);
	}

	if (!pmlmepriv->cur_network.join_res) { /* setting only at  first time */

		/* WEP Key will be set before this function, do not clear CAM. */
		if ((psecuritypriv->dot11PrivacyAlgrthm != _WEP40_) &&
		    (psecuritypriv->dot11PrivacyAlgrthm != _WEP104_))
			flush_all_cam_entry(padapter);	/* clear CAM */
	}

	/* set MSR to AP_Mode */
	Set_MSR(padapter, _HW_STATE_AP_);

	/* Set BSSID REG */
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pnetwork->mac_address);

	/* Set EDCA param reg */
	acparm = 0x002F3217; /*  VO */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
	acparm = 0x005E4317; /*  VI */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
	/* acparm = 0x00105320; // BE */
	acparm = 0x005ea42b;
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
	acparm = 0x0000A444; /*  BK */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));

	/* Set Security */
	val8 = (
		psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X
	) ? 0xcc : 0xcf;
	rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

	/* Beacon Control related register */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&bcn_interval));

	rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, NULL);

	if (!pmlmepriv->cur_network.join_res) { /* setting only at  first time */
		/* u32 initialgain; */

		/* initialgain = 0x1e; */

		/* disable dynamic functions, such as high power, DIG */
		/* Save_DM_Func_Flag(padapter); */
		/* Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, false); */

		/* turn on all dynamic functions */
		Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, true);

		/* rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain)); */
	}

	/* set channel, bwmode */
	p = rtw_get_ie((pnetwork->ies + sizeof(struct ndis_802_11_fix_ie)),
		       WLAN_EID_HT_OPERATION,
		       &ie_len,
		       (pnetwork->ie_length - sizeof(struct ndis_802_11_fix_ie))
	);
	if (p && ie_len) {
		pht_info = (struct HT_info_element *)(p + 2);

		if (cur_channel > 14) {
			if ((pregpriv->bw_mode & 0xf0) > 0)
				cbw40_enable = 1;
		} else {
			if ((pregpriv->bw_mode & 0x0f) > 0)
				cbw40_enable = 1;
		}

		if ((cbw40_enable) &&	 (pht_info->infos[0] & BIT(2))) {
			/* switch to the 40M Hz mode */
			/* pmlmeext->cur_bwmode = CHANNEL_WIDTH_40; */
			cur_bwmode = CHANNEL_WIDTH_40;
			switch (pht_info->infos[0] & 0x3) {
			case 1:
				/* pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER; */
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;

			case 3:
				/* pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER; */
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;

			default:
				/* pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE; */
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
			}
		}
	}

	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
	pmlmeext->cur_channel = cur_channel;
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;
	pmlmeext->cur_wireless_mode = pmlmepriv->cur_network.network_type;

	/* let pnetwork_mlmeext == pnetwork_mlme. */
	memcpy(pnetwork_mlmeext, pnetwork, pnetwork->length);

	/* update cur_wireless_mode */
	update_wireless_mode(padapter);

	/* update RRSR after set channel and bandwidth */
	UpdateBrateTbl(padapter, pnetwork->supported_rates);
	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, pnetwork->supported_rates);

	/* update capability after cur_wireless_mode updated */
	update_capinfo(
		padapter,
		rtw_get_capability((struct wlan_bssid_ex *)pnetwork)
	);

	if (pmlmeext->bstart_bss) {
		update_beacon(padapter, WLAN_EID_TIM, NULL, true);

		/* issue beacon frame */
		send_beacon(padapter);
	}

	/* update bc/mc sta_info */
	update_bmc_sta(padapter);

	/* pmlmeext->bstart_bss = true; */
}

int rtw_check_beacon_data(struct adapter *padapter, u8 *pbuf,  int len)
{
	int ret = _SUCCESS;
	u8 *p;
	u8 *pHT_caps_ie = NULL;
	u8 *pHT_info_ie = NULL;
	struct sta_info *psta = NULL;
	u16 cap, ht_cap = false;
	uint ie_len = 0;
	int group_cipher, pairwise_cipher;
	u8 channel, network_type, supportRate[NDIS_802_11_LENGTH_RATES_EX];
	int supportRateNum = 0;
	u8 OUI1[] = {0x00, 0x50, 0xf2, 0x01};
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex
		*pbss_network = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	u8 *ie = pbss_network->ies;

	if (!check_fwstate(pmlmepriv, WIFI_AP_STATE))
		return _FAIL;

	if (len < 0 || len > MAX_IE_SZ)
		return _FAIL;

	pbss_network->ie_length = len;

	memset(ie, 0, MAX_IE_SZ);

	memcpy(ie, pbuf, pbss_network->ie_length);

	if (pbss_network->infrastructure_mode != Ndis802_11APMode)
		return _FAIL;

	pbss_network->rssi = 0;

	memcpy(pbss_network->mac_address, myid(&(padapter->eeprompriv)), ETH_ALEN);

	/* beacon interval */
	p = rtw_get_beacon_interval_from_ie(ie);/* ie + 8;	8: TimeStamp, 2: Beacon Interval 2:Capability */
	/* pbss_network->configuration.beacon_period = le16_to_cpu(*(unsigned short*)p); */
	pbss_network->configuration.beacon_period = get_unaligned_le16(p);

	/* capability */
	/* cap = *(unsigned short *)rtw_get_capability_from_ie(ie); */
	/* cap = le16_to_cpu(cap); */
	cap = get_unaligned_le16(ie);

	/* SSID */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_SSID,
		&ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && ie_len > 0) {
		memset(&pbss_network->ssid, 0, sizeof(struct ndis_802_11_ssid));
		memcpy(pbss_network->ssid.ssid, (p + 2), ie_len);
		pbss_network->ssid.ssid_length = ie_len;
	}

	/* channel */
	channel = 0;
	pbss_network->configuration.length = 0;
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_DS_PARAMS, &ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && ie_len > 0)
		channel = *(p + 2);

	pbss_network->configuration.ds_config = channel;

	memset(supportRate, 0, NDIS_802_11_LENGTH_RATES_EX);
	/*  get supported rates */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_SUPP_RATES,
		&ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p) {
		memcpy(supportRate, p + 2, ie_len);
		supportRateNum = ie_len;
	}

	/* get ext_supported rates */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_EXT_SUPP_RATES,
		&ie_len,
		pbss_network->ie_length - _BEACON_IE_OFFSET_
	);
	if (p) {
		memcpy(supportRate + supportRateNum, p + 2, ie_len);
		supportRateNum += ie_len;
	}

	network_type = rtw_check_network_type(supportRate, supportRateNum, channel);

	rtw_set_supported_rate(pbss_network->supported_rates, network_type);

	/* parsing ERP_IE */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_ERP_INFO,
		&ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && ie_len > 0)
		ERP_IE_handler(padapter, (struct ndis_80211_var_ie *)p);

	/* update privacy/security */
	if (cap & BIT(4))
		pbss_network->privacy = 1;
	else
		pbss_network->privacy = 0;

	psecuritypriv->wpa_psk = 0;

	/* wpa2 */
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_RSN,
		&ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && ie_len > 0) {
		if (rtw_parse_wpa2_ie(
			p,
			ie_len + 2,
			&group_cipher,
			&pairwise_cipher,
			NULL
		) == _SUCCESS) {
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

			psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */
			psecuritypriv->wpa_psk |= BIT(1);

			psecuritypriv->wpa2_group_cipher = group_cipher;
			psecuritypriv->wpa2_pairwise_cipher = pairwise_cipher;
		}
	}

	/* wpa */
	ie_len = 0;
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;
	for (p = ie + _BEACON_IE_OFFSET_; ; p += (ie_len + 2)) {
		p = rtw_get_ie(
			p,
			WLAN_EID_VENDOR_SPECIFIC,
			&ie_len,
			(pbss_network->ie_length - _BEACON_IE_OFFSET_ - (ie_len + 2))
		);
		if ((p) && (!memcmp(p + 2, OUI1, 4))) {
			if (rtw_parse_wpa_ie(
				p,
				ie_len + 2,
				&group_cipher,
				&pairwise_cipher,
				NULL
			) == _SUCCESS) {
				psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

				psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */

				psecuritypriv->wpa_psk |= BIT(0);

				psecuritypriv->wpa_group_cipher = group_cipher;
				psecuritypriv->wpa_pairwise_cipher = pairwise_cipher;
			}

			break;
		}

		if (!p || ie_len == 0)
			break;
	}

	/* wmm */
	ie_len = 0;
	pmlmepriv->qospriv.qos_option = 0;
	if (pregistrypriv->wmm_enable) {
		for (p = ie + _BEACON_IE_OFFSET_; ; p += (ie_len + 2)) {
			p = rtw_get_ie(
				p,
				WLAN_EID_VENDOR_SPECIFIC,
				&ie_len,
				(pbss_network->ie_length - _BEACON_IE_OFFSET_ - (ie_len + 2))
			);
			if ((p) && !memcmp(p + 2, WMM_PARA_IE, 6)) {
				pmlmepriv->qospriv.qos_option = 1;

				*(p + 8) |= BIT(7);/* QoS Info, support U-APSD */

				/* disable all ACM bits since the WMM admission */
				/* control is not supported */
				*(p + 10) &= ~BIT(4); /* BE */
				*(p + 14) &= ~BIT(4); /* BK */
				*(p + 18) &= ~BIT(4); /* VI */
				*(p + 22) &= ~BIT(4); /* VO */

				break;
			}

			if (!p || ie_len == 0)
				break;
		}
	}

	/* parsing HT_CAP_IE */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_HT_CAPABILITY,
		&ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && ie_len > 0) {
		u8 max_rx_ampdu_factor = 0;
		struct ieee80211_ht_cap *pht_cap = (struct ieee80211_ht_cap *)(p + 2);

		pHT_caps_ie = p;

		ht_cap = true;
		network_type |= WIRELESS_11_24N;

		rtw_ht_use_default_setting(padapter);

		if (pmlmepriv->htpriv.sgi_20m == false)
			pht_cap->cap_info &= cpu_to_le16(~(IEEE80211_HT_CAP_SGI_20));

		if (pmlmepriv->htpriv.sgi_40m == false)
			pht_cap->cap_info &= cpu_to_le16(~(IEEE80211_HT_CAP_SGI_40));

		if (!TEST_FLAG(pmlmepriv->htpriv.ldpc_cap, LDPC_HT_ENABLE_RX))
			pht_cap->cap_info &= cpu_to_le16(~(IEEE80211_HT_CAP_LDPC_CODING));

		if (!TEST_FLAG(pmlmepriv->htpriv.stbc_cap, STBC_HT_ENABLE_TX))
			pht_cap->cap_info &= cpu_to_le16(~(IEEE80211_HT_CAP_TX_STBC));

		if (!TEST_FLAG(pmlmepriv->htpriv.stbc_cap, STBC_HT_ENABLE_RX))
			pht_cap->cap_info &= cpu_to_le16(~(IEEE80211_HT_CAP_RX_STBC_3R));

		pht_cap->ampdu_params_info &= ~(
			IEEE80211_HT_CAP_AMPDU_FACTOR | IEEE80211_HT_CAP_AMPDU_DENSITY
		);

		if ((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
		     (psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP)) {
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & (0x07 << 2));
		} else {
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);
		}

		rtw_hal_get_def_var(
			padapter,
			HW_VAR_MAX_RX_AMPDU_FACTOR,
			&max_rx_ampdu_factor
		);
		pht_cap->ampdu_params_info |= (
			IEEE80211_HT_CAP_AMPDU_FACTOR & max_rx_ampdu_factor
		); /* set  Max Rx AMPDU size  to 64K */

		pht_cap->mcs.rx_mask[0] = 0xff;
		pht_cap->mcs.rx_mask[1] = 0x0;

		memcpy(&pmlmepriv->htpriv.ht_cap, p + 2, ie_len);
	}

	/* parsing HT_INFO_IE */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_HT_OPERATION,
		&ie_len,
		(pbss_network->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && ie_len > 0)
		pHT_info_ie = p;

	switch (network_type) {
	case WIRELESS_11B:
		pbss_network->network_type_in_use = Ndis802_11DS;
		break;
	case WIRELESS_11G:
	case WIRELESS_11BG:
	case WIRELESS_11G_24N:
	case WIRELESS_11BG_24N:
		pbss_network->network_type_in_use = Ndis802_11OFDM24;
		break;
	default:
		pbss_network->network_type_in_use = Ndis802_11OFDM24;
		break;
	}

	pmlmepriv->cur_network.network_type = network_type;

	pmlmepriv->htpriv.ht_option = false;

	if ((psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_TKIP) ||
	     (psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_TKIP)) {
		/* todo: */
		/* ht_cap = false; */
	}

	/* ht_cap */
	if (pregistrypriv->ht_enable && ht_cap) {
		pmlmepriv->htpriv.ht_option = true;
		pmlmepriv->qospriv.qos_option = 1;

		if (pregistrypriv->ampdu_enable == 1)
			pmlmepriv->htpriv.ampdu_enable = true;

		HT_caps_handler(padapter, (struct ndis_80211_var_ie *)pHT_caps_ie);

		HT_info_handler(padapter, (struct ndis_80211_var_ie *)pHT_info_ie);
	}

	pbss_network->length = get_wlan_bssid_ex_sz(
		(struct wlan_bssid_ex  *)pbss_network
	);

	/* issue beacon to start bss network */
	/* start_bss_network(padapter, (u8 *)pbss_network); */
	rtw_startbss_cmd(padapter, RTW_CMDF_WAIT_ACK);

	/* alloc sta_info for ap itself */
	psta = rtw_get_stainfo(&padapter->stapriv, pbss_network->mac_address);
	if (!psta) {
		psta = rtw_alloc_stainfo(&padapter->stapriv, pbss_network->mac_address);
		if (!psta)
			return _FAIL;
	}

	/*  update AP's sta info */
	update_ap_info(padapter, psta);

	psta->state |= WIFI_AP_STATE;		/* Aries, add, fix bug of flush_cam_entry at STOP AP mode , 0724 */
	rtw_indicate_connect(padapter);

	pmlmepriv->cur_network.join_res = true;/* for check if already set beacon */

	/* update bc/mc sta_info */
	/* update_bmc_sta(padapter); */

	return ret;
}

void rtw_set_macaddr_acl(struct adapter *padapter, int mode)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	pacl_list->mode = mode;
}

int rtw_acl_add_sta(struct adapter *padapter, u8 *addr)
{
	struct list_head	*plist, *phead;
	u8 added = false;
	int i, ret = 0;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue	*pacl_node_q = &pacl_list->acl_node_q;

	if ((NUM_ACL - 1) < pacl_list->num)
		return (-1);

	spin_lock_bh(&(pacl_node_q->lock));

	phead = get_list_head(pacl_node_q);
	list_for_each(plist, phead) {
		paclnode = list_entry(plist, struct rtw_wlan_acl_node, list);

		if (!memcmp(paclnode->addr, addr, ETH_ALEN)) {
			if (paclnode->valid == true) {
				added = true;
				break;
			}
		}
	}

	spin_unlock_bh(&(pacl_node_q->lock));

	if (added)
		return ret;

	spin_lock_bh(&(pacl_node_q->lock));

	for (i = 0; i < NUM_ACL; i++) {
		paclnode = &pacl_list->aclnode[i];

		if (!paclnode->valid) {
			INIT_LIST_HEAD(&paclnode->list);

			memcpy(paclnode->addr, addr, ETH_ALEN);

			paclnode->valid = true;

			list_add_tail(&paclnode->list, get_list_head(pacl_node_q));

			pacl_list->num++;

			break;
		}
	}

	spin_unlock_bh(&(pacl_node_q->lock));

	return ret;
}

void rtw_acl_remove_sta(struct adapter *padapter, u8 *addr)
{
	struct list_head *plist, *phead, *tmp;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue	*pacl_node_q = &pacl_list->acl_node_q;
	u8 baddr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };	/* Baddr is used for clearing acl_list */

	spin_lock_bh(&(pacl_node_q->lock));

	phead = get_list_head(pacl_node_q);
	list_for_each_safe(plist, tmp, phead) {
		paclnode = list_entry(plist, struct rtw_wlan_acl_node, list);

		if (
			!memcmp(paclnode->addr, addr, ETH_ALEN) ||
			!memcmp(baddr, addr, ETH_ALEN)
		) {
			if (paclnode->valid) {
				paclnode->valid = false;

				list_del_init(&paclnode->list);

				pacl_list->num--;
			}
		}
	}

	spin_unlock_bh(&(pacl_node_q->lock));

}

u8 rtw_ap_set_pairwise_key(struct adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = rtw_zmalloc(sizeof(struct set_stakey_parm));
	if (!psetstakey_para) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);

	psetstakey_para->algorithm = (u8)psta->dot118021XPrivacy;

	memcpy(psetstakey_para->addr, psta->hwaddr, ETH_ALEN);

	memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

static int rtw_ap_set_key(
	struct adapter *padapter,
	u8 *key,
	u8 alg,
	int keyid,
	u8 set_tx
)
{
	u8 keylen;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	int res = _SUCCESS;

	pcmd = rtw_zmalloc(sizeof(struct cmd_obj));
	if (!pcmd) {
		res = _FAIL;
		goto exit;
	}
	psetkeyparm = rtw_zmalloc(sizeof(struct setkey_parm));
	if (!psetkeyparm) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	psetkeyparm->keyid = (u8)keyid;
	if (is_wep_enc(alg))
		padapter->securitypriv.key_mask |= BIT(psetkeyparm->keyid);

	psetkeyparm->algorithm = alg;

	psetkeyparm->set_tx = set_tx;

	switch (alg) {
	case _WEP40_:
		keylen = 5;
		break;
	case _WEP104_:
		keylen = 13;
		break;
	case _TKIP_:
	case _TKIP_WTMIC_:
	case _AES_:
	default:
		keylen = 16;
	}

	memcpy(&(psetkeyparm->key[0]), key, keylen);

	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz =  (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	INIT_LIST_HEAD(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

int rtw_ap_set_group_key(struct adapter *padapter, u8 *key, u8 alg, int keyid)
{
	return rtw_ap_set_key(padapter, key, alg, keyid, 1);
}

int rtw_ap_set_wep_key(
	struct adapter *padapter,
	u8 *key,
	u8 keylen,
	int keyid,
	u8 set_tx
)
{
	u8 alg;

	switch (keylen) {
	case 5:
		alg = _WEP40_;
		break;
	case 13:
		alg = _WEP104_;
		break;
	default:
		alg = _NO_PRIVACY_;
	}

	return rtw_ap_set_key(padapter, key, alg, keyid, set_tx);
}

static void update_bcn_fixed_ie(struct adapter *padapter)
{
}

static void update_bcn_erpinfo_ie(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);
	unsigned char *p, *ie = pnetwork->ies;
	u32 len = 0;

	if (!pmlmeinfo->ERP_enable)
		return;

	/* parsing ERP_IE */
	p = rtw_get_ie(
		ie + _BEACON_IE_OFFSET_,
		WLAN_EID_ERP_INFO,
		&len,
		(pnetwork->ie_length - _BEACON_IE_OFFSET_)
	);
	if (p && len > 0) {
		struct ndis_80211_var_ie *pIE = (struct ndis_80211_var_ie *)p;

		if (pmlmepriv->num_sta_non_erp == 1)
			pIE->data[0] |= RTW_ERP_INFO_NON_ERP_PRESENT | RTW_ERP_INFO_USE_PROTECTION;
		else
			pIE->data[0] &= ~(
				RTW_ERP_INFO_NON_ERP_PRESENT | RTW_ERP_INFO_USE_PROTECTION
			);

		if (pmlmepriv->num_sta_no_short_preamble > 0)
			pIE->data[0] |= RTW_ERP_INFO_BARKER_PREAMBLE_MODE;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_BARKER_PREAMBLE_MODE);

		ERP_IE_handler(padapter, pIE);
	}
}

static void update_bcn_htcap_ie(struct adapter *padapter)
{
}

static void update_bcn_htinfo_ie(struct adapter *padapter)
{
}

static void update_bcn_rsn_ie(struct adapter *padapter)
{
}

static void update_bcn_wpa_ie(struct adapter *padapter)
{
}

static void update_bcn_wmm_ie(struct adapter *padapter)
{
}

static void update_bcn_wps_ie(struct adapter *padapter)
{
	u8 *pwps_ie = NULL;
	u8 *pwps_ie_src;
	u8 *premainder_ie;
	u8 *pbackup_remainder_ie = NULL;

	uint wps_ielen = 0, wps_offset, remainder_ielen;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);
	unsigned char *ie = pnetwork->ies;
	u32 ielen = pnetwork->ie_length;

	pwps_ie = rtw_get_wps_ie(
		ie + _FIXED_IE_LENGTH_,
		ielen - _FIXED_IE_LENGTH_,
		NULL,
		&wps_ielen
	);

	if (!pwps_ie || wps_ielen == 0)
		return;

	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if (!pwps_ie_src)
		return;

	wps_offset = (uint)(pwps_ie - ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	wps_ielen = (uint)pwps_ie_src[1];/* to get ie data len */
	if ((wps_offset + wps_ielen + 2 + remainder_ielen) <= MAX_IE_SZ) {
		memcpy(pwps_ie, pwps_ie_src, wps_ielen + 2);
		pwps_ie += (wps_ielen+2);

		if (pbackup_remainder_ie)
			memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);

		/* update ie_length */
		pnetwork->ie_length = wps_offset + (wps_ielen + 2) + remainder_ielen;
	}

	kfree(pbackup_remainder_ie);
}

static void update_bcn_p2p_ie(struct adapter *padapter)
{
}

static void update_bcn_vendor_spec_ie(struct adapter *padapter, u8 *oui)
{
	if (!memcmp(RTW_WPA_OUI, oui, 4))
		update_bcn_wpa_ie(padapter);

	else if (!memcmp(WMM_OUI, oui, 4))
		update_bcn_wmm_ie(padapter);

	else if (!memcmp(WPS_OUI, oui, 4))
		update_bcn_wps_ie(padapter);

	else if (!memcmp(P2P_OUI, oui, 4))
		update_bcn_p2p_ie(padapter);
}

void update_beacon(struct adapter *padapter, u8 ie_id, u8 *oui, u8 tx)
{
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	/* struct mlme_ext_info *pmlmeinfo; */

	if (!padapter)
		return;

	pmlmepriv = &(padapter->mlmepriv);
	pmlmeext = &(padapter->mlmeextpriv);
	/* pmlmeinfo = &(pmlmeext->mlmext_info); */

	if (!pmlmeext->bstart_bss)
		return;

	spin_lock_bh(&pmlmepriv->bcn_update_lock);

	switch (ie_id) {
	case 0xFF:

		update_bcn_fixed_ie(padapter);/* 8: TimeStamp, 2: Beacon Interval 2:Capability */

		break;

	case WLAN_EID_TIM:

		update_BCNTIM(padapter);

		break;

	case WLAN_EID_ERP_INFO:

		update_bcn_erpinfo_ie(padapter);

		break;

	case WLAN_EID_HT_CAPABILITY:

		update_bcn_htcap_ie(padapter);

		break;

	case WLAN_EID_RSN:

		update_bcn_rsn_ie(padapter);

		break;

	case WLAN_EID_HT_OPERATION:

		update_bcn_htinfo_ie(padapter);

		break;

	case WLAN_EID_VENDOR_SPECIFIC:

		update_bcn_vendor_spec_ie(padapter, oui);

		break;

	default:
		break;
	}

	pmlmepriv->update_bcn = true;

	spin_unlock_bh(&pmlmepriv->bcn_update_lock);

	if (tx) {
		/* send_beacon(padapter);//send_beacon must execute on TSR level */
		set_tx_beacon_cmd(padapter);
	}
}

/*
 * op_mode
 * Set to 0 (HT pure) under the following conditions
 *	  - all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
 *	  - all STAs in the BSS are 20 MHz HT in 20 MHz BSS
 * Set to 1 (HT non-member protection) if there may be non-HT STAs
 *	  in both the primary and the secondary channel
 * Set to 2 if only HT STAs are associated in BSS,
 *	  however and at least one 20 MHz HT STA is associated
 * Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
 *	  (currently non-GF HT station is considered as non-HT STA also)
 */
static int rtw_ht_operation_update(struct adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct ht_priv *phtpriv_ap = &pmlmepriv->htpriv;

	if (pmlmepriv->htpriv.ht_option)
		return 0;

	if (!(pmlmepriv->ht_op_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT)
	    && pmlmepriv->num_sta_ht_no_gf) {
		pmlmepriv->ht_op_mode |=
			IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		    IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT) &&
		   pmlmepriv->num_sta_ht_no_gf == 0) {
		pmlmepriv->ht_op_mode &=
			~IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT;
		op_mode_changes++;
	}

	if (!(pmlmepriv->ht_op_mode & IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT) &&
	    (pmlmepriv->num_sta_no_ht || pmlmepriv->olbc_ht)) {
		pmlmepriv->ht_op_mode |= IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		    IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT) &&
		   (pmlmepriv->num_sta_no_ht == 0 && !pmlmepriv->olbc_ht)) {
		pmlmepriv->ht_op_mode &=
			~IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT;
		op_mode_changes++;
	}

	/* Note: currently we switch to the MIXED op mode if HT non-greenfield
	 * station is associated. Probably it's a theoretical case, since
	 * it looks like all known HT STAs support greenfield.
	 */
	new_op_mode = 0;
	if (pmlmepriv->num_sta_no_ht ||
	    (pmlmepriv->ht_op_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT))
		new_op_mode = IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED;
	else if (
		(le16_to_cpu(phtpriv_ap->ht_cap.cap_info) & IEEE80211_HT_CAP_SUP_WIDTH)
		&& pmlmepriv->num_sta_ht_20mhz)
		new_op_mode = IEEE80211_HT_OP_MODE_PROTECTION_20MHZ;
	else if (pmlmepriv->olbc_ht)
		new_op_mode = IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER;
	else
		new_op_mode = IEEE80211_HT_OP_MODE_PROTECTION_NONE;

	cur_op_mode = pmlmepriv->ht_op_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	if (cur_op_mode != new_op_mode) {
		pmlmepriv->ht_op_mode &= ~IEEE80211_HT_OP_MODE_PROTECTION;
		pmlmepriv->ht_op_mode |= new_op_mode;
		op_mode_changes++;
	}

	return op_mode_changes;
}

void associated_clients_update(struct adapter *padapter, u8 updated)
{
	/* update associated stations cap. */
	if (updated) {
		struct list_head	*phead, *plist;
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &padapter->stapriv;

		spin_lock_bh(&pstapriv->asoc_list_lock);

		phead = &pstapriv->asoc_list;
		/* check asoc_queue */
		list_for_each(plist, phead) {
			psta = list_entry(plist, struct sta_info, asoc_list);

			VCS_update(padapter, psta);
		}

		spin_unlock_bh(&pstapriv->asoc_list_lock);
	}
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void bss_cap_update_on_sta_join(struct adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = false;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	if (!(psta->flags & WLAN_STA_SHORT_PREAMBLE)) {
		if (!psta->no_short_preamble_set) {
			psta->no_short_preamble_set = 1;

			pmlmepriv->num_sta_no_short_preamble++;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
			    (pmlmepriv->num_sta_no_short_preamble == 1)) {
				beacon_updated = true;
				update_beacon(padapter, 0xFF, NULL, true);
			}
		}
	} else {
		if (psta->no_short_preamble_set) {
			psta->no_short_preamble_set = 0;

			pmlmepriv->num_sta_no_short_preamble--;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				(pmlmepriv->num_sta_no_short_preamble == 0)) {
				beacon_updated = true;
				update_beacon(padapter, 0xFF, NULL, true);
			}
		}
	}

	if (psta->flags & WLAN_STA_NONERP) {
		if (!psta->nonerp_set) {
			psta->nonerp_set = 1;

			pmlmepriv->num_sta_non_erp++;

			if (pmlmepriv->num_sta_non_erp == 1) {
				beacon_updated = true;
				update_beacon(padapter, WLAN_EID_ERP_INFO, NULL, true);
			}
		}
	} else {
		if (psta->nonerp_set) {
			psta->nonerp_set = 0;

			pmlmepriv->num_sta_non_erp--;

			if (pmlmepriv->num_sta_non_erp == 0) {
				beacon_updated = true;
				update_beacon(padapter, WLAN_EID_ERP_INFO, NULL, true);
			}
		}
	}

	if (!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)) {
		if (!psta->no_short_slot_time_set) {
			psta->no_short_slot_time_set = 1;

			pmlmepriv->num_sta_no_short_slot_time++;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				 (pmlmepriv->num_sta_no_short_slot_time == 1)) {
				beacon_updated = true;
				update_beacon(padapter, 0xFF, NULL, true);
			}
		}
	} else {
		if (psta->no_short_slot_time_set) {
			psta->no_short_slot_time_set = 0;

			pmlmepriv->num_sta_no_short_slot_time--;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				 (pmlmepriv->num_sta_no_short_slot_time == 0)) {
				beacon_updated = true;
				update_beacon(padapter, 0xFF, NULL, true);
			}
		}
	}

	if (psta->flags & WLAN_STA_HT) {
		u16 ht_capab = le16_to_cpu(psta->htpriv.ht_cap.cap_info);

		if (psta->no_ht_set) {
			psta->no_ht_set = 0;
			pmlmepriv->num_sta_no_ht--;
		}

		if ((ht_capab & IEEE80211_HT_CAP_GRN_FLD) == 0) {
			if (!psta->no_ht_gf_set) {
				psta->no_ht_gf_set = 1;
				pmlmepriv->num_sta_ht_no_gf++;
			}
		}

		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH) == 0) {
			if (!psta->ht_20mhz_set) {
				psta->ht_20mhz_set = 1;
				pmlmepriv->num_sta_ht_20mhz++;
			}
		}

	} else {
		if (!psta->no_ht_set) {
			psta->no_ht_set = 1;
			pmlmepriv->num_sta_no_ht++;
		}
	}

	if (rtw_ht_operation_update(padapter) > 0) {
		update_beacon(padapter, WLAN_EID_HT_CAPABILITY, NULL, false);
		update_beacon(padapter, WLAN_EID_HT_OPERATION, NULL, true);
	}

	/* update associated stations cap. */
	associated_clients_update(padapter,  beacon_updated);
}

u8 bss_cap_update_on_sta_leave(struct adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = false;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	if (!psta)
		return beacon_updated;

	if (psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 0;
		pmlmepriv->num_sta_no_short_preamble--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_preamble == 0){
			beacon_updated = true;
			update_beacon(padapter, 0xFF, NULL, true);
		}
	}

	if (psta->nonerp_set) {
		psta->nonerp_set = 0;
		pmlmepriv->num_sta_non_erp--;
		if (pmlmepriv->num_sta_non_erp == 0) {
			beacon_updated = true;
			update_beacon(padapter, WLAN_EID_ERP_INFO, NULL, true);
		}
	}

	if (psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 0;
		pmlmepriv->num_sta_no_short_slot_time--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_slot_time == 0){
			beacon_updated = true;
			update_beacon(padapter, 0xFF, NULL, true);
		}
	}

	if (psta->no_ht_gf_set) {
		psta->no_ht_gf_set = 0;
		pmlmepriv->num_sta_ht_no_gf--;
	}

	if (psta->no_ht_set) {
		psta->no_ht_set = 0;
		pmlmepriv->num_sta_no_ht--;
	}

	if (psta->ht_20mhz_set) {
		psta->ht_20mhz_set = 0;
		pmlmepriv->num_sta_ht_20mhz--;
	}

	if (rtw_ht_operation_update(padapter) > 0) {
		update_beacon(padapter, WLAN_EID_HT_CAPABILITY, NULL, false);
		update_beacon(padapter, WLAN_EID_HT_OPERATION, NULL, true);
	}

	return beacon_updated;
}

u8 ap_free_sta(
	struct adapter *padapter,
	struct sta_info *psta,
	bool active,
	u16 reason
)
{
	u8 beacon_updated = false;

	if (!psta)
		return beacon_updated;

	if (active) {
		/* tear down Rx AMPDU */
		send_delba(padapter, 0, psta->hwaddr);/*  recipient */

		/* tear down TX AMPDU */
		send_delba(padapter, 1, psta->hwaddr);/*  // originator */

		issue_deauth(padapter, psta->hwaddr, reason);
	}

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	/* report_del_sta_event(padapter, psta->hwaddr, reason); */

	/* clear cam entry / key */
	rtw_clearstakey_cmd(padapter, psta, true);

	spin_lock_bh(&psta->lock);
	psta->state &= ~_FW_LINKED;
	spin_unlock_bh(&psta->lock);

	rtw_cfg80211_indicate_sta_disassoc(padapter, psta->hwaddr, reason);

	report_del_sta_event(padapter, psta->hwaddr, reason);

	beacon_updated = bss_cap_update_on_sta_leave(padapter, psta);

	rtw_free_stainfo(padapter, psta);

	return beacon_updated;
}

void rtw_sta_flush(struct adapter *padapter)
{
	struct list_head *phead, *plist, *tmp;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		return;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	/* free sta asoc_queue */
	list_for_each_safe(plist, tmp, phead) {
		psta = list_entry(plist, struct sta_info, asoc_list);

		list_del_init(&psta->asoc_list);
		pstapriv->asoc_list_cnt--;

		/* spin_unlock_bh(&pstapriv->asoc_list_lock); */
		ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);
		/* spin_lock_bh(&pstapriv->asoc_list_lock); */
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	issue_deauth(padapter, bc_addr, WLAN_REASON_DEAUTH_LEAVING);

	associated_clients_update(padapter, true);
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void sta_info_update(struct adapter *padapter, struct sta_info *psta)
{
	int flags = psta->flags;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	/* update wmm cap. */
	if (WLAN_STA_WME & flags)
		psta->qos_option = 1;
	else
		psta->qos_option = 0;

	if (pmlmepriv->qospriv.qos_option == 0)
		psta->qos_option = 0;

	/* update 802.11n ht cap. */
	if (WLAN_STA_HT & flags) {
		psta->htpriv.ht_option = true;
		psta->qos_option = 1;
	} else {
		psta->htpriv.ht_option = false;
	}

	if (!pmlmepriv->htpriv.ht_option)
		psta->htpriv.ht_option = false;

	update_sta_info_apmode(padapter, psta);
}

/* called >= TSR LEVEL for USB or SDIO Interface*/
void ap_sta_info_defer_update(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (psta->state & _FW_LINKED) {
		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

		/* add ratid */
		add_RATid(padapter, psta, 0);/* DM_RATR_STA_INIT */
	}
}
/* restore hw setting from sw data structures */
void rtw_ap_restore_network(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct list_head	*phead, *plist;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	rtw_setopmode_cmd(padapter, Ndis802_11APMode, false);

	set_channel_bwmode(
		padapter,
		pmlmeext->cur_channel,
		pmlmeext->cur_ch_offset,
		pmlmeext->cur_bwmode
	);

	start_bss_network(padapter);

	if ((padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
		(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)) {
		/* restore group key, WEP keys is restored in ips_leave() */
		rtw_set_key(
			padapter,
			psecuritypriv,
			psecuritypriv->dot118021XGrpKeyid,
			0,
			false
		);
	}

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;
	list_for_each(plist, phead) {
		int stainfo_offset;

		psta = list_entry(plist, struct sta_info, asoc_list);

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset))
			chk_alive_list[chk_alive_num++] = stainfo_offset;
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);

	for (i = 0; i < chk_alive_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);

		if (!psta)
			continue;

		if (psta->state & _FW_LINKED) {
			rtw_sta_media_status_rpt(padapter, psta, 1);
			Update_RA_Entry(padapter, psta);
			/* pairwise key */
			/* per sta pairwise key and settings */
			if ((psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) ||
				(psecuritypriv->dot11PrivacyAlgrthm == _AES_)) {
				rtw_setstakey_cmd(padapter, psta, true, false);
			}
		}
	}
}

void start_ap_mode(struct adapter *padapter)
{
	int i;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	pmlmepriv->update_bcn = false;

	/* init_mlme_ap_info(padapter); */
	pmlmeext->bstart_bss = false;

	pmlmepriv->num_sta_non_erp = 0;

	pmlmepriv->num_sta_no_short_slot_time = 0;

	pmlmepriv->num_sta_no_short_preamble = 0;

	pmlmepriv->num_sta_ht_no_gf = 0;
	pmlmepriv->num_sta_no_ht = 0;
	pmlmepriv->num_sta_ht_20mhz = 0;

	pmlmepriv->olbc = false;

	pmlmepriv->olbc_ht = false;

	pmlmepriv->ht_op_mode = 0;

	for (i = 0; i < NUM_STA; i++)
		pstapriv->sta_aid[i] = NULL;

	pmlmepriv->wps_beacon_ie = NULL;
	pmlmepriv->wps_probe_resp_ie = NULL;
	pmlmepriv->wps_assoc_resp_ie = NULL;

	pmlmepriv->p2p_beacon_ie = NULL;
	pmlmepriv->p2p_probe_resp_ie = NULL;

	/* for ACL */
	INIT_LIST_HEAD(&(pacl_list->acl_node_q.queue));
	pacl_list->num = 0;
	pacl_list->mode = 0;
	for (i = 0; i < NUM_ACL; i++) {
		INIT_LIST_HEAD(&pacl_list->aclnode[i].list);
		pacl_list->aclnode[i].valid = false;
	}
}

void stop_ap_mode(struct adapter *padapter)
{
	struct list_head *phead, *plist, *tmp;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue	*pacl_node_q = &pacl_list->acl_node_q;

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	/* reset and init security priv , this can refine with rtw_reset_securitypriv */
	memset(
		(unsigned char *)&padapter->securitypriv,
		0,
		sizeof(struct security_priv)
	);
	padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
	padapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

	/* for ACL */
	spin_lock_bh(&(pacl_node_q->lock));
	phead = get_list_head(pacl_node_q);
	list_for_each_safe(plist, tmp, phead) {
		paclnode = list_entry(plist, struct rtw_wlan_acl_node, list);

		if (paclnode->valid) {
			paclnode->valid = false;

			list_del_init(&paclnode->list);

			pacl_list->num--;
		}
	}
	spin_unlock_bh(&(pacl_node_q->lock));

	rtw_sta_flush(padapter);

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo(padapter);

	psta = rtw_get_bcmc_stainfo(padapter);
	rtw_free_stainfo(padapter, psta);

	rtw_init_bcmc_stainfo(padapter);

	rtw_free_mlme_priv_ie_data(pmlmepriv);

	rtw_btcoex_MediaStatusNotify(padapter, 0); /* disconnect */
}
