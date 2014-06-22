/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_AP_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <ieee80211.h>

#ifdef CONFIG_88EU_AP_MODE

void init_mlme_ap_info(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;


	spin_lock_init(&pmlmepriv->bcn_update_lock);

	/* for ACL */
	_rtw_init_queue(&pacl_list->acl_node_q);

	start_ap_mode(padapter);
}

void free_mlme_ap_info(struct adapter *padapter)
{
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	rtw_sta_flush(padapter);

	pmlmeinfo->state = _HW_STATE_NOLINK_;

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo(padapter);

	/* free bc/mc sta_info */
	psta = rtw_get_bcmc_stainfo(padapter);
	spin_lock_bh(&(pstapriv->sta_hash_lock));
	rtw_free_stainfo(padapter, psta);
	spin_unlock_bh(&(pstapriv->sta_hash_lock));
}

static void update_BCNTIM(struct adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork_mlmeext = &(pmlmeinfo->network);
	unsigned char *pie = pnetwork_mlmeext->IEs;

	/* update TIM IE */
	if (true) {
		u8 *p, *dst_ie, *premainder_ie = NULL;
		u8 *pbackup_remainder_ie = NULL;
		__le16 tim_bitmap_le;
		uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

		tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);

		p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, _TIM_IE_, &tim_ielen, pnetwork_mlmeext->IELength - _FIXED_IE_LENGTH_);
		if (p != NULL && tim_ielen > 0) {
			tim_ielen += 2;
			premainder_ie = p+tim_ielen;
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
			p = rtw_get_ie(pie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &tmp_len, (pnetwork_mlmeext->IELength - _BEACON_IE_OFFSET_));
			if (p !=  NULL)
				offset += tmp_len+2;

			/* DS Parameter Set IE, len = 3 */
			offset += 3;

			premainder_ie = pie + offset;

			remainder_ielen = pnetwork_mlmeext->IELength - offset - tim_ielen;

			/* append TIM IE from offset */
			dst_ie = pie + offset;
		}

		if (remainder_ielen > 0) {
			pbackup_remainder_ie = rtw_malloc(remainder_ielen);
			if (pbackup_remainder_ie && premainder_ie)
				memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
		}
		*dst_ie++ = _TIM_IE_;

		if ((pstapriv->tim_bitmap&0xff00) && (pstapriv->tim_bitmap&0x00fc))
			tim_ielen = 5;
		else
			tim_ielen = 4;

		*dst_ie++ = tim_ielen;

		*dst_ie++ = 0;/* DTIM count */
		*dst_ie++ = 1;/* DTIM period */

		if (pstapriv->tim_bitmap&BIT(0))/* for bc/mc frames */
			*dst_ie++ = BIT(0);/* bitmap ctrl */
		else
			*dst_ie++ = 0;

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
	}

	set_tx_beacon_cmd(padapter);
}

void rtw_add_bcn_ie(struct adapter *padapter, struct wlan_bssid_ex *pnetwork, u8 index, u8 *data, u8 len)
{
	struct ndis_802_11_var_ie *pIE;
	u8 bmatch = false;
	u8 *pie = pnetwork->IEs;
	u8 *p = NULL, *dst_ie = NULL, *premainder_ie = NULL;
	u8 *pbackup_remainder_ie = NULL;
	u32 i, offset, ielen = 0, ie_offset, remainder_ielen = 0;

	for (i = sizeof(struct ndis_802_11_fixed_ie); i < pnetwork->IELength;) {
		pIE = (struct ndis_802_11_var_ie *)(pnetwork->IEs + i);

		if (pIE->ElementID > index) {
			break;
		} else if (pIE->ElementID == index) { /*  already exist the same IE */
			p = (u8 *)pIE;
			ielen = pIE->Length;
			bmatch = true;
			break;
		}
		p = (u8 *)pIE;
		ielen = pIE->Length;
		i += (pIE->Length + 2);
	}

	if (p != NULL && ielen > 0) {
		ielen += 2;

		premainder_ie = p+ielen;

		ie_offset = (int)(p - pie);

		remainder_ielen = pnetwork->IELength - ie_offset - ielen;

		if (bmatch)
			dst_ie = p;
		else
			dst_ie = (p+ielen);
	}

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie && premainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	*dst_ie++ = index;
	*dst_ie++ = len;

	memcpy(dst_ie, data, len);
	dst_ie += len;

	/* copy remainder IE */
	if (pbackup_remainder_ie) {
		memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		kfree(pbackup_remainder_ie);
	}

	offset =  (uint)(dst_ie - pie);
	pnetwork->IELength = offset + remainder_ielen;
}

