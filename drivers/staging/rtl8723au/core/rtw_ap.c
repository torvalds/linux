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
 ******************************************************************************/
#define _RTW_AP_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <linux/ieee80211.h>
#include <wifi.h>
#include <rtl8723a_cmd.h>
#include <rtl8723a_hal.h>

extern unsigned char WMM_OUI23A[];
extern unsigned char WPS_OUI23A[];
extern unsigned char P2P_OUI23A[];

void init_mlme_ap_info23a(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	spin_lock_init(&pmlmepriv->bcn_update_lock);

	/* for ACL */
	_rtw_init_queue23a(&pacl_list->acl_node_q);

	start_ap_mode23a(padapter);
}

void free_mlme_ap_info23a(struct rtw_adapter *padapter)
{
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	rtw_sta_flush23a(padapter);

	pmlmeinfo->state = _HW_STATE_NOLINK_;

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo23a(padapter);

	/* free bc/mc sta_info */
	psta = rtw_get_bcmc_stainfo23a(padapter);
	spin_lock_bh(&pstapriv->sta_hash_lock);
	rtw_free_stainfo23a(padapter, psta);
	spin_unlock_bh(&pstapriv->sta_hash_lock);
}

static void update_BCNTIM(struct rtw_adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork_mlmeext = &pmlmeinfo->network;
	unsigned char *pie = pnetwork_mlmeext->IEs;
	u8 *p, *dst_ie, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
	__le16 tim_bitmap_le;
	uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

	tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);

	p = rtw_get_ie23a(pie, WLAN_EID_TIM, &tim_ielen,
			  pnetwork_mlmeext->IELength);
	if (p != NULL && tim_ielen>0) {
		tim_ielen += 2;

		premainder_ie = p+tim_ielen;

		tim_ie_offset = (int)(p -pie);

		remainder_ielen = pnetwork_mlmeext->IELength - tim_ie_offset - tim_ielen;

		/* append TIM IE from dst_ie offset */
		dst_ie = p;
	} else {
		tim_ielen = 0;

		/* calulate head_len */
		offset = 0;

		/* get ssid_ie len */
		p = rtw_get_ie23a(pie, WLAN_EID_SSID,
				  &tmp_len, pnetwork_mlmeext->IELength);
		if (p != NULL)
			offset += tmp_len+2;

		/*  get supported rates len */
		p = rtw_get_ie23a(pie, WLAN_EID_SUPP_RATES,
				  &tmp_len, pnetwork_mlmeext->IELength);
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
		pbackup_remainder_ie = kmalloc(remainder_ielen, GFP_ATOMIC);
		if (pbackup_remainder_ie && premainder_ie)
			memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	*dst_ie++= WLAN_EID_TIM;

	if ((pstapriv->tim_bitmap&0xff00) && (pstapriv->tim_bitmap&0x00fc))
		tim_ielen = 5;
	else
		tim_ielen = 4;

	*dst_ie++= tim_ielen;

	*dst_ie++= 0;/* DTIM count */
	*dst_ie++= 1;/* DTIM peroid */

	if (pstapriv->tim_bitmap & BIT(0))/* for bc/mc frames */
		*dst_ie++ = BIT(0);/* bitmap ctrl */
	else
		*dst_ie++ = 0;

	if (tim_ielen == 4) {
		*dst_ie++ = *(u8*)&tim_bitmap_le;
	} else if (tim_ielen == 5) {
		memcpy(dst_ie, &tim_bitmap_le, 2);
		dst_ie+= 2;
	}

	/* copy remainder IE */
	if (pbackup_remainder_ie) {
		memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		kfree(pbackup_remainder_ie);
	}

	offset =  (uint)(dst_ie - pie);
	pnetwork_mlmeext->IELength = offset + remainder_ielen;

	set_tx_beacon_cmd23a(padapter);
}

static u8 chk_sta_is_alive(struct sta_info *psta)
{
	u8 ret = false;

	if ((psta->sta_stats.last_rx_data_pkts +
	    psta->sta_stats.last_rx_ctrl_pkts) !=
	    (psta->sta_stats.rx_data_pkts + psta->sta_stats.rx_ctrl_pkts))
		ret = true;

	sta_update_last_rx_pkts(psta);

	return ret;
}

