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
#define _RTW_AP_C_

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_AP_MODE

extern unsigned char	RTW_WPA_OUI[];
extern unsigned char	WMM_OUI[];
extern unsigned char	WPS_OUI[];
extern unsigned char	P2P_OUI[];
extern unsigned char	WFD_OUI[];

void init_mlme_ap_info(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	_rtw_spinlock_init(&pmlmepriv->bcn_update_lock);

	/* pmlmeext->bstart_bss = _FALSE; */

}

void free_mlme_ap_info(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	stop_ap_mode(padapter);
	_rtw_spinlock_free(&pmlmepriv->bcn_update_lock);

}

static void update_BCNTIM(_adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork_mlmeext = &(pmlmeinfo->network);
	unsigned char *pie = pnetwork_mlmeext->IEs;

#if 0


	/* update TIM IE */
	/* if(pstapriv->tim_bitmap) */
#endif
	if (_TRUE) {
		u8 *p, *dst_ie, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
		u16 tim_bitmap_le;
		uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

		tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);

		p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, _TIM_IE_, &tim_ielen, pnetwork_mlmeext->IELength - _FIXED_IE_LENGTH_);
		if (p != NULL && tim_ielen > 0) {
			tim_ielen += 2;

			premainder_ie = p + tim_ielen;

			tim_ie_offset = (sint)(p - pie);

			remainder_ielen = pnetwork_mlmeext->IELength - tim_ie_offset - tim_ielen;

			/*append TIM IE from dst_ie offset*/
			dst_ie = p;
		} else {
			tim_ielen = 0;

			/*calculate head_len*/
			offset = _FIXED_IE_LENGTH_;

			/* get ssid_ie len */
			p = rtw_get_ie(pie + _BEACON_IE_OFFSET_, _SSID_IE_, &tmp_len, (pnetwork_mlmeext->IELength - _BEACON_IE_OFFSET_));
			if (p != NULL)
				offset += tmp_len + 2;

			/*get supported rates len*/
			p = rtw_get_ie(pie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &tmp_len, (pnetwork_mlmeext->IELength - _BEACON_IE_OFFSET_));
			if (p !=  NULL)
				offset += tmp_len + 2;

			/*DS Parameter Set IE, len=3*/
			offset += 3;

			premainder_ie = pie + offset;

			remainder_ielen = pnetwork_mlmeext->IELength - offset - tim_ielen;

			/*append TIM IE from offset*/
			dst_ie = pie + offset;

		}

		if (remainder_ielen > 0) {
			pbackup_remainder_ie = rtw_malloc(remainder_ielen);
			if (pbackup_remainder_ie && premainder_ie)
				_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
		}

		*dst_ie++ = _TIM_IE_;

		if ((pstapriv->tim_bitmap & 0xff00) && (pstapriv->tim_bitmap & 0x00fe))
			tim_ielen = 5;
		else
			tim_ielen = 4;

		*dst_ie++ = tim_ielen;

		*dst_ie++ = 0;/*DTIM count*/
		*dst_ie++ = 1;/*DTIM period*/

		if (pstapriv->tim_bitmap & BIT(0))/*for bc/mc frames*/
			*dst_ie++ = BIT(0);/*bitmap ctrl */
		else
			*dst_ie++ = 0;

		if (tim_ielen == 4) {
			u8 pvb = 0;

			if (pstapriv->tim_bitmap & 0x00fe)
				pvb = (u8)tim_bitmap_le;
			else if (pstapriv->tim_bitmap & 0xff00)
				pvb = (u8)(tim_bitmap_le >> 8);
			else
				pvb = (u8)tim_bitmap_le;

			*dst_ie++ = pvb;

		} else if (tim_ielen == 5) {
			_rtw_memcpy(dst_ie, &tim_bitmap_le, 2);
			dst_ie += 2;
		}

		/*copy remainder IE*/
		if (pbackup_remainder_ie) {
			_rtw_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

			rtw_mfree(pbackup_remainder_ie, remainder_ielen);
		}

		offset = (uint)(dst_ie - pie);
		pnetwork_mlmeext->IELength = offset + remainder_ielen;

	}
}

void rtw_add_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index, u8 *data, u8 len)
{
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8	bmatch = _FALSE;
	u8	*pie = pnetwork->IEs;
	u8	*p = NULL, *dst_ie = NULL, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
	u32	i, offset, ielen, ie_offset, remainder_ielen = 0;

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pnetwork->IELength;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pnetwork->IEs + i);

		if (pIE->ElementID > index)
			break;
		else if (pIE->ElementID == index) { /* already exist the same IE */
			p = (u8 *)pIE;
			ielen = pIE->Length;
			bmatch = _TRUE;
			break;
		}

		p = (u8 *)pIE;
		ielen = pIE->Length;
		i += (pIE->Length + 2);
	}

	if (p != NULL && ielen > 0) {
		ielen += 2;

		premainder_ie = p + ielen;

		ie_offset = (sint)(p - pie);

		remainder_ielen = pnetwork->IELength - ie_offset - ielen;

		if (bmatch)
			dst_ie = p;
		else
			dst_ie = (p + ielen);
	}

	if (dst_ie == NULL)
		return;

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie && premainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	*dst_ie++ = index;
	*dst_ie++ = len;

	_rtw_memcpy(dst_ie, data, len);
	dst_ie += len;

	/* copy remainder IE */
	if (pbackup_remainder_ie) {
		_rtw_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		rtw_mfree(pbackup_remainder_ie, remainder_ielen);
	}

	offset = (uint)(dst_ie - pie);
	pnetwork->IELength = offset + remainder_ielen;
}

void rtw_remove_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index)
{
	u8 *p, *dst_ie = NULL, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
	uint offset, ielen, ie_offset, remainder_ielen = 0;
	u8	*pie = pnetwork->IEs;

	p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, index, &ielen, pnetwork->IELength - _FIXED_IE_LENGTH_);
	if (p != NULL && ielen > 0) {
		ielen += 2;

		premainder_ie = p + ielen;

		ie_offset = (sint)(p - pie);

		remainder_ielen = pnetwork->IELength - ie_offset - ielen;

		dst_ie = p;
	} else
		return;

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie && premainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	/* copy remainder IE */
	if (pbackup_remainder_ie) {
		_rtw_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		rtw_mfree(pbackup_remainder_ie, remainder_ielen);
	}

	offset = (uint)(dst_ie - pie);
	pnetwork->IELength = offset + remainder_ielen;
}


u8 chk_sta_is_alive(struct sta_info *psta);
u8 chk_sta_is_alive(struct sta_info *psta)
{
	u8 ret = _FALSE;
#ifdef DBG_EXPIRATION_CHK
	RTW_INFO("sta:"MAC_FMT", rssi:%d, rx:"STA_PKTS_FMT", expire_to:%u, %s%ssq_len:%u\n"
		 , MAC_ARG(psta->cmn.mac_addr)
		 , psta->cmn.rssi_stat.rssi
		 /* , STA_RX_PKTS_ARG(psta) */
		 , STA_RX_PKTS_DIFF_ARG(psta)
		 , psta->expire_to
		 , psta->state & WIFI_SLEEP_STATE ? "PS, " : ""
		 , psta->state & WIFI_STA_ALIVE_CHK_STATE ? "SAC, " : ""
		 , psta->sleepq_len
		);
#endif

	/* if(sta_last_rx_pkts(psta) == sta_rx_pkts(psta)) */
	if ((psta->sta_stats.last_rx_data_pkts + psta->sta_stats.last_rx_ctrl_pkts) == (psta->sta_stats.rx_data_pkts + psta->sta_stats.rx_ctrl_pkts)) {
#if 0
		if (psta->state & WIFI_SLEEP_STATE)
			ret = _TRUE;
#endif
	} else
		ret = _TRUE;

	sta_update_last_rx_pkts(psta);

	return ret;
}

void	expire_timeout_chk(_adapter *padapter)
{
	_irqL irqL;
	_list	*phead, *plist;
	u8 updated = _FALSE;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;


#ifdef CONFIG_MCC_MODE
	/*	then driver may check fail due to not recv client's frame under sitesurvey,
	 *	don't expire timeout chk under MCC under sitesurvey */

	if (rtw_hal_mcc_link_status_chk(padapter, __func__) == _FALSE)
		return;
#endif

	_enter_critical_bh(&pstapriv->auth_list_lock, &irqL);

	phead = &pstapriv->auth_list;
	plist = get_next(phead);

	/* check auth_queue */
#ifdef DBG_EXPIRATION_CHK
	if (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		RTW_INFO(FUNC_NDEV_FMT" auth_list, cnt:%u\n"
			, FUNC_NDEV_ARG(padapter->pnetdev), pstapriv->auth_list_cnt);
	}
#endif
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, auth_list);

		plist = get_next(plist);


#ifdef CONFIG_ATMEL_RC_PATCH
		if (_rtw_memcmp((void *)(pstapriv->atmel_rc_pattern), (void *)(psta->cmn.mac_addr), ETH_ALEN) == _TRUE)
			continue;
		if (psta->flag_atmel_rc)
			continue;
#endif
		if (psta->expire_to > 0) {
			psta->expire_to--;
			if (psta->expire_to == 0) {
				rtw_list_delete(&psta->auth_list);
				pstapriv->auth_list_cnt--;

				RTW_INFO("auth expire %02X%02X%02X%02X%02X%02X\n",
					psta->cmn.mac_addr[0], psta->cmn.mac_addr[1], psta->cmn.mac_addr[2],
					psta->cmn.mac_addr[3], psta->cmn.mac_addr[4], psta->cmn.mac_addr[5]);

				_exit_critical_bh(&pstapriv->auth_list_lock, &irqL);

				/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	 */
				rtw_free_stainfo(padapter, psta);
				/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	 */

				_enter_critical_bh(&pstapriv->auth_list_lock, &irqL);
			}
		}

	}

	_exit_critical_bh(&pstapriv->auth_list_lock, &irqL);
	psta = NULL;


	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* check asoc_queue */
#ifdef DBG_EXPIRATION_CHK
	if (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		RTW_INFO(FUNC_NDEV_FMT" asoc_list, cnt:%u\n"
			, FUNC_NDEV_ARG(padapter->pnetdev), pstapriv->asoc_list_cnt);
	}
#endif
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);
#ifdef CONFIG_ATMEL_RC_PATCH
		RTW_INFO("%s:%d  psta=%p, %02x,%02x||%02x,%02x  \n\n", __func__,  __LINE__,
			psta, pstapriv->atmel_rc_pattern[0], pstapriv->atmel_rc_pattern[5], psta->cmn.mac_addr[0], psta->cmn.mac_addr[5]);
		if (_rtw_memcmp((void *)pstapriv->atmel_rc_pattern, (void *)(psta->cmn.mac_addr), ETH_ALEN) == _TRUE)
			continue;
		if (psta->flag_atmel_rc)
			continue;
		RTW_INFO("%s: debug line:%d\n", __func__, __LINE__);
#endif
#ifdef CONFIG_AUTO_AP_MODE
		if (psta->isrc)
			continue;
#endif
		if (chk_sta_is_alive(psta) || !psta->expire_to) {
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
#ifdef CONFIG_TX_MCAST2UNI
			psta->under_exist_checking = 0;
#endif	/* CONFIG_TX_MCAST2UNI */
		} else
			psta->expire_to--;

#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#ifdef CONFIG_80211N_HT
#ifdef CONFIG_TX_MCAST2UNI
		if ((psta->flags & WLAN_STA_HT) && (psta->htpriv.agg_enable_bitmap || psta->under_exist_checking)) {
			/* check sta by delba(addba) for 11n STA */
			/* ToDo: use CCX report to check for all STAs */
			/* RTW_INFO("asoc check by DELBA/ADDBA! (pstapriv->expire_to=%d s)(psta->expire_to=%d s), [%02x, %d]\n", pstapriv->expire_to*2, psta->expire_to*2, psta->htpriv.agg_enable_bitmap, psta->under_exist_checking); */

			if (psta->expire_to <= (pstapriv->expire_to - 50)) {
				RTW_INFO("asoc expire by DELBA/ADDBA! (%d s)\n", (pstapriv->expire_to - psta->expire_to) * 2);
				psta->under_exist_checking = 0;
				psta->expire_to = 0;
			} else if (psta->expire_to <= (pstapriv->expire_to - 3) && (psta->under_exist_checking == 0)) {
				RTW_INFO("asoc check by DELBA/ADDBA! (%d s)\n", (pstapriv->expire_to - psta->expire_to) * 2);
				psta->under_exist_checking = 1;
				/* tear down TX AMPDU */
				send_delba(padapter, 1, psta->cmn.mac_addr);/*  */ /* originator */
				psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
				psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */
			}
		}
#endif /* CONFIG_TX_MCAST2UNI */
#endif /* CONFIG_80211N_HT */
#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */

		if (psta->expire_to <= 0) {
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			if (padapter->registrypriv.wifi_spec == 1) {
				psta->expire_to = pstapriv->expire_to;
				continue;
			}

#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#ifdef CONFIG_80211N_HT

#define KEEP_ALIVE_TRYCNT (3)

			if (psta->keep_alive_trycnt > 0 && psta->keep_alive_trycnt <= KEEP_ALIVE_TRYCNT) {
				if (psta->state & WIFI_STA_ALIVE_CHK_STATE)
					psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
				else
					psta->keep_alive_trycnt = 0;

			} else if ((psta->keep_alive_trycnt > KEEP_ALIVE_TRYCNT) && !(psta->state & WIFI_STA_ALIVE_CHK_STATE))
				psta->keep_alive_trycnt = 0;
			if ((psta->htpriv.ht_option == _TRUE) && (psta->htpriv.ampdu_enable == _TRUE)) {
				uint priority = 1; /* test using BK */
				u8 issued = 0;

				/* issued = (psta->htpriv.agg_enable_bitmap>>priority)&0x1; */
				issued |= (psta->htpriv.candidate_tid_bitmap >> priority) & 0x1;

				if (0 == issued) {
					if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
						psta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);

						if (psta->state & WIFI_SLEEP_STATE)
							psta->expire_to = 2; /* 2x2=4 sec */
						else
							psta->expire_to = 1; /* 2 sec */

						psta->state |= WIFI_STA_ALIVE_CHK_STATE;

						/* add_ba_hdl(padapter, (u8*)paddbareq_parm); */

						RTW_INFO("issue addba_req to check if sta alive, keep_alive_trycnt=%d\n", psta->keep_alive_trycnt);

						issue_addba_req(padapter, psta->cmn.mac_addr, (u8)priority);

						_set_timer(&psta->addba_retry_timer, ADDBA_TO);

						psta->keep_alive_trycnt++;

						continue;
					}
				}
			}
			if (psta->keep_alive_trycnt > 0 && psta->state & WIFI_STA_ALIVE_CHK_STATE) {
				psta->keep_alive_trycnt = 0;
				psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
				RTW_INFO("change to another methods to check alive if staion is at ps mode\n");
			}

#endif /* CONFIG_80211N_HT */
#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK	 */
			if (psta->state & WIFI_SLEEP_STATE) {
				if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
					/* to check if alive by another methods if staion is at ps mode.					 */
					psta->expire_to = pstapriv->expire_to;
					psta->state |= WIFI_STA_ALIVE_CHK_STATE;

					/* RTW_INFO("alive chk, sta:" MAC_FMT " is at ps mode!\n", MAC_ARG(psta->cmn.mac_addr)); */

					/* to update bcn with tim_bitmap for this station */
					pstapriv->tim_bitmap |= BIT(psta->cmn.aid);
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE);

					if (!pmlmeext->active_keep_alive_check)
						continue;
				}
			}
#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
			if (pmlmeext->active_keep_alive_check) {
				int stainfo_offset;

				stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
				if (stainfo_offset_valid(stainfo_offset))
					chk_alive_list[chk_alive_num++] = stainfo_offset;

				continue;
			}
#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */
			rtw_list_delete(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			RTW_INFO("asoc expire "MAC_FMT", state=0x%x\n", MAC_ARG(psta->cmn.mac_addr), psta->state);
			updated = ap_free_sta(padapter, psta, _FALSE, WLAN_REASON_DEAUTH_LEAVING, _TRUE);
		} else {
			/* TODO: Aging mechanism to digest frames in sleep_q to avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME / pstapriv->asoc_list_cnt)
			    && padapter->xmitpriv.free_xmitframe_cnt < ((NR_XMITFRAME / pstapriv->asoc_list_cnt) / 2)
			   ) {
				RTW_INFO("%s sta:"MAC_FMT", sleepq_len:%u, free_xmitframe_cnt:%u, asoc_list_cnt:%u, clear sleep_q\n", __func__
					 , MAC_ARG(psta->cmn.mac_addr)
					, psta->sleepq_len, padapter->xmitpriv.free_xmitframe_cnt, pstapriv->asoc_list_cnt);
				wakeup_sta_to_xmit(padapter, psta);
			}
		}
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
	if (chk_alive_num) {

		u8 backup_ch = 0, backup_bw = 0, backup_offset = 0;
		u8 union_ch = 0, union_bw, union_offset;
		u8 switch_channel = _TRUE;
		struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

		if (!rtw_mi_get_ch_setting_union(padapter, &union_ch, &union_bw, &union_offset)
			|| pmlmeext->cur_channel != union_ch)
			goto bypass_active_keep_alive;

#ifdef CONFIG_MCC_MODE
		if (MCC_EN(padapter)) {
			/* driver doesn't switch channel under MCC */
			if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
				switch_channel = _FALSE;
		}
#endif
		/* switch to correct channel of current network  before issue keep-alive frames */
		if (switch_channel == _TRUE && rtw_get_oper_ch(padapter) != pmlmeext->cur_channel) {
			backup_ch = rtw_get_oper_ch(padapter);
			backup_bw = rtw_get_oper_bw(padapter);
			backup_offset = rtw_get_oper_choffset(padapter);
			set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
		}

		/* issue null data to check sta alive*/
		for (i = 0; i < chk_alive_num; i++) {
			int ret = _FAIL;

			psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);
#ifdef CONFIG_ATMEL_RC_PATCH
			if (_rtw_memcmp(pstapriv->atmel_rc_pattern, psta->cmn.mac_addr, ETH_ALEN) == _TRUE)
				continue;
			if (psta->flag_atmel_rc)
				continue;