void rtw_remove_bcn_ie(struct adapter *padapter, struct wlan_bssid_ex *pnetwork, u8 index)
{
	u8 *p, *dst_ie = NULL, *premainder_ie = NULL;
	u8 *pbackup_remainder_ie = NULL;
	uint offset, ielen, ie_offset, remainder_ielen = 0;
	u8	*pie = pnetwork->IEs;

	p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, index, &ielen,
		       pnetwork->IELength - _FIXED_IE_LENGTH_);
	if (p != NULL && ielen > 0) {
		ielen += 2;

		premainder_ie = p+ielen;

		ie_offset = (int)(p - pie);

		remainder_ielen = pnetwork->IELength - ie_offset - ielen;

		dst_ie = p;
	}

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie && premainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	/* copy remainder IE */
	if (pbackup_remainder_ie) {
		memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		kfree(pbackup_remainder_ie);
	}

	offset =  (uint)(dst_ie - pie);
	pnetwork->IELength = offset + remainder_ielen;
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

				DBG_88E("auth expire %6ph\n",
					psta->hwaddr);

				spin_unlock_bh(&pstapriv->auth_list_lock);

				spin_lock_bh(&(pstapriv->sta_hash_lock));
				rtw_free_stainfo(padapter, psta);
				spin_unlock_bh(&(pstapriv->sta_hash_lock));

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

			DBG_88E("asoc expire %pM, state = 0x%x\n", (psta->hwaddr), psta->state);
			updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);
		} else {
			/* TODO: Aging mechanism to digest frames in sleep_q to avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME/pstapriv->asoc_list_cnt) &&
			    padapter->xmitpriv.free_xmitframe_cnt < (NR_XMITFRAME/pstapriv->asoc_list_cnt/2)) {
				DBG_88E("%s sta:%pM, sleepq_len:%u, free_xmitframe_cnt:%u, asoc_list_cnt:%u, clear sleep_q\n", __func__,
					(psta->hwaddr), psta->sleepq_len,
					padapter->xmitpriv.free_xmitframe_cnt,
					pstapriv->asoc_list_cnt);
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
				DBG_88E("asoc check, sta(%pM) is alive\n", (psta->hwaddr));
				psta->expire_to = pstapriv->expire_to;
				psta->keep_alive_trycnt = 0;
				continue;
			} else if (psta->keep_alive_trycnt <= 3) {
				DBG_88E("ack check for asoc expire, keep_alive_trycnt =%d\n", psta->keep_alive_trycnt);
				psta->expire_to = 1;
				continue;
			}

			psta->keep_alive_trycnt = 0;

			DBG_88E("asoc expire %pM, state = 0x%x\n", (psta->hwaddr), psta->state);
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
	u8 rf_type;
	u32 init_rate = 0;
	unsigned char sta_band = 0, raid, shortGIrate = false;
	unsigned char limit;
	unsigned int tx_ra_bitmap = 0;
	struct ht_priv	*psta_ht = NULL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
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
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
	}
	/* n mode ra_bitmap */
	if (psta_ht->ht_option) {
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if (rf_type == RF_2T2R)
			limit = 16;/*  2R */
		else
			limit = 8;/*   1R */

		for (i = 0; i < limit; i++) {
			if (psta_ht->ht_cap.supp_mcs_set[i/8] & BIT(i%8))
				tx_ra_bitmap |= BIT(i+12);
		}

		/* max short GI rate */
		shortGIrate = psta_ht->sgi;
	}

	if (pcur_network->Configuration.DSConfig > 14) {
		/*  5G band */
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_5N | WIRELESS_11A;
		else
			sta_band |= WIRELESS_11A;
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
	init_rate = get_highest_rate_idx(tx_ra_bitmap&0x0fffffff)&0x3f;

	if (psta->aid < NUM_STA) {
		u8 arg = 0;

		arg = psta->mac_id&0x1f;

		arg |= BIT(7);/* support entry 2~31 */

		if (shortGIrate)
			arg |= BIT(5);

		tx_ra_bitmap |= ((raid<<28)&0xf0000000);

		DBG_88E("%s => mac_id:%d , raid:%d , bitmap = 0x%x, arg = 0x%x\n",
			__func__ , psta->mac_id, raid , tx_ra_bitmap, arg);

		/* bitmap[0:27] = tx_rate_bitmap */
		/* bitmap[28:31]= Rate Adaptive id */
		/* arg[0:4] = macid */
		/* arg[5] = Short GI */
		rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, rssi_level);

		if (shortGIrate)
			init_rate |= BIT(6);

		/* set ra_id, init_rate */
		psta->raid = raid;
		psta->init_rate = init_rate;

	} else {
		DBG_88E("station aid %d exceed the max number\n", psta->aid);
	}
}

