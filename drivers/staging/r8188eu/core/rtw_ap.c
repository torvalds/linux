// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _RTW_AP_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/wifi.h"
#include "../include/ieee80211.h"
#include "../include/rtl8188e_cmd.h"

void init_mlme_ap_info(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	spin_lock_init(&pmlmepriv->bcn_update_lock);

	/* for ACL */
	rtw_init_queue(&pacl_list->acl_node_q);

	start_ap_mode(padapter);
}

void free_mlme_ap_info(struct adapter *padapter)
{
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	rtw_sta_flush(padapter);

	pmlmeinfo->state = _HW_STATE_NOLINK_;

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo(padapter);

	/* free bc/mc sta_info */
	psta = rtw_get_bcmc_stainfo(padapter);
	spin_lock_bh(&pstapriv->sta_hash_lock);
	rtw_free_stainfo(padapter, psta);
	spin_unlock_bh(&pstapriv->sta_hash_lock);
}

static void update_BCNTIM(struct adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork_mlmeext = &pmlmeinfo->network;
	unsigned char *pie = pnetwork_mlmeext->IEs;
	u8 *p, *dst_ie, *premainder_ie = NULL;
	u8 *pbackup_remainder_ie = NULL;
	__le16 tim_bitmap_le;
	uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

	/* update TIM IE */

	p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, _TIM_IE_, &tim_ielen,
		       pnetwork_mlmeext->IELength - _FIXED_IE_LENGTH_);
	if (p && tim_ielen > 0) {
		tim_ielen += 2;
		premainder_ie = p + tim_ielen;
		tim_ie_offset = (int)(p - pie);
		remainder_ielen = pnetwork_mlmeext->IELength - tim_ie_offset - tim_ielen;
		/* append TIM IE from dst_ie offset */
		dst_ie = p;
	} else {
		tim_ielen = 0;

		/* calculate head_len */
		offset = _FIXED_IE_LENGTH_;
		offset += pnetwork_mlmeext->Ssid.SsidLength + 2;

		/*  get supported rates len */
		p = rtw_get_ie(pie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_,
			       &tmp_len, (pnetwork_mlmeext->IELength - _BEACON_IE_OFFSET_));
		if (p)
			offset += tmp_len + 2;

		/* DS Parameter Set IE, len = 3 */
		offset += 3;

		premainder_ie = pie + offset;

		remainder_ielen = pnetwork_mlmeext->IELength - offset - tim_ielen;

		/* append TIM IE from offset */
		dst_ie = pie + offset;
	}

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = kmalloc(remainder_ielen, GFP_ATOMIC);
		if (pbackup_remainder_ie && premainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}
	*dst_ie++ = _TIM_IE_;

	if ((pstapriv->tim_bitmap & 0xff00) && (pstapriv->tim_bitmap & 0x00fc))
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

	tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);

	if (tim_ielen == 4) {
		*dst_ie++ = *(u8 *)&tim_bitmap_le;
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
	pnetwork_mlmeext->IELength = offset + remainder_ielen;

	set_tx_beacon_cmd(padapter);
}

static u8 chk_sta_is_alive(struct sta_info *psta)
{
	u8 ret = false;

	if ((psta->sta_stats.last_rx_data_pkts + psta->sta_stats.last_rx_ctrl_pkts) ==
	    (psta->sta_stats.rx_data_pkts + psta->sta_stats.rx_ctrl_pkts))
		;
	else
		ret = true;

	sta_update_last_rx_pkts(psta);

	return ret;
}