#endif
			if (!(psta->state & _FW_LINKED))
				continue;

			if (psta->state & WIFI_SLEEP_STATE)
				ret = issue_nulldata(padapter, psta->cmn.mac_addr, 0, 1, 50);
			else
				ret = issue_nulldata(padapter, psta->cmn.mac_addr, 0, 3, 50);

			psta->keep_alive_trycnt++;
			if (ret == _SUCCESS) {
				RTW_INFO("asoc check, sta(" MAC_FMT ") is alive\n", MAC_ARG(psta->cmn.mac_addr));
				psta->expire_to = pstapriv->expire_to;
				psta->keep_alive_trycnt = 0;
				continue;
			} else if (psta->keep_alive_trycnt <= 3) {
				RTW_INFO("ack check for asoc expire, keep_alive_trycnt=%d\n", psta->keep_alive_trycnt);
				psta->expire_to = 1;
				continue;
			}

			psta->keep_alive_trycnt = 0;
			RTW_INFO("asoc expire "MAC_FMT", state=0x%x\n", MAC_ARG(psta->cmn.mac_addr), psta->state);
			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&psta->asoc_list) == _FALSE) {
				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, _FALSE, WLAN_REASON_DEAUTH_LEAVING, _TRUE);
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

		}

		/* back to the original operation channel */
		if (switch_channel && backup_ch > 0)
			set_channel_bwmode(padapter, backup_ch, backup_offset, backup_bw);

bypass_active_keep_alive:
		;
	}
#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */

	associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);
}

void rtw_ap_update_sta_ra_info(_adapter *padapter, struct sta_info *psta)
{
	int i;
	u8 rf_type;
	unsigned char sta_band = 0;
	u64 tx_ra_bitmap = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;

	if (!psta)
		return;

	if (!(psta->state & _FW_LINKED))
		return;

	rtw_hal_update_sta_ra_info(padapter, psta);
	tx_ra_bitmap = psta->cmn.ra_info.ramask;

	if (pcur_network->Configuration.DSConfig > 14) {

		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_5N;

		if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11A;

		/* 5G band */
#ifdef CONFIG_80211AC_VHT
		if (psta->vhtpriv.vht_option)
			sta_band = WIRELESS_11_5AC;
#endif
	} else {
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_24N;

		if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11G;

		if (tx_ra_bitmap & 0x0f)
			sta_band |= WIRELESS_11B;
	}

	psta->wireless_mode = sta_band;
	rtw_hal_update_sta_wset(padapter, psta);
	RTW_INFO("%s=> mac_id:%d , tx_ra_bitmap:0x%016llx, networkType:0x%02x\n",
			__FUNCTION__, psta->cmn.mac_id, tx_ra_bitmap, psta->wireless_mode);
}

#ifdef CONFIG_BMC_TX_RATE_SELECT
u8 rtw_ap_find_mini_tx_rate(_adapter *adapter)
{
	_irqL irqL;
	_list	*phead, *plist;
	u8 miini_tx_rate = ODM_RATEVHTSS4MCS9, sta_tx_rate;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &adapter->stapriv;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		sta_tx_rate = psta->cmn.ra_info.curr_tx_rate & 0x7F;
		if (sta_tx_rate < miini_tx_rate)
			miini_tx_rate = sta_tx_rate;
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	return miini_tx_rate;
}

u8 rtw_ap_find_bmc_rate(_adapter *adapter, u8 tx_rate)
{
	PHAL_DATA_TYPE	hal_data = GET_HAL_DATA(adapter);
	u8 tx_ini_rate = ODM_RATE6M;

	switch (tx_rate) {
	case ODM_RATEVHTSS3MCS9:
	case ODM_RATEVHTSS3MCS8:
	case ODM_RATEVHTSS3MCS7:
	case ODM_RATEVHTSS3MCS6:
	case ODM_RATEVHTSS3MCS5:
	case ODM_RATEVHTSS3MCS4:
	case ODM_RATEVHTSS3MCS3:
	case ODM_RATEVHTSS2MCS9:
	case ODM_RATEVHTSS2MCS8:
	case ODM_RATEVHTSS2MCS7:
	case ODM_RATEVHTSS2MCS6:
	case ODM_RATEVHTSS2MCS5:
	case ODM_RATEVHTSS2MCS4:
	case ODM_RATEVHTSS2MCS3:
	case ODM_RATEVHTSS1MCS9:
	case ODM_RATEVHTSS1MCS8:
	case ODM_RATEVHTSS1MCS7:
	case ODM_RATEVHTSS1MCS6:
	case ODM_RATEVHTSS1MCS5:
	case ODM_RATEVHTSS1MCS4:
	case ODM_RATEVHTSS1MCS3:
	case ODM_RATEMCS15:
	case ODM_RATEMCS14:
	case ODM_RATEMCS13:
	case ODM_RATEMCS12:
	case ODM_RATEMCS11:
	case ODM_RATEMCS7:
	case ODM_RATEMCS6:
	case ODM_RATEMCS5:
	case ODM_RATEMCS4:
	case ODM_RATEMCS3:
	case ODM_RATE54M:
	case ODM_RATE48M:
	case ODM_RATE36M:
	case ODM_RATE24M:
		tx_ini_rate = ODM_RATE24M;
		break;
	case ODM_RATEVHTSS3MCS2:
	case ODM_RATEVHTSS3MCS1:
	case ODM_RATEVHTSS2MCS2:
	case ODM_RATEVHTSS2MCS1:
	case ODM_RATEVHTSS1MCS2:
	case ODM_RATEVHTSS1MCS1:
	case ODM_RATEMCS10:
	case ODM_RATEMCS9:
	case ODM_RATEMCS2:
	case ODM_RATEMCS1:
	case ODM_RATE18M:
	case ODM_RATE12M:
		tx_ini_rate = ODM_RATE12M;
		break;
	case ODM_RATEVHTSS3MCS0:
	case ODM_RATEVHTSS2MCS0:
	case ODM_RATEVHTSS1MCS0:
	case ODM_RATEMCS8:
	case ODM_RATEMCS0:
	case ODM_RATE9M:
	case ODM_RATE6M:
		tx_ini_rate = ODM_RATE6M;
		break;
	case ODM_RATE11M:
	case ODM_RATE5_5M:
	case ODM_RATE2M:
	case ODM_RATE1M:
		tx_ini_rate = ODM_RATE1M;
		break;
	default:
		tx_ini_rate = ODM_RATE6M;
		break;
	}

	if (hal_data->current_band_type == BAND_ON_5G)
		if (tx_ini_rate < ODM_RATE6M)
			tx_ini_rate = ODM_RATE6M;

	return tx_ini_rate;
}

void rtw_update_bmc_sta_tx_rate(_adapter *adapter)
{
	struct sta_info *psta = NULL;
	u8 tx_rate;

	psta = rtw_get_bcmc_stainfo(adapter);
	if (psta == NULL) {
		RTW_ERR(ADPT_FMT "could not get bmc_sta !!\n", ADPT_ARG(adapter));
		return;
	}

	if (adapter->bmc_tx_rate != MGN_UNKNOWN) {
		psta->init_rate = adapter->bmc_tx_rate;
		goto _exit;
	}

	if (adapter->stapriv.asoc_sta_count <= 2)
		goto _exit;

	tx_rate = rtw_ap_find_mini_tx_rate(adapter);
	#ifdef CONFIG_BMC_TX_LOW_RATE
	tx_rate = rtw_ap_find_bmc_rate(adapter, tx_rate);
	#endif

	psta->init_rate = hw_rate_to_m_rate(tx_rate);

_exit:
	RTW_INFO(ADPT_FMT" BMC Tx rate - %s\n", ADPT_ARG(adapter), MGN_RATE_STR(psta->init_rate));
}
#endif

void rtw_init_bmc_sta_tx_rate(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	u8 rate_idx = 0;
	u8 brate_table[] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M,
		MGN_6M, MGN_9M, MGN_12M, MGN_18M, MGN_24M, MGN_36M, MGN_48M, MGN_54M};

	if (!MLME_IS_AP(padapter) && !MLME_IS_MESH(padapter))
		return;

	if (padapter->bmc_tx_rate != MGN_UNKNOWN)
		psta->init_rate = padapter->bmc_tx_rate;
	else {
		#ifdef CONFIG_BMC_TX_LOW_RATE
		if (IsEnableHWOFDM(pmlmeext->cur_wireless_mode) && (psta->cmn.ra_info.ramask && 0xFF0))
			rate_idx = get_lowest_rate_idx_ex(psta->cmn.ra_info.ramask, 4); /*from basic rate*/
		else
			rate_idx = get_lowest_rate_idx(psta->cmn.ra_info.ramask); /*from basic rate*/
		#else
		rate_idx = get_highest_rate_idx(psta->cmn.ra_info.ramask); /*from basic rate*/
		#endif
		if (rate_idx < 12)
			psta->init_rate = brate_table[rate_idx];
		else
			psta->init_rate = MGN_1M;
	}

	RTW_INFO(ADPT_FMT" BMC Init Tx rate - %s\n", ADPT_ARG(padapter), MGN_RATE_STR(psta->init_rate));
}

void update_bmc_sta(_adapter *padapter)
{
	_irqL	irqL;
	unsigned char	network_type;
	int supportRateNum = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct sta_info *psta = rtw_get_bcmc_stainfo(padapter);

	if (psta) {
		psta->cmn.aid = 0;/* default set to 0 */
		psta->qos_option = 0;
#ifdef CONFIG_80211N_HT
		psta->htpriv.ht_option = _FALSE;
#endif /* CONFIG_80211N_HT */

		psta->ieee8021x_blocked = 0;

		_rtw_memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		/* psta->dot118021XPrivacy = _NO_PRIVACY_; */ /* !!! remove it, because it has been set before this. */

		supportRateNum = rtw_get_rateset_len((u8 *)&pcur_network->SupportedRates);
		network_type = rtw_check_network_type((u8 *)&pcur_network->SupportedRates, supportRateNum, pcur_network->Configuration.DSConfig);
		if (IsSupportedTxCCK(network_type))
			network_type = WIRELESS_11B;
		else if (network_type == WIRELESS_INVALID) { /* error handling */
			if (pcur_network->Configuration.DSConfig > 14)
				network_type = WIRELESS_11A;
			else
				network_type = WIRELESS_11B;
		}
		update_sta_basic_rate(psta, network_type);
		psta->wireless_mode = network_type;

		rtw_hal_update_sta_ra_info(padapter, psta);

		_enter_critical_bh(&psta->lock, &irqL);
		psta->state = _FW_LINKED;
		_exit_critical_bh(&psta->lock, &irqL);

		rtw_sta_media_status_rpt(padapter, psta, 1);
		rtw_init_bmc_sta_tx_rate(padapter, psta);

	} else
		RTW_INFO("add_RATid_bmc_sta error!\n");

}

#if defined(CONFIG_80211N_HT) && defined(CONFIG_BEAMFORMING)
void update_sta_info_apmode_ht_bf_cap(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv	*phtpriv_sta = &psta->htpriv;

	u8 cur_beamform_cap = 0;

	/*Config Tx beamforming setting*/
	if (TEST_FLAG(phtpriv_ap->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) &&
		GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP((u8 *)(&phtpriv_sta->ht_cap))) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
		/*Shift to BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP*/
		SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS((u8 *)(&phtpriv_sta->ht_cap)) << 6);
	}

	if (TEST_FLAG(phtpriv_ap->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
		GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP((u8 *)(&phtpriv_sta->ht_cap))) {
		SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
		/*Shift to BEAMFORMING_HT_BEAMFORMER_STEER_NUM*/
		SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS((u8 *)(&phtpriv_sta->ht_cap)) << 4);
	}
	if (cur_beamform_cap)
		RTW_INFO("Client STA(%d) HT Beamforming Cap = 0x%02X\n", psta->cmn.aid, cur_beamform_cap);

	phtpriv_sta->beamform_cap = cur_beamform_cap;
	psta->cmn.bf_info.ht_beamform_cap = cur_beamform_cap;

}
#endif /*CONFIG_80211N_HT && CONFIG_BEAMFORMING*/

/* notes:
 * AID: 1~MAX for sta and 0 for bc/mc in ap/adhoc mode  */
void update_sta_info_apmode(_adapter *padapter, struct sta_info *psta)
{
	_irqL	irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
#ifdef CONFIG_80211N_HT
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv	*phtpriv_sta = &psta->htpriv;
#endif /* CONFIG_80211N_HT */
	u8	cur_ldpc_cap = 0, cur_stbc_cap = 0;
	/* set intf_tag to if1 */
	/* psta->intf_tag = 0; */

	RTW_INFO("%s\n", __FUNCTION__);

	/*alloc macid when call rtw_alloc_stainfo(),release macid when call rtw_free_stainfo()*/

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->ieee8021x_blocked = _TRUE;
	else
		psta->ieee8021x_blocked = _FALSE;


	/* update sta's cap */

	/* ERP */
	VCS_update(padapter, psta);
#ifdef CONFIG_80211N_HT
	/* HT related cap */
	if (phtpriv_sta->ht_option) {
		/* check if sta supports rx ampdu */
		phtpriv_sta->ampdu_enable = phtpriv_ap->ampdu_enable;

		phtpriv_sta->rx_ampdu_min_spacing = (phtpriv_sta->ht_cap.ampdu_params_info & IEEE80211_HT_CAP_AMPDU_DENSITY) >> 2;

		/* bwmode */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH))
			psta->cmn.bw_mode = CHANNEL_WIDTH_40;
		else
			psta->cmn.bw_mode = CHANNEL_WIDTH_20;

		if (psta->ht_40mhz_intolerant)
			psta->cmn.bw_mode = CHANNEL_WIDTH_20;

		if (pmlmeext->cur_bwmode < psta->cmn.bw_mode)
			psta->cmn.bw_mode = pmlmeext->cur_bwmode;

		phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;


		/* check if sta support s Short GI 20M */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
			phtpriv_sta->sgi_20m = _TRUE;

		/* check if sta support s Short GI 40M */
		if ((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_40)) {
			if (psta->cmn.bw_mode == CHANNEL_WIDTH_40) /* according to psta->bw_mode */
				phtpriv_sta->sgi_40m = _TRUE;
			else
				phtpriv_sta->sgi_40m = _FALSE;
		}

		psta->qos_option = _TRUE;

		/* B0 Config LDPC Coding Capability */
		if (TEST_FLAG(phtpriv_ap->ldpc_cap, LDPC_HT_ENABLE_TX) &&
		    GET_HT_CAP_ELE_LDPC_CAP((u8 *)(&phtpriv_sta->ht_cap))) {
			SET_FLAG(cur_ldpc_cap, (LDPC_HT_ENABLE_TX | LDPC_HT_CAP_TX));
			RTW_INFO("Enable HT Tx LDPC for STA(%d)\n", psta->cmn.aid);
		}

		/* B7 B8 B9 Config STBC setting */
		if (TEST_FLAG(phtpriv_ap->stbc_cap, STBC_HT_ENABLE_TX) &&
		    GET_HT_CAP_ELE_RX_STBC((u8 *)(&phtpriv_sta->ht_cap))) {
			SET_FLAG(cur_stbc_cap, (STBC_HT_ENABLE_TX | STBC_HT_CAP_TX));
			RTW_INFO("Enable HT Tx STBC for STA(%d)\n", psta->cmn.aid);
		}

		#ifdef CONFIG_BEAMFORMING
		update_sta_info_apmode_ht_bf_cap(padapter, psta);
		#endif
	} else {
		phtpriv_sta->ampdu_enable = _FALSE;

		phtpriv_sta->sgi_20m = _FALSE;
		phtpriv_sta->sgi_40m = _FALSE;
		psta->cmn.bw_mode = CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	phtpriv_sta->ldpc_cap = cur_ldpc_cap;
	phtpriv_sta->stbc_cap = cur_stbc_cap;

	/* Rx AMPDU */
	send_delba(padapter, 0, psta->cmn.mac_addr);/* recipient */

	/* TX AMPDU */
	send_delba(padapter, 1, psta->cmn.mac_addr);/*  */ /* originator */
	phtpriv_sta->agg_enable_bitmap = 0x0;/* reset */
	phtpriv_sta->candidate_tid_bitmap = 0x0;/* reset */
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_80211AC_VHT
	update_sta_vht_info_apmode(padapter, psta);
#endif
	psta->cmn.ra_info.is_support_sgi = query_ra_short_GI(psta, rtw_get_tx_bw_mode(padapter, psta));
	update_ldpc_stbc_cap(psta);

	/* todo: init other variables */

	_rtw_memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));


	/* add ratid */
	/* add_RATid(padapter, psta); */ /* move to ap_sta_info_defer_update() */

	/* ap mode */
	rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

	_enter_critical_bh(&psta->lock, &irqL);
	psta->state |= _FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL);


}