void	expire_timeout_chk23a(struct rtw_adapter *padapter)
{
	struct list_head *phead, *plist, *ptmp;
	u8 updated = 0;
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 chk_alive_num = 0;
	struct sta_info *chk_alive_list[NUM_STA];
	int i;

	spin_lock_bh(&pstapriv->auth_list_lock);

	phead = &pstapriv->auth_list;

	/* check auth_queue */
	list_for_each_safe(plist, ptmp, phead) {
		psta = container_of(plist, struct sta_info, auth_list);

		if (psta->expire_to>0) {
			psta->expire_to--;
			if (psta->expire_to == 0) {
				list_del_init(&psta->auth_list);
				pstapriv->auth_list_cnt--;

				DBG_8723A("auth expire %pM\n", psta->hwaddr);

				spin_unlock_bh(&pstapriv->auth_list_lock);

				spin_lock_bh(&pstapriv->sta_hash_lock);
				rtw_free_stainfo23a(padapter, psta);
				spin_unlock_bh(&pstapriv->sta_hash_lock);

				spin_lock_bh(&pstapriv->auth_list_lock);
			}
		}

	}

	spin_unlock_bh(&pstapriv->auth_list_lock);

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;

	/* check asoc_queue */
	list_for_each_safe(plist, ptmp, phead) {
		psta = container_of(plist, struct sta_info, asoc_list);

		if (chk_sta_is_alive(psta) || !psta->expire_to) {
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
		} else {
			psta->expire_to--;
		}

		if (psta->expire_to <= 0)
		{
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			if (padapter->registrypriv.wifi_spec == 1)
			{
				psta->expire_to = pstapriv->expire_to;
				continue;
			}

			if (psta->state & WIFI_SLEEP_STATE) {
				if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
					/* to check if alive by another methods if staion is at ps mode. */
					psta->expire_to = pstapriv->expire_to;
					psta->state |= WIFI_STA_ALIVE_CHK_STATE;

					/* to update bcn with tim_bitmap for this station */
					pstapriv->tim_bitmap |= CHKBIT(psta->aid);
					update_beacon23a(padapter, WLAN_EID_TIM, NULL, false);

					if (!pmlmeext->active_keep_alive_check)
						continue;
				}
			}

			if (pmlmeext->active_keep_alive_check) {
				chk_alive_list[chk_alive_num++] = psta;
				continue;
			}

			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;

			DBG_8723A("asoc expire "MAC_FMT", state = 0x%x\n", MAC_ARG(psta->hwaddr), psta->state);
			updated = ap_free_sta23a(padapter, psta, false, WLAN_REASON_DEAUTH_LEAVING);
		} else {
			/* TODO: Aging mechanism to digest frames in sleep_q to avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME/pstapriv->asoc_list_cnt)
				&& padapter->xmitpriv.free_xmitframe_cnt < ((NR_XMITFRAME/pstapriv->asoc_list_cnt)/2)
			) {
				DBG_8723A("%s sta:"MAC_FMT", sleepq_len:%u, free_xmitframe_cnt:%u, asoc_list_cnt:%u, clear sleep_q\n", __func__,
					  MAC_ARG(psta->hwaddr),
					  psta->sleepq_len,
					  padapter->xmitpriv.free_xmitframe_cnt,
					  pstapriv->asoc_list_cnt);
				wakeup_sta_to_xmit23a(padapter, psta);
			}
		}
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);

	if (chk_alive_num) {

		u8 backup_oper_channel = 0;
		struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
		/* switch to correct channel of current network  before issue keep-alive frames */
		if (rtw_get_oper_ch23a(padapter) != pmlmeext->cur_channel) {
			backup_oper_channel = rtw_get_oper_ch23a(padapter);
			SelectChannel23a(padapter, pmlmeext->cur_channel);
		}

	/* issue null data to check sta alive*/
	for (i = 0; i < chk_alive_num; i++) {

		int ret = _FAIL;

		psta = chk_alive_list[i];
		if (!(psta->state &_FW_LINKED))
			continue;

		if (psta->state & WIFI_SLEEP_STATE)
			ret = issue_nulldata23a(padapter, psta->hwaddr, 0, 1, 50);
		else
			ret = issue_nulldata23a(padapter, psta->hwaddr, 0, 3, 50);

		psta->keep_alive_trycnt++;
		if (ret == _SUCCESS)
		{
			DBG_8723A("asoc check, sta(" MAC_FMT ") is alive\n", MAC_ARG(psta->hwaddr));
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
			continue;
		}
		else if (psta->keep_alive_trycnt <= 3)
		{
			DBG_8723A("ack check for asoc expire, keep_alive_trycnt =%d\n", psta->keep_alive_trycnt);
			psta->expire_to = 1;
			continue;
		}

		psta->keep_alive_trycnt = 0;

		DBG_8723A("asoc expire "MAC_FMT", state = 0x%x\n", MAC_ARG(psta->hwaddr), psta->state);
		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (!list_empty(&psta->asoc_list)) {
			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta23a(padapter, psta, false, WLAN_REASON_DEAUTH_LEAVING);
		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);

	}

	if (backup_oper_channel>0) /* back to the original operation channel */
		SelectChannel23a(padapter, backup_oper_channel);
}

	associated_clients_update23a(padapter, updated);
}

void add_RATid23a(struct rtw_adapter *padapter, struct sta_info *psta, u8 rssi_level)
{
	int i;
	u8 rf_type;
	u32 init_rate = 0;
	unsigned char sta_band = 0, raid, shortGIrate = false;
	unsigned char limit;
	unsigned int tx_ra_bitmap = 0;
	struct ht_priv *psta_ht = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pcur_network = &pmlmepriv->cur_network.network;

	if (psta)
		psta_ht = &psta->htpriv;
	else
		return;

	if (!(psta->state & _FW_LINKED))
		return;

	/* b/g mode ra_bitmap */
	for (i = 0; i<sizeof(psta->bssrateset); i++)
	{
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value23a(psta->bssrateset[i]&0x7f);
	}
	/* n mode ra_bitmap */
	if (psta_ht->ht_option) {
		rf_type = rtl8723a_get_rf_type(padapter);

		if (rf_type == RF_2T2R)
			limit = 16;/*  2R */
		else
			limit = 8;/*   1R */

		for (i = 0; i < limit; i++) {
			if (psta_ht->ht_cap.mcs.rx_mask[i / 8] & BIT(i % 8))
				tx_ra_bitmap |= BIT(i + 12);
		}

		/* max short GI rate */
		shortGIrate = psta_ht->sgi;
	}

	if (pcur_network->DSConfig > 14) {
		/*  5G band */
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_5N | WIRELESS_11A;
		else
			sta_band |= WIRELESS_11A;
	} else {
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_24N | WIRELESS_11G | WIRELESS_11B;
		else if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11G |WIRELESS_11B;
		else
			sta_band |= WIRELESS_11B;
	}

	psta->wireless_mode = sta_band;

	raid = networktype_to_raid23a(sta_band);
	init_rate = get_highest_rate_idx23a(tx_ra_bitmap&0x0fffffff)&0x3f;

	if (psta->aid < NUM_STA)
	{
		u8 arg = 0;

		arg = psta->mac_id&0x1f;

		arg |= BIT(7);/* support entry 2~31 */

		if (shortGIrate == true)
			arg |= BIT(5);

		tx_ra_bitmap |= ((raid<<28)&0xf0000000);

		DBG_8723A("%s => mac_id:%d , raid:%d , bitmap = 0x%x, arg = "
			  "0x%x\n",
			  __func__, psta->mac_id, raid, tx_ra_bitmap, arg);

		/* bitmap[0:27] = tx_rate_bitmap */
		/* bitmap[28:31]= Rate Adaptive id */
		/* arg[0:4] = macid */
		/* arg[5] = Short GI */
		rtl8723a_add_rateatid(padapter, tx_ra_bitmap, arg, rssi_level);

		if (shortGIrate == true)
			init_rate |= BIT(6);

		/* set ra_id, init_rate */
		psta->raid = raid;
		psta->init_rate = init_rate;

	}
	else
	{
		DBG_8723A("station aid %d exceed the max number\n", psta->aid);
	}
}