void	expire_timeout_chk(struct adapter *padapter)
{
	struct list_head *phead, *plist;
	u8 updated = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	spin_lock_bh(&pstapriv->auth_list_lock);

	phead = &pstapriv->auth_list;
	plist = phead->next;

	/* check auth_queue */
	while (phead != plist) {
		psta = container_of(plist, struct sta_info, auth_list);
		plist = plist->next;

		if (psta->expire_to > 0) {
			psta->expire_to--;
			if (psta->expire_to == 0) {
				list_del_init(&psta->auth_list);
				pstapriv->auth_list_cnt--;

				spin_unlock_bh(&pstapriv->auth_list_lock);

				spin_lock_bh(&pstapriv->sta_hash_lock);
				rtw_free_stainfo(padapter, psta);
				spin_unlock_bh(&pstapriv->sta_hash_lock);

				spin_lock_bh(&pstapriv->auth_list_lock);
			}
		}
	}
	spin_unlock_bh(&pstapriv->auth_list_lock);

	psta = NULL;

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;
	plist = phead->next;

	/* check asoc_queue */
	while (phead != plist) {
		psta = container_of(plist, struct sta_info, asoc_list);
		plist = plist->next;

		if (chk_sta_is_alive(psta) || !psta->expire_to) {
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
			psta->under_exist_checking = 0;
		} else {
			psta->expire_to--;
		}

		if (psta->expire_to <= 0) {
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			if (padapter->registrypriv.wifi_spec == 1) {
				psta->expire_to = pstapriv->expire_to;
				continue;
			}

			if (psta->state & WIFI_SLEEP_STATE) {
				if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
					/* to check if alive by another methods if station is at ps mode. */
					psta->expire_to = pstapriv->expire_to;
					psta->state |= WIFI_STA_ALIVE_CHK_STATE;

					/* to update bcn with tim_bitmap for this station */
					pstapriv->tim_bitmap |= BIT(psta->aid);
					update_beacon(padapter, _TIM_IE_, NULL, false);

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

			updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);
		} else {
			/* TODO: Aging mechanism to digest frames in sleep_q to avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME / pstapriv->asoc_list_cnt) &&
			    padapter->xmitpriv.free_xmitframe_cnt < (NR_XMITFRAME / pstapriv->asoc_list_cnt / 2)) {
				wakeup_sta_to_xmit(padapter, psta);
			}
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
			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);
			spin_unlock_bh(&pstapriv->asoc_list_lock);
		}

		if (backup_oper_channel > 0) /* back to the original operation channel */
			SelectChannel(padapter, backup_oper_channel);
	}

	associated_clients_update(padapter, updated);
}

void add_RATid(struct adapter *padapter, struct sta_info *psta, u8 rssi_level)
{
	int i;
	u32 init_rate = 0;
	unsigned char sta_band = 0, raid, shortGIrate = false;
	unsigned char limit;
	unsigned int tx_ra_bitmap = 0;
	struct ht_priv	*psta_ht = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pcur_network = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;

	if (psta)
		psta_ht = &psta->htpriv;
	else
		return;

	if (!(psta->state & _FW_LINKED))
		return;

	/* b/g mode ra_bitmap */
	for (i = 0; i < sizeof(psta->bssrateset); i++) {
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i] & 0x7f);
	}
	/* n mode ra_bitmap */
	if (psta_ht->ht_option) {
		limit = 8; /* 1R */

		for (i = 0; i < limit; i++) {
			if (psta_ht->ht_cap.mcs.rx_mask[i / 8] & BIT(i % 8))
				tx_ra_bitmap |= BIT(i + 12);
		}

		/* max short GI rate */
		shortGIrate = psta_ht->sgi;
	}

	if (pcur_network->Configuration.DSConfig > 14) {
		sta_band |= WIRELESS_INVALID;
	} else {
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_24N | WIRELESS_11G | WIRELESS_11B;
		else if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11G | WIRELESS_11B;
		else
			sta_band |= WIRELESS_11B;
	}

	psta->wireless_mode = sta_band;

	raid = networktype_to_raid(sta_band);
	init_rate = get_highest_rate_idx(tx_ra_bitmap & 0x0fffffff) & 0x3f;

	if (psta->aid < NUM_STA) {
		u8 arg = 0;

		arg = psta->mac_id & 0x1f;

		arg |= BIT(7);/* support entry 2~31 */

		if (shortGIrate)
			arg |= BIT(5);

		tx_ra_bitmap |= ((raid << 28) & 0xf0000000);

		/* bitmap[0:27] = tx_rate_bitmap */
		/* bitmap[28:31]= Rate Adaptive id */
		/* arg[0:4] = macid */
		/* arg[5] = Short GI */
		rtl8188e_Add_RateATid(padapter, tx_ra_bitmap, arg, rssi_level);

		if (shortGIrate)
			init_rate |= BIT(6);

		/* set ra_id, init_rate */
		psta->raid = raid;
		psta->init_rate = init_rate;
	}
}