static void update_ap_info(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
#ifdef CONFIG_80211N_HT
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
#endif /* CONFIG_80211N_HT */

	psta->wireless_mode = pmlmeext->cur_wireless_mode;

	psta->bssratelen = rtw_get_rateset_len(pnetwork->SupportedRates);
	_rtw_memcpy(psta->bssrateset, pnetwork->SupportedRates, psta->bssratelen);

#ifdef CONFIG_80211N_HT
	/* HT related cap */
	if (phtpriv_ap->ht_option) {
		/* check if sta supports rx ampdu */
		/* phtpriv_ap->ampdu_enable = phtpriv_ap->ampdu_enable; */

		/* check if sta support s Short GI 20M */
		if ((phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
			phtpriv_ap->sgi_20m = _TRUE;
		/* check if sta support s Short GI 40M */
		if ((phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_40))
			phtpriv_ap->sgi_40m = _TRUE;

		psta->qos_option = _TRUE;
	} else {
		phtpriv_ap->ampdu_enable = _FALSE;

		phtpriv_ap->sgi_20m = _FALSE;
		phtpriv_ap->sgi_40m = _FALSE;
	}

	psta->cmn.bw_mode = pmlmeext->cur_bwmode;
	phtpriv_ap->ch_offset = pmlmeext->cur_ch_offset;

	phtpriv_ap->agg_enable_bitmap = 0x0;/* reset */
	phtpriv_ap->candidate_tid_bitmap = 0x0;/* reset */

	_rtw_memcpy(&psta->htpriv, &pmlmepriv->htpriv, sizeof(struct ht_priv));

#ifdef CONFIG_80211AC_VHT
	_rtw_memcpy(&psta->vhtpriv, &pmlmepriv->vhtpriv, sizeof(struct vht_priv));
#endif /* CONFIG_80211AC_VHT */

#endif /* CONFIG_80211N_HT */

	psta->state |= WIFI_AP_STATE; /* Aries, add,fix bug of flush_cam_entry at STOP AP mode , 0724 */
}

static void rtw_set_hw_wmm_param(_adapter *padapter)
{
	u8	ACI, ACM, AIFS, ECWMin, ECWMax, aSifsTime;
	u8	acm_mask;
	u16	TXOP;
	u32	acParm, i;
	u32	edca[4], inx[4];
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	acm_mask = 0;

	if (is_supported_5g(pmlmeext->cur_wireless_mode) ||
	    (pmlmeext->cur_wireless_mode & WIRELESS_11_24N))
		aSifsTime = 16;
	else
		aSifsTime = 10;

	if (pmlmeinfo->WMM_enable == 0) {
		padapter->mlmepriv.acm_mask = 0;

		AIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

		if (pmlmeext->cur_wireless_mode & (WIRELESS_11G | WIRELESS_11A)) {
			ECWMin = 4;
			ECWMax = 10;
		} else if (pmlmeext->cur_wireless_mode & WIRELESS_11B) {
			ECWMin = 5;
			ECWMax = 10;
		} else {
			ECWMin = 4;
			ECWMax = 10;
		}

		TXOP = 0;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));

		ECWMin = 2;
		ECWMax = 3;
		TXOP = 0x2f;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));

	} else {
		edca[0] = edca[1] = edca[2] = edca[3] = 0;

		/*TODO:*/
		acm_mask = 0;
		padapter->mlmepriv.acm_mask = acm_mask;

#if 0
		/* BK */
		/* AIFS = AIFSN * slot time + SIFS - r2t phy delay */
#endif
		AIFS = (7 * pmlmeinfo->slotTime) + aSifsTime;
		ECWMin = 4;
		ECWMax = 10;
		TXOP = 0;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
		edca[XMIT_BK_QUEUE] = acParm;
		RTW_INFO("WMM(BK): %x\n", acParm);

		/* BE */
		AIFS = (3 * pmlmeinfo->slotTime) + aSifsTime;
		ECWMin = 4;
		ECWMax = 6;
		TXOP = 0;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
		edca[XMIT_BE_QUEUE] = acParm;
		RTW_INFO("WMM(BE): %x\n", acParm);

		/* VI */
		AIFS = (1 * pmlmeinfo->slotTime) + aSifsTime;
		ECWMin = 3;
		ECWMax = 4;
		TXOP = 94;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));
		edca[XMIT_VI_QUEUE] = acParm;
		RTW_INFO("WMM(VI): %x\n", acParm);

		/* VO */
		AIFS = (1 * pmlmeinfo->slotTime) + aSifsTime;
		ECWMin = 2;
		ECWMax = 3;
		TXOP = 47;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));
		edca[XMIT_VO_QUEUE] = acParm;
		RTW_INFO("WMM(VO): %x\n", acParm);


		if (padapter->registrypriv.acm_method == 1)
			rtw_hal_set_hwreg(padapter, HW_VAR_ACM_CTRL, (u8 *)(&acm_mask));
		else
			padapter->mlmepriv.acm_mask = acm_mask;

		inx[0] = 0;
		inx[1] = 1;
		inx[2] = 2;
		inx[3] = 3;

		if (pregpriv->wifi_spec == 1) {
			u32	j, tmp, change_inx = _FALSE;

			/* entry indx: 0->vo, 1->vi, 2->be, 3->bk. */
			for (i = 0 ; i < 4 ; i++) {
				for (j = i + 1 ; j < 4 ; j++) {
					/* compare CW and AIFS */
					if ((edca[j] & 0xFFFF) < (edca[i] & 0xFFFF))
						change_inx = _TRUE;
					else if ((edca[j] & 0xFFFF) == (edca[i] & 0xFFFF)) {
						/* compare TXOP */
						if ((edca[j] >> 16) > (edca[i] >> 16))
							change_inx = _TRUE;
					}

					if (change_inx) {
						tmp = edca[i];
						edca[i] = edca[j];
						edca[j] = tmp;

						tmp = inx[i];
						inx[i] = inx[j];
						inx[j] = tmp;

						change_inx = _FALSE;
					}
				}
			}
		}

		for (i = 0 ; i < 4 ; i++) {
			pxmitpriv->wmm_para_seq[i] = inx[i];
			RTW_INFO("wmm_para_seq(%d): %d\n", i, pxmitpriv->wmm_para_seq[i]);
		}

	}

}

static void update_hw_ht_param(_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	RTW_INFO("%s\n", __FUNCTION__);


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
	/* Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & 0x0C) >> 2;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC) {
#if 0
		u8 i;
		/* update the MCS rates */
		for (i = 0; i < 16; i++)
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
#endif
		RTW_INFO("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __FUNCTION__);
	}

	/*  */
	/* Config current HT Protection mode. */
	/*  */
	/* pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3; */

}

static void rtw_ap_check_scan(_adapter *padapter)
{
	_irqL	irqL;
	_list		*plist, *phead;
	u32	delta_time, lifetime;
	struct	wlan_network	*pnetwork = NULL;
	WLAN_BSSID_EX *pbss = NULL;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	u8 do_scan = _FALSE;
	u8 reason = RTW_AUTO_SCAN_REASON_UNSPECIFIED;

	lifetime = SCANQUEUE_LIFETIME; /* 20 sec */

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	phead = get_list_head(queue);
	if (rtw_end_of_queue_search(phead, get_next(phead)) == _TRUE)
		if (padapter->registrypriv.wifi_spec) {
			do_scan = _TRUE;
			reason |= RTW_AUTO_SCAN_REASON_2040_BSS;
		}
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

#ifdef CONFIG_RTW_ACS
	if (padapter->registrypriv.acs_auto_scan) {
		do_scan = _TRUE;
		reason |= RTW_AUTO_SCAN_REASON_ACS;
		rtw_acs_start(padapter);
	}
#endif/*CONFIG_RTW_ACS*/

	if (_TRUE == do_scan) {
		RTW_INFO("%s : drv scans by itself and wait_completed\n", __func__);
		rtw_drv_scan_by_self(padapter, reason);
		rtw_scan_wait_completed(padapter);
	}

#ifdef CONFIG_RTW_ACS
	if (padapter->registrypriv.acs_auto_scan)
		rtw_acs_stop(padapter);
#endif

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);

	while (1) {

		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		if (rtw_chset_search_ch(adapter_to_chset(padapter), pnetwork->network.Configuration.DSConfig) >= 0
		    && rtw_mlme_band_check(padapter, pnetwork->network.Configuration.DSConfig) == _TRUE
		    && _TRUE == rtw_validate_ssid(&(pnetwork->network.Ssid))) {
			delta_time = (u32) rtw_get_passing_time_ms(pnetwork->last_scanned);

			if (delta_time < lifetime) {

				uint ie_len = 0;
				u8 *pbuf = NULL;
				u8 *ie = NULL;

				pbss = &pnetwork->network;
				ie = pbss->IEs;

				/*check if HT CAP INFO IE exists or not*/
				pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pbss->IELength - _BEACON_IE_OFFSET_));
				if (pbuf == NULL) {
					/* HT CAP INFO IE don't exist, it is b/g mode bss.*/

					if (_FALSE == ATOMIC_READ(&pmlmepriv->olbc))
						ATOMIC_SET(&pmlmepriv->olbc, _TRUE);

					if (_FALSE == ATOMIC_READ(&pmlmepriv->olbc_ht))
						ATOMIC_SET(&pmlmepriv->olbc_ht, _TRUE);
					
					if (padapter->registrypriv.wifi_spec)
						RTW_INFO("%s: %s is a/b/g ap\n", __func__, pnetwork->network.Ssid.Ssid);
				}
			}
		}

		plist = get_next(plist);

	}

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	pmlmepriv->num_sta_no_ht = 0; /* reset to 0 after ap do scanning*/

}

void rtw_start_bss_hdl_after_chbw_decided(_adapter *adapter)
{
	WLAN_BSSID_EX *pnetwork = &(adapter->mlmepriv.cur_network.network);
	struct sta_info *sta = NULL;

	/* update cur_wireless_mode */
	update_wireless_mode(adapter);

	/* update RRSR and RTS_INIT_RATE register after set channel and bandwidth */
	UpdateBrateTbl(adapter, pnetwork->SupportedRates);
	rtw_hal_set_hwreg(adapter, HW_VAR_BASIC_RATE, pnetwork->SupportedRates);

	/* update capability after cur_wireless_mode updated */
	update_capinfo(adapter, rtw_get_capability(pnetwork));

	/* update bc/mc sta_info */
	update_bmc_sta(adapter);

	/* update AP's sta info */
	sta = rtw_get_stainfo(&adapter->stapriv, pnetwork->MacAddress);
	if (!sta) {
		RTW_INFO(FUNC_ADPT_FMT" !sta for macaddr="MAC_FMT"\n", FUNC_ADPT_ARG(adapter), MAC_ARG(pnetwork->MacAddress));
		rtw_warn_on(1);
		return;
	}

	update_ap_info(adapter, sta);
}

void start_bss_network(_adapter *padapter, struct createbss_parm *parm)
{
#define DUMP_ADAPTERS_STATUS 0
	u8 self_action = MLME_ACTION_UNKNOWN;
	u8 val8;
	u16 bcn_interval;
	u32	acparm;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network; /* used as input */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork_mlmeext = &(pmlmeinfo->network);
	struct dvobj_priv *pdvobj = padapter->dvobj;
	s16 req_ch = -1, req_bw = -1, req_offset = -1;
	bool ch_setting_changed = _FALSE;
	u8 ch_to_set = 0, bw_to_set, offset_to_set;
	u8 doiqk = _FALSE;
	/* use for check ch bw offset can be allowed or not */
	u8 chbw_allow = _TRUE;

	if (MLME_IS_AP(padapter))
		self_action = MLME_AP_STARTED;
	else if (MLME_IS_MESH(padapter))
		self_action = MLME_MESH_STARTED;
	else
		rtw_warn_on(1);

	if (parm->req_ch != 0) {
		/* bypass other setting, go checking ch, bw, offset */
		req_ch = parm->req_ch;
		req_bw = parm->req_bw;
		req_offset = parm->req_offset;
		goto chbw_decision;
	} else {
		/* inform this request comes from upper layer */
		req_ch = 0;
	}

	bcn_interval = (u16)pnetwork->Configuration.BeaconPeriod;

	/* check if there is wps ie, */
	/* if there is wpsie in beacon, the hostapd will update beacon twice when stating hostapd, */
	/* and at first time the security ie ( RSN/WPA IE) will not include in beacon. */
	if (NULL == rtw_get_wps_ie(pnetwork->IEs + _FIXED_IE_LENGTH_, pnetwork->IELength - _FIXED_IE_LENGTH_, NULL, NULL))
		pmlmeext->bstart_bss = _TRUE;

	/* todo: update wmm, ht cap */
	/* pmlmeinfo->WMM_enable; */
	/* pmlmeinfo->HT_enable; */
	if (pmlmepriv->qospriv.qos_option)
		pmlmeinfo->WMM_enable = _TRUE;
#ifdef CONFIG_80211N_HT
	if (pmlmepriv->htpriv.ht_option) {
		pmlmeinfo->WMM_enable = _TRUE;
		pmlmeinfo->HT_enable = _TRUE;
		/* pmlmeinfo->HT_info_enable = _TRUE; */
		/* pmlmeinfo->HT_caps_enable = _TRUE; */

		update_hw_ht_param(padapter);
	}
#endif /* #CONFIG_80211N_HT */

#ifdef CONFIG_80211AC_VHT
	if (pmlmepriv->vhtpriv.vht_option) {
		pmlmeinfo->VHT_enable = _TRUE;
		update_hw_vht_param(padapter);
	}
#endif /* CONFIG_80211AC_VHT */

	if (pmlmepriv->cur_network.join_res != _TRUE) { /* setting only at  first time */
		/* WEP Key will be set before this function, do not clear CAM. */
		if ((psecuritypriv->dot11PrivacyAlgrthm != _WEP40_) && (psecuritypriv->dot11PrivacyAlgrthm != _WEP104_))
			flush_all_cam_entry(padapter);	/* clear CAM */
	}

	/* set MSR to AP_Mode		 */
	Set_MSR(padapter, _HW_STATE_AP_);

	/* Set BSSID REG */
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pnetwork->MacAddress);

	/* Set EDCA param reg */
#ifdef CONFIG_CONCURRENT_MODE
	acparm = 0x005ea42b;
#else
	acparm = 0x002F3217; /* VO */
#endif
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
	acparm = 0x005E4317; /* VI */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
	/* acparm = 0x00105320; */ /* BE */
	acparm = 0x005ea42b;
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
	acparm = 0x0000A444; /* BK */
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));

	/* Set Security */
	val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) ? 0xcc : 0xcf;
	rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

	/* Beacon Control related register */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&bcn_interval));

chbw_decision:
	ch_setting_changed = rtw_ap_chbw_decision(padapter, req_ch, req_bw, req_offset
		     , &ch_to_set, &bw_to_set, &offset_to_set, &chbw_allow);

	/* let pnetwork_mlmeext == pnetwork_mlme. */
	_rtw_memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);

	rtw_start_bss_hdl_after_chbw_decided(padapter);

#if defined(CONFIG_DFS_MASTER)
	rtw_dfs_master_status_apply(padapter, self_action);
#endif
	rtw_hal_rcr_set_chk_bssid(padapter, self_action);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter)) {
		/* 
		* due to check under rtw_ap_chbw_decision
		* if under MCC mode, means req channel setting is the same as current channel setting
		* if not under MCC mode, mean req channel setting is not the same as current channel setting
		*/
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
				RTW_INFO(FUNC_ADPT_FMT": req channel setting is the same as current channel setting, go to update BCN\n"
				, FUNC_ADPT_ARG(padapter));

				goto update_beacon;

		}
	}

	/* issue null data to AP for all interface connecting to AP before switch channel setting for softap */
	rtw_hal_mcc_issue_null_data(padapter, chbw_allow, 1);
#endif /* CONFIG_MCC_MODE */

	doiqk = _TRUE;
	rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

	if (ch_to_set != 0) {
		set_channel_bwmode(padapter, ch_to_set, offset_to_set, bw_to_set);
		rtw_mi_update_union_chan_inf(padapter, ch_to_set, offset_to_set, bw_to_set);
	}

	doiqk = _FALSE;
	rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

#ifdef CONFIG_MCC_MODE
	/* after set_channel_bwmode for backup IQK */
	rtw_hal_set_mcc_setting_start_bss_network(padapter, chbw_allow);
#endif

	if (DUMP_ADAPTERS_STATUS) {
		RTW_INFO(FUNC_ADPT_FMT" done\n", FUNC_ADPT_ARG(padapter));
		dump_adapters_status(RTW_DBGDUMP , adapter_to_dvobj(padapter));
	}

update_beacon:
	/* update beacon content only if bstart_bss is _TRUE */
	if (_TRUE == pmlmeext->bstart_bss) {

		_irqL irqL;

		if ((ATOMIC_READ(&pmlmepriv->olbc) == _TRUE) || (ATOMIC_READ(&pmlmepriv->olbc_ht) == _TRUE)) {
			/* AP is not starting a 40 MHz BSS in presence of an 802.11g BSS. */

			pmlmepriv->ht_op_mode &= (~HT_INFO_OPERATION_MODE_OP_MODE_MASK);
			pmlmepriv->ht_op_mode |= OP_MODE_MAY_BE_LEGACY_STAS;
			update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _FALSE);
		}

		update_beacon(padapter, _TIM_IE_, NULL, _FALSE);

#ifdef CONFIG_SWTIMER_BASED_TXBCN
		_enter_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
		if (rtw_is_list_empty(&padapter->list)) {
			rtw_list_insert_tail(&padapter->list, get_list_head(&pdvobj->ap_if_q));
			pdvobj->nr_ap_if++;
			pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL / pdvobj->nr_ap_if;
		}
		_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);

		rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pdvobj->inter_bcn_space));

#endif /*CONFIG_SWTIMER_BASED_TXBCN*/

	}

	rtw_scan_wait_completed(padapter);

	/* send beacon */
	if (!rtw_mi_check_fwstate(padapter, _FW_UNDER_SURVEY)) {

		/*update_beacon(padapter, _TIM_IE_, NULL, _TRUE);*/

#if !defined(CONFIG_INTERRUPT_BASED_TXBCN)
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifdef CONFIG_SWTIMER_BASED_TXBCN
		if (pdvobj->nr_ap_if == 1) {
			RTW_INFO("start SW BCN TIMER!\n");
			_set_timer(&pdvobj->txbcn_timer, bcn_interval);
		}
#else
		/* other case will  tx beacon when bcn interrupt coming in. */
		if (send_beacon(padapter) == _FAIL)
			RTW_INFO("issue_beacon, fail!\n");
#endif
#endif
#endif /* !defined(CONFIG_INTERRUPT_BASED_TXBCN) */
	}

	/*Set EDCA param reg after update cur_wireless_mode & update_capinfo*/
	if (pregpriv->wifi_spec == 1)
		rtw_set_hw_wmm_param(padapter);

	/*pmlmeext->bstart_bss = _TRUE;*/
}

int rtw_check_beacon_data(_adapter *padapter, u8 *pbuf,  int len)
{
	int ret = _SUCCESS;
	u8 *p;
	u8 *pHT_caps_ie = NULL;
	u8 *pHT_info_ie = NULL;
	u16 cap, ht_cap = _FALSE;
	uint ie_len = 0;
	int group_cipher, pairwise_cipher;
	u8	channel, network_type, supportRate[NDIS_802_11_LENGTH_RATES_EX];
	int supportRateNum = 0;
	u8 OUI1[] = {0x00, 0x50, 0xf2, 0x01};
	u8 wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pbss_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ie = pbss_network->IEs;
	u8 vht_cap = _FALSE;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	u8 rf_num = 0;

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

	RTW_INFO("%s, len=%d\n", __FUNCTION__, len);

	if (!MLME_IS_AP(padapter) && !MLME_IS_MESH(padapter))
		return _FAIL;


	if (len > MAX_IE_SZ)
		return _FAIL;

	pbss_network->IELength = len;

	_rtw_memset(ie, 0, MAX_IE_SZ);

	_rtw_memcpy(ie, pbuf, pbss_network->IELength);


	if (pbss_network->InfrastructureMode != Ndis802_11APMode) {
		rtw_warn_on(1);
		return _FAIL;
	}


	rtw_ap_check_scan(padapter);


	pbss_network->Rssi = 0;

	_rtw_memcpy(pbss_network->MacAddress, adapter_mac_addr(padapter), ETH_ALEN);

	/* beacon interval */
	p = rtw_get_beacon_interval_from_ie(ie);/* ie + 8;	 */ /* 8: TimeStamp, 2: Beacon Interval 2:Capability */
	/* pbss_network->Configuration.BeaconPeriod = le16_to_cpu(*(unsigned short*)p); */
	pbss_network->Configuration.BeaconPeriod = RTW_GET_LE16(p);

	/* capability */
	/* cap = *(unsigned short *)rtw_get_capability_from_ie(ie); */
	/* cap = le16_to_cpu(cap); */
	cap = RTW_GET_LE16(ie);

	/* SSID */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SSID_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0) {
		_rtw_memset(&pbss_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
		_rtw_memcpy(pbss_network->Ssid.Ssid, (p + 2), ie_len);
		pbss_network->Ssid.SsidLength = ie_len;
#ifdef CONFIG_P2P
		_rtw_memcpy(padapter->wdinfo.p2p_group_ssid, pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength);
		padapter->wdinfo.p2p_group_ssid_len = pbss_network->Ssid.SsidLength;
#endif
	}

	/* chnnel */
	channel = 0;
	pbss_network->Configuration.Length = 0;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _DSSET_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		channel = *(p + 2);

	pbss_network->Configuration.DSConfig = channel;


	_rtw_memset(supportRate, 0, NDIS_802_11_LENGTH_RATES_EX);
	/* get supported rates */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p !=  NULL) {
		_rtw_memcpy(supportRate, p + 2, ie_len);
		supportRateNum = ie_len;
	}

	/* get ext_supported rates */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ie_len, pbss_network->IELength - _BEACON_IE_OFFSET_);
	if (p !=  NULL) {
		_rtw_memcpy(supportRate + supportRateNum, p + 2, ie_len);
		supportRateNum += ie_len;

	}

	network_type = rtw_check_network_type(supportRate, supportRateNum, channel);

	rtw_set_supported_rate(pbss_network->SupportedRates, network_type);


	/* parsing ERP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		ERP_IE_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)p);

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
		if (rtw_parse_wpa2_ie(p, ie_len + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

			psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */
			psecuritypriv->wpa_psk |= BIT(1);

			psecuritypriv->wpa2_group_cipher = group_cipher;
			psecuritypriv->wpa2_pairwise_cipher = pairwise_cipher;