static void update_bmc_sta(struct adapter *padapter)
{
	u32 init_rate = 0;
	unsigned char	network_type, raid;
	int i, supportRateNum = 0;
	unsigned int tx_ra_bitmap = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
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
				tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
		}

		if (pcur_network->Configuration.DSConfig > 14) {
			/* force to A mode. 5G doesn't support CCK rates */
			network_type = WIRELESS_11A;
			tx_ra_bitmap = 0x150; /*  6, 12, 24 Mbps */
		} else {
			/* force to b mode */
			network_type = WIRELESS_11B;
			tx_ra_bitmap = 0xf;
		}

		raid = networktype_to_raid(network_type);
		init_rate = get_highest_rate_idx(tx_ra_bitmap&0x0fffffff)&0x3f;

		/* ap mode */
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, true);

		{
			u8 arg = 0;

			arg = psta->mac_id&0x1f;
			arg |= BIT(7);
			tx_ra_bitmap |= ((raid<<28)&0xf0000000);
			DBG_88E("update_bmc_sta, mask = 0x%x, arg = 0x%x\n", tx_ra_bitmap, arg);

			/* bitmap[0:27] = tx_rate_bitmap */
			/* bitmap[28:31]= Rate Adaptive id */
			/* arg[0:4] = macid */
			/* arg[5] = Short GI */
			rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, 0);
		}
		/* set ra_id, init_rate */
		psta->raid = raid;
		psta->init_rate = init_rate;

		rtw_stassoc_hw_rpt(padapter, psta);

		spin_lock_bh(&psta->lock);
		psta->state = _FW_LINKED;
		spin_unlock_bh(&psta->lock);

	} else {
		DBG_88E("add_RATid_bmc_sta error!\n");
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
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv	*phtpriv_sta = &psta->htpriv;

	psta->mac_id = psta->aid+1;
	DBG_88E("%s\n", __func__);

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

		/* check if sta support s Short GI */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40))
			phtpriv_sta->sgi = true;

		/*  bwmode */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & IEEE80211_HT_CAP_SUP_WIDTH) {
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

static void update_hw_ht_param(struct adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_88E("%s\n", __func__);

	/* handle A-MPDU parameter field */
	/*
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing
	*/
	max_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;

	min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) >> 2;

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MIN_SPACE, (u8 *)(&min_MPDU_spacing));

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));

	/*  */
	/*  Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS = (le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) & 0x0C) >> 2;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
		DBG_88E("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __func__);
}

static void start_bss_network(struct adapter *padapter, u8 *pbuf)
{
	u8 *p;
	u8 val8, cur_channel, cur_bwmode, cur_ch_offset;
	u16 bcn_interval;
	u32	acparm;
	int	ie_len;
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork_mlmeext = &(pmlmeinfo->network);
	struct HT_info_element *pht_info = NULL;
#ifdef CONFIG_88EU_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_88EU_P2P */

	bcn_interval = (u16)pnetwork->Configuration.BeaconPeriod;
	cur_channel = pnetwork->Configuration.DSConfig;
	cur_bwmode = HT_CHANNEL_WIDTH_20;
	cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;


	/* check if there is wps ie, */
	/* if there is wpsie in beacon, the hostapd will update beacon twice when stating hostapd, */
	/* and at first time the security ie (RSN/WPA IE) will not include in beacon. */
	if (!rtw_get_wps_ie(pnetwork->IEs+_FIXED_IE_LENGTH_, pnetwork->IELength-_FIXED_IE_LENGTH_, NULL, NULL))
		pmlmeext->bstart_bss = true;

	/* todo: update wmm, ht cap */
	if (pmlmepriv->qospriv.qos_option)
		pmlmeinfo->WMM_enable = true;
	if (pmlmepriv->htpriv.ht_option) {
		pmlmeinfo->WMM_enable = true;
		pmlmeinfo->HT_enable = true;

		update_hw_ht_param(padapter);
	}

	if (pmlmepriv->cur_network.join_res != true) { /* setting only at  first time */
		/* WEP Key will be set before this function, do not clear CAM. */
		if ((psecuritypriv->dot11PrivacyAlgrthm != _WEP40_) &&
		    (psecuritypriv->dot11PrivacyAlgrthm != _WEP104_))
			flush_all_cam_entry(padapter);	/* clear CAM */
	}

	/* set MSR to AP_Mode */
	Set_MSR(padapter, _HW_STATE_AP_);

	/* Set BSSID REG */
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pnetwork->MacAddress);

	/* Set EDCA param reg */
	acparm = 0x002F3217; /*  VO */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
	acparm = 0x005E4317; /*  VI */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
	acparm = 0x005ea42b;
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
	acparm = 0x0000A444; /*  BK */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));

	/* Set Security */
	val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) ? 0xcc : 0xcf;
	rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

	/* Beacon Control related register */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&bcn_interval));

	UpdateBrateTbl(padapter, pnetwork->SupportedRates);
	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, pnetwork->SupportedRates);

	if (!pmlmepriv->cur_network.join_res) { /* setting only at  first time */
		/* turn on all dynamic functions */
		Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, true);
	}
	/* set channel, bwmode */
	p = rtw_get_ie((pnetwork->IEs + sizeof(struct ndis_802_11_fixed_ie)), _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - sizeof(struct ndis_802_11_fixed_ie)));
	if (p && ie_len) {
		pht_info = (struct HT_info_element *)(p+2);

		if ((pregpriv->cbw40_enable) &&	 (pht_info->infos[0] & BIT(2))) {
			/* switch to the 40M Hz mode */
			cur_bwmode = HT_CHANNEL_WIDTH_40;
			switch (pht_info->infos[0] & 0x3) {
			case 1:
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;
			case 3:
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;
			default:
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
			}
		}
	}
	/* TODO: need to judge the phy parameters on concurrent mode for single phy */
	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);

	DBG_88E("CH =%d, BW =%d, offset =%d\n", cur_channel, cur_bwmode, cur_ch_offset);

	/*  */
	pmlmeext->cur_channel = cur_channel;
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;
	pmlmeext->cur_wireless_mode = pmlmepriv->cur_network.network_type;

	/* update cur_wireless_mode */
	update_wireless_mode(padapter);

	/* update capability after cur_wireless_mode updated */
	update_capinfo(padapter, rtw_get_capability((struct wlan_bssid_ex *)pnetwork));

	/* let pnetwork_mlmeext == pnetwork_mlme. */
	memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);