static void update_bmc_sta(struct rtw_adapter *padapter)
{
	u32 init_rate = 0;
	unsigned char network_type, raid;
	int i, supportRateNum = 0;
	unsigned int tx_ra_bitmap = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pcur_network = &pmlmepriv->cur_network.network;
	struct sta_info *psta = rtw_get_bcmc_stainfo23a(padapter);

	if (psta)
	{
		psta->aid = 0;/* default set to 0 */
		psta->mac_id = psta->aid + 1;

		psta->qos_option = 0;
		psta->htpriv.ht_option = false;

		psta->ieee8021x_blocked = 0;

		memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		/* prepare for add_RATid23a */
		supportRateNum = rtw_get_rateset_len23a((u8*)&pcur_network->SupportedRates);
		network_type = rtw_check_network_type23a((u8*)&pcur_network->SupportedRates, supportRateNum, 1);

		memcpy(psta->bssrateset, &pcur_network->SupportedRates, supportRateNum);
		psta->bssratelen = supportRateNum;

		/* b/g mode ra_bitmap */
		for (i = 0; i<supportRateNum; i++)
		{
			if (psta->bssrateset[i])
				tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value23a(psta->bssrateset[i]&0x7f);
		}

		if (pcur_network->DSConfig > 14) {
			/* force to A mode. 5G doesn't support CCK rates */
			network_type = WIRELESS_11A;
			tx_ra_bitmap = 0x150; /*  6, 12, 24 Mbps */
		} else {
			/* force to b mode */
			network_type = WIRELESS_11B;
			tx_ra_bitmap = 0xf;
		}

		raid = networktype_to_raid23a(network_type);
		init_rate = get_highest_rate_idx23a(tx_ra_bitmap&0x0fffffff)&0x3f;

		/* ap mode */
		rtl8723a_SetHalODMVar(padapter, HAL_ODM_STA_INFO, psta, true);

		{
			u8 arg = 0;

			arg = psta->mac_id&0x1f;

			arg |= BIT(7);

			tx_ra_bitmap |= ((raid<<28)&0xf0000000);

			DBG_8723A("update_bmc_sta, mask = 0x%x, arg = 0x%x\n", tx_ra_bitmap, arg);

			/* bitmap[0:27] = tx_rate_bitmap */
			/* bitmap[28:31]= Rate Adaptive id */
			/* arg[0:4] = macid */
			/* arg[5] = Short GI */
			rtl8723a_add_rateatid(padapter, tx_ra_bitmap, arg, 0);
		}

		/* set ra_id, init_rate */
		psta->raid = raid;
		psta->init_rate = init_rate;

		spin_lock_bh(&psta->lock);
		psta->state = _FW_LINKED;
		spin_unlock_bh(&psta->lock);

	}
	else
	{
		DBG_8723A("add_RATid23a_bmc_sta error!\n");
	}
}

/* notes: */
/* AID: 1~MAX for sta and 0 for bc/mc in ap/adhoc mode */
/* MAC_ID = AID+1 for sta in ap/adhoc mode */
/* MAC_ID = 1 for bc/mc for sta/ap/adhoc */
/* MAC_ID = 0 for bssid for sta/ap/adhoc */
/* CAM_ID = 0~3 for default key, cmd_id = macid + 3, macid = aid+1; */

void update_sta_info23a_apmode23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct ht_priv *phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv *phtpriv_sta = &psta->htpriv;
	/* set intf_tag to if1 */

	psta->mac_id = psta->aid+1;
	DBG_8723A("%s\n", __func__);

	/* ap mode */
	rtl8723a_SetHalODMVar(padapter, HAL_ODM_STA_INFO, psta, true);

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->ieee8021x_blocked = true;
	else
		psta->ieee8021x_blocked = false;

	/* update sta's cap */

	/* ERP */
	VCS_update23a(padapter, psta);
	/* HT related cap */
	if (phtpriv_sta->ht_option)
	{
		/* check if sta supports rx ampdu */
		phtpriv_sta->ampdu_enable = phtpriv_ap->ampdu_enable;

		/* check if sta support s Short GI */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40))
			phtpriv_sta->sgi = true;

		/*  bwmode */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH_20_40)) {
			/* phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_40; */
			phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
			phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;

		}

		psta->qos_option = true;

	}
	else
	{
		phtpriv_sta->ampdu_enable = false;

		phtpriv_sta->sgi = false;
		phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	/* Rx AMPDU */
	send_delba23a(padapter, 0, psta->hwaddr);/*  recipient */

	/* TX AMPDU */
	send_delba23a(padapter, 1, psta->hwaddr);/*  originator */
	phtpriv_sta->agg_enable_bitmap = 0x0;/* reset */
	phtpriv_sta->candidate_tid_bitmap = 0x0;/* reset */

	/* todo: init other variables */

	memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

	spin_lock_bh(&psta->lock);
	psta->state |= _FW_LINKED;
	spin_unlock_bh(&psta->lock);
}