void update_bmc_sta(struct adapter *padapter)
{
	u32 init_rate = 0;
	unsigned char	network_type, raid;
	int i, supportRateNum = 0;
	unsigned int tx_ra_bitmap = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pcur_network = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	struct sta_info *psta = rtw_get_bcmc_stainfo(padapter);

	if (psta) {
		psta->aid = 0;/* default set to 0 */
		psta->mac_id = psta->aid + 1;

		psta->qos_option = 0;
		psta->htpriv.ht_option = false;

		psta->ieee8021x_blocked = 0;

		memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		/* prepare for add_RATid */
		supportRateNum = rtw_get_rateset_len((u8 *)&pcur_network->SupportedRates);
		network_type = rtw_check_network_type((u8 *)&pcur_network->SupportedRates, supportRateNum, 1);

		memcpy(psta->bssrateset, &pcur_network->SupportedRates, supportRateNum);
		psta->bssratelen = supportRateNum;

		/* b/g mode ra_bitmap */
		for (i = 0; i < supportRateNum; i++) {
			if (psta->bssrateset[i])
				tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i] & 0x7f);
		}

		if (pcur_network->Configuration.DSConfig > 14) {
			network_type = WIRELESS_INVALID;
		} else {
			/* force to b mode */
			network_type = WIRELESS_11B;
			tx_ra_bitmap = 0xf;
		}

		raid = networktype_to_raid(network_type);
		init_rate = get_highest_rate_idx(tx_ra_bitmap & 0x0fffffff) & 0x3f;

		/* ap mode */
		rtl8188e_SetHalODMVar(padapter, psta, true);

		{
			u8 arg = 0;

			arg = psta->mac_id & 0x1f;
			arg |= BIT(7);
			tx_ra_bitmap |= ((raid << 28) & 0xf0000000);

			/* bitmap[0:27] = tx_rate_bitmap */
			/* bitmap[28:31]= Rate Adaptive id */
			/* arg[0:4] = macid */
			/* arg[5] = Short GI */
			rtl8188e_Add_RateATid(padapter, tx_ra_bitmap, arg, 0);
		}
		/* set ra_id, init_rate */
		psta->raid = raid;
		psta->init_rate = init_rate;

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
/* CAM_ID = 0~3 for default key, cmd_id = macid + 3, macid = aid+1; */

void update_sta_info_apmode(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv	*phtpriv_sta = &psta->htpriv;
	u16 sta_cap_info;
	u16 ap_cap_info;

	psta->mac_id = psta->aid + 1;

	/* ap mode */
	rtl8188e_SetHalODMVar(padapter, psta, true);

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
		sta_cap_info = le16_to_cpu(phtpriv_sta->ht_cap.cap_info);
		ap_cap_info = le16_to_cpu(phtpriv_ap->ht_cap.cap_info);

		/* check if sta support s Short GI */
		if ((sta_cap_info & ap_cap_info) &
		    (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40))
			phtpriv_sta->sgi = true;

		/*  bwmode */
		if ((sta_cap_info & ap_cap_info) & IEEE80211_HT_CAP_SUP_WIDTH_20_40) {
			phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
			phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;
		}
		psta->qos_option = true;
	} else {
		phtpriv_sta->ampdu_enable = false;
		phtpriv_sta->sgi = false;
		phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	/* Rx AMPDU */
	send_delba(padapter, 0, psta->hwaddr);/*  recipient */

	/* TX AMPDU */
	send_delba(padapter, 1, psta->hwaddr);/* originator */
	phtpriv_sta->agg_enable_bitmap = 0x0;/* reset */
	phtpriv_sta->candidate_tid_bitmap = 0x0;/* reset */

	/* todo: init other variables */

	memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

	spin_lock_bh(&psta->lock);
	psta->state |= _FW_LINKED;
	spin_unlock_bh(&psta->lock);
}