#ifdef CONFIG_88EU_P2P
	memcpy(pwdinfo->p2p_group_ssid, pnetwork->Ssid.Ssid, pnetwork->Ssid.SsidLength);
	pwdinfo->p2p_group_ssid_len = pnetwork->Ssid.SsidLength;
#endif /* CONFIG_88EU_P2P */

	if (pmlmeext->bstart_bss) {
		update_beacon(padapter, _TIM_IE_, NULL, false);

		/* issue beacon frame */
		if (send_beacon(padapter) == _FAIL)
			DBG_88E("issue_beacon, fail!\n");
	}

	/* update bc/mc sta_info */
	update_bmc_sta(padapter);
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
	u8	channel, network_type, supportRate[NDIS_802_11_LENGTH_RATES_EX];
	int supportRateNum = 0;
	u8 OUI1[] = {0x00, 0x50, 0xf2, 0x01};
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex *pbss_network = (struct wlan_bssid_ex *)&pmlmepriv->cur_network.network;
	u8 *ie = pbss_network->IEs;

	/* SSID */
	/* Supported rates */
	/* DS Params */
	/* WLAN_EID_COUNTRY */
	/* ERP Information element */
	/* Extended supported rates */
	/* WPA/WPA2 */
	/* Wi-Fi Wireless Multimedia Extensions */
	/* ht_capab, ht_oper */
	/* WPS IE */

	DBG_88E("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return _FAIL;


	if (len > MAX_IE_SZ)
		return _FAIL;

	pbss_network->IELength = len;

	memset(ie, 0, MAX_IE_SZ);

	memcpy(ie, pbuf, pbss_network->IELength);


	if (pbss_network->InfrastructureMode != Ndis802_11APMode)
		return _FAIL;

	pbss_network->Rssi = 0;

	memcpy(pbss_network->MacAddress, myid(&(padapter->eeprompriv)), ETH_ALEN);

	/* beacon interval */
	p = rtw_get_beacon_interval_from_ie(ie);/* 8: TimeStamp, 2: Beacon Interval 2:Capability */
	pbss_network->Configuration.BeaconPeriod = get_unaligned_le16(p);

	/* capability */
	cap = get_unaligned_le16(ie);

	/* SSID */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SSID_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0) {
		memset(&pbss_network->Ssid, 0, sizeof(struct ndis_802_11_ssid));
		memcpy(pbss_network->Ssid.Ssid, (p + 2), ie_len);
		pbss_network->Ssid.SsidLength = ie_len;
	}

	/* channel */
	channel = 0;
	pbss_network->Configuration.Length = 0;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _DSSET_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		channel = *(p + 2);

	pbss_network->Configuration.DSConfig = channel;

	memset(supportRate, 0, NDIS_802_11_LENGTH_RATES_EX);
	/*  get supported rates */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p !=  NULL) {
		memcpy(supportRate, p+2, ie_len);
		supportRateNum = ie_len;
	}

	/* get ext_supported rates */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ie_len, pbss_network->IELength - _BEACON_IE_OFFSET_);
	if (p !=  NULL) {
		memcpy(supportRate+supportRateNum, p+2, ie_len);
		supportRateNum += ie_len;
	}

	network_type = rtw_check_network_type(supportRate, supportRateNum, channel);

	rtw_set_supported_rate(pbss_network->SupportedRates, network_type);

	/* parsing ERP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		ERP_IE_handler(padapter, (struct ndis_802_11_var_ie *)p);

	/* update privacy/security */
	if (cap & BIT(4))
		pbss_network->Privacy = 1;
	else
		pbss_network->Privacy = 0;

	psecuritypriv->wpa_psk = 0;

	/* wpa2 */
	group_cipher = 0;
	pairwise_cipher = 0;
	psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0) {
		if (rtw_parse_wpa2_ie(p, ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

			psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */
			psecuritypriv->wpa_psk |= BIT(1);

			psecuritypriv->wpa2_group_cipher = group_cipher;
			psecuritypriv->wpa2_pairwise_cipher = pairwise_cipher;
		}
	}
	/* wpa */
	ie_len = 0;
	group_cipher = 0;
	pairwise_cipher = 0;
	psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;
	for (p = ie + _BEACON_IE_OFFSET_;; p += (ie_len + 2)) {
		p = rtw_get_ie(p, _SSN_IE_1_, &ie_len,
			       (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
		if ((p) && (!memcmp(p+2, OUI1, 4))) {
			if (rtw_parse_wpa_ie(p, ie_len+2, &group_cipher,
					     &pairwise_cipher, NULL) == _SUCCESS) {
				psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

				psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */

				psecuritypriv->wpa_psk |= BIT(0);

				psecuritypriv->wpa_group_cipher = group_cipher;
				psecuritypriv->wpa_pairwise_cipher = pairwise_cipher;
			}
			break;
		}
		if ((p == NULL) || (ie_len == 0))
			break;
	}

	/* wmm */
	ie_len = 0;
	pmlmepriv->qospriv.qos_option = 0;
	if (pregistrypriv->wmm_enable) {
		for (p = ie + _BEACON_IE_OFFSET_;; p += (ie_len + 2)) {
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len,
				       (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
			if ((p) && !memcmp(p+2, WMM_PARA_IE, 6)) {
				pmlmepriv->qospriv.qos_option = 1;

				*(p+8) |= BIT(7);/* QoS Info, support U-APSD */

				/* disable all ACM bits since the WMM admission control is not supported */
				*(p + 10) &= ~BIT(4); /* BE */
				*(p + 14) &= ~BIT(4); /* BK */
				*(p + 18) &= ~BIT(4); /* VI */
				*(p + 22) &= ~BIT(4); /* VO */
				break;
			}

			if ((p == NULL) || (ie_len == 0))
				break;
		}
	}
	/* parsing HT_CAP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len,
		       (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0) {
		u8 rf_type;
		struct rtw_ieee80211_ht_cap *pht_cap = (struct rtw_ieee80211_ht_cap *)(p+2);

		pHT_caps_ie = p;
		ht_cap = true;
		network_type |= WIRELESS_11_24N;

		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		if ((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
		    (psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP))
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2));
		else
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);

		/* set  Max Rx AMPDU size  to 64K */
		pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_FACTOR & 0x03);

		if (rf_type == RF_1T1R) {
			pht_cap->supp_mcs_set[0] = 0xff;
			pht_cap->supp_mcs_set[1] = 0x0;
		}
		memcpy(&pmlmepriv->htpriv.ht_cap, p+2, ie_len);
	}

	/* parsing HT_INFO_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len,
		       (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		pHT_info_ie = p;
	switch (network_type) {
	case WIRELESS_11B:
		pbss_network->NetworkTypeInUse = Ndis802_11DS;
		break;
	case WIRELESS_11G:
	case WIRELESS_11BG:
	case WIRELESS_11G_24N:
	case WIRELESS_11BG_24N:
		pbss_network->NetworkTypeInUse = Ndis802_11OFDM24;
		break;
	case WIRELESS_11A:
		pbss_network->NetworkTypeInUse = Ndis802_11OFDM5;
		break;
	default:
		pbss_network->NetworkTypeInUse = Ndis802_11OFDM24;
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
		HT_caps_handler(padapter, (struct ndis_802_11_var_ie *)pHT_caps_ie);

		HT_info_handler(padapter, (struct ndis_802_11_var_ie *)pHT_info_ie);
	}

	pbss_network->Length = get_wlan_bssid_ex_sz((struct wlan_bssid_ex  *)pbss_network);

	/* issue beacon to start bss network */
	start_bss_network(padapter, (u8 *)pbss_network);

	/* alloc sta_info for ap itself */
	psta = rtw_get_stainfo(&padapter->stapriv, pbss_network->MacAddress);
	if (!psta) {
		psta = rtw_alloc_stainfo(&padapter->stapriv, pbss_network->MacAddress);
		if (psta == NULL)
			return _FAIL;
	}

	/* fix bug of flush_cam_entry at STOP AP mode */
	psta->state |= WIFI_AP_STATE;
	rtw_indicate_connect(padapter);
	pmlmepriv->cur_network.join_res = true;/* for check if already set beacon */
	return ret;
}

void rtw_set_macaddr_acl(struct adapter *padapter, int mode)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	DBG_88E("%s, mode =%d\n", __func__, mode);

	pacl_list->mode = mode;
}