static void update_hw_ht_param(struct rtw_adapter *padapter)
{
	unsigned char max_AMPDU_len;
	unsigned char min_MPDU_spacing;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	DBG_8723A("%s\n", __func__);

	/* handle A-MPDU parameter field */
	/*
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing
	*/
	max_AMPDU_len = pmlmeinfo->ht_cap.ampdu_params_info &
		IEEE80211_HT_AMPDU_PARM_FACTOR;

	min_MPDU_spacing = (pmlmeinfo->ht_cap.ampdu_params_info &
			    IEEE80211_HT_AMPDU_PARM_DENSITY) >> 2;

	rtl8723a_set_ampdu_min_space(padapter, min_MPDU_spacing);
	rtl8723a_set_ampdu_factor(padapter, max_AMPDU_len);

	/*  Config SM Power Save setting */
	pmlmeinfo->SM_PS = (le16_to_cpu(pmlmeinfo->ht_cap.cap_info) &
			    IEEE80211_HT_CAP_SM_PS) >> 2;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
		DBG_8723A("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __func__);
}

static void start_bss_network(struct rtw_adapter *padapter, u8 *pbuf)
{
	const u8 *p;
	u8 val8, cur_channel, cur_bwmode, cur_ch_offset;
	u16 bcn_interval;
	u32 acparm;
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv* psecuritypriv = &padapter->securitypriv;
	struct wlan_bssid_ex *pnetwork = &pmlmepriv->cur_network.network;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork_mlmeext = &pmlmeinfo->network;
	struct ieee80211_ht_operation *pht_info = NULL;

	bcn_interval = (u16)pnetwork->beacon_interval;
	cur_channel = pnetwork->DSConfig;
	cur_bwmode = HT_CHANNEL_WIDTH_20;;
	cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	/* check if there is wps ie, */
	/* if there is wpsie in beacon, the hostapd will update beacon twice when stating hostapd, */
	/* and at first time the security ie (RSN/WPA IE) will not include in beacon. */
	if (NULL == cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					    WLAN_OUI_TYPE_MICROSOFT_WPS,
					    pnetwork->IEs,
					    pnetwork->IELength))
		pmlmeext->bstart_bss = true;

	/* todo: update wmm, ht cap */
	/* pmlmeinfo->WMM_enable; */
	/* pmlmeinfo->HT_enable; */
	if (pmlmepriv->qos_option)
		pmlmeinfo->WMM_enable = true;
	if (pmlmepriv->htpriv.ht_option) {
		pmlmeinfo->WMM_enable = true;
		pmlmeinfo->HT_enable = true;

		update_hw_ht_param(padapter);
	}

	if (pmlmepriv->cur_network.join_res != true) {
		/* setting only at  first time */
		/* WEP Key will be set before this function, do not clear CAM. */
		if (psecuritypriv->dot11PrivacyAlgrthm !=
		    WLAN_CIPHER_SUITE_WEP40 &&
		    psecuritypriv->dot11PrivacyAlgrthm !=
		    WLAN_CIPHER_SUITE_WEP104)
			flush_all_cam_entry23a(padapter);	/* clear CAM */
	}

	/* set MSR to AP_Mode */
	rtl8723a_set_media_status(padapter, _HW_STATE_AP_);

	/* Set BSSID REG */
	hw_var_set_bssid(padapter, pnetwork->MacAddress);

	/* Set EDCA param reg */
	acparm = 0x002F3217; /*  VO */
	rtl8723a_set_ac_param_vo(padapter, acparm);
	acparm = 0x005E4317; /*  VI */
	rtl8723a_set_ac_param_vi(padapter, acparm);
	acparm = 0x005ea42b;
	rtl8723a_set_ac_param_be(padapter, acparm);
	acparm = 0x0000A444; /*  BK */
	rtl8723a_set_ac_param_bk(padapter, acparm);

	/* Set Security */
	val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) ?
		0xcc: 0xcf;
	rtl8723a_set_sec_cfg(padapter, val8);

	/* Beacon Control related register */
	rtl8723a_set_beacon_interval(padapter, bcn_interval);

	UpdateBrateTbl23a(padapter, pnetwork->SupportedRates);
	HalSetBrateCfg23a(padapter, pnetwork->SupportedRates);

	if (!pmlmepriv->cur_network.join_res) {
		/* setting only at  first time */

		/* disable dynamic functions, such as high power, DIG */

		/* turn on all dynamic functions */
		rtl8723a_odm_support_ability_set(padapter,
						 DYNAMIC_ALL_FUNC_ENABLE);
	}
	/* set channel, bwmode */

	p = cfg80211_find_ie(WLAN_EID_HT_OPERATION, pnetwork->IEs,
			     pnetwork->IELength);
	if (p && p[1]) {
		pht_info = (struct ieee80211_ht_operation *)(p + 2);

		if (pregpriv->cbw40_enable && pht_info->ht_param &
		    IEEE80211_HT_PARAM_CHAN_WIDTH_ANY) {
			/* switch to the 40M Hz mode */
			cur_bwmode = HT_CHANNEL_WIDTH_40;
			switch (pht_info->ht_param &
				IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				/* pmlmeext->cur_ch_offset =
				   HAL_PRIME_CHNL_OFFSET_LOWER; */
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;
			default:
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
			}
		}
	}
	/* TODO: need to judge the phy parameters on concurrent mode for single phy */
	set_channel_bwmode23a(padapter, cur_channel, cur_ch_offset, cur_bwmode);

	DBG_8723A("CH =%d, BW =%d, offset =%d\n", cur_channel, cur_bwmode,
		  cur_ch_offset);

	pmlmeext->cur_channel = cur_channel;
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;
	pmlmeext->cur_wireless_mode = pmlmepriv->cur_network.network_type;

	/* update cur_wireless_mode */
	update_wireless_mode23a(padapter);

	/* udpate capability after cur_wireless_mode updated */
	update_capinfo23a(padapter, pnetwork->capability);

	/* let pnetwork_mlmeext == pnetwork_mlme. */
	memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);

	if (pmlmeext->bstart_bss) {
		update_beacon23a(padapter, WLAN_EID_TIM, NULL, false);

		/* issue beacon frame */
		if (send_beacon23a(padapter) == _FAIL)
			DBG_8723A("issue_beacon23a, fail!\n");
	}

	/* update bc/mc sta_info */
	update_bmc_sta(padapter);
}

int rtw_check_beacon_data23a(struct rtw_adapter *padapter,
			     struct ieee80211_mgmt *mgmt, unsigned int len)
{
	int ret = _SUCCESS;
	u8 *p;
	u8 *pHT_caps_ie = NULL;
	u8 *pHT_info_ie = NULL;
	struct sta_info *psta = NULL;
	u16 ht_cap = false;
	uint ie_len = 0;
	int group_cipher, pairwise_cipher;
	u8 channel, network_type, supportRate[NDIS_802_11_LENGTH_RATES_EX];
	int supportRateNum = 0;
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pbss_network = &pmlmepriv->cur_network.network;
	u8 *ie = pbss_network->IEs;
	u8 *pbuf = mgmt->u.beacon.variable;
	len -= offsetof(struct ieee80211_mgmt, u.beacon.variable);
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

	DBG_8723A("%s, len =%d\n", __func__, len);

	if (!check_fwstate(pmlmepriv, WIFI_AP_STATE))
		return _FAIL;

	if (len > MAX_IE_SZ)
		return _FAIL;

	pbss_network->IELength = len;

	memset(ie, 0, MAX_IE_SZ);

	memcpy(ie, pbuf, pbss_network->IELength);

	if (pbss_network->ifmode != NL80211_IFTYPE_AP &&
	    pbss_network->ifmode != NL80211_IFTYPE_P2P_GO)
		return _FAIL;

	pbss_network->Rssi = 0;

	memcpy(pbss_network->MacAddress, myid(&padapter->eeprompriv), ETH_ALEN);

	/* SSID */
	p = rtw_get_ie23a(ie, WLAN_EID_SSID, &ie_len, pbss_network->IELength);
	if (p && ie_len > 0) {
		memset(&pbss_network->Ssid, 0, sizeof(struct cfg80211_ssid));
		memcpy(pbss_network->Ssid.ssid, (p + 2), ie_len);
		pbss_network->Ssid.ssid_len = ie_len;
	}

	/* chnnel */
	channel = 0;
	p = rtw_get_ie23a(ie, WLAN_EID_DS_PARAMS, &ie_len,
			  pbss_network->IELength);
	if (p && ie_len > 0)
		channel = *(p + 2);

	pbss_network->DSConfig = channel;

	memset(supportRate, 0, NDIS_802_11_LENGTH_RATES_EX);
	/*  get supported rates */
	p = rtw_get_ie23a(ie, WLAN_EID_SUPP_RATES, &ie_len,
			  pbss_network->IELength);
	if (p) {
		memcpy(supportRate, p+2, ie_len);
		supportRateNum = ie_len;
	}

	/* get ext_supported rates */
	p = rtw_get_ie23a(ie, WLAN_EID_EXT_SUPP_RATES,
			  &ie_len, pbss_network->IELength);
	if (p) {
		memcpy(supportRate+supportRateNum, p+2, ie_len);
		supportRateNum += ie_len;
	}