static void update_bcn_erpinfo_ie(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	unsigned char *p, *ie = pnetwork->IEs;
	u32 len = 0;

	if (!pmlmeinfo->ERP_enable)
		return;

	/* parsing ERP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &len,
		       (pnetwork->IELength - _BEACON_IE_OFFSET_));
	if (p && len > 0) {
		struct ndis_802_11_var_ie *pIE = (struct ndis_802_11_var_ie *)p;

		if (pmlmepriv->num_sta_non_erp == 1)
			pIE->data[0] |= RTW_ERP_INFO_NON_ERP_PRESENT | RTW_ERP_INFO_USE_PROTECTION;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_NON_ERP_PRESENT | RTW_ERP_INFO_USE_PROTECTION);

		if (pmlmepriv->num_sta_no_short_preamble > 0)
			pIE->data[0] |= RTW_ERP_INFO_BARKER_PREAMBLE_MODE;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_BARKER_PREAMBLE_MODE);

		ERP_IE_handler(padapter, pIE);
	}
}

static void update_bcn_wps_ie(struct adapter *padapter)
{
	u8 *pwps_ie = NULL, *pwps_ie_src;
	u8 *premainder_ie, *pbackup_remainder_ie = NULL;
	uint wps_ielen = 0, wps_offset, remainder_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	unsigned char *ie = pnetwork->IEs;
	u32 ielen = pnetwork->IELength;

	pwps_ie = rtw_get_wps_ie(ie + _FIXED_IE_LENGTH_, ielen - _FIXED_IE_LENGTH_, NULL, &wps_ielen);

	if (!pwps_ie || wps_ielen == 0)
		return;

	wps_offset = (uint)(pwps_ie - ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = kmalloc(remainder_ielen, GFP_ATOMIC);
		if (pbackup_remainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if (!pwps_ie_src)
		goto exit;

	wps_ielen = (uint)pwps_ie_src[1];/* to get ie data len */
	if ((wps_offset + wps_ielen + 2 + remainder_ielen) <= MAX_IE_SZ) {
		memcpy(pwps_ie, pwps_ie_src, wps_ielen + 2);
		pwps_ie += (wps_ielen + 2);

		if (pbackup_remainder_ie)
			memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);

		/* update IELength */
		pnetwork->IELength = wps_offset + (wps_ielen + 2) + remainder_ielen;
	}

exit:
	kfree(pbackup_remainder_ie);
}

static void update_bcn_vendor_spec_ie(struct adapter *padapter, u8 *oui)
{
	if (!memcmp(WPS_OUI, oui, 4))
		update_bcn_wps_ie(padapter);
}

void update_beacon(struct adapter *padapter, u8 ie_id, u8 *oui, u8 tx)
{
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv	*pmlmeext;

	if (!padapter)
		return;

	pmlmepriv = &padapter->mlmepriv;
	pmlmeext = &padapter->mlmeextpriv;

	if (!pmlmeext->bstart_bss)
		return;

	spin_lock_bh(&pmlmepriv->bcn_update_lock);

	switch (ie_id) {
	case _TIM_IE_:
		update_BCNTIM(padapter);
		break;
	case _ERPINFO_IE_:
		update_bcn_erpinfo_ie(padapter);
		break;
	case _VENDOR_SPECIFIC_IE_:
		update_bcn_vendor_spec_ie(padapter, oui);
		break;
	default:
		break;
	}

	pmlmepriv->update_bcn = true;

	spin_unlock_bh(&pmlmepriv->bcn_update_lock);

	if (tx)
		set_tx_beacon_cmd(padapter);
}

/* op_mode
 * Set to 0 (HT pure) under the following conditions
 *	- all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
 *	- all STAs in the BSS are 20 MHz HT in 20 MHz BSS
 * Set to 1 (HT non-member protection) if there may be non-HT STAs
 *	in both the primary and the secondary channel
 * Set to 2 if only HT STAs are associated in BSS,
 *	however and at least one 20 MHz HT STA is associated
 * Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
 *	(currently non-GF HT station is considered as non-HT STA also)
 */