#if 0
			switch (group_cipher) {
			case WPA_CIPHER_NONE:
				psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
				break;
			case WPA_CIPHER_WEP40:
				psecuritypriv->wpa2_group_cipher = _WEP40_;
				break;
			case WPA_CIPHER_TKIP:
				psecuritypriv->wpa2_group_cipher = _TKIP_;
				break;
			case WPA_CIPHER_CCMP:
				psecuritypriv->wpa2_group_cipher = _AES_;
				break;
			case WPA_CIPHER_WEP104:
				psecuritypriv->wpa2_group_cipher = _WEP104_;
				break;
			}

			switch (pairwise_cipher) {
			case WPA_CIPHER_NONE:
				psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;
				break;
			case WPA_CIPHER_WEP40:
				psecuritypriv->wpa2_pairwise_cipher = _WEP40_;
				break;
			case WPA_CIPHER_TKIP:
				psecuritypriv->wpa2_pairwise_cipher = _TKIP_;
				break;
			case WPA_CIPHER_CCMP:
				psecuritypriv->wpa2_pairwise_cipher = _AES_;
				break;
			case WPA_CIPHER_WEP104:
				psecuritypriv->wpa2_pairwise_cipher = _WEP104_;
				break;
			}
#endif
		}

	}

	/* wpa */
	ie_len = 0;
	group_cipher = 0;
	pairwise_cipher = 0;
	psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;
	for (p = ie + _BEACON_IE_OFFSET_; ; p += (ie_len + 2)) {
		p = rtw_get_ie(p, _SSN_IE_1_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
		if ((p) && (_rtw_memcmp(p + 2, OUI1, 4))) {
			if (rtw_parse_wpa_ie(p, ie_len + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
				psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

				psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */

				psecuritypriv->wpa_psk |= BIT(0);

				psecuritypriv->wpa_group_cipher = group_cipher;
				psecuritypriv->wpa_pairwise_cipher = pairwise_cipher;

#if 0
				switch (group_cipher) {
				case WPA_CIPHER_NONE:
					psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
					break;
				case WPA_CIPHER_WEP40:
					psecuritypriv->wpa_group_cipher = _WEP40_;
					break;
				case WPA_CIPHER_TKIP:
					psecuritypriv->wpa_group_cipher = _TKIP_;
					break;
				case WPA_CIPHER_CCMP:
					psecuritypriv->wpa_group_cipher = _AES_;
					break;
				case WPA_CIPHER_WEP104:
					psecuritypriv->wpa_group_cipher = _WEP104_;
					break;
				}

				switch (pairwise_cipher) {
				case WPA_CIPHER_NONE:
					psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;
					break;
				case WPA_CIPHER_WEP40:
					psecuritypriv->wpa_pairwise_cipher = _WEP40_;
					break;
				case WPA_CIPHER_TKIP:
					psecuritypriv->wpa_pairwise_cipher = _TKIP_;
					break;
				case WPA_CIPHER_CCMP:
					psecuritypriv->wpa_pairwise_cipher = _AES_;
					break;
				case WPA_CIPHER_WEP104:
					psecuritypriv->wpa_pairwise_cipher = _WEP104_;
					break;
				}
#endif
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
		for (p = ie + _BEACON_IE_OFFSET_; ; p += (ie_len + 2)) {
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
			if ((p) && _rtw_memcmp(p + 2, WMM_PARA_IE, 6)) {
				pmlmepriv->qospriv.qos_option = 1;

				*(p + 8) |= BIT(7); /* QoS Info, support U-APSD */

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
#ifdef CONFIG_80211N_HT
	/* parsing HT_CAP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0) {
		u8 rf_type = 0;
		HT_CAP_AMPDU_FACTOR max_rx_ampdu_factor = MAX_AMPDU_FACTOR_64K;
		struct rtw_ieee80211_ht_cap *pht_cap = (struct rtw_ieee80211_ht_cap *)(p + 2);

		if (0) {
			RTW_INFO(FUNC_ADPT_FMT" HT_CAP_IE from upper layer:\n", FUNC_ADPT_ARG(padapter));
			dump_ht_cap_ie_content(RTW_DBGDUMP, p + 2, ie_len);
		}

		pHT_caps_ie = p;

		ht_cap = _TRUE;
		network_type |= WIRELESS_11_24N;

		rtw_ht_use_default_setting(padapter);

		/* Update HT Capabilities Info field */
		if (pmlmepriv->htpriv.sgi_20m == _FALSE)
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_SGI_20);

		if (pmlmepriv->htpriv.sgi_40m == _FALSE)
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_SGI_40);

		if (!TEST_FLAG(pmlmepriv->htpriv.ldpc_cap, LDPC_HT_ENABLE_RX))
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_LDPC_CODING);

		if (!TEST_FLAG(pmlmepriv->htpriv.stbc_cap, STBC_HT_ENABLE_TX))
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_TX_STBC);

		if (!TEST_FLAG(pmlmepriv->htpriv.stbc_cap, STBC_HT_ENABLE_RX))
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_RX_STBC_3R);

		/* Update A-MPDU Parameters field */
		pht_cap->ampdu_params_info &= ~(IEEE80211_HT_CAP_AMPDU_FACTOR | IEEE80211_HT_CAP_AMPDU_DENSITY);

		if ((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
		    (psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP))
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & (0x07 << 2));
		else
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);

		rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);
		pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_FACTOR & max_rx_ampdu_factor); /* set  Max Rx AMPDU size  to 64K */

		_rtw_memcpy(&(pmlmeinfo->HT_caps), pht_cap, sizeof(struct HT_caps_element));

		/* Update Supported MCS Set field */
		{
			struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
			u8 rx_nss = 0;
			int i;

			rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
			rx_nss = rtw_min(rf_type_to_rf_rx_cnt(rf_type), hal_spec->rx_nss_num);

			/* RX MCS Bitmask */
			switch (rx_nss) {
			case 1:
				set_mcs_rate_by_mask(HT_CAP_ELE_RX_MCS_MAP(pht_cap), MCS_RATE_1R);
				break;
			case 2:
				set_mcs_rate_by_mask(HT_CAP_ELE_RX_MCS_MAP(pht_cap), MCS_RATE_2R);
				break;
			case 3:
				set_mcs_rate_by_mask(HT_CAP_ELE_RX_MCS_MAP(pht_cap), MCS_RATE_3R);
				break;
			case 4:
				set_mcs_rate_by_mask(HT_CAP_ELE_RX_MCS_MAP(pht_cap), MCS_RATE_4R);
				break;
			default:
				RTW_WARN("rf_type:%d or rx_nss:%u is not expected\n", rf_type, hal_spec->rx_nss_num);
			}
			for (i = 0; i < 10; i++)
				*(HT_CAP_ELE_RX_MCS_MAP(pht_cap) + i) &= padapter->mlmeextpriv.default_supported_mcs_set[i];
		}

#ifdef CONFIG_BEAMFORMING
		/* Use registry value to enable HT Beamforming. */
		/* ToDo: use configure file to set these capability. */
		pht_cap->tx_BF_cap_info = 0;

		/* HT Beamformer */
		if (TEST_FLAG(pmlmepriv->htpriv.beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE)) {
			/* Transmit NDP Capable */
			SET_HT_CAP_TXBF_TRANSMIT_NDP_CAP(pht_cap, 1);
			/* Explicit Compressed Steering Capable */
			SET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pht_cap, 1);
			/* Compressed Steering Number Antennas */
			SET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pht_cap, 1);
			rtw_hal_get_def_var(padapter, HAL_DEF_BEAMFORMER_CAP, (u8 *)&rf_num);
			SET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(pht_cap, rf_num);
		}

		/* HT Beamformee */
		if (TEST_FLAG(pmlmepriv->htpriv.beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE)) {
			/* Receive NDP Capable */
			SET_HT_CAP_TXBF_RECEIVE_NDP_CAP(pht_cap, 1);
			/* Explicit Compressed Beamforming Feedback Capable */
			SET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pht_cap, 2);
			rtw_hal_get_def_var(padapter, HAL_DEF_BEAMFORMEE_CAP, (u8 *)&rf_num);
			SET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pht_cap, rf_num);
		}
#endif /* CONFIG_BEAMFORMING */

		_rtw_memcpy(&pmlmepriv->htpriv.ht_cap, p + 2, ie_len);

		if (0) {
			RTW_INFO(FUNC_ADPT_FMT" HT_CAP_IE driver masked:\n", FUNC_ADPT_ARG(padapter));
			dump_ht_cap_ie_content(RTW_DBGDUMP, p + 2, ie_len);
		}
	}

	/* parsing HT_INFO_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		pHT_info_ie = p;
#endif /* CONFIG_80211N_HT */
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

#ifdef CONFIG_80211N_HT
	pmlmepriv->htpriv.ht_option = _FALSE;

	if ((psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_TKIP) ||
	    (psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_TKIP)) {
		/* todo: */
		/* ht_cap = _FALSE; */
	}

	/* ht_cap	 */
	if (pregistrypriv->ht_enable && ht_cap == _TRUE) {
		pmlmepriv->htpriv.ht_option = _TRUE;
		pmlmepriv->qospriv.qos_option = 1;

		pmlmepriv->htpriv.ampdu_enable = pregistrypriv->ampdu_enable ? _TRUE : _FALSE;

		HT_caps_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)pHT_caps_ie);

		HT_info_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)pHT_info_ie);
	}
#endif

#ifdef CONFIG_80211AC_VHT

	/* Parsing VHT CAP IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTCapability, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		vht_cap = _TRUE;
	/* Parsing VHT OPERATION IE */


	pmlmepriv->vhtpriv.vht_option = _FALSE;
	/* if channel in 5G band, then add vht ie . */
	if ((pbss_network->Configuration.DSConfig > 14)
	    && (pmlmepriv->htpriv.ht_option == _TRUE)
	    && REGSTY_IS_11AC_ENABLE(pregistrypriv)
	    && hal_chk_proto_cap(padapter, PROTO_CAP_11AC)
	    && (!rfctl->country_ent || COUNTRY_CHPLAN_EN_11AC(rfctl->country_ent))
	   ) {
		if (vht_cap == _TRUE)
			pmlmepriv->vhtpriv.vht_option = _TRUE;
		else if (REGSTY_IS_11AC_AUTO(pregistrypriv))
			rtw_vht_ies_attach(padapter, pbss_network);
	}
#endif /* CONFIG_80211AC_VHT */

	if(pbss_network->Configuration.DSConfig <= 14 && padapter->registrypriv.wifi_spec == 1) {
		uint len = 0;

		SET_EXT_CAPABILITY_ELE_BSS_COEXIST(pmlmepriv->ext_capab_ie_data, 1);
		pmlmepriv->ext_capab_ie_len = 10;
		rtw_set_ie(pbss_network->IEs + pbss_network->IELength, EID_EXTCapability, 8, pmlmepriv->ext_capab_ie_data, &len);
		pbss_network->IELength += pmlmepriv->ext_capab_ie_len;
	}

	pbss_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX *)pbss_network);

	rtw_ies_get_chbw(pbss_network->IEs + _BEACON_IE_OFFSET_, pbss_network->IELength - _BEACON_IE_OFFSET_
		, &pmlmepriv->ori_ch, &pmlmepriv->ori_bw, &pmlmepriv->ori_offset);
	rtw_warn_on(pmlmepriv->ori_ch == 0);

	{
		/* alloc sta_info for ap itself */

		struct sta_info *sta;

		sta = rtw_get_stainfo(&padapter->stapriv, pbss_network->MacAddress);
		if (!sta) {
			sta = rtw_alloc_stainfo(&padapter->stapriv, pbss_network->MacAddress);
			if (sta == NULL)
				return _FAIL;
		}
	}

	rtw_startbss_cmd(padapter, RTW_CMDF_WAIT_ACK);
	{
		int sk_band = RTW_GET_SCAN_BAND_SKIP(padapter);

		if (sk_band)
			RTW_CLR_SCAN_BAND_SKIP(padapter, sk_band);
	}

	rtw_indicate_connect(padapter);

	pmlmepriv->cur_network.join_res = _TRUE;/* for check if already set beacon */

	/* update bc/mc sta_info */
	/* update_bmc_sta(padapter); */

	return ret;

}

#if CONFIG_RTW_MACADDR_ACL
void rtw_macaddr_acl_init(_adapter *adapter)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl = &stapriv->acl_list;
	_queue *acl_node_q = &acl->acl_node_q;
	int i;
	_irqL irqL;

	_enter_critical_bh(&(acl_node_q->lock), &irqL);
	_rtw_init_listhead(&(acl_node_q->queue));
	acl->num = 0;
	acl->mode = RTW_ACL_MODE_DISABLED;
	for (i = 0; i < NUM_ACL; i++) {
		_rtw_init_listhead(&acl->aclnode[i].list);
		acl->aclnode[i].valid = _FALSE;
	}
	_exit_critical_bh(&(acl_node_q->lock), &irqL);
}

void rtw_macaddr_acl_deinit(_adapter *adapter)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl = &stapriv->acl_list;
	_queue *acl_node_q = &acl->acl_node_q;
	_irqL irqL;
	_list *head, *list;
	struct rtw_wlan_acl_node *acl_node;

	_enter_critical_bh(&(acl_node_q->lock), &irqL);
	head = get_list_head(acl_node_q);
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		acl_node = LIST_CONTAINOR(list, struct rtw_wlan_acl_node, list);
		list = get_next(list);

		if (acl_node->valid == _TRUE) {
			acl_node->valid = _FALSE;
			rtw_list_delete(&acl_node->list);
			acl->num--;
		}
	}
	_exit_critical_bh(&(acl_node_q->lock), &irqL);

	rtw_warn_on(acl->num);
	acl->mode = RTW_ACL_MODE_DISABLED;
}

void rtw_set_macaddr_acl(_adapter *adapter, int mode)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl = &stapriv->acl_list;

	RTW_INFO(FUNC_ADPT_FMT" mode=%d\n", FUNC_ADPT_ARG(adapter), mode);

	acl->mode = mode;

	if (mode == RTW_ACL_MODE_DISABLED)
		rtw_macaddr_acl_deinit(adapter);
}

int rtw_acl_add_sta(_adapter *adapter, const u8 *addr)
{
	_irqL irqL;
	_list *list, *head;
	u8 existed = 0;
	int i = -1, ret = 0;
	struct rtw_wlan_acl_node *acl_node;
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl = &stapriv->acl_list;
	_queue *acl_node_q = &acl->acl_node_q;

	_enter_critical_bh(&(acl_node_q->lock), &irqL);

	head = get_list_head(acl_node_q);
	list = get_next(head);

	/* search for existed entry */
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		acl_node = LIST_CONTAINOR(list, struct rtw_wlan_acl_node, list);
		list = get_next(list);

		if (_rtw_memcmp(acl_node->addr, addr, ETH_ALEN)) {
			if (acl_node->valid == _TRUE) {
				existed = 1;
				break;
			}
		}
	}
	if (existed)
		goto release_lock;

	if (acl->num >= NUM_ACL)
		goto release_lock;

	/* find empty one and use */
	for (i = 0; i < NUM_ACL; i++) {

		acl_node = &acl->aclnode[i];
		if (acl_node->valid == _FALSE) {

			_rtw_init_listhead(&acl_node->list);
			_rtw_memcpy(acl_node->addr, addr, ETH_ALEN);
			acl_node->valid = _TRUE;

			rtw_list_insert_tail(&acl_node->list, get_list_head(acl_node_q));
			acl->num++;
			break;
		}
	}

release_lock:
	_exit_critical_bh(&(acl_node_q->lock), &irqL);

	if (!existed && (i < 0 || i >= NUM_ACL))
		ret = -1;

	RTW_INFO(FUNC_ADPT_FMT" "MAC_FMT" %s (acl_num=%d)\n"
		 , FUNC_ADPT_ARG(adapter), MAC_ARG(addr)
		, (existed ? "existed" : ((i < 0 || i >= NUM_ACL) ? "no room" : "added"))
		 , acl->num);

	return ret;
}

int rtw_acl_remove_sta(_adapter *adapter, const u8 *addr)
{
	_irqL irqL;
	_list *list, *head;
	int ret = 0;
	struct rtw_wlan_acl_node *acl_node;
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl = &stapriv->acl_list;
	_queue	*acl_node_q = &acl->acl_node_q;
	u8 is_baddr = is_broadcast_mac_addr(addr);
	u8 match = 0;

	_enter_critical_bh(&(acl_node_q->lock), &irqL);

	head = get_list_head(acl_node_q);
	list = get_next(head);

	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		acl_node = LIST_CONTAINOR(list, struct rtw_wlan_acl_node, list);
		list = get_next(list);

		if (is_baddr || _rtw_memcmp(acl_node->addr, addr, ETH_ALEN)) {
			if (acl_node->valid == _TRUE) {
				acl_node->valid = _FALSE;
				rtw_list_delete(&acl_node->list);
				acl->num--;
				match = 1;
			}
		}
	}

	_exit_critical_bh(&(acl_node_q->lock), &irqL);

	RTW_INFO(FUNC_ADPT_FMT" "MAC_FMT" %s (acl_num=%d)\n"
		 , FUNC_ADPT_ARG(adapter), MAC_ARG(addr)
		 , is_baddr ? "clear all" : (match ? "match" : "no found")
		 , acl->num);

	return ret;
}
#endif /* CONFIG_RTW_MACADDR_ACL */