int rtw_acl_add_sta(struct adapter *padapter, u8 *addr)
{
	struct list_head *plist, *phead;
	u8 added = false;
	int i, ret = 0;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue *pacl_node_q = &pacl_list->acl_node_q;

	DBG_88E("%s(acl_num =%d) =%pM\n", __func__, pacl_list->num, (addr));

	if ((NUM_ACL-1) < pacl_list->num)
		return -1;

	spin_lock_bh(&(pacl_node_q->lock));

	phead = get_list_head(pacl_node_q);
	plist = phead->next;

	while (phead != plist) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);
		plist = plist->next;

		if (!memcmp(paclnode->addr, addr, ETH_ALEN)) {
			if (paclnode->valid) {
				added = true;
				DBG_88E("%s, sta has been added\n", __func__);
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

	DBG_88E("%s, acl_num =%d\n", __func__, pacl_list->num);

	spin_unlock_bh(&(pacl_node_q->lock));

	return ret;
}

int rtw_acl_remove_sta(struct adapter *padapter, u8 *addr)
{
	struct list_head *plist, *phead;
	int ret = 0;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue *pacl_node_q = &pacl_list->acl_node_q;

	DBG_88E("%s(acl_num =%d) =%pM\n", __func__, pacl_list->num, (addr));

	spin_lock_bh(&(pacl_node_q->lock));

	phead = get_list_head(pacl_node_q);
	plist = phead->next;

	while (phead != plist) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);
		plist = plist->next;

		if (!memcmp(paclnode->addr, addr, ETH_ALEN)) {
			if (paclnode->valid) {
				paclnode->valid = false;

				list_del_init(&paclnode->list);

				pacl_list->num--;
			}
		}
	}

	spin_unlock_bh(&(pacl_node_q->lock));

	DBG_88E("%s, acl_num =%d\n", __func__, pacl_list->num);
	return ret;
}