static int rtw_ht_operation_update(struct adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;

	if (pmlmepriv->htpriv.ht_option)
		return 0;

	if (!(pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT) &&
	    pmlmepriv->num_sta_ht_no_gf) {
		pmlmepriv->ht_op_mode |=
			HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		   HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT) &&
		   pmlmepriv->num_sta_ht_no_gf == 0) {
		pmlmepriv->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT;
		op_mode_changes++;
	}

	if (!(pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
	    (pmlmepriv->num_sta_no_ht || pmlmepriv->olbc_ht)) {
		pmlmepriv->ht_op_mode |= HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		    HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
		   (pmlmepriv->num_sta_no_ht == 0 && !pmlmepriv->olbc_ht)) {
		pmlmepriv->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	}

	/* Note: currently we switch to the MIXED op mode if HT non-greenfield
	 * station is associated. Probably it's a theoretical case, since
	 * it looks like all known HT STAs support greenfield.
	 */
	new_op_mode = 0;
	if (pmlmepriv->num_sta_no_ht ||
	    (pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT))
		new_op_mode = OP_MODE_MIXED;
	else if ((le16_to_cpu(phtpriv_ap->ht_cap.cap_info) &
		  IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
		 pmlmepriv->num_sta_ht_20mhz)
		new_op_mode = OP_MODE_20MHZ_HT_STA_ASSOCED;
	else if (pmlmepriv->olbc_ht)
		new_op_mode = OP_MODE_MAY_BE_LEGACY_STAS;
	else
		new_op_mode = OP_MODE_PURE;

	cur_op_mode = pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_OP_MODE_MASK;
	if (cur_op_mode != new_op_mode) {
		pmlmepriv->ht_op_mode &= ~HT_INFO_OPERATION_MODE_OP_MODE_MASK;
		pmlmepriv->ht_op_mode |= new_op_mode;
		op_mode_changes++;
	}

	return op_mode_changes;
}

void associated_clients_update(struct adapter *padapter, u8 updated)
{
	/* update associated stations cap. */
	if (updated) {
		struct list_head *phead, *plist;
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &padapter->stapriv;

		spin_lock_bh(&pstapriv->asoc_list_lock);

		phead = &pstapriv->asoc_list;
		plist = phead->next;

		/* check asoc_queue */
		while (phead != plist) {
			psta = container_of(plist, struct sta_info, asoc_list);

			plist = plist->next;

			VCS_update(padapter, psta);
		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);
	}
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void bss_cap_update_on_sta_join(struct adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = false;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

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
				update_beacon(padapter, _ERPINFO_IE_, NULL, true);
			}
		}
	} else {
		if (psta->nonerp_set) {
			psta->nonerp_set = 0;

			pmlmepriv->num_sta_non_erp--;

			if (pmlmepriv->num_sta_non_erp == 0) {
				beacon_updated = true;
				update_beacon(padapter, _ERPINFO_IE_, NULL, true);
			}
		}
	}

	if (!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT)) {
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

		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH_20_40) == 0) {
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
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, false);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, true);
	}

	/* update associated stations cap. */
	associated_clients_update(padapter,  beacon_updated);
}

u8 bss_cap_update_on_sta_leave(struct adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = false;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!psta)
		return beacon_updated;

	if (psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 0;
		pmlmepriv->num_sta_no_short_preamble--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B &&
		    pmlmepriv->num_sta_no_short_preamble == 0) {
			beacon_updated = true;
			update_beacon(padapter, 0xFF, NULL, true);
		}
	}

	if (psta->nonerp_set) {
		psta->nonerp_set = 0;
		pmlmepriv->num_sta_non_erp--;
		if (pmlmepriv->num_sta_non_erp == 0) {
			beacon_updated = true;
			update_beacon(padapter, _ERPINFO_IE_, NULL, true);
		}
	}

	if (psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 0;
		pmlmepriv->num_sta_no_short_slot_time--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B &&
		    pmlmepriv->num_sta_no_short_slot_time == 0) {
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
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, false);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, true);
	}

	/* update associated stations cap. */

	return beacon_updated;
}

void rtw_indicate_sta_assoc_event(struct adapter *padapter, struct sta_info *psta)
{
	union iwreq_data wrqu;
	struct sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return;

	if (psta->aid > NUM_STA)
		return;

	if (pstapriv->sta_aid[psta->aid - 1] != psta)
		return;

	wrqu.addr.sa_family = ARPHRD_ETHER;

	memcpy(wrqu.addr.sa_data, psta->hwaddr, ETH_ALEN);

	wireless_send_event(padapter->pnetdev, IWEVREGISTERED, &wrqu, NULL);
}