u8 rtw_ap_set_pairwise_key(_adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj			*ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv			*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = (struct set_stakey_parm *)rtw_zmalloc(sizeof(struct set_stakey_parm));
	if (psetstakey_para == NULL) {
		rtw_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);


	psetstakey_para->algorithm = (u8)psta->dot118021XPrivacy;

	_rtw_memcpy(psetstakey_para->addr, psta->cmn.mac_addr, ETH_ALEN);

	_rtw_memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);


	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

static int rtw_ap_set_key(_adapter *padapter, u8 *key, u8 alg, int keyid, u8 set_tx)
{
	u8 keylen;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	int res = _SUCCESS;

	/* RTW_INFO("%s\n", __FUNCTION__); */

	pcmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}
	psetkeyparm = (struct setkey_parm *)rtw_zmalloc(sizeof(struct setkey_parm));
	if (psetkeyparm == NULL) {
		rtw_mfree((unsigned char *)pcmd, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	_rtw_memset(psetkeyparm, 0, sizeof(struct setkey_parm));

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

	_rtw_memcpy(&(psetkeyparm->key[0]), key, keylen);

	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz = (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;


	_rtw_init_listhead(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

int rtw_ap_set_group_key(_adapter *padapter, u8 *key, u8 alg, int keyid)
{
	RTW_INFO("%s\n", __FUNCTION__);

	return rtw_ap_set_key(padapter, key, alg, keyid, 1);
}

int rtw_ap_set_wep_key(_adapter *padapter, u8 *key, u8 keylen, int keyid, u8 set_tx)
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

	RTW_INFO("%s\n", __FUNCTION__);

	return rtw_ap_set_key(padapter, key, alg, keyid, set_tx);
}

u8 rtw_ap_bmc_frames_hdl(_adapter *padapter)
{
#define HIQ_XMIT_COUNTS (6)
	_irqL irqL;
	struct sta_info *psta_bmc;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct sta_priv  *pstapriv = &padapter->stapriv;
	bool update_tim = _FALSE;


	if (padapter->registrypriv.wifi_spec != 1)
		return H2C_SUCCESS;


	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return H2C_SUCCESS;


	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	if ((pstapriv->tim_bitmap & BIT(0)) && (psta_bmc->sleepq_len > 0)) {
		int tx_counts = 0;

		_update_beacon(padapter, _TIM_IE_, NULL, _FALSE, "update TIM with TIB=1");

		RTW_INFO("sleepq_len of bmc_sta = %d\n", psta_bmc->sleepq_len);

		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
			pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			rtw_list_delete(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			tx_counts++;

			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;

			if (tx_counts == HIQ_XMIT_COUNTS)
				pxmitframe->attrib.mdata = 0;

			pxmitframe->attrib.triggered = 1;

			if (xmitframe_hiq_filter(pxmitframe) == _TRUE)
				pxmitframe->attrib.qsel = QSLT_HIGH;/*HIQ*/

			rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

			if (tx_counts == HIQ_XMIT_COUNTS)
				break;

		}

	} else {
		if (psta_bmc->sleepq_len == 0) {

			/*RTW_INFO("sleepq_len of bmc_sta = %d\n", psta_bmc->sleepq_len);*/

			if (pstapriv->tim_bitmap & BIT(0))
				update_tim = _TRUE;

			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);

			if (update_tim == _TRUE) {
				RTW_INFO("clear TIB\n");
				_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "bmc sleepq and HIQ empty");
			}
		}
	}

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

#if 0
	/* HIQ Check */
	rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &empty);

	while (_FALSE == empty && rtw_get_passing_time_ms(start) < 3000) {
		rtw_msleep_os(100);
		rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &empty);
	}


	printk("check if hiq empty=%d\n", empty);
#endif

	return H2C_SUCCESS;
}

#ifdef CONFIG_NATIVEAP_MLME

static void associated_stainfo_update(_adapter *padapter, struct sta_info *psta, u32 sta_info_type)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	RTW_INFO("%s: "MAC_FMT", updated_type=0x%x\n", __func__, MAC_ARG(psta->cmn.mac_addr), sta_info_type);

	if (sta_info_type & STA_INFO_UPDATE_BW) {

		if ((psta->flags & WLAN_STA_HT) && !psta->ht_20mhz_set) {
			if (pmlmepriv->sw_to_20mhz) {
				psta->cmn.bw_mode = CHANNEL_WIDTH_20;
				/*psta->htpriv.ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;*/
				psta->htpriv.sgi_40m = _FALSE;
			} else {
				/*TODO: Switch back to 40MHZ?80MHZ*/
			}
		}
	}

	/*
		if (sta_info_type & STA_INFO_UPDATE_RATE) {

		}
	*/

	if (sta_info_type & STA_INFO_UPDATE_PROTECTION_MODE)
		VCS_update(padapter, psta);

	/*
		if (sta_info_type & STA_INFO_UPDATE_CAP) {

		}

		if (sta_info_type & STA_INFO_UPDATE_HT_CAP) {

		}

		if (sta_info_type & STA_INFO_UPDATE_VHT_CAP) {

		}
	*/

}

static void update_bcn_ext_capab_ie(_adapter *padapter)
{
	sint ie_len = 0;
	unsigned char	*pbuf;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	u8 *ie = pnetwork->IEs;
	u8 null_extcap_data[8] = {0};

	pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_CAP_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
	if (pbuf && ie_len > 0)
		rtw_remove_bcn_ie(padapter, pnetwork, _EXT_CAP_IE_);

	if ((pmlmepriv->ext_capab_ie_len > 0) &&
	    (_rtw_memcmp(pmlmepriv->ext_capab_ie_data, null_extcap_data, sizeof(null_extcap_data)) == _FALSE))
		rtw_add_bcn_ie(padapter, pnetwork, _EXT_CAP_IE_, pmlmepriv->ext_capab_ie_data, pmlmepriv->ext_capab_ie_len);

}

static void update_bcn_erpinfo_ie(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	unsigned char *p, *ie = pnetwork->IEs;
	u32 len = 0;

	RTW_INFO("%s, ERP_enable=%d\n", __FUNCTION__, pmlmeinfo->ERP_enable);

	if (!pmlmeinfo->ERP_enable)
		return;

	/* parsing ERP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
	if (p && len > 0) {
		PNDIS_802_11_VARIABLE_IEs pIE = (PNDIS_802_11_VARIABLE_IEs)p;

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

static void update_bcn_htcap_ie(_adapter *padapter)
{
	RTW_INFO("%s\n", __FUNCTION__);

}

static void update_bcn_htinfo_ie(_adapter *padapter)
{
	/*
	u8 beacon_updated = _FALSE;
	u32 sta_info_update_type = STA_INFO_UPDATE_NONE;
	*/
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	unsigned char *p, *ie = pnetwork->IEs;
	u32 len = 0;

	if (pmlmepriv->htpriv.ht_option == _FALSE)
		return;

	if (pmlmeinfo->HT_info_enable != 1)
		return;


	RTW_INFO("%s current operation mode=0x%X\n",
		 __FUNCTION__, pmlmepriv->ht_op_mode);

	RTW_INFO("num_sta_40mhz_intolerant(%d), 20mhz_width_req(%d), intolerant_ch_rpt(%d), olbc(%d)\n",
		pmlmepriv->num_sta_40mhz_intolerant, pmlmepriv->ht_20mhz_width_req, pmlmepriv->ht_intolerant_ch_reported, ATOMIC_READ(&pmlmepriv->olbc));

	/*parsing HT_INFO_IE, currently only update ht_op_mode - pht_info->infos[1] & pht_info->infos[2] for wifi logo test*/
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
	if (p && len > 0) {
		struct HT_info_element *pht_info = NULL;

		pht_info = (struct HT_info_element *)(p + 2);

		/* for STA Channel Width/Secondary Channel Offset*/
		if ((pmlmepriv->sw_to_20mhz == 0) && (pmlmeext->cur_channel <= 14)) {
			if ((pmlmepriv->num_sta_40mhz_intolerant > 0) || (pmlmepriv->ht_20mhz_width_req == _TRUE)
			    || (pmlmepriv->ht_intolerant_ch_reported == _TRUE) || (ATOMIC_READ(&pmlmepriv->olbc) == _TRUE)) {
				SET_HT_OP_ELE_2ND_CHL_OFFSET(pht_info, 0);
				SET_HT_OP_ELE_STA_CHL_WIDTH(pht_info, 0);

				pmlmepriv->sw_to_20mhz = 1;
				/*
				sta_info_update_type |= STA_INFO_UPDATE_BW;
				beacon_updated = _TRUE;
				*/

				RTW_INFO("%s:switching to 20Mhz\n", __FUNCTION__);

				/*TODO : cur_bwmode/cur_ch_offset switches to 20Mhz*/
			}
		} else {

			if ((pmlmepriv->num_sta_40mhz_intolerant == 0) && (pmlmepriv->ht_20mhz_width_req == _FALSE)
			    && (pmlmepriv->ht_intolerant_ch_reported == _FALSE) && (ATOMIC_READ(&pmlmepriv->olbc) == _FALSE)) {

				if (pmlmeext->cur_bwmode >= CHANNEL_WIDTH_40) {

					SET_HT_OP_ELE_STA_CHL_WIDTH(pht_info, 1);

					SET_HT_OP_ELE_2ND_CHL_OFFSET(pht_info,
						(pmlmeext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER) ?
						HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE : HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW);

					pmlmepriv->sw_to_20mhz = 0;
					/*
					sta_info_update_type |= STA_INFO_UPDATE_BW;
					beacon_updated = _TRUE;
					*/

					RTW_INFO("%s:switching back to 40Mhz\n", __FUNCTION__);
				}
			}
		}

		/* to update  ht_op_mode*/
		*(u16 *)(pht_info->infos + 1) = cpu_to_le16(pmlmepriv->ht_op_mode);

	}

	/*associated_clients_update(padapter, beacon_updated, sta_info_update_type);*/

}

static void update_bcn_rsn_ie(_adapter *padapter)
{
	RTW_INFO("%s\n", __FUNCTION__);

}

static void update_bcn_wpa_ie(_adapter *padapter)
{
	RTW_INFO("%s\n", __FUNCTION__);

}

static void update_bcn_wmm_ie(_adapter *padapter)
{
	RTW_INFO("%s\n", __FUNCTION__);

}

static void update_bcn_wps_ie(_adapter *padapter)
{
	u8 *pwps_ie = NULL, *pwps_ie_src, *premainder_ie, *pbackup_remainder_ie = NULL;
	uint wps_ielen = 0, wps_offset, remainder_ielen;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	unsigned char *ie = pnetwork->IEs;
	u32 ielen = pnetwork->IELength;


	RTW_INFO("%s\n", __FUNCTION__);

	pwps_ie = rtw_get_wps_ie(ie + _FIXED_IE_LENGTH_, ielen - _FIXED_IE_LENGTH_, NULL, &wps_ielen);

	if (pwps_ie == NULL || wps_ielen == 0)
		return;

	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if (pwps_ie_src == NULL)
		return;

	wps_offset = (uint)(pwps_ie - ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if (remainder_ielen > 0) {
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if (pbackup_remainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	wps_ielen = (uint)pwps_ie_src[1];/* to get ie data len */
	if ((wps_offset + wps_ielen + 2 + remainder_ielen) <= MAX_IE_SZ) {
		_rtw_memcpy(pwps_ie, pwps_ie_src, wps_ielen + 2);
		pwps_ie += (wps_ielen + 2);

		if (pbackup_remainder_ie)
			_rtw_memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);

		/* update IELength */
		pnetwork->IELength = wps_offset + (wps_ielen + 2) + remainder_ielen;
	}

	if (pbackup_remainder_ie)
		rtw_mfree(pbackup_remainder_ie, remainder_ielen);

	/* deal with the case without set_tx_beacon_cmd() in update_beacon() */
#if defined(CONFIG_INTERRUPT_BASED_TXBCN) || defined(CONFIG_PCI_HCI)
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		u8 sr = 0;
		rtw_get_wps_attr_content(pwps_ie_src,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);

		if (sr) {
			set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			RTW_INFO("%s, set WIFI_UNDER_WPS\n", __func__);
		} else {
			clr_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			RTW_INFO("%s, clr WIFI_UNDER_WPS\n", __func__);
		}
	}
#endif
}

static void update_bcn_p2p_ie(_adapter *padapter)
{

}

static void update_bcn_vendor_spec_ie(_adapter *padapter, u8 *oui)
{
	RTW_INFO("%s\n", __FUNCTION__);

	if (_rtw_memcmp(RTW_WPA_OUI, oui, 4))
		update_bcn_wpa_ie(padapter);
	else if (_rtw_memcmp(WMM_OUI, oui, 4))
		update_bcn_wmm_ie(padapter);
	else if (_rtw_memcmp(WPS_OUI, oui, 4))
		update_bcn_wps_ie(padapter);
	else if (_rtw_memcmp(P2P_OUI, oui, 4))
		update_bcn_p2p_ie(padapter);
	else
		RTW_INFO("unknown OUI type!\n");


}

void _update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx, const char *tag)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;

	if (!padapter)
		return;

	pmlmepriv = &(padapter->mlmepriv);
	pmlmeext = &(padapter->mlmeextpriv);

	if (pmlmeext->bstart_bss == _FALSE)
		return;

	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);

	switch (ie_id) {
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

	case _EXT_CAP_IE_:
		update_bcn_ext_capab_ie(padapter);
		break;

	case _VENDOR_SPECIFIC_IE_:
		update_bcn_vendor_spec_ie(padapter, oui);
		break;

	case 0xFF:
	default:
		break;
	}

	pmlmepriv->update_bcn = _TRUE;

	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);

#ifndef CONFIG_INTERRUPT_BASED_TXBCN
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	if (tx) {
		/* send_beacon(padapter); */ /* send_beacon must execute on TSR level */
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" ie_id:%u - %s\n", FUNC_ADPT_ARG(padapter), ie_id, tag);
		set_tx_beacon_cmd(padapter);
	}
#else
	{
		/* PCI will issue beacon when BCN interrupt occurs.		 */
	}
#endif
#endif /* !CONFIG_INTERRUPT_BASED_TXBCN */
}

#ifdef CONFIG_80211N_HT

void rtw_process_public_act_bsscoex(_adapter *padapter, u8 *pframe, uint frame_len)
{
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 beacon_updated = _FALSE;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	uint frame_body_len = frame_len - sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 category, action;

	psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
	if (psta == NULL)
		return;


	category = frame_body[0];
	action = frame_body[1];

	if (frame_body_len > 0) {
		if ((frame_body[2] == EID_BSSCoexistence) && (frame_body[3] > 0)) {
			u8 ie_data = frame_body[4];

			if (ie_data & RTW_WLAN_20_40_BSS_COEX_40MHZ_INTOL) {
				if (psta->ht_40mhz_intolerant == 0) {
					psta->ht_40mhz_intolerant = 1;
					pmlmepriv->num_sta_40mhz_intolerant++;
					beacon_updated = _TRUE;
				}
			} else if (ie_data & RTW_WLAN_20_40_BSS_COEX_20MHZ_WIDTH_REQ)	{
				if (pmlmepriv->ht_20mhz_width_req == _FALSE) {
					pmlmepriv->ht_20mhz_width_req = _TRUE;
					beacon_updated = _TRUE;
				}
			} else
				beacon_updated = _FALSE;
		}
	}

	if (frame_body_len > 8) {
		/* if EID_BSSIntolerantChlReport ie exists */
		if ((frame_body[5] == EID_BSSIntolerantChlReport) && (frame_body[6] > 0)) {
			/*todo:*/
			if (pmlmepriv->ht_intolerant_ch_reported == _FALSE) {
				pmlmepriv->ht_intolerant_ch_reported = _TRUE;
				beacon_updated = _TRUE;
			}
		}
	}

	if (beacon_updated) {

		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE);

		associated_stainfo_update(padapter, psta, STA_INFO_UPDATE_BW);
	}



}

void rtw_process_ht_action_smps(_adapter *padapter, u8 *ta, u8 ctrl_field)
{
	u8 e_field, m_field;
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta = rtw_get_stainfo(pstapriv, ta);
	if (psta == NULL)
		return;

	e_field = (ctrl_field & BIT(0)) ? 1 : 0;
	m_field = (ctrl_field & BIT(1)) ? 1 : 0;

	if (e_field) {

		/* enable */
		/* 0:static SMPS, 1:dynamic SMPS, 3:SMPS disabled, 2:reserved*/

		if (m_field) /*mode*/
			psta->htpriv.smps_cap = 1;
		else
			psta->htpriv.smps_cap = 0;
	} else {
		/*disable*/
		psta->htpriv.smps_cap = 3;
	}

	rtw_dm_ra_mask_wk_cmd(padapter, (u8 *)psta);

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
int rtw_ht_operation_update(_adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;

	if (pmlmepriv->htpriv.ht_option == _FALSE)
		return 0;

	/*if (!iface->conf->ieee80211n || iface->conf->ht_op_mode_fixed)
		return 0;*/

	RTW_INFO("%s current operation mode=0x%X\n",
		 __FUNCTION__, pmlmepriv->ht_op_mode);

	if (!(pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT)
	    && pmlmepriv->num_sta_ht_no_gf) {
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
	    (pmlmepriv->num_sta_no_ht || ATOMIC_READ(&pmlmepriv->olbc_ht))) {
		pmlmepriv->ht_op_mode |= HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		    HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
		   (pmlmepriv->num_sta_no_ht == 0 && !ATOMIC_READ(&pmlmepriv->olbc_ht))) {
		pmlmepriv->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	}

	/* Note: currently we switch to the MIXED op mode if HT non-greenfield
	 * station is associated. Probably it's a theoretical case, since
	 * it looks like all known HT STAs support greenfield.
	 */
	new_op_mode = 0;
	if (pmlmepriv->num_sta_no_ht /*||
	    (pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT)*/)
		new_op_mode = OP_MODE_MIXED;
	else if ((phtpriv_ap->ht_cap.cap_info & IEEE80211_HT_CAP_SUP_WIDTH)
		 && pmlmepriv->num_sta_ht_20mhz)
		new_op_mode = OP_MODE_20MHZ_HT_STA_ASSOCED;
	else if (ATOMIC_READ(&pmlmepriv->olbc_ht))
		new_op_mode = OP_MODE_MAY_BE_LEGACY_STAS;
	else
		new_op_mode = OP_MODE_PURE;

	cur_op_mode = pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_OP_MODE_MASK;
	if (cur_op_mode != new_op_mode) {
		pmlmepriv->ht_op_mode &= ~HT_INFO_OPERATION_MODE_OP_MODE_MASK;
		pmlmepriv->ht_op_mode |= new_op_mode;
		op_mode_changes++;
	}

	RTW_INFO("%s new operation mode=0x%X changes=%d\n",
		 __FUNCTION__, pmlmepriv->ht_op_mode, op_mode_changes);

	return op_mode_changes;

}

#endif /* CONFIG_80211N_HT */

void associated_clients_update(_adapter *padapter, u8 updated, u32 sta_info_type)
{
	/* update associcated stations cap. */
	if (updated == _TRUE) {
		_irqL irqL;
		_list	*phead, *plist;
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &padapter->stapriv;

		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

		phead = &pstapriv->asoc_list;
		plist = get_next(phead);

		/* check asoc_queue */
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);

			plist = get_next(plist);

			associated_stainfo_update(padapter, psta, sta_info_type);
		}

		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	}

}