static void update_bcn_fixed_ie(struct adapter *padapter)
{
	DBG_88E("%s\n", __func__);
}

static void update_bcn_erpinfo_ie(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);
	unsigned char *p, *ie = pnetwork->IEs;
	u32 len = 0;

	DBG_88E("%s, ERP_enable =%d\n", __func__, pmlmeinfo->ERP_enable);

	if (!pmlmeinfo->ERP_enable)
		return;

	/* parsing ERP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &len,
		       (pnetwork->IELength - _BEACON_IE_OFFSET_));
	if (p && len > 0) {
		struct ndis_802_11_var_ie *pIE = (struct ndis_802_11_var_ie *)p;

		if (pmlmepriv->num_sta_non_erp == 1)
			pIE->data[0] |= RTW_ERP_INFO_NON_ERP_PRESENT|RTW_ERP_INFO_USE_PROTECTION;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_NON_ERP_PRESENT|RTW_ERP_INFO_USE_PROTECTION);

		if (pmlmepriv->num_sta_no_short_preamble > 0)
			pIE->data[0] |= RTW_ERP_INFO_BARKER_PREAMBLE_MODE;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_BARKER_PREAMBLE_MODE);

		ERP_IE_handler(padapter, pIE);
	}
}

static void update_bcn_htcap_ie(struct adapter *padapter)
{
	DBG_88E("%s\n", __func__);
}

static void update_bcn_htinfo_ie(struct adapter *padapter)
{
	DBG_88E("%s\n", __func__);
}

static void update_bcn_rsn_ie(struct adapter *padapter)
{
	DBG_88E("%s\n", __func__);
}

static void update_bcn_wpa_ie(struct adapter *padapter)
{
	DBG_88E("%s\n", __func__);
}

static void update_bcn_wmm_ie(struct adapter *padapter)
{
	DBG_88E("%s\n", __func__);
}

static void update_bcn_wps_ie(struct adapter *padapter)
{
	u8 *pwps_ie = NULL, *pwps_ie_src;
	u8 *premainder_ie, *pbackup_remainder_ie = NULL;
	uint wps_ielen = 0, wps_offset, remainder_ielen;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);
	unsigned char *ie = pnetwork->IEs;
	u32 ielen = pnetwork->IELength;

	DBG_88E("%s\n", __func__);

	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if (pwps_ie_src == NULL)
		return;

	pwps_ie = rtw_get_wps_ie(ie+_FIXED_IE_LENGTH_, ielen-_FIXED_IE_LENGTH_, NULL, &wps_ielen);

	if (pwps_ie == NULL || wps_ielen == 0)
		return;

	wps_offset = (uint)(pwps_ie-ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	wps_ielen = (uint)pwps_ie_src[1];/* to get ie data len */
	if ((wps_offset+wps_ielen+2+remainder_ielen) <= MAX_IE_SZ) {
		memcpy(pwps_ie, pwps_ie_src, wps_ielen+2);
		pwps_ie += (wps_ielen+2);

		if (pbackup_remainder_ie)
			memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);

		/* update IELength */
		pnetwork->IELength = wps_offset + (wps_ielen+2) + remainder_ielen;
	}

	kfree(pbackup_remainder_ie);
}