	network_type = rtw_check_network_type23a(supportRate,
						 supportRateNum, channel);

	rtw_set_supported_rate23a(pbss_network->SupportedRates, network_type);

	/* parsing ERP_IE */
	p = rtw_get_ie23a(ie, WLAN_EID_ERP_INFO, &ie_len,
			  pbss_network->IELength);
	if (p && ie_len > 0)
		ERP_IE_handler23a(padapter, p);

	/* update privacy/security */
	if (pbss_network->capability & BIT(4))
		pbss_network->Privacy = 1;
	else
		pbss_network->Privacy = 0;

	psecuritypriv->wpa_psk = 0;

	/* wpa2 */
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa2_group_cipher = 0;
	psecuritypriv->wpa2_pairwise_cipher = 0;
	p = rtw_get_ie23a(ie, WLAN_EID_RSN, &ie_len,
			  pbss_network->IELength);
	if (p && ie_len > 0) {
		if (rtw_parse_wpa2_ie23a(p, ie_len+2, &group_cipher,
					 &pairwise_cipher, NULL) == _SUCCESS) {
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
	psecuritypriv->wpa_group_cipher = 0;
	psecuritypriv->wpa_pairwise_cipher = 0;
	for (p = ie; ;p += (ie_len + 2)) {
		p = rtw_get_ie23a(p, WLAN_EID_VENDOR_SPECIFIC, &ie_len,
				  pbss_network->IELength - (ie_len + 2));
		if ((p) && (!memcmp(p+2, RTW_WPA_OUI23A_TYPE, 4))) {
			if (rtw_parse_wpa_ie23a(p, ie_len+2, &group_cipher,
						&pairwise_cipher, NULL) == _SUCCESS) {
				psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

				/* psk,  todo:802.1x */
				psecuritypriv->dot8021xalg = 1;

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
	pmlmepriv->qos_option = 0;
	if (pregistrypriv->wmm_enable) {
		for (p = ie; ;p += (ie_len + 2)) {
			p = rtw_get_ie23a(p, WLAN_EID_VENDOR_SPECIFIC, &ie_len,
					  (pbss_network->IELength -
					   (ie_len + 2)));
			if ((p) && !memcmp(p+2, WMM_PARA_IE, 6)) {
				pmlmepriv->qos_option = 1;

				*(p+8) |= BIT(7);/* QoS Info, support U-APSD */

				/* disable all ACM bits since the WMM admission
				 * control is not supported
				 */
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
	p = rtw_get_ie23a(ie, WLAN_EID_HT_CAPABILITY, &ie_len,
			  pbss_network->IELength);
	if (p && ie_len > 0) {
		u8 rf_type;

		struct ieee80211_ht_cap *pht_cap = (struct ieee80211_ht_cap *)(p+2);

		pHT_caps_ie = p;

		ht_cap = true;
		network_type |= WIRELESS_11_24N;

		rf_type = rtl8723a_get_rf_type(padapter);

		if ((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
		    (psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP))
			pht_cap->ampdu_params_info |= (IEEE80211_HT_AMPDU_PARM_DENSITY & (0x07<<2));
		else
			pht_cap->ampdu_params_info |= (IEEE80211_HT_AMPDU_PARM_DENSITY&0x00);

		/* set  Max Rx AMPDU size  to 64K */
		pht_cap->ampdu_params_info |= (IEEE80211_HT_AMPDU_PARM_FACTOR & 0x03);

		if (rf_type == RF_1T1R) {
			pht_cap->mcs.rx_mask[0] = 0xff;
			pht_cap->mcs.rx_mask[1] = 0x0;
		}

		memcpy(&pmlmepriv->htpriv.ht_cap, p+2, ie_len);
	}

	/* parsing HT_INFO_IE */
	p = rtw_get_ie23a(ie, WLAN_EID_HT_OPERATION, &ie_len,
			  pbss_network->IELength);
	if (p && ie_len > 0)
		pHT_info_ie = p;

	pmlmepriv->cur_network.network_type = network_type;

	pmlmepriv->htpriv.ht_option = false;

	/* ht_cap */
	if (pregistrypriv->ht_enable && ht_cap) {
		pmlmepriv->htpriv.ht_option = true;
		pmlmepriv->qos_option = 1;

		if (pregistrypriv->ampdu_enable == 1)
			pmlmepriv->htpriv.ampdu_enable = true;

		HT_caps_handler23a(padapter, pHT_caps_ie);

		HT_info_handler23a(padapter, pHT_info_ie);
	}

	pbss_network->Length = get_wlan_bssid_ex_sz(pbss_network);

	/* issue beacon to start bss network */
	start_bss_network(padapter, (u8*)pbss_network);

	/* alloc sta_info for ap itself */
	psta = rtw_get_stainfo23a(&padapter->stapriv, pbss_network->MacAddress);
	if (!psta) {
		psta = rtw_alloc_stainfo23a(&padapter->stapriv,
					    pbss_network->MacAddress,
					    GFP_KERNEL);
		if (!psta)
			return _FAIL;
	}
	/* fix bug of flush_cam_entry at STOP AP mode */
	psta->state |= WIFI_AP_STATE;
	rtw_indicate_connect23a(padapter);

	/* for check if already set beacon */
	pmlmepriv->cur_network.join_res = true;

	return ret;
}

void rtw_set_macaddr_acl23a(struct rtw_adapter *padapter, int mode)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	DBG_8723A("%s, mode =%d\n", __func__, mode);

	pacl_list->mode = mode;
}

int rtw_acl_add_sta23a(struct rtw_adapter *padapter, u8 *addr)
{
	struct list_head *plist, *phead;
	u8 added = false;
	int i, ret = 0;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct rtw_queue *pacl_node_q = &pacl_list->acl_node_q;

	DBG_8723A("%s(acl_num =%d) =" MAC_FMT "\n", __func__, pacl_list->num, MAC_ARG(addr));

	if ((NUM_ACL-1) < pacl_list->num)
		return -1;

	spin_lock_bh(&pacl_node_q->lock);

	phead = get_list_head(pacl_node_q);

	list_for_each(plist, phead) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);

		if (!memcmp(paclnode->addr, addr, ETH_ALEN)) {
			if (paclnode->valid == true) {
				added = true;
				DBG_8723A("%s, sta has been added\n", __func__);
				break;
			}
		}
	}

	spin_unlock_bh(&pacl_node_q->lock);

	if (added)
		return ret;

	spin_lock_bh(&pacl_node_q->lock);

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

	DBG_8723A("%s, acl_num =%d\n", __func__, pacl_list->num);

	spin_unlock_bh(&pacl_node_q->lock);
	return ret;
}

int rtw_acl_remove_sta23a(struct rtw_adapter *padapter, u8 *addr)
{
	struct list_head *plist, *phead, *ptmp;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct rtw_queue *pacl_node_q = &pacl_list->acl_node_q;
	int ret = 0;

	DBG_8723A("%s(acl_num =%d) = %pM\n", __func__, pacl_list->num, addr);

	spin_lock_bh(&pacl_node_q->lock);

	phead = get_list_head(pacl_node_q);

	list_for_each_safe(plist, ptmp, phead) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);

		if (!memcmp(paclnode->addr, addr, ETH_ALEN)) {
			if (paclnode->valid) {
				paclnode->valid = false;

				list_del_init(&paclnode->list);

				pacl_list->num--;
			}
		}
	}

	spin_unlock_bh(&pacl_node_q->lock);

	DBG_8723A("%s, acl_num =%d\n", __func__, pacl_list->num);

	return ret;
}

static void update_bcn_fixed_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);
}

static void update_bcn_erpinfo_ie(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	unsigned char *p, *ie = pnetwork->IEs;
	u32 len = 0;

	DBG_8723A("%s, ERP_enable =%d\n", __func__, pmlmeinfo->ERP_enable);

	if (!pmlmeinfo->ERP_enable)
		return;

	/* parsing ERP_IE */
	p = rtw_get_ie23a(ie, WLAN_EID_ERP_INFO, &len, pnetwork->IELength);
	if (p && len > 0) {
		if (pmlmepriv->num_sta_non_erp == 1)
			p[2] |= WLAN_ERP_NON_ERP_PRESENT |
				WLAN_ERP_USE_PROTECTION;
		else
			p[2] &= ~(WLAN_ERP_NON_ERP_PRESENT |
				  WLAN_ERP_USE_PROTECTION);

		if (pmlmepriv->num_sta_no_short_preamble > 0)
			p[2] |= WLAN_ERP_BARKER_PREAMBLE;
		else
			p[2] &= ~(WLAN_ERP_BARKER_PREAMBLE);

		ERP_IE_handler23a(padapter, p);
	}
}

static void update_bcn_htcap_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);
}