/* called > TSR LEVEL for USB or SDIO Interface*/
void bss_cap_update_on_sta_join(_adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = _FALSE;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);


#if 0
	if (!(psta->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) &&
	    !psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 1;
		pmlmepriv->num_sta_no_short_preamble++;
		if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		    (pmlmepriv->num_sta_no_short_preamble == 1))
			ieee802_11_set_beacons(hapd->iface);
	}
#endif


	if (!(psta->flags & WLAN_STA_SHORT_PREAMBLE)) {
		if (!psta->no_short_preamble_set) {
			psta->no_short_preamble_set = 1;

			pmlmepriv->num_sta_no_short_preamble++;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
			    (pmlmepriv->num_sta_no_short_preamble == 1))
				beacon_updated = _TRUE;
		}
	} else {
		if (psta->no_short_preamble_set) {
			psta->no_short_preamble_set = 0;

			pmlmepriv->num_sta_no_short_preamble--;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
			    (pmlmepriv->num_sta_no_short_preamble == 0))
				beacon_updated = _TRUE;
		}
	}

#if 0
	if (psta->flags & WLAN_STA_NONERP && !psta->nonerp_set) {
		psta->nonerp_set = 1;
		pmlmepriv->num_sta_non_erp++;
		if (pmlmepriv->num_sta_non_erp == 1)
			ieee802_11_set_beacons(hapd->iface);
	}
#endif

	if (psta->flags & WLAN_STA_NONERP) {
		if (!psta->nonerp_set) {
			psta->nonerp_set = 1;

			pmlmepriv->num_sta_non_erp++;

			if (pmlmepriv->num_sta_non_erp == 1) {
				beacon_updated = _TRUE;
				update_beacon(padapter, _ERPINFO_IE_, NULL, _FALSE);
			}
		}

	} else {
		if (psta->nonerp_set) {
			psta->nonerp_set = 0;

			pmlmepriv->num_sta_non_erp--;

			if (pmlmepriv->num_sta_non_erp == 0) {
				beacon_updated = _TRUE;
				update_beacon(padapter, _ERPINFO_IE_, NULL, _FALSE);
			}
		}

	}


#if 0
	if (!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT) &&
	    !psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 1;
		pmlmepriv->num_sta_no_short_slot_time++;
		if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		    (pmlmepriv->num_sta_no_short_slot_time == 1))
			ieee802_11_set_beacons(hapd->iface);
	}
#endif

	if (!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT)) {
		if (!psta->no_short_slot_time_set) {
			psta->no_short_slot_time_set = 1;

			pmlmepriv->num_sta_no_short_slot_time++;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
			    (pmlmepriv->num_sta_no_short_slot_time == 1))
				beacon_updated = _TRUE;
		}
	} else {
		if (psta->no_short_slot_time_set) {
			psta->no_short_slot_time_set = 0;

			pmlmepriv->num_sta_no_short_slot_time--;

			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
			    (pmlmepriv->num_sta_no_short_slot_time == 0))
				beacon_updated = _TRUE;
		}
	}

#ifdef CONFIG_80211N_HT
	if (psta->flags & WLAN_STA_HT) {
		u16 ht_capab = le16_to_cpu(psta->htpriv.ht_cap.cap_info);

		RTW_INFO("HT: STA " MAC_FMT " HT Capabilities Info: 0x%04x\n",
			MAC_ARG(psta->cmn.mac_addr), ht_capab);

		if (psta->no_ht_set) {
			psta->no_ht_set = 0;
			pmlmepriv->num_sta_no_ht--;
		}

		if ((ht_capab & IEEE80211_HT_CAP_GRN_FLD) == 0) {
			if (!psta->no_ht_gf_set) {
				psta->no_ht_gf_set = 1;
				pmlmepriv->num_sta_ht_no_gf++;
			}
			RTW_INFO("%s STA " MAC_FMT " - no "
				 "greenfield, num of non-gf stations %d\n",
				 __FUNCTION__, MAC_ARG(psta->cmn.mac_addr),
				 pmlmepriv->num_sta_ht_no_gf);
		}

		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH) == 0) {
			if (!psta->ht_20mhz_set) {
				psta->ht_20mhz_set = 1;
				pmlmepriv->num_sta_ht_20mhz++;
			}
			RTW_INFO("%s STA " MAC_FMT " - 20 MHz HT, "
				 "num of 20MHz HT STAs %d\n",
				 __FUNCTION__, MAC_ARG(psta->cmn.mac_addr),
				 pmlmepriv->num_sta_ht_20mhz);
		}

	} else {
		if (!psta->no_ht_set) {
			psta->no_ht_set = 1;
			pmlmepriv->num_sta_no_ht++;
		}
		if (pmlmepriv->htpriv.ht_option == _TRUE) {
			RTW_INFO("%s STA " MAC_FMT
				 " - no HT, num of non-HT stations %d\n",
				 __FUNCTION__, MAC_ARG(psta->cmn.mac_addr),
				 pmlmepriv->num_sta_no_ht);
		}
	}

	if (rtw_ht_operation_update(padapter) > 0) {
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _FALSE);
		beacon_updated = _TRUE;
	}
#endif /* CONFIG_80211N_HT */

	if (beacon_updated)
		update_beacon(padapter, 0xFF, NULL, _TRUE);

	/* update associcated stations cap. */
	associated_clients_update(padapter,  beacon_updated, STA_INFO_UPDATE_ALL);

	RTW_INFO("%s, updated=%d\n", __func__, beacon_updated);

}

u8 bss_cap_update_on_sta_leave(_adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = _FALSE;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	if (!psta)
		return beacon_updated;

	if (pstapriv->tim_bitmap & BIT(psta->cmn.aid)) {
		pstapriv->tim_bitmap &= (~ BIT(psta->cmn.aid));
		beacon_updated = _TRUE;
		update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
	}

	if (psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 0;
		pmlmepriv->num_sta_no_short_preamble--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_preamble == 0)
			beacon_updated = _TRUE;
	}

	if (psta->nonerp_set) {
		psta->nonerp_set = 0;
		pmlmepriv->num_sta_non_erp--;
		if (pmlmepriv->num_sta_non_erp == 0) {
			beacon_updated = _TRUE;
			update_beacon(padapter, _ERPINFO_IE_, NULL, _FALSE);
		}
	}

	if (psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 0;
		pmlmepriv->num_sta_no_short_slot_time--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_slot_time == 0)
			beacon_updated = _TRUE;
	}

#ifdef CONFIG_80211N_HT
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
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _FALSE);
	}
#endif /* CONFIG_80211N_HT */

	if (beacon_updated == _TRUE)
		update_beacon(padapter, 0xFF, NULL, _TRUE);

#if 0
	/* update associated stations cap. */
	associated_clients_update(padapter,  beacon_updated, STA_INFO_UPDATE_ALL); /* move it to avoid deadlock */
#endif

	RTW_INFO("%s, updated=%d\n", __func__, beacon_updated);

	return beacon_updated;

}

u8 ap_free_sta(_adapter *padapter, struct sta_info *psta, bool active, u16 reason, bool enqueue)
{
	_irqL irqL;
	u8 beacon_updated = _FALSE;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return beacon_updated;

	if (active == _TRUE) {
#ifdef CONFIG_80211N_HT
		/* tear down Rx AMPDU */
		send_delba(padapter, 0, psta->cmn.mac_addr);/* recipient */

		/* tear down TX AMPDU */
		send_delba(padapter, 1, psta->cmn.mac_addr);/*  */ /* originator */

#endif /* CONFIG_80211N_HT */

		issue_deauth(padapter, psta->cmn.mac_addr, reason);
	}

#ifdef CONFIG_BEAMFORMING
	beamforming_wk_cmd(padapter, BEAMFORMING_CTRL_LEAVE, psta->cmn.mac_addr, ETH_ALEN, 1);
#endif

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	/* clear cam entry / key */
	rtw_clearstakey_cmd(padapter, psta, enqueue);


	_enter_critical_bh(&psta->lock, &irqL);
	psta->state &= ~_FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL);

	{
#ifdef CONFIG_IOCTL_CFG80211
		#ifdef COMPAT_KERNEL_RELEASE
		rtw_cfg80211_indicate_sta_disassoc(padapter, psta->cmn.mac_addr, reason);
		#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
		rtw_cfg80211_indicate_sta_disassoc(padapter, psta->cmn.mac_addr, reason);
		#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER) */
		/* will call rtw_cfg80211_indicate_sta_disassoc() in cmd_thread for old API context */
		#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER) */
#else
		rtw_indicate_sta_disassoc_event(padapter, psta);
#endif
	}

	report_del_sta_event(padapter, psta->cmn.mac_addr, reason, enqueue, _FALSE);

	beacon_updated = bss_cap_update_on_sta_leave(padapter, psta);

	/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);					 */
	rtw_free_stainfo(padapter, psta);
	/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL); */


	return beacon_updated;

}

int rtw_ap_inform_ch_switch(_adapter *padapter, u8 new_ch, u8 ch_offset)
{
	_irqL irqL;
	_list	*phead, *plist;
	int ret = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		return ret;

	RTW_INFO(FUNC_NDEV_FMT" with ch:%u, offset:%u\n",
		 FUNC_NDEV_ARG(padapter->pnetdev), new_ch, ch_offset);

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* for each sta in asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		issue_action_spct_ch_switch(padapter, psta->cmn.mac_addr, new_ch, ch_offset);
		psta->expire_to = ((pstapriv->expire_to * 2) > 5) ? 5 : (pstapriv->expire_to * 2);
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	issue_action_spct_ch_switch(padapter, bc_addr, new_ch, ch_offset);

	return ret;
}

int rtw_sta_flush(_adapter *padapter, bool enqueue)
{
	_irqL irqL;
	_list	*phead, *plist;
	int ret = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 flush_num = 0;
	char flush_list[NUM_STA];
	int i;

	if (!MLME_IS_AP(padapter) && !MLME_IS_MESH(padapter))
		return ret;

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));

	/* pick sta from sta asoc_queue */
	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		int stainfo_offset;

		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		rtw_list_delete(&psta->asoc_list);
		pstapriv->asoc_list_cnt--;

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset))
			flush_list[flush_num++] = stainfo_offset;
		else
			rtw_warn_on(1);
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	/* call ap_free_sta() for each sta picked */
	for (i = 0; i < flush_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, flush_list[i]);
		ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_DEAUTH_LEAVING, enqueue);
	}

	issue_deauth(padapter, bc_addr, WLAN_REASON_DEAUTH_LEAVING);

	associated_clients_update(padapter, _TRUE, STA_INFO_UPDATE_ALL);

	return ret;
}

/* called > TSR LEVEL for USB or SDIO Interface*/
void sta_info_update(_adapter *padapter, struct sta_info *psta)
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


#ifdef CONFIG_80211N_HT
	/* update 802.11n ht cap. */
	if (WLAN_STA_HT & flags) {
		psta->htpriv.ht_option = _TRUE;
		psta->qos_option = 1;

		psta->htpriv.smps_cap = (psta->htpriv.ht_cap.cap_info & IEEE80211_HT_CAP_SM_PS) >> 2;
	} else
		psta->htpriv.ht_option = _FALSE;

	if (pmlmepriv->htpriv.ht_option == _FALSE)
		psta->htpriv.ht_option = _FALSE;
#endif

#ifdef CONFIG_80211AC_VHT
	/* update 802.11AC vht cap. */
	if (WLAN_STA_VHT & flags)
		psta->vhtpriv.vht_option = _TRUE;
	else
		psta->vhtpriv.vht_option = _FALSE;

	if (pmlmepriv->vhtpriv.vht_option == _FALSE)
		psta->vhtpriv.vht_option = _FALSE;
#endif

	update_sta_info_apmode(padapter, psta);
}

/* called >= TSR LEVEL for USB or SDIO Interface*/
void ap_sta_info_defer_update(_adapter *padapter, struct sta_info *psta)
{
	if (psta->state & _FW_LINKED)
		rtw_hal_update_ra_mask(psta); /* DM_RATR_STA_INIT */
}
/* restore hw setting from sw data structures */
void rtw_ap_restore_network(_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	_irqL irqL;
	_list	*phead, *plist;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	rtw_setopmode_cmd(padapter, Ndis802_11APMode, _FALSE);

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	rtw_startbss_cmd(padapter, RTW_CMDF_DIRECTLY);

	if ((padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
	    (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)) {
		/* restore group key, WEP keys is restored in ips_leave() */
		rtw_set_key(padapter, psecuritypriv, psecuritypriv->dot118021XGrpKeyid, 0, _FALSE);
	}

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		int stainfo_offset;

		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset))
			chk_alive_list[chk_alive_num++] = stainfo_offset;
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	for (i = 0; i < chk_alive_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);

		if (psta == NULL)
			RTW_INFO(FUNC_ADPT_FMT" sta_info is null\n", FUNC_ADPT_ARG(padapter));
		else if (psta->state & _FW_LINKED) {
			rtw_sta_media_status_rpt(padapter, psta, 1);
			Update_RA_Entry(padapter, psta);
			/* pairwise key */
			/* per sta pairwise key and settings */
			if ((padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
			    (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_))
				rtw_setstakey_cmd(padapter, psta, UNICAST_KEY, _FALSE);
		}
	}

}

void start_ap_mode(_adapter *padapter)
{
	int i;
	struct sta_info *psta = NULL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	pmlmepriv->update_bcn = _FALSE;

	/*init_mlme_ap_info(padapter);*/

	pmlmeext->bstart_bss = _FALSE;

	pmlmepriv->num_sta_non_erp = 0;

	pmlmepriv->num_sta_no_short_slot_time = 0;

	pmlmepriv->num_sta_no_short_preamble = 0;

	pmlmepriv->num_sta_ht_no_gf = 0;
#ifdef CONFIG_80211N_HT
	pmlmepriv->num_sta_no_ht = 0;
#endif /* CONFIG_80211N_HT */
	pmlmeinfo->HT_info_enable = 0;
	pmlmeinfo->HT_caps_enable = 0;
	pmlmeinfo->HT_enable = 0;

	pmlmepriv->num_sta_ht_20mhz = 0;
	pmlmepriv->num_sta_40mhz_intolerant = 0;
	ATOMIC_SET(&pmlmepriv->olbc, _FALSE);
	ATOMIC_SET(&pmlmepriv->olbc_ht, _FALSE);

#ifdef CONFIG_80211N_HT
	pmlmepriv->ht_20mhz_width_req = _FALSE;
	pmlmepriv->ht_intolerant_ch_reported = _FALSE;
	pmlmepriv->ht_op_mode = 0;
	pmlmepriv->sw_to_20mhz = 0;
#endif

	_rtw_memset(pmlmepriv->ext_capab_ie_data, 0, sizeof(pmlmepriv->ext_capab_ie_data));
	pmlmepriv->ext_capab_ie_len = 0;

#ifdef CONFIG_CONCURRENT_MODE
	psecuritypriv->dot118021x_bmc_cam_id = INVALID_SEC_MAC_CAM_ID;
#endif

	for (i = 0 ;  i < NUM_STA ; i++)
		pstapriv->sta_aid[i] = NULL;

#if CONFIG_RTW_MACADDR_ACL
	rtw_macaddr_acl_init(padapter);
#endif

	psta = rtw_get_bcmc_stainfo(padapter);
	/*_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);*/
	if (psta)
		rtw_free_stainfo(padapter, psta);
	/*_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);*/

	rtw_init_bcmc_stainfo(padapter);

	if (rtw_mi_get_ap_num(padapter))
		RTW_SET_SCAN_BAND_SKIP(padapter, BAND_5G);

}

void rtw_ap_bcmc_sta_flush(_adapter *padapter)
{
#ifdef CONFIG_CONCURRENT_MODE
	int cam_id = -1;
	u8 *addr = adapter_mac_addr(padapter);

	cam_id = rtw_iface_bcmc_id_get(padapter);
	if (cam_id != INVALID_SEC_MAC_CAM_ID) {
		RTW_PRINT("clear group key for "ADPT_FMT" addr:"MAC_FMT", camid:%d\n",
			ADPT_ARG(padapter), MAC_ARG(addr), cam_id);
		clear_cam_entry(padapter, cam_id);
		rtw_camid_free(padapter, cam_id);
		rtw_iface_bcmc_id_set(padapter, INVALID_SEC_MAC_CAM_ID);	/*init default value*/
	}
#else
	invalidate_cam_all(padapter);
#endif
}