static void update_bcn_p2p_ie(struct adapter *padapter)
{
}

static void update_bcn_vendor_spec_ie(struct adapter *padapter, u8 *oui)
{
	DBG_88E("%s\n", __func__);

	if (!memcmp(RTW_WPA_OUI, oui, 4))
		update_bcn_wpa_ie(padapter);
	else if (!memcmp(WMM_OUI, oui, 4))
		update_bcn_wmm_ie(padapter);
	else if (!memcmp(WPS_OUI, oui, 4))
		update_bcn_wps_ie(padapter);
	else if (!memcmp(P2P_OUI, oui, 4))
		update_bcn_p2p_ie(padapter);
	else
		DBG_88E("unknown OUI type!\n");
}

void update_beacon(struct adapter *padapter, u8 ie_id, u8 *oui, u8 tx)
{
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv	*pmlmeext;

	if (!padapter)
		return;

	pmlmepriv = &(padapter->mlmepriv);
	pmlmeext = &(padapter->mlmeextpriv);

	if (!pmlmeext->bstart_bss)
		return;

	spin_lock_bh(&pmlmepriv->bcn_update_lock);

	switch (ie_id) {
	case 0xFF:
		update_bcn_fixed_ie(padapter);/* 8: TimeStamp, 2: Beacon Interval 2:Capability */
		break;
	case _TIM_IE_:
		update_BCNTIM(padapter);
		break;
	case _ERPINFO_IE_:
		update_bcn_erpinfo_ie(padapter);
		break;
	case _HT_CAPABILITY_IE_:
		update_bcn_htcap_ie(padapter);
		break;
	case _RSN_IE_2_:
		update_bcn_rsn_ie(padapter);
		break;
	case _HT_ADD_INFO_IE_:
		update_bcn_htinfo_ie(padapter);
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

/*
op_mode
Set to 0 (HT pure) under the following conditions
	- all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
	- all STAs in the BSS are 20 MHz HT in 20 MHz BSS
Set to 1 (HT non-member protection) if there may be non-HT STAs
	in both the primary and the secondary channel
Set to 2 if only HT STAs are associated in BSS,
	however and at least one 20 MHz HT STA is associated
Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
	(currently non-GF HT station is considered as non-HT STA also)
*/
static int rtw_ht_operation_update(struct adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;

	if (pmlmepriv->htpriv.ht_option)
		return 0;

	DBG_88E("%s current operation mode = 0x%X\n",
		__func__, pmlmepriv->ht_op_mode);

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
	else if ((phtpriv_ap->ht_cap.cap_info & IEEE80211_HT_CAP_SUP_WIDTH) &&
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

	DBG_88E("%s new operation mode = 0x%X changes =%d\n",
		__func__, pmlmepriv->ht_op_mode, op_mode_changes);

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
		u16 ht_capab = psta->htpriv.ht_cap.cap_info;

		DBG_88E("HT: STA %pM HT Capabilities Info: 0x%04x\n",
			(psta->hwaddr), ht_capab);

		if (psta->no_ht_set) {
			psta->no_ht_set = 0;
			pmlmepriv->num_sta_no_ht--;
		}

		if ((ht_capab & IEEE80211_HT_CAP_GRN_FLD) == 0) {
			if (!psta->no_ht_gf_set) {
				psta->no_ht_gf_set = 1;
				pmlmepriv->num_sta_ht_no_gf++;
			}
			DBG_88E("%s STA %pM - no greenfield, num of non-gf stations %d\n",
				   __func__, (psta->hwaddr),
				   pmlmepriv->num_sta_ht_no_gf);
		}

		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH) == 0) {
			if (!psta->ht_20mhz_set) {
				psta->ht_20mhz_set = 1;
				pmlmepriv->num_sta_ht_20mhz++;
			}
			DBG_88E("%s STA %pM - 20 MHz HT, num of 20MHz HT STAs %d\n",
				   __func__, (psta->hwaddr),
				   pmlmepriv->num_sta_ht_20mhz);
		}
	} else {
		if (!psta->no_ht_set) {
			psta->no_ht_set = 1;
			pmlmepriv->num_sta_no_ht++;
		}
		if (pmlmepriv->htpriv.ht_option) {
			DBG_88E("%s STA %pM - no HT, num of non-HT stations %d\n",
				__func__, (psta->hwaddr),
				pmlmepriv->num_sta_no_ht);
		}
	}

	if (rtw_ht_operation_update(padapter) > 0) {
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, false);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, true);
	}

	/* update associated stations cap. */
	associated_clients_update(padapter,  beacon_updated);

	DBG_88E("%s, updated =%d\n", __func__, beacon_updated);
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

	DBG_88E("%s, updated =%d\n", __func__, beacon_updated);

	return beacon_updated;
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

	spin_lock_bh(&(pstapriv->sta_hash_lock));
	rtw_free_stainfo(padapter, psta);
	spin_unlock_bh(&(pstapriv->sta_hash_lock));

	return beacon_updated;
}