static void update_bcn_htinfo_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);
}

static void update_bcn_rsn_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);
}

static void update_bcn_wpa_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);
}

static void update_bcn_wmm_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);
}

static void update_bcn_wps_ie(struct rtw_adapter *padapter)
{
	DBG_8723A("%s\n", __func__);

	return;
}

static void update_bcn_p2p_ie(struct rtw_adapter *padapter)
{
}

static void update_bcn_vendor_spec_ie(struct rtw_adapter *padapter, u8*oui)
{
	DBG_8723A("%s\n", __func__);

	if (!memcmp(RTW_WPA_OUI23A_TYPE, oui, 4))
		update_bcn_wpa_ie(padapter);
	else if (!memcmp(WMM_OUI23A, oui, 4))
		update_bcn_wmm_ie(padapter);
	else if (!memcmp(WPS_OUI23A, oui, 4))
		update_bcn_wps_ie(padapter);
	else if (!memcmp(P2P_OUI23A, oui, 4))
		update_bcn_p2p_ie(padapter);
	else
		DBG_8723A("unknown OUI type!\n");
}

void update_beacon23a(struct rtw_adapter *padapter, u8 ie_id, u8 *oui, u8 tx)
{
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	/* struct mlme_ext_info *pmlmeinfo; */

	/* DBG_8723A("%s\n", __func__); */

	if (!padapter)
		return;

	pmlmepriv = &padapter->mlmepriv;
	pmlmeext = &padapter->mlmeextpriv;
	/* pmlmeinfo = &pmlmeext->mlmext_info; */

	if (false == pmlmeext->bstart_bss)
		return;

	spin_lock_bh(&pmlmepriv->bcn_update_lock);

	switch (ie_id)
	{
	case 0xFF:
		/* 8: TimeStamp, 2: Beacon Interval 2:Capability */
		update_bcn_fixed_ie(padapter);
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

	if (tx)
		set_tx_beacon_cmd23a(padapter);
}

/*
op_mode
Set to 0 (HT pure) under the followign conditions
	- all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
	- all STAs in the BSS are 20 MHz HT in 20 MHz BSS
Set to 1 (HT non-member protection) if there may be non-HT STAs
	in both the primary and the secondary channel
Set to 2 if only HT STAs are associated in BSS,
	however and at least one 20 MHz HT STA is associated
Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
	(currently non-GF HT station is considered as non-HT STA also)
*/
static int rtw_ht_operation_update(struct rtw_adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv_ap = &pmlmepriv->htpriv;

	if (pmlmepriv->htpriv.ht_option)
		return 0;

	/* if (!iface->conf->ieee80211n || iface->conf->ht_op_mode_fixed) */
	/*	return 0; */

	DBG_8723A("%s current operation mode = 0x%X\n",
		   __func__, pmlmepriv->ht_op_mode);

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
	if (pmlmepriv->num_sta_no_ht ||
	    (pmlmepriv->ht_op_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT))
		new_op_mode = IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED;
	else if ((le16_to_cpu(phtpriv_ap->ht_cap.cap_info) &
		  IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
		 pmlmepriv->num_sta_ht_20mhz)
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

	DBG_8723A("%s new operation mode = 0x%X changes =%d\n",
		   __func__, pmlmepriv->ht_op_mode, op_mode_changes);

	return op_mode_changes;
}

void associated_clients_update23a(struct rtw_adapter *padapter, u8 updated)
{
	/* update associcated stations cap. */
	if (updated == true)
	{
		struct list_head *phead, *plist, *ptmp;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		spin_lock_bh(&pstapriv->asoc_list_lock);

		phead = &pstapriv->asoc_list;

		list_for_each_safe(plist, ptmp, phead) {
			psta = container_of(plist, struct sta_info, asoc_list);

			VCS_update23a(padapter, psta);
		}

		spin_unlock_bh(&pstapriv->asoc_list_lock);
	}
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void bss_cap_update_on_sta_join23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = false;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!(psta->flags & WLAN_STA_SHORT_PREAMBLE))
	{
		if (!psta->no_short_preamble_set)
		{
			psta->no_short_preamble_set = 1;

			pmlmepriv->num_sta_no_short_preamble++;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				(pmlmepriv->num_sta_no_short_preamble == 1))
			{
				beacon_updated = true;
				update_beacon23a(padapter, 0xFF, NULL, true);
			}

		}
	}
	else
	{
		if (psta->no_short_preamble_set)
		{
			psta->no_short_preamble_set = 0;

			pmlmepriv->num_sta_no_short_preamble--;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				(pmlmepriv->num_sta_no_short_preamble == 0))
			{
				beacon_updated = true;
				update_beacon23a(padapter, 0xFF, NULL, true);
			}

		}
	}

	if (psta->flags & WLAN_STA_NONERP)
	{
		if (!psta->nonerp_set)
		{
			psta->nonerp_set = 1;

			pmlmepriv->num_sta_non_erp++;

			if (pmlmepriv->num_sta_non_erp == 1)
			{
				beacon_updated = true;
				update_beacon23a(padapter, WLAN_EID_ERP_INFO, NULL, true);
			}
		}

	}
	else
	{
		if (psta->nonerp_set)
		{
			psta->nonerp_set = 0;

			pmlmepriv->num_sta_non_erp--;

			if (pmlmepriv->num_sta_non_erp == 0)
			{
				beacon_updated = true;
				update_beacon23a(padapter, WLAN_EID_ERP_INFO, NULL, true);
			}
		}

	}

	if (!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT_TIME))
	{
		if (!psta->no_short_slot_time_set)
		{
			psta->no_short_slot_time_set = 1;

			pmlmepriv->num_sta_no_short_slot_time++;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				 (pmlmepriv->num_sta_no_short_slot_time == 1))
			{
				beacon_updated = true;
				update_beacon23a(padapter, 0xFF, NULL, true);
			}

		}
	}
	else
	{
		if (psta->no_short_slot_time_set)
		{
			psta->no_short_slot_time_set = 0;

			pmlmepriv->num_sta_no_short_slot_time--;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
				 (pmlmepriv->num_sta_no_short_slot_time == 0))
			{
				beacon_updated = true;
				update_beacon23a(padapter, 0xFF, NULL, true);
			}
		}
	}

	if (psta->flags & WLAN_STA_HT)
	{
		u16 ht_capab = le16_to_cpu(psta->htpriv.ht_cap.cap_info);

		DBG_8723A("HT: STA " MAC_FMT " HT Capabilities "
			   "Info: 0x%04x\n", MAC_ARG(psta->hwaddr), ht_capab);

		if (psta->no_ht_set) {
			psta->no_ht_set = 0;
			pmlmepriv->num_sta_no_ht--;
		}

		if ((ht_capab & IEEE80211_HT_CAP_GRN_FLD) == 0) {
			if (!psta->no_ht_gf_set) {
				psta->no_ht_gf_set = 1;
				pmlmepriv->num_sta_ht_no_gf++;
			}
			DBG_8723A("%s STA " MAC_FMT " - no "
				   "greenfield, num of non-gf stations %d\n",
				   __func__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_ht_no_gf);
		}

		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH_20_40) == 0) {
			if (!psta->ht_20mhz_set) {
				psta->ht_20mhz_set = 1;
				pmlmepriv->num_sta_ht_20mhz++;
			}
			DBG_8723A("%s STA " MAC_FMT " - 20 MHz HT, "
				   "num of 20MHz HT STAs %d\n",
				   __func__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_ht_20mhz);
		}

	}
	else
	{
		if (!psta->no_ht_set) {
			psta->no_ht_set = 1;
			pmlmepriv->num_sta_no_ht++;
		}
		if (pmlmepriv->htpriv.ht_option) {
			DBG_8723A("%s STA " MAC_FMT
				   " - no HT, num of non-HT stations %d\n",
				   __func__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_no_ht);
		}
	}

	if (rtw_ht_operation_update(padapter) > 0)
	{
		update_beacon23a(padapter, WLAN_EID_HT_CAPABILITY, NULL, false);
		update_beacon23a(padapter, WLAN_EID_HT_OPERATION, NULL, true);
	}

	/* update associcated stations cap. */
	associated_clients_update23a(padapter,  beacon_updated);

	DBG_8723A("%s, updated =%d\n", __func__, beacon_updated);
}