static void rtw_indicate_sta_disassoc_event(struct adapter *padapter, struct sta_info *psta)
{
	union iwreq_data wrqu;
	struct sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return;

	if (psta->aid > NUM_STA)
		return;

	if (pstapriv->sta_aid[psta->aid - 1] != psta)
		return;

	wrqu.addr.sa_family = ARPHRD_ETHER;

	memcpy(wrqu.addr.sa_data, psta->hwaddr, ETH_ALEN);

	wireless_send_event(padapter->pnetdev, IWEVEXPIRED, &wrqu, NULL);
}

u8 ap_free_sta(struct adapter *padapter, struct sta_info *psta,
	       bool active, u16 reason)
{
	u8 beacon_updated = false;
	struct sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return beacon_updated;

	/* tear down Rx AMPDU */
	send_delba(padapter, 0, psta->hwaddr);/*  recipient */

	/* tear down TX AMPDU */
	send_delba(padapter, 1, psta->hwaddr);/*  originator */
	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	if (active)
		issue_deauth(padapter, psta->hwaddr, reason);

	/* clear cam entry / key */
	rtw_clearstakey_cmd(padapter, (u8 *)psta, (u8)(psta->mac_id + 3), true);

	spin_lock_bh(&psta->lock);
	psta->state &= ~_FW_LINKED;
	spin_unlock_bh(&psta->lock);

	rtw_indicate_sta_disassoc_event(padapter, psta);

	report_del_sta_event(padapter, psta->hwaddr, reason);

	beacon_updated = bss_cap_update_on_sta_leave(padapter, psta);

	spin_lock_bh(&pstapriv->sta_hash_lock);
	rtw_free_stainfo(padapter, psta);
	spin_unlock_bh(&pstapriv->sta_hash_lock);

	return beacon_updated;
}

int rtw_sta_flush(struct adapter *padapter)
{
	struct list_head *phead, *plist;
	int ret = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		return ret;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	plist = phead->next;

	/* free sta asoc_queue */
	while (phead != plist) {
		psta = container_of(plist, struct sta_info, asoc_list);

		plist = plist->next;

		list_del_init(&psta->asoc_list);
		pstapriv->asoc_list_cnt--;

		ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	issue_deauth(padapter, bc_addr, WLAN_REASON_DEAUTH_LEAVING);

	associated_clients_update(padapter, true);

	return ret;
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void sta_info_update(struct adapter *padapter, struct sta_info *psta)
{
	int flags = psta->flags;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

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

void start_ap_mode(struct adapter *padapter)
{
	int i;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	pmlmepriv->update_bcn = false;

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
	INIT_LIST_HEAD(&pacl_list->acl_node_q.queue);
	pacl_list->num = 0;
	pacl_list->mode = 0;
	for (i = 0; i < NUM_ACL; i++) {
		INIT_LIST_HEAD(&pacl_list->aclnode[i].list);
		pacl_list->aclnode[i].valid = false;
	}
}

void stop_ap_mode(struct adapter *padapter)
{
	struct list_head *phead, *plist;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue *pacl_node_q = &pacl_list->acl_node_q;

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	/* reset and init security priv , this can refine with rtw_reset_securitypriv */
	memset((unsigned char *)&padapter->securitypriv, 0, sizeof(struct security_priv));
	padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
	padapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

	/* for ACL */
	spin_lock_bh(&pacl_node_q->lock);
	phead = get_list_head(pacl_node_q);
	plist = phead->next;
	while (phead != plist) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);
		plist = plist->next;

		if (paclnode->valid) {
			paclnode->valid = false;

			list_del_init(&paclnode->list);

			pacl_list->num--;
		}
	}
	spin_unlock_bh(&pacl_node_q->lock);

	rtw_sta_flush(padapter);

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo(padapter);

	psta = rtw_get_bcmc_stainfo(padapter);
	spin_lock_bh(&pstapriv->sta_hash_lock);
	rtw_free_stainfo(padapter, psta);
	spin_unlock_bh(&pstapriv->sta_hash_lock);

	rtw_init_bcmc_stainfo(padapter);

	rtw_free_mlme_priv_ie_data(pmlmepriv);
}