int rtw_ap_inform_ch_switch(struct adapter *padapter, u8 new_ch, u8 ch_offset)
{
	struct list_head *phead, *plist;
	int ret = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	DBG_88E(FUNC_NDEV_FMT" with ch:%u, offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev), new_ch, ch_offset);

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	plist = phead->next;

	/* for each sta in asoc_queue */
	while (phead != plist) {
		psta = container_of(plist, struct sta_info, asoc_list);
		plist = plist->next;

		issue_action_spct_ch_switch(padapter, psta->hwaddr, new_ch, ch_offset);
		psta->expire_to = ((pstapriv->expire_to * 2) > 5) ? 5 : (pstapriv->expire_to * 2);
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	issue_action_spct_ch_switch(padapter, bc_addr, new_ch, ch_offset);

	return ret;
}

int rtw_sta_flush(struct adapter *padapter)
{
	struct list_head *phead, *plist;
	int ret = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	DBG_88E(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
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
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	/* update wmm cap. */
	if (WLAN_STA_WME&flags)
		psta->qos_option = 1;
	else
		psta->qos_option = 0;

	if (pmlmepriv->qospriv.qos_option == 0)
		psta->qos_option = 0;

	/* update 802.11n ht cap. */
	if (WLAN_STA_HT&flags) {
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
	if (psta->state & _FW_LINKED) {
		/* add ratid */
		add_RATid(padapter, psta, 0);/* DM_RATR_STA_INIT */
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
	struct list_head *phead, *plist;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
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
	spin_lock_bh(&(pacl_node_q->lock));
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
	spin_unlock_bh(&(pacl_node_q->lock));

	DBG_88E("%s, free acl_node_queue, num =%d\n", __func__, pacl_list->num);

	rtw_sta_flush(padapter);

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo(padapter);

	psta = rtw_get_bcmc_stainfo(padapter);
	spin_lock_bh(&(pstapriv->sta_hash_lock));
	rtw_free_stainfo(padapter, psta);
	spin_unlock_bh(&(pstapriv->sta_hash_lock));

	rtw_init_bcmc_stainfo(padapter);

	rtw_free_mlme_priv_ie_data(pmlmepriv);
}

#endif /* CONFIG_88EU_AP_MODE */