u8 bss_cap_update_on_sta_leave23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = false;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!psta)
		return beacon_updated;

	if (psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 0;
		pmlmepriv->num_sta_no_short_preamble--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_preamble == 0)
		{
			beacon_updated = true;
			update_beacon23a(padapter, 0xFF, NULL, true);
		}
	}

	if (psta->nonerp_set) {
		psta->nonerp_set = 0;
		pmlmepriv->num_sta_non_erp--;
		if (pmlmepriv->num_sta_non_erp == 0)
		{
			beacon_updated = true;
			update_beacon23a(padapter, WLAN_EID_ERP_INFO,
					 NULL, true);
		}
	}

	if (psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 0;
		pmlmepriv->num_sta_no_short_slot_time--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_slot_time == 0)
		{
			beacon_updated = true;
			update_beacon23a(padapter, 0xFF, NULL, true);
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

	if (rtw_ht_operation_update(padapter) > 0)
	{
		update_beacon23a(padapter, WLAN_EID_HT_CAPABILITY, NULL, false);
		update_beacon23a(padapter, WLAN_EID_HT_OPERATION, NULL, true);
	}

	/* update associcated stations cap. */

	DBG_8723A("%s, updated =%d\n", __func__, beacon_updated);

	return beacon_updated;
}

u8 ap_free_sta23a(struct rtw_adapter *padapter, struct sta_info *psta, bool active, u16 reason)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 beacon_updated = false;

	if (!psta)
		return beacon_updated;

	if (active == true)
	{
		/* tear down Rx AMPDU */
		send_delba23a(padapter, 0, psta->hwaddr);/*  recipient */

		/* tear down TX AMPDU */
		send_delba23a(padapter, 1, psta->hwaddr);/* originator */

		issue_deauth23a(padapter, psta->hwaddr, reason);
	}

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	/* report_del_sta_event23a(padapter, psta->hwaddr, reason); */

	/* clear cam entry / key */
	/* clear_cam_entry23a(padapter, (psta->mac_id + 3)); */
	rtw_clearstakey_cmd23a(padapter, (u8*)psta, (u8)(psta->mac_id + 3), true);

	spin_lock_bh(&psta->lock);
	psta->state &= ~_FW_LINKED;
	spin_unlock_bh(&psta->lock);

	rtw_cfg80211_indicate_sta_disassoc(padapter, psta->hwaddr, reason);

	report_del_sta_event23a(padapter, psta->hwaddr, reason);

	beacon_updated = bss_cap_update_on_sta_leave23a(padapter, psta);

	spin_lock_bh(&pstapriv->sta_hash_lock);
	rtw_free_stainfo23a(padapter, psta);
	spin_unlock_bh(&pstapriv->sta_hash_lock);

	return beacon_updated;
}