void stop_ap_mode(_adapter *padapter)
{
	u8 self_action = MLME_ACTION_UNKNOWN;
	_irqL irqL;
	struct sta_info *psta = NULL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct dvobj_priv *pdvobj = padapter->dvobj;

	RTW_INFO("%s -"ADPT_FMT"\n", __func__, ADPT_ARG(padapter));

	if (MLME_IS_AP(padapter))
		self_action = MLME_AP_STOPPED;
	else if (MLME_IS_MESH(padapter))
		self_action = MLME_MESH_STOPPED;
	else
		rtw_warn_on(1);

	pmlmepriv->update_bcn = _FALSE;
	/*pmlmeext->bstart_bss = _FALSE;*/
	padapter->netif_up = _FALSE;
	/* _rtw_spinlock_free(&pmlmepriv->bcn_update_lock); */

	/* reset and init security priv , this can refine with rtw_reset_securitypriv */
	_rtw_memset((unsigned char *)&padapter->securitypriv, 0, sizeof(struct security_priv));
	padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
	padapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

#ifdef CONFIG_DFS_MASTER
	rtw_dfs_master_status_apply(padapter, self_action);
#endif

	/* free scan queue */
	rtw_free_network_queue(padapter, _TRUE);

#if CONFIG_RTW_MACADDR_ACL
	rtw_macaddr_acl_deinit(padapter);
#endif

	rtw_sta_flush(padapter, _TRUE);
	rtw_ap_bcmc_sta_flush(padapter);

	/* free_assoc_sta_resources	 */
	rtw_free_all_stainfo(padapter);

	psta = rtw_get_bcmc_stainfo(padapter);
	/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */
	rtw_free_stainfo(padapter, psta);
	/*_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);*/

	rtw_free_mlme_priv_ie_data(pmlmepriv);

#ifdef CONFIG_SWTIMER_BASED_TXBCN
	if (pmlmeext->bstart_bss == _TRUE) {
		_enter_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
		pdvobj->nr_ap_if--;
		if (pdvobj->nr_ap_if > 0)
			pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL / pdvobj->nr_ap_if;
		else
			pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL;

		rtw_list_delete(&padapter->list);
		_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);

		rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pdvobj->inter_bcn_space));

		if (pdvobj->nr_ap_if == 0)
			_cancel_timer_ex(&pdvobj->txbcn_timer);
	}
#endif

	pmlmeext->bstart_bss = _FALSE;

	rtw_hal_rcr_set_chk_bssid(padapter, self_action);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_MediaStatusNotify(padapter, 0); /* disconnect */
#endif

}

#endif /* CONFIG_NATIVEAP_MLME */