int rtw_ap_inform_ch_switch23a (struct rtw_adapter *padapter, u8 new_ch, u8 ch_offset)
{
	struct list_head *phead, *plist;
	int ret = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	DBG_8723A("%s(%s): with ch:%u, offset:%u\n", __func__,
		  padapter->pnetdev->name, new_ch, ch_offset);

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;

	list_for_each(plist, phead) {
		psta = container_of(plist, struct sta_info, asoc_list);

		issue_action_spct_ch_switch23a (padapter, psta->hwaddr, new_ch, ch_offset);
		psta->expire_to = ((pstapriv->expire_to * 2) > 5) ? 5 : (pstapriv->expire_to * 2);
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	issue_action_spct_ch_switch23a (padapter, bc_addr, new_ch, ch_offset);

	return ret;
}

int rtw_sta_flush23a(struct rtw_adapter *padapter)
{
	struct list_head *phead, *plist, *ptmp;
	int ret = 0;
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 chk_alive_num = 0;
	struct sta_info *chk_alive_list[NUM_STA];
	int i;

	DBG_8723A("%s(%s)\n", __func__, padapter->pnetdev->name);

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;

	list_for_each_safe(plist, ptmp, phead) {
		psta = container_of(plist, struct sta_info, asoc_list);

		/* Remove sta from asoc_list */
		list_del_init(&psta->asoc_list);
		pstapriv->asoc_list_cnt--;

		/* Keep sta for ap_free_sta23a() beyond this asoc_list loop */
		chk_alive_list[chk_alive_num++] = psta;
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	/* For each sta in chk_alive_list, call ap_free_sta23a */
	for (i = 0; i < chk_alive_num; i++)
		ap_free_sta23a(padapter, chk_alive_list[i], true,
			       WLAN_REASON_DEAUTH_LEAVING);

	issue_deauth23a(padapter, bc_addr, WLAN_REASON_DEAUTH_LEAVING);

	associated_clients_update23a(padapter, true);

	return ret;
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void sta_info_update23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	int flags = psta->flags;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	/* update wmm cap. */
	if (WLAN_STA_WME&flags)
		psta->qos_option = 1;
	else
		psta->qos_option = 0;

	if (pmlmepriv->qos_option == 0)
		psta->qos_option = 0;

	/* update 802.11n ht cap. */
	if (WLAN_STA_HT&flags)
	{
		psta->htpriv.ht_option = true;
		psta->qos_option = 1;
	}
	else
	{
		psta->htpriv.ht_option = false;
	}

	if (!pmlmepriv->htpriv.ht_option)
		psta->htpriv.ht_option = false;

	update_sta_info23a_apmode23a(padapter, psta);
}

/* called >= TSR LEVEL for USB or SDIO Interface*/
void ap_sta_info_defer_update23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	if (psta->state & _FW_LINKED)
	{
		/* add ratid */
		add_RATid23a(padapter, psta, 0);/* DM_RATR_STA_INIT */
	}
}

/* restore hw setting from sw data structures */
void rtw_ap_restore_network(struct rtw_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct sta_priv * pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct list_head *phead, *plist, *ptmp;
	u8 chk_alive_num = 0;
	struct sta_info *chk_alive_list[NUM_STA];
	int i;

	rtw_setopmode_cmd23a(padapter, NL80211_IFTYPE_AP);

	set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	start_bss_network(padapter, (u8*)&mlmepriv->cur_network.network);

	if (padapter->securitypriv.dot11PrivacyAlgrthm ==
	    WLAN_CIPHER_SUITE_TKIP ||
	    padapter->securitypriv.dot11PrivacyAlgrthm ==
	    WLAN_CIPHER_SUITE_CCMP) {
		/* restore group key, WEP keys is restored in ips_leave23a() */
		rtw_set_key23a(padapter, psecuritypriv,
			       psecuritypriv->dot118021XGrpKeyid, 0);
	}

	/* per sta pairwise key and settings */
	if (padapter->securitypriv.dot11PrivacyAlgrthm !=
	    WLAN_CIPHER_SUITE_TKIP &&
	    padapter->securitypriv.dot11PrivacyAlgrthm !=
	    WLAN_CIPHER_SUITE_CCMP) {
		return;
	}

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;

	list_for_each_safe(plist, ptmp, phead) {
		psta = container_of(plist, struct sta_info, asoc_list);

		chk_alive_list[chk_alive_num++] = psta;
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);

	for (i = 0; i < chk_alive_num; i++) {
		psta = chk_alive_list[i];

		if (psta->state &_FW_LINKED) {
			Update_RA_Entry23a(padapter, psta);
			/* pairwise key */
			rtw_setstakey_cmd23a(padapter, (unsigned char *)psta, true);
		}
	}
}

void start_ap_mode23a(struct rtw_adapter *padapter)
{
	int i;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	pmlmepriv->update_bcn = false;

	/* init_mlme_ap_info23a(padapter); */
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

	for (i = 0; i<NUM_STA; i++)
		pstapriv->sta_aid[i] = NULL;

	/* for ACL */
	INIT_LIST_HEAD(&pacl_list->acl_node_q.queue);
	pacl_list->num = 0;
	pacl_list->mode = 0;
	for (i = 0; i < NUM_ACL; i++) {
		INIT_LIST_HEAD(&pacl_list->aclnode[i].list);
		pacl_list->aclnode[i].valid = false;
	}
}

void stop_ap_mode23a(struct rtw_adapter *padapter)
{
	struct list_head *phead, *plist, *ptmp;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct rtw_queue *pacl_node_q = &pacl_list->acl_node_q;

	pmlmepriv->update_bcn = false;
	pmlmeext->bstart_bss = false;

	/* reset and init security priv , this can refine with rtw_reset_securitypriv23a */
	memset((unsigned char *)&padapter->securitypriv, 0, sizeof (struct security_priv));
	padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
	padapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

	/* for ACL */
	spin_lock_bh(&pacl_node_q->lock);
	phead = get_list_head(pacl_node_q);

	list_for_each_safe(plist, ptmp, phead) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);

		if (paclnode->valid == true) {
			paclnode->valid = false;

			list_del_init(&paclnode->list);

			pacl_list->num--;
		}
	}
	spin_unlock_bh(&pacl_node_q->lock);

	DBG_8723A("%s, free acl_node_queue, num =%d\n", __func__, pacl_list->num);

	rtw_sta_flush23a(padapter);

	/* free_assoc_sta_resources */
	rtw_free_all_stainfo23a(padapter);

	psta = rtw_get_bcmc_stainfo23a(padapter);
	spin_lock_bh(&pstapriv->sta_hash_lock);
	rtw_free_stainfo23a(padapter, psta);
	spin_unlock_bh(&pstapriv->sta_hash_lock);

	rtw_init_bcmc_stainfo23a(padapter);

	rtw23a_free_mlme_priv_ie_data(pmlmepriv);
}