void rtw_ap_update_bss_chbw(_adapter *adapter, WLAN_BSSID_EX *bss, u8 ch, u8 bw, u8 offset)
{
#define UPDATE_VHT_CAP 1
#define UPDATE_HT_CAP 1
#ifdef CONFIG_80211AC_VHT
	struct vht_priv *vhtpriv = &adapter->mlmepriv.vhtpriv;
#endif
	{
		u8 *p;
		int ie_len;
		u8 old_ch = bss->Configuration.DSConfig;
		bool change_band = _FALSE;

		if ((ch <= 14 && old_ch >= 36) || (ch >= 36 && old_ch <= 14))
			change_band = _TRUE;

		/* update channel in IE */
		p = rtw_get_ie((bss->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _DSSET_IE_, &ie_len, (bss->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		if (p && ie_len > 0)
			*(p + 2) = ch;

		bss->Configuration.DSConfig = ch;

		/* band is changed, update ERP, support rate, ext support rate IE */
		if (change_band == _TRUE)
			change_band_update_ie(adapter, bss, ch);
	}

#ifdef CONFIG_80211AC_VHT
	if (vhtpriv->vht_option == _TRUE) {
		u8 *vht_cap_ie, *vht_op_ie;
		int vht_cap_ielen, vht_op_ielen;
		u8	center_freq;

		vht_cap_ie = rtw_get_ie((bss->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_VHTCapability, &vht_cap_ielen, (bss->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		vht_op_ie = rtw_get_ie((bss->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_VHTOperation, &vht_op_ielen, (bss->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		center_freq = rtw_get_center_ch(ch, bw, offset);

		/* update vht cap ie */
		if (vht_cap_ie && vht_cap_ielen) {
			#if UPDATE_VHT_CAP
			/* if ((bw == CHANNEL_WIDTH_160 || bw == CHANNEL_WIDTH_80_80) && pvhtpriv->sgi_160m)
				SET_VHT_CAPABILITY_ELE_SHORT_GI160M(pvht_cap_ie + 2, 1);
			else */
				SET_VHT_CAPABILITY_ELE_SHORT_GI160M(vht_cap_ie + 2, 0);

			if (bw >= CHANNEL_WIDTH_80 && vhtpriv->sgi_80m)
				SET_VHT_CAPABILITY_ELE_SHORT_GI80M(vht_cap_ie + 2, 1);
			else
				SET_VHT_CAPABILITY_ELE_SHORT_GI80M(vht_cap_ie + 2, 0);
			#endif
		}

		/* update vht op ie */
		if (vht_op_ie && vht_op_ielen) {
			if (bw < CHANNEL_WIDTH_80) {
				SET_VHT_OPERATION_ELE_CHL_WIDTH(vht_op_ie + 2, 0);
				SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(vht_op_ie + 2, 0);
				SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ2(vht_op_ie + 2, 0);
			} else if (bw == CHANNEL_WIDTH_80) {
				SET_VHT_OPERATION_ELE_CHL_WIDTH(vht_op_ie + 2, 1);
				SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(vht_op_ie + 2, center_freq);
				SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ2(vht_op_ie + 2, 0);
			} else {
				RTW_ERR(FUNC_ADPT_FMT" unsupported BW:%u\n", FUNC_ADPT_ARG(adapter), bw);
				rtw_warn_on(1);
			}
		}
	}
#endif /* CONFIG_80211AC_VHT */
#ifdef CONFIG_80211N_HT
	{
		struct ht_priv	*htpriv = &adapter->mlmepriv.htpriv;
		u8 *ht_cap_ie, *ht_op_ie;
		int ht_cap_ielen, ht_op_ielen;

		ht_cap_ie = rtw_get_ie((bss->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_HTCapability, &ht_cap_ielen, (bss->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		ht_op_ie = rtw_get_ie((bss->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_HTInfo, &ht_op_ielen, (bss->IELength - sizeof(NDIS_802_11_FIXED_IEs)));

		/* update ht cap ie */
		if (ht_cap_ie && ht_cap_ielen) {
			#if UPDATE_HT_CAP
			if (bw >= CHANNEL_WIDTH_40)
				SET_HT_CAP_ELE_CHL_WIDTH(ht_cap_ie + 2, 1);
			else
				SET_HT_CAP_ELE_CHL_WIDTH(ht_cap_ie + 2, 0);

			if (bw >= CHANNEL_WIDTH_40 && htpriv->sgi_40m)
				SET_HT_CAP_ELE_SHORT_GI40M(ht_cap_ie + 2, 1);
			else
				SET_HT_CAP_ELE_SHORT_GI40M(ht_cap_ie + 2, 0);

			if (htpriv->sgi_20m)
				SET_HT_CAP_ELE_SHORT_GI20M(ht_cap_ie + 2, 1);
			else
				SET_HT_CAP_ELE_SHORT_GI20M(ht_cap_ie + 2, 0);
			#endif
		}

		/* update ht op ie */
		if (ht_op_ie && ht_op_ielen) {
			SET_HT_OP_ELE_PRI_CHL(ht_op_ie + 2, ch);
			switch (offset) {
			case HAL_PRIME_CHNL_OFFSET_LOWER:
				SET_HT_OP_ELE_2ND_CHL_OFFSET(ht_op_ie + 2, SCA);
				break;
			case HAL_PRIME_CHNL_OFFSET_UPPER:
				SET_HT_OP_ELE_2ND_CHL_OFFSET(ht_op_ie + 2, SCB);
				break;
			case HAL_PRIME_CHNL_OFFSET_DONT_CARE:
			default:
				SET_HT_OP_ELE_2ND_CHL_OFFSET(ht_op_ie + 2, SCN);
				break;
			}

			if (bw >= CHANNEL_WIDTH_40)
				SET_HT_OP_ELE_STA_CHL_WIDTH(ht_op_ie + 2, 1);
			else
				SET_HT_OP_ELE_STA_CHL_WIDTH(ht_op_ie + 2, 0);
		}
	}
#endif /* CONFIG_80211N_HT */
}

bool rtw_ap_chbw_decision(_adapter *adapter, s16 req_ch, s8 req_bw, s8 req_offset
			  , u8 *ch, u8 *bw, u8 *offset, u8 *chbw_allow)
{
	u8 cur_ie_ch, cur_ie_bw, cur_ie_offset;
	u8 dec_ch, dec_bw, dec_offset;
	u8 u_ch = 0, u_offset, u_bw;
	bool changed = _FALSE;
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	WLAN_BSSID_EX *network = &(adapter->mlmepriv.cur_network.network);
	struct mi_state mstate;
	bool set_u_ch = _FALSE, set_dec_ch = _FALSE;

	rtw_ies_get_chbw(network->IEs + sizeof(NDIS_802_11_FIXED_IEs)
			 , network->IELength - sizeof(NDIS_802_11_FIXED_IEs)
			 , &cur_ie_ch, &cur_ie_bw, &cur_ie_offset);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter)) {
		if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC)) {
			/* check channel settings are the same */
			if (cur_ie_ch == mlmeext->cur_channel
				&& cur_ie_bw == mlmeext->cur_bwmode
					&& cur_ie_offset == mlmeext->cur_ch_offset) {


					RTW_INFO(FUNC_ADPT_FMT"req ch settings are the same as current ch setting, go to exit\n"
						, FUNC_ADPT_ARG(adapter));

					*chbw_allow = _FALSE;
					goto exit;
			} else {
					RTW_INFO(FUNC_ADPT_FMT"request channel settings are not the same as current channel setting(%d,%d,%d,%d,%d,%d), restart MCC\n"
						, FUNC_ADPT_ARG(adapter)
						, cur_ie_ch, cur_ie_bw, cur_ie_bw
						, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

				rtw_hal_set_mcc_setting_disconnect(adapter);
			}
		}	
	}
#endif /* CONFIG_MCC_MODE */

	/* use chbw of cur_ie updated with specifying req as temporary decision */
	dec_ch = (req_ch <= 0) ? cur_ie_ch : req_ch;
	dec_bw = (req_bw < 0) ? cur_ie_bw : req_bw;
	dec_offset = (req_offset < 0) ? cur_ie_offset : req_offset;

	rtw_mi_status_no_self(adapter, &mstate);
	RTW_INFO(FUNC_ADPT_FMT" ld_sta_num:%u, lg_sta_num%u, ap_num:%u, mesh_num:%u\n"
		, FUNC_ADPT_ARG(adapter), MSTATE_STA_LD_NUM(&mstate), MSTATE_STA_LG_NUM(&mstate)
		, MSTATE_AP_NUM(&mstate), MSTATE_MESH_NUM(&mstate));

	if (MSTATE_STA_LD_NUM(&mstate) || MSTATE_AP_NUM(&mstate) || MSTATE_MESH_NUM(&mstate)) {
		/* has linked STA or AP/Mesh mode, follow */

		rtw_warn_on(!rtw_mi_get_ch_setting_union_no_self(adapter, &u_ch, &u_bw, &u_offset));

		RTW_INFO(FUNC_ADPT_FMT" union no self: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);
		RTW_INFO(FUNC_ADPT_FMT" req: %d,%d,%d\n", FUNC_ADPT_ARG(adapter), req_ch, req_bw, req_offset);

		rtw_adjust_chbw(adapter, u_ch, &dec_bw, &dec_offset);
#ifdef CONFIG_MCC_MODE
		if (MCC_EN(adapter)) {
			if (!rtw_is_chbw_grouped(u_ch, u_bw, u_offset, dec_ch, dec_bw, dec_offset)) {
				mlmeext->cur_channel = *ch = dec_ch;
				mlmeext->cur_bwmode = *bw = dec_bw;
				mlmeext->cur_ch_offset = *offset = dec_offset;
				/* channel bw offset can not be allowed, need MCC */
				*chbw_allow = _FALSE;
				RTW_INFO(FUNC_ADPT_FMT" enable mcc: %u,%u,%u\n", FUNC_ADPT_ARG(adapter)
					 , *ch, *bw, *offset);
				goto exit;
			} else
				/* channel bw offset can be allowed, not need MCC */
				*chbw_allow = _TRUE;
		}
#endif /* CONFIG_MCC_MODE */
		rtw_sync_chbw(&dec_ch, &dec_bw, &dec_offset
			      , &u_ch, &u_bw, &u_offset);

		rtw_ap_update_bss_chbw(adapter, &(adapter->mlmepriv.cur_network.network)
				       , dec_ch, dec_bw, dec_offset);

		set_u_ch = _TRUE;
	} else if (MSTATE_STA_LG_NUM(&mstate)) {
		/* has linking STA */

		rtw_warn_on(!rtw_mi_get_ch_setting_union_no_self(adapter, &u_ch, &u_bw, &u_offset));

		RTW_INFO(FUNC_ADPT_FMT" union no self: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);
		RTW_INFO(FUNC_ADPT_FMT" req: %d,%d,%d\n", FUNC_ADPT_ARG(adapter), req_ch, req_bw, req_offset);

		rtw_adjust_chbw(adapter, dec_ch, &dec_bw, &dec_offset);

		if (rtw_is_chbw_grouped(u_ch, u_bw, u_offset, dec_ch, dec_bw, dec_offset)) {

			rtw_sync_chbw(&dec_ch, &dec_bw, &dec_offset
				      , &u_ch, &u_bw, &u_offset);

			rtw_ap_update_bss_chbw(adapter, &(adapter->mlmepriv.cur_network.network)
					       , dec_ch, dec_bw, dec_offset);

			set_u_ch = _TRUE;

			/* channel bw offset can be allowed, not need MCC */
			*chbw_allow = _TRUE;
		} else {
#ifdef CONFIG_MCC_MODE
			if (MCC_EN(adapter)) {
				mlmeext->cur_channel = *ch = dec_ch;
				mlmeext->cur_bwmode = *bw = dec_bw;
				mlmeext->cur_ch_offset = *offset = dec_offset;

				/* channel bw offset can not be allowed, need MCC */
				*chbw_allow = _FALSE;
				RTW_INFO(FUNC_ADPT_FMT" enable mcc: %u,%u,%u\n", FUNC_ADPT_ARG(adapter)
					 , *ch, *bw, *offset);
				goto exit;
			}
#endif /* CONFIG_MCC_MODE */
			/* set this for possible ch change when join down*/
			set_fwstate(&adapter->mlmepriv, WIFI_OP_CH_SWITCHING);
		}
	} else {
		/* single AP/Mesh mode */

		RTW_INFO(FUNC_ADPT_FMT" req: %d,%d,%d\n", FUNC_ADPT_ARG(adapter), req_ch, req_bw, req_offset);

		/* check temporary decision first */
		rtw_adjust_chbw(adapter, dec_ch, &dec_bw, &dec_offset);
		if (!rtw_get_offset_by_chbw(dec_ch, dec_bw, &dec_offset)) {
			if (req_ch == -1 || req_bw == -1)
				goto choose_chbw;
			RTW_WARN(FUNC_ADPT_FMT" req: %u,%u has no valid offset\n", FUNC_ADPT_ARG(adapter), dec_ch, dec_bw);
			*chbw_allow = _FALSE;
			goto exit;
		}

		if (!rtw_chset_is_chbw_valid(adapter_to_chset(adapter), dec_ch, dec_bw, dec_offset)) {
			if (req_ch == -1 || req_bw == -1)
				goto choose_chbw;
			RTW_WARN(FUNC_ADPT_FMT" req: %u,%u,%u doesn't fit in chplan\n", FUNC_ADPT_ARG(adapter), dec_ch, dec_bw, dec_offset);
			*chbw_allow = _FALSE;
			goto exit;
		}

		if (rtw_odm_dfs_domain_unknown(adapter) && rtw_is_dfs_chbw(dec_ch, dec_bw, dec_offset)) {
			if (req_ch >= 0)
				RTW_WARN(FUNC_ADPT_FMT" DFS channel %u,%u,%u can't be used\n", FUNC_ADPT_ARG(adapter), dec_ch, dec_bw, dec_offset);
			if (req_ch > 0) {
				/* specific channel and not from IE => don't change channel setting */
				*chbw_allow = _FALSE;
				goto exit;
			}
			goto choose_chbw;
		}

		if (rtw_chset_is_ch_non_ocp(adapter_to_chset(adapter), dec_ch, dec_bw, dec_offset) == _FALSE)
			goto update_bss_chbw;

choose_chbw:
		if (req_bw < 0)
			req_bw = cur_ie_bw;

#if defined(CONFIG_DFS_MASTER)
		if (!rtw_odm_dfs_domain_unknown(adapter)) {
			/* choose 5G DFS channel for debug */
			if (adapter_to_rfctl(adapter)->dbg_dfs_master_choose_dfs_ch_first
				&& rtw_choose_shortest_waiting_ch(adapter, req_bw, &dec_ch, &dec_bw, &dec_offset, RTW_CHF_2G | RTW_CHF_NON_DFS) == _TRUE)
				RTW_INFO(FUNC_ADPT_FMT" choose 5G DFS channel for debug\n", FUNC_ADPT_ARG(adapter));
			else if (adapter_to_rfctl(adapter)->dfs_ch_sel_d_flags
				&& rtw_choose_shortest_waiting_ch(adapter, req_bw, &dec_ch, &dec_bw, &dec_offset, adapter_to_rfctl(adapter)->dfs_ch_sel_d_flags) == _TRUE)
				RTW_INFO(FUNC_ADPT_FMT" choose with dfs_ch_sel_d_flags:0x%02x for debug\n", FUNC_ADPT_ARG(adapter), adapter_to_rfctl(adapter)->dfs_ch_sel_d_flags);
			else if (rtw_choose_shortest_waiting_ch(adapter, req_bw, &dec_ch, &dec_bw, &dec_offset, 0) == _FALSE) {
				RTW_WARN(FUNC_ADPT_FMT" no available channel\n", FUNC_ADPT_ARG(adapter));
				*chbw_allow = _FALSE;
				goto exit;
			}
		} else
#endif /* defined(CONFIG_DFS_MASTER) */
		if (rtw_choose_shortest_waiting_ch(adapter, req_bw, &dec_ch, &dec_bw, &dec_offset, RTW_CHF_DFS) == _FALSE) {
			RTW_WARN(FUNC_ADPT_FMT" no available channel\n", FUNC_ADPT_ARG(adapter));
			*chbw_allow = _FALSE;
			goto exit;
		}

update_bss_chbw:
		rtw_ap_update_bss_chbw(adapter, &(adapter->mlmepriv.cur_network.network)
				       , dec_ch, dec_bw, dec_offset);

		/* channel bw offset can be allowed for single AP, not need MCC */
		*chbw_allow = _TRUE;
		set_dec_ch = _TRUE;
	}

	if (rtw_mi_check_fwstate(adapter, _FW_UNDER_SURVEY)) {
		/* scanning, leave ch setting to scan state machine */
		set_u_ch = set_dec_ch = _FALSE;
	}

	if (mlmeext->cur_channel != dec_ch
	    || mlmeext->cur_bwmode != dec_bw
	    || mlmeext->cur_ch_offset != dec_offset)
		changed = _TRUE;

	if (changed == _TRUE && rtw_linked_check(adapter) == _TRUE) {
#ifdef CONFIG_SPCT_CH_SWITCH
		if (1)
			rtw_ap_inform_ch_switch(adapter, dec_ch, dec_offset);
		else
#endif
			rtw_sta_flush(adapter, _FALSE);
	}

	mlmeext->cur_channel = dec_ch;
	mlmeext->cur_bwmode = dec_bw;
	mlmeext->cur_ch_offset = dec_offset;

	if (u_ch != 0)
		RTW_INFO(FUNC_ADPT_FMT" union: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);

	RTW_INFO(FUNC_ADPT_FMT" dec: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), dec_ch, dec_bw, dec_offset);

	if (set_u_ch == _TRUE) {
		*ch = u_ch;
		*bw = u_bw;
		*offset = u_offset;
	} else if (set_dec_ch == _TRUE) {
		*ch = dec_ch;
		*bw = dec_bw;
		*offset = dec_offset;
	}
exit:
	return changed;
}

u8 rtw_ap_sta_linking_state_check(_adapter *adapter)
{
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	int i;
	_list *plist, *phead;
	_irqL irqL;
	u8 rst = _FALSE;

	if (!MLME_IS_AP(adapter) && !MLME_IS_MESH(adapter))
		return _FALSE;

	if (pstapriv->auth_list_cnt !=0)
		return _TRUE;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);
		if (!(psta->state &_FW_LINKED)) {
			rst = _TRUE;
			break;
		}
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	return rst;
}

/*#define DBG_SWTIMER_BASED_TXBCN*/
#ifdef CONFIG_SWTIMER_BASED_TXBCN
void tx_beacon_handlder(struct dvobj_priv *pdvobj)
{
#define BEACON_EARLY_TIME		20	/* unit:TU*/
	_irqL irqL;
	_list	*plist, *phead;
	u32 timestamp[2];
	u32 bcn_interval_us; /* unit : usec */
	u64 time;
	u32 cur_tick, time_offset; /* unit : usec */
	u32 inter_bcn_space_us; /* unit : usec */
	u32 txbcn_timer_ms; /* unit : ms */
	int nr_vap, idx, bcn_idx;
	int i;
	u8 val8, late = 0;
	_adapter *padapter = NULL;

	i = 0;

	/* get first ap mode interface */
	_enter_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
	if (rtw_is_list_empty(&pdvobj->ap_if_q.queue) || (pdvobj->nr_ap_if == 0)) {
		RTW_INFO("[%s] ERROR: ap_if_q is empty!or nr_ap = %d\n", __func__, pdvobj->nr_ap_if);
		_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
		return;
	} else
		padapter = LIST_CONTAINOR(get_next(&(pdvobj->ap_if_q.queue)), struct _ADAPTER, list);
	_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);

	if (NULL == padapter) {
		RTW_INFO("[%s] ERROR: no any ap interface!\n", __func__);
		return;
	}


	bcn_interval_us = DEFAULT_BCN_INTERVAL * NET80211_TU_TO_US;
	if (0 == bcn_interval_us) {
		RTW_INFO("[%s] ERROR: beacon interval = 0\n", __func__);
		return;
	}

	/* read TSF */
	timestamp[1] = rtw_read32(padapter, 0x560 + 4);
	timestamp[0] = rtw_read32(padapter, 0x560);
	while (timestamp[1]) {
		time = (0xFFFFFFFF % bcn_interval_us + 1) * timestamp[1] + timestamp[0];
		timestamp[0] = (u32)time;
		timestamp[1] = (u32)(time >> 32);
	}
	cur_tick = timestamp[0] % bcn_interval_us;


	_enter_critical_bh(&pdvobj->ap_if_q.lock, &irqL);

	nr_vap = (pdvobj->nr_ap_if - 1);
	if (nr_vap > 0) {
		inter_bcn_space_us = pdvobj->inter_bcn_space * NET80211_TU_TO_US; /* beacon_interval / (nr_vap+1); */
		idx = cur_tick / inter_bcn_space_us;
		if (idx < nr_vap)	/* if (idx < (nr_vap+1))*/
			bcn_idx = idx + 1;	/* bcn_idx = (idx + 1) % (nr_vap+1);*/
		else
			bcn_idx = 0;

		/* to get padapter based on bcn_idx */
		padapter = NULL;
		phead = get_list_head(&pdvobj->ap_if_q);
		plist = get_next(phead);
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			padapter = LIST_CONTAINOR(plist, struct _ADAPTER, list);

			plist = get_next(plist);

			if (i == bcn_idx)
				break;

			i++;
		}
		if ((NULL == padapter) || (i > pdvobj->nr_ap_if)) {
			RTW_INFO("[%s] ERROR: nr_ap_if = %d, padapter=%p, bcn_idx=%d, index=%d\n",
				__func__, pdvobj->nr_ap_if, padapter, bcn_idx, i);
			_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
			return;
		}
#ifdef DBG_SWTIMER_BASED_TXBCN
		RTW_INFO("BCN_IDX=%d, cur_tick=%d, padapter=%p\n", bcn_idx, cur_tick, padapter);
#endif
		if (((idx + 2 == nr_vap + 1) && (idx < nr_vap + 1)) || (0 == bcn_idx)) {
			time_offset = bcn_interval_us - cur_tick - BEACON_EARLY_TIME * NET80211_TU_TO_US;
			if ((s32)time_offset < 0)
				time_offset += inter_bcn_space_us;

		} else {
			time_offset = (idx + 2) * inter_bcn_space_us - cur_tick - BEACON_EARLY_TIME * NET80211_TU_TO_US;
			if (time_offset > (inter_bcn_space_us + (inter_bcn_space_us >> 1))) {
				time_offset -= inter_bcn_space_us;
				late = 1;
			}
		}
	} else
		/*#endif*/ { /* MBSSID */
		time_offset = 2 * bcn_interval_us - cur_tick - BEACON_EARLY_TIME * NET80211_TU_TO_US;
		if (time_offset > (bcn_interval_us + (bcn_interval_us >> 1))) {
			time_offset -= bcn_interval_us;
			late = 1;
		}
	}
	_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);

#ifdef DBG_SWTIMER_BASED_TXBCN
	RTW_INFO("set sw bcn timer %d us\n", time_offset);
#endif
	txbcn_timer_ms = time_offset / NET80211_TU_TO_US;
	_set_timer(&pdvobj->txbcn_timer, txbcn_timer_ms);

	if (padapter) {
#ifdef CONFIG_BCN_RECOVERY
		rtw_ap_bcn_recovery(padapter);
#endif /*CONFIG_BCN_RECOVERY*/

#ifdef CONFIG_BCN_XMIT_PROTECT
		rtw_ap_bcn_queue_empty_check(padapter, txbcn_timer_ms);
#endif /*CONFIG_BCN_XMIT_PROTECT*/

#ifdef DBG_SWTIMER_BASED_TXBCN
		RTW_INFO("padapter=%p, PORT=%d\n", padapter, padapter->hw_port);
#endif
		/* bypass TX BCN queue if op ch is switching/waiting */
		if (!check_fwstate(&padapter->mlmepriv, WIFI_OP_CH_SWITCHING)
			#ifdef CONFIG_DFS_MASTER
			&& !IS_CH_WAITING(adapter_to_rfctl(padapter))
			#endif
		) {
			/*update_beacon(padapter, _TIM_IE_, NULL, _FALSE);*/
			/*issue_beacon(padapter, 0);*/
			send_beacon(padapter);
		}
	}

#if 0
	/* handle any buffered BC/MC frames*/
	/* Don't dynamically change DIS_ATIM due to HW will auto send ACQ after HIQ empty.*/
	val8 = *((unsigned char *)priv->beaconbuf + priv->timoffset + 4);
	if (val8 & 0x01) {
		process_mcast_dzqueue(priv);
		priv->pkt_in_dtimQ = 0;
	}
#endif

}

void tx_beacon_timer_handlder(void *ctx)
{
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)ctx;
	_adapter *padapter = pdvobj->padapters[0];

	if (padapter)
		set_tx_beacon_cmd(padapter);
}
#endif

void rtw_ap_acdata_control(_adapter *padapter, u8 power_mode)
{
	_irqL irqL;
	_list	*phead, *plist;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 sta_alive_num = 0, i;
	char sta_alive_list[NUM_STA];

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter) && rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
		/* driver doesn't access macid sleep reg under MCC */
		return;
#endif

	/*RTW_INFO(FUNC_ADPT_FMT " associated sta num:%d, make macid_%s!!\n",
				FUNC_ADPT_ARG(padapter), pstapriv->asoc_list_cnt, power_mode ? "sleep" : "wakeup");*/

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		int stainfo_offset;

		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset))
			sta_alive_list[sta_alive_num++] = stainfo_offset;
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	for (i = 0; i < sta_alive_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, sta_alive_list[i]);

		if (psta) {
			if (power_mode)
				rtw_hal_macid_sleep(padapter, psta->cmn.mac_id);
			else
				rtw_hal_macid_wakeup(padapter, psta->cmn.mac_id);
		}
	}
}

void rtw_ap_parse_sta_capability(_adapter *adapter, struct sta_info *sta, u8 *cap)
{
	sta->capability = RTW_GET_LE16(cap);
	if (sta->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		sta->flags |= WLAN_STA_SHORT_PREAMBLE;
	else
		sta->flags &= ~WLAN_STA_SHORT_PREAMBLE;
}

u16 rtw_ap_parse_sta_supported_rates(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len)
{
	u8 rate_set[16];
	u8 rate_num;
	int i;
	u16 status = _STATS_SUCCESSFUL_;

	rtw_ies_get_supported_rate(tlv_ies, tlv_ies_len, rate_set, &rate_num);
	if (rate_num == 0) {
		RTW_INFO(FUNC_ADPT_FMT" sta "MAC_FMT" with no supported rate\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
		status = _STATS_FAILURE_;
		goto exit;
	}

	_rtw_memcpy(sta->bssrateset, rate_set, rate_num);
	sta->bssratelen = rate_num;

	if (MLME_IS_AP(adapter)) {
		/* this function force only CCK rates to be bassic rate... */
		UpdateBrateTblForSoftAP(sta->bssrateset, sta->bssratelen);
	}

	/* if (hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G) */ /* ? */
	sta->flags |= WLAN_STA_NONERP;
	for (i = 0; i < sta->bssratelen; i++) {
		if ((sta->bssrateset[i] & 0x7f) > 22) {
			sta->flags &= ~WLAN_STA_NONERP;
			break;
		}
	}

exit:
	return status;
}

u16 rtw_ap_parse_sta_security_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems)
{
	struct security_priv *sec = &adapter->securitypriv;
	u8 *wpa_ie;
	int wpa_ie_len;
	int group_cipher = 0, pairwise_cipher = 0;
	u16 status = _STATS_SUCCESSFUL_;

	sta->dot8021xalg = 0;
	sta->wpa_psk = 0;
	sta->wpa_group_cipher = 0;
	sta->wpa2_group_cipher = 0;
	sta->wpa_pairwise_cipher = 0;
	sta->wpa2_pairwise_cipher = 0;
	_rtw_memset(sta->wpa_ie, 0, sizeof(sta->wpa_ie));

	if ((sec->wpa_psk & BIT(1)) && elems->rsn_ie) {
		wpa_ie = elems->rsn_ie;
		wpa_ie_len = elems->rsn_ie_len;

		if (rtw_parse_wpa2_ie(wpa_ie - 2, wpa_ie_len + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			sta->dot8021xalg = 1;/* psk, todo:802.1x */
			sta->wpa_psk |= BIT(1);

			sta->wpa2_group_cipher = group_cipher & sec->wpa2_group_cipher;
			sta->wpa2_pairwise_cipher = pairwise_cipher & sec->wpa2_pairwise_cipher;

			if (!sta->wpa2_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if (!sta->wpa2_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		} else
			status = WLAN_STATUS_INVALID_IE;

	}
	else if ((sec->wpa_psk & BIT(0)) && elems->wpa_ie) {
		wpa_ie = elems->wpa_ie;
		wpa_ie_len = elems->wpa_ie_len;

		if (rtw_parse_wpa_ie(wpa_ie - 2, wpa_ie_len + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			sta->dot8021xalg = 1;/* psk, todo:802.1x */
			sta->wpa_psk |= BIT(0);

			sta->wpa_group_cipher = group_cipher & sec->wpa_group_cipher;
			sta->wpa_pairwise_cipher = pairwise_cipher & sec->wpa_pairwise_cipher;

			if (!sta->wpa_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if (!sta->wpa_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		} else
			status = WLAN_STATUS_INVALID_IE;

	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

	if (status != _STATS_SUCCESSFUL_)
		goto exit;

	if (!MLME_IS_AP(adapter))
		goto exit;

	sta->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);
	/* if (hapd->conf->wps_state && wpa_ie == NULL) { */ /* todo: to check ap if supporting WPS */
	if (wpa_ie == NULL) {
		if (elems->wps_ie) {
			RTW_INFO("STA included WPS IE in "
				 "(Re)Association Request - assume WPS is "
				 "used\n");
			sta->flags |= WLAN_STA_WPS;
			/* wpabuf_free(sta->wps_ie); */
			/* sta->wps_ie = wpabuf_alloc_copy(elems.wps_ie + 4, */
			/*				elems.wps_ie_len - 4); */
		} else {
			RTW_INFO("STA did not include WPA/RSN IE "
				 "in (Re)Association Request - possible WPS "
				 "use\n");
			sta->flags |= WLAN_STA_MAYBE_WPS;
		}

		/* AP support WPA/RSN, and sta is going to do WPS, but AP is not ready */
		/* that the selected registrar of AP is _FLASE */
		if ((sec->wpa_psk > 0)
			&& (sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS))
		) {
			struct mlme_priv *mlme = &adapter->mlmepriv;

			if (mlme->wps_beacon_ie) {
				u8 selected_registrar = 0;

				rtw_get_wps_attr_content(mlme->wps_beacon_ie, mlme->wps_beacon_ie_len, WPS_ATTR_SELECTED_REGISTRAR, &selected_registrar, NULL);

				if (!selected_registrar) {
					RTW_INFO("selected_registrar is _FALSE , or AP is not ready to do WPS\n");
					status = _STATS_UNABLE_HANDLE_STA_;
					goto exit;
				}
			}
		}

	} else {
		int copy_len;

		if (sec->wpa_psk == 0) {
			RTW_INFO("STA " MAC_FMT
				": WPA/RSN IE in association request, but AP don't support WPA/RSN\n",
				MAC_ARG(sta->cmn.mac_addr));
			status = WLAN_STATUS_INVALID_IE;
			goto exit;
		}

		if (elems->wps_ie) {
			RTW_INFO("STA included WPS IE in "
				 "(Re)Association Request - WPS is "
				 "used\n");
			sta->flags |= WLAN_STA_WPS;
			copy_len = 0;
		} else
			copy_len = ((wpa_ie_len + 2) > sizeof(sta->wpa_ie)) ? (sizeof(sta->wpa_ie)) : (wpa_ie_len + 2);

		if (copy_len > 0)
			_rtw_memcpy(sta->wpa_ie, wpa_ie - 2, copy_len);
	}

exit:
	return status;
}

void rtw_ap_parse_sta_wmm_ie(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
	u8 *p;

	sta->flags &= ~WLAN_STA_WME;
	sta->qos_option = 0;
	sta->qos_info = 0;
	sta->has_legacy_ac = _TRUE;
	sta->uapsd_vo = 0;
	sta->uapsd_vi = 0;
	sta->uapsd_be = 0;
	sta->uapsd_bk = 0;

	if (!mlme->qospriv.qos_option)
		goto exit;

	p = rtw_get_ie_ex(tlv_ies, tlv_ies_len, WLAN_EID_VENDOR_SPECIFIC, WMM_IE, 6, NULL, NULL);
	if (!p)
		goto exit;

	sta->flags |= WLAN_STA_WME;
	sta->qos_option = 1;
	sta->qos_info = *(p + 8);
	sta->max_sp_len = (sta->qos_info >> 5) & 0x3;

	if ((sta->qos_info & 0xf) != 0xf)
		sta->has_legacy_ac = _TRUE;
	else
		sta->has_legacy_ac = _FALSE;

	if (sta->qos_info & 0xf) {
		if (sta->qos_info & BIT(0))
			sta->uapsd_vo = BIT(0) | BIT(1);
		else
			sta->uapsd_vo = 0;

		if (sta->qos_info & BIT(1))
			sta->uapsd_vi = BIT(0) | BIT(1);
		else
			sta->uapsd_vi = 0;

		if (sta->qos_info & BIT(2))
			sta->uapsd_bk = BIT(0) | BIT(1);
		else
			sta->uapsd_bk = 0;

		if (sta->qos_info & BIT(3))
			sta->uapsd_be = BIT(0) | BIT(1);
		else
			sta->uapsd_be = 0;
	}

exit:
	return;
}

void rtw_ap_parse_sta_ht_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;

	sta->flags &= ~WLAN_STA_HT;

#ifdef CONFIG_80211N_HT
	if (mlme->htpriv.ht_option == _FALSE)
		goto exit;

	/* save HT capabilities in the sta object */
	_rtw_memset(&sta->htpriv.ht_cap, 0, sizeof(struct rtw_ieee80211_ht_cap));
	if (elems->ht_capabilities && elems->ht_capabilities_len >= sizeof(struct rtw_ieee80211_ht_cap)) {
		sta->flags |= WLAN_STA_HT;
		sta->flags |= WLAN_STA_WME;
		_rtw_memcpy(&sta->htpriv.ht_cap, elems->ht_capabilities, sizeof(struct rtw_ieee80211_ht_cap));
	}
exit:
#endif

	return;
}

void rtw_ap_parse_sta_vht_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;

	sta->flags &= ~WLAN_STA_VHT;

#ifdef CONFIG_80211AC_VHT
	if (mlme->vhtpriv.vht_option == _FALSE)
		goto exit;

	_rtw_memset(&sta->vhtpriv, 0, sizeof(struct vht_priv));
	if (elems->vht_capabilities && elems->vht_capabilities_len == 12) {
		sta->flags |= WLAN_STA_VHT;
		_rtw_memcpy(sta->vhtpriv.vht_cap, elems->vht_capabilities, 12);

		if (elems->vht_op_mode_notify && elems->vht_op_mode_notify_len == 1)
			_rtw_memcpy(&sta->vhtpriv.vht_op_mode_notify, elems->vht_op_mode_notify, 1);
		else /* for Frame without Operating Mode notify ie; default: 80M */
			sta->vhtpriv.vht_op_mode_notify = CHANNEL_WIDTH_80;
	}
exit:
#endif

	return;
}
#endif /* CONFIG_AP_MODE */

