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

/*
* Set TIM IE
* return length of total TIM IE
*/
u8 rtw_set_tim_ie(u8 dtim_cnt, u8 dtim_period
	, const u8 *tim_bmp, u8 tim_bmp_len, u8 *tim_ie)
{
	u8 *p = tim_ie;
	u8 i, n1, n2;
	u8 bmp_len;

	if (rtw_bmp_not_empty(tim_bmp, tim_bmp_len)) {
		/* find the first nonzero octet in tim_bitmap */
		for (i = 0; i < tim_bmp_len; i++)
			if (tim_bmp[i])
				break;
		n1 = i & 0xFE;
	
		/* find the last nonzero octet in tim_bitmap, except octet 0 */
		for (i = tim_bmp_len - 1; i > 0; i--)
			if (tim_bmp[i])
				break;
		n2 = i;
		bmp_len = n2 - n1 + 1;
	} else {
		n1 = n2 = 0;
		bmp_len = 1;
	}

	*p++ = WLAN_EID_TIM;
	*p++ = 2 + 1 + bmp_len;
	*p++ = dtim_cnt;
	*p++ = dtim_period;
	*p++ = (rtw_bmp_is_set(tim_bmp, tim_bmp_len, 0) ? BIT0 : 0) | n1;
	_rtw_memcpy(p, tim_bmp + n1, bmp_len);

#if 0
	RTW_INFO("n1:%u, n2:%u, bmp_offset:%u, bmp_len:%u\n", n1, n2, n1 / 2, bmp_len);
	RTW_INFO_DUMP("tim_ie: ", tim_ie + 2, 2 + 1 + bmp_len);
#endif
	return 2 + 2 + 1 + bmp_len;
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
	/* if(rtw_tim_map_anyone_be_set(padapter, pstapriv->tim_bitmap)) */
#endif
	if (_TRUE) {
		u8 *p, *dst_ie, *premainder_ie = NULL, *pbackup_remainder_ie = NULL;
		uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;

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

		/* append TIM IE */
		dst_ie += rtw_set_tim_ie(0, 1, pstapriv->tim_bitmap, pstapriv->aid_bmp_len, dst_ie);

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
	u32	i, offset, ielen = 0, ie_offset, remainder_ielen = 0;

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

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(psta->padapter)) {
		u8 bcn_alive, hwmp_alive;

		hwmp_alive = (psta->sta_stats.rx_hwmp_pkts !=
			      psta->sta_stats.last_rx_hwmp_pkts);
		bcn_alive = (psta->sta_stats.rx_beacon_pkts != 
			     psta->sta_stats.last_rx_beacon_pkts);
		/* The reference for nexthop_lookup */
		psta->alive = ret || hwmp_alive || bcn_alive;
		/* The reference for expire_timeout_chk */
		/* Exclude bcn_alive to avoid a misjudge condition
		   that a peer unexpectedly leave and restart quickly*/
		ret = ret || hwmp_alive;
	}
#endif

	sta_update_last_rx_pkts(psta);

	return ret;
}

/**
 * issue_aka_chk_frame - issue active keep alive check frame
 *	aka = active keep alive
 */
#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
static int issue_aka_chk_frame(_adapter *adapter, struct sta_info *psta)
{
	int ret = _FAIL;
	u8 *target_addr = psta->cmn.mac_addr;

	if (MLME_IS_AP(adapter)) {
		/* issue null data to check sta alive */
		if (psta->state & WIFI_SLEEP_STATE)
			ret = issue_nulldata(adapter, target_addr, 0, 1, 50);
		else
			ret = issue_nulldata(adapter, target_addr, 0, 3, 50);
	}

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(adapter)) {
		struct rtw_mesh_path *mpath;

		rtw_rcu_read_lock();
		mpath = rtw_mesh_path_lookup(adapter, target_addr);
		if (!mpath) {
			mpath = rtw_mesh_path_add(adapter, target_addr);
			if (IS_ERR(mpath)) {
				rtw_rcu_read_unlock();
				RTW_ERR(FUNC_ADPT_FMT" rtw_mesh_path_add for "MAC_FMT" fail.\n",
					FUNC_ADPT_ARG(adapter), MAC_ARG(target_addr));
				return _FAIL;
			}
		}
		if (mpath->flags & RTW_MESH_PATH_ACTIVE)
			ret = _SUCCESS;
		else {
			u8 flags = RTW_PREQ_Q_F_START | RTW_PREQ_Q_F_PEER_AKA;
			/* issue PREQ to check peer alive */
			rtw_mesh_queue_preq(mpath, flags);
			ret = _FALSE;
		}
		rtw_rcu_read_unlock();
	}
#endif
	return ret;
}
#endif

#ifdef RTW_CONFIG_RFREG18_WA
static void rtw_check_restore_rf18(_adapter *padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	u32 reg;
	u8 union_ch = 0, union_bw = 0, union_offset = 0, setchbw = _FALSE;
		
	reg = rtw_hal_read_rfreg(padapter, 0, 0x18, 0x3FF);
	if ((reg & 0xFF) == 0)
			setchbw = _TRUE;
	reg = rtw_hal_read_rfreg(padapter, 1, 0x18, 0x3FF);
	if ((reg & 0xFF) == 0)
			setchbw = _TRUE;

	if (setchbw) {
		if (!rtw_mi_get_ch_setting_union(padapter, &union_ch, &union_bw, &union_offset)) {
			RTW_INFO("Hit RF(0x18)=0!! restore original channel setting.\n");
			union_ch =  pmlmeext->cur_channel;
			union_offset = pmlmeext->cur_ch_offset ;
			union_bw = pmlmeext->cur_bwmode;
		} else {
			RTW_INFO("Hit RF(0x18)=0!! set ch(%x) offset(%x) bwmode(%x)\n", union_ch, union_offset, union_bw);
		}
		/*	Initial the channel_bw setting procedure.	*/
		pHalData->current_channel = 0;
		set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
	}
}
#endif

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

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)
		&& check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE)
	) {
		struct rtw_mesh_cfg *mcfg = &padapter->mesh_cfg;

		rtw_mesh_path_expire(padapter);

		/* TBD: up layer timeout mechanism */
		/* if (!mcfg->plink_timeout)
			return; */
#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
		return;
#endif
	}
#endif

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
		RTW_INFO(FUNC_ADPT_FMT" auth_list, cnt:%u\n"
			, FUNC_ADPT_ARG(padapter), pstapriv->auth_list_cnt);
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

				RTW_INFO(FUNC_ADPT_FMT" auth expire "MAC_FMT"\n"
					, FUNC_ADPT_ARG(padapter), MAC_ARG(psta->cmn.mac_addr));

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
		RTW_INFO(FUNC_ADPT_FMT" asoc_list, cnt:%u\n"
			, FUNC_ADPT_ARG(padapter), pstapriv->asoc_list_cnt);
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
					rtw_tim_map_set(padapter, pstapriv->tim_bitmap, psta->cmn.aid);
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE, 0);

					if (!pmlmeext->active_keep_alive_check)
						continue;
				}
			}

			{
				int stainfo_offset;

				stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
				if (stainfo_offset_valid(stainfo_offset))
					chk_alive_list[chk_alive_num++] = stainfo_offset;
				continue;
			}
		} else {
			/* TODO: Aging mechanism to digest frames in sleep_q to avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME / pstapriv->asoc_list_cnt)
			    && padapter->xmitpriv.free_xmitframe_cnt < ((NR_XMITFRAME / pstapriv->asoc_list_cnt) / 2)
			   ) {
				RTW_INFO(FUNC_ADPT_FMT" sta:"MAC_FMT", sleepq_len:%u, free_xmitframe_cnt:%u, asoc_list_cnt:%u, clear sleep_q\n"
					, FUNC_ADPT_ARG(padapter), MAC_ARG(psta->cmn.mac_addr)
					, psta->sleepq_len, padapter->xmitpriv.free_xmitframe_cnt, pstapriv->asoc_list_cnt);
				wakeup_sta_to_xmit(padapter, psta);
			}
		}
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	if (chk_alive_num) {
#if defined(CONFIG_ACTIVE_KEEP_ALIVE_CHECK)
		u8 backup_ch = 0, backup_bw = 0, backup_offset = 0;
		u8 union_ch = 0, union_bw = 0, union_offset = 0;
		u8 switch_channel_by_drv = _TRUE;
		struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
#endif
		char del_asoc_list[NUM_STA];

		_rtw_memset(del_asoc_list, NUM_STA, NUM_STA);

		#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
		if (pmlmeext->active_keep_alive_check) {
			#ifdef CONFIG_MCC_MODE
			if (MCC_EN(padapter)) {
				/* driver doesn't switch channel under MCC */
				if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
					switch_channel_by_drv = _FALSE;
			}
			#endif

			if (!rtw_mi_get_ch_setting_union(padapter, &union_ch, &union_bw, &union_offset)
				|| pmlmeext->cur_channel != union_ch)
				switch_channel_by_drv = _FALSE;

			/* switch to correct channel of current network  before issue keep-alive frames */
			if (switch_channel_by_drv == _TRUE && rtw_get_oper_ch(padapter) != pmlmeext->cur_channel) {
				backup_ch = rtw_get_oper_ch(padapter);
				backup_bw = rtw_get_oper_bw(padapter);
				backup_offset = rtw_get_oper_choffset(padapter);
				set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
			}
		}
		#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */

		/* check loop */
		for (i = 0; i < chk_alive_num; i++) {
			#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
			int ret = _FAIL;
			#endif

			psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);

			#ifdef CONFIG_ATMEL_RC_PATCH
			if (_rtw_memcmp(pstapriv->atmel_rc_pattern, psta->cmn.mac_addr, ETH_ALEN) == _TRUE)
				continue;
			if (psta->flag_atmel_rc)
				continue;
			#endif

			if (!(psta->state & _FW_LINKED))
				continue;

			#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
			if (pmlmeext->active_keep_alive_check) {
				/* issue active keep alive frame to check */
				ret = issue_aka_chk_frame(padapter, psta);

				psta->keep_alive_trycnt++;
				if (ret == _SUCCESS) {
					RTW_INFO(FUNC_ADPT_FMT" asoc check, "MAC_FMT" is alive\n"
						, FUNC_ADPT_ARG(padapter), MAC_ARG(psta->cmn.mac_addr));
					psta->expire_to = pstapriv->expire_to;
					psta->keep_alive_trycnt = 0;
					continue;
				} else if (psta->keep_alive_trycnt <= 3) {
					RTW_INFO(FUNC_ADPT_FMT" asoc check, "MAC_FMT" keep_alive_trycnt=%d\n"
						, FUNC_ADPT_ARG(padapter) , MAC_ARG(psta->cmn.mac_addr), psta->keep_alive_trycnt);
					psta->expire_to = 1;
					continue;
				}
			}
			#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */

			psta->keep_alive_trycnt = 0;
			del_asoc_list[i] = chk_alive_list[i];
			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&psta->asoc_list) == _FALSE) {
				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				STA_SET_MESH_PLINK(psta, NULL);
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		}

		/* delete loop */
		for (i = 0; i < chk_alive_num; i++) {
			u8 sta_addr[ETH_ALEN];

			if (del_asoc_list[i] >= NUM_STA)
				continue;

			psta = rtw_get_stainfo_by_offset(pstapriv, del_asoc_list[i]);
			_rtw_memcpy(sta_addr, psta->cmn.mac_addr, ETH_ALEN);

			RTW_INFO(FUNC_ADPT_FMT" asoc expire "MAC_FMT", state=0x%x\n"
				, FUNC_ADPT_ARG(padapter), MAC_ARG(psta->cmn.mac_addr), psta->state);
			updated |= ap_free_sta(padapter, psta, _FALSE, WLAN_REASON_DEAUTH_LEAVING, _FALSE);
			#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(padapter))
				rtw_mesh_expire_peer(padapter, sta_addr);
			#endif
		}

		#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
		if (pmlmeext->active_keep_alive_check) {
			/* back to the original operation channel */
			if (switch_channel_by_drv == _TRUE && backup_ch > 0)
				set_channel_bwmode(padapter, backup_ch, backup_offset, backup_bw);
		}
		#endif
	}

#ifdef RTW_CONFIG_RFREG18_WA
	rtw_check_restore_rf18(padapter);
#endif
	associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);
}

void rtw_ap_update_sta_ra_info(_adapter *padapter, struct sta_info *psta)
{
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
#ifdef CONFIG_BMC_TX_LOW_RATE
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
#endif
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
#ifdef CONFIG_RTW_MESH
		if (MLME_IS_MESH(padapter))
			psta->qos_option = 1;
		else
#endif
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

	if (!MLME_IS_MESH(padapter) && psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
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

		if (phtpriv_sta->op_present
			&& !GET_HT_OP_ELE_STA_CHL_WIDTH(phtpriv_sta->ht_op))
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

	/* Check encryption */
	if (!MLME_IS_MESH(padapter) && psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->state |= WIFI_UNDER_KEY_HANDSHAKE;

	psta->state |= _FW_LINKED;

	_exit_critical_bh(&psta->lock, &irqL);
}

static void update_ap_info(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
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
	u8	AIFS, ECWMin, ECWMax, aSifsTime;
	u8	acm_mask;
	u16	TXOP;
	u32	acParm, i;
	u32	edca[4], inx[4];
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	acm_mask = 0;
#ifdef CONFIG_80211N_HT
	if (pregpriv->ht_enable &&
		(is_supported_5g(pmlmeext->cur_wireless_mode) ||
	    (pmlmeext->cur_wireless_mode & WIRELESS_11_24N)))
		aSifsTime = 16;
	else
#endif /* CONFIG_80211N_HT */
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
#ifdef CONFIG_80211N_HT
static void update_hw_ht_param(_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
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
#endif /* CONFIG_80211N_HT */
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
#ifdef CONFIG_80211N_HT
	pmlmepriv->num_sta_no_ht = 0; /* reset to 0 after ap do scanning*/
#endif
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

#ifdef CONFIG_FW_HANDLE_TXBCN
bool rtw_ap_nums_check(_adapter *adapter)
{
	if (rtw_ap_get_nums(adapter) < CONFIG_LIMITED_AP_NUM)
		return _TRUE;
	return _FALSE;
}
u8 rtw_ap_allocate_vapid(struct dvobj_priv *dvobj)
{
	u8 vap_id;

	for (vap_id = 0; vap_id < CONFIG_LIMITED_AP_NUM; vap_id++) {
		if (!(dvobj->vap_map & BIT(vap_id)))
			break;
	}

	if (vap_id < CONFIG_LIMITED_AP_NUM)
		dvobj->vap_map |= BIT(vap_id);

	return vap_id;
}
u8 rtw_ap_release_vapid(struct dvobj_priv *dvobj, u8 vap_id)
{
	if (vap_id >= CONFIG_LIMITED_AP_NUM) {
		RTW_ERR("%s - vapid(%d) failed\n", __func__, vap_id);
		rtw_warn_on(1);
		return _FAIL;
	}
	dvobj->vap_map &= ~ BIT(vap_id);
	return _SUCCESS;
}
#endif
static void _rtw_iface_undersurvey_chk(const char *func, _adapter *adapter)
{
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mlme_priv *pmlmepriv;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			pmlmepriv = &iface->mlmepriv;
			if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
				RTW_ERR("%s ("ADPT_FMT") under survey\n", func, ADPT_ARG(iface));
		}
	}
}
void start_bss_network(_adapter *padapter, struct createbss_parm *parm)
{
#define DUMP_ADAPTERS_STATUS 0
	u8 mlme_act = MLME_ACTION_UNKNOWN;
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
	s16 req_ch = REQ_CH_NONE, req_bw = REQ_BW_NONE, req_offset = REQ_OFFSET_NONE;
	u8 ch_to_set = 0, bw_to_set, offset_to_set;
	u8 doiqk = _FALSE;
	/* use for check ch bw offset can be allowed or not */
	u8 chbw_allow = _TRUE;
	int i;
	u8 ifbmp_ch_changed = 0;

	if (parm->req_ch != 0) {
		/* bypass other setting, go checking ch, bw, offset */
		mlme_act = MLME_OPCH_SWITCH;
		req_ch = parm->req_ch;
		req_bw = parm->req_bw;
		req_offset = parm->req_offset;
		goto chbw_decision;
	} else {
		/* request comes from upper layer */
		if (MLME_IS_AP(padapter))
			mlme_act = MLME_AP_STARTED;
		else if (MLME_IS_MESH(padapter))
			mlme_act = MLME_MESH_STARTED;
		else
			rtw_warn_on(1);
		req_ch = 0;
		_rtw_memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);
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
		if ((psecuritypriv->dot11PrivacyAlgrthm != _WEP40_) && (psecuritypriv->dot11PrivacyAlgrthm != _WEP104_)
			&& !MLME_IS_MESH(padapter) /* mesh group key is set before this function */
		)
			flush_all_cam_entry(padapter);	/* clear CAM */
	}

	/* set MSR to AP_Mode		 */
	Set_MSR(padapter, _HW_STATE_AP_);

	/* Set BSSID REG */
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pnetwork->MacAddress);

	/* Set Security */
	val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) ? 0xcc : 0xcf;
	rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

	/* Beacon Control related register */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&bcn_interval));

	rtw_hal_rcr_set_chk_bssid(padapter, mlme_act);

chbw_decision:
	ifbmp_ch_changed = rtw_ap_chbw_decision(padapter, parm->ifbmp, parm->excl_ifbmp
						, req_ch, req_bw, req_offset
						, &ch_to_set, &bw_to_set, &offset_to_set, &chbw_allow);

	for (i = 0; i < pdvobj->iface_nums; i++) {
		if (!(parm->ifbmp & BIT(i)) || !pdvobj->padapters[i])
			continue;

		/* let pnetwork_mlme == pnetwork_mlmeext */
		_rtw_memcpy(&(pdvobj->padapters[i]->mlmepriv.cur_network.network)
			, &(pdvobj->padapters[i]->mlmeextpriv.mlmext_info.network)
			, pdvobj->padapters[i]->mlmeextpriv.mlmext_info.network.Length);

		rtw_start_bss_hdl_after_chbw_decided(pdvobj->padapters[i]);

		/* Set EDCA param reg after update cur_wireless_mode & update_capinfo */
		if (pregpriv->wifi_spec == 1)
			rtw_set_hw_wmm_param(pdvobj->padapters[i]);
	}

#if defined(CONFIG_DFS_MASTER)
	rtw_dfs_rd_en_decision(padapter, mlme_act, parm->excl_ifbmp);
#endif

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

	if (!IS_CH_WAITING(adapter_to_rfctl(padapter))) {
		doiqk = _TRUE;
		rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);
	}

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

#if defined(CONFIG_IOCTL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	for (i = 0; i < pdvobj->iface_nums; i++) {
		if (!(ifbmp_ch_changed & BIT(i)) || !pdvobj->padapters[i])
			continue;

		{
			u8 ht_option = 0;

			#ifdef CONFIG_80211N_HT
			ht_option = pdvobj->padapters[i]->mlmepriv.htpriv.ht_option;
			#endif

			rtw_cfg80211_ch_switch_notify(pdvobj->padapters[i]
				, pdvobj->padapters[i]->mlmeextpriv.cur_channel
				, pdvobj->padapters[i]->mlmeextpriv.cur_bwmode
				, pdvobj->padapters[i]->mlmeextpriv.cur_ch_offset
				, ht_option, 0);
		}
	}
#endif /* defined(CONFIG_IOCTL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) */

	if (DUMP_ADAPTERS_STATUS) {
		RTW_INFO(FUNC_ADPT_FMT" done\n", FUNC_ADPT_ARG(padapter));
		dump_adapters_status(RTW_DBGDUMP , adapter_to_dvobj(padapter));
	}

#ifdef CONFIG_MCC_MODE
update_beacon:
#endif

	for (i = 0; i < pdvobj->iface_nums; i++) {
		struct mlme_priv *mlme;

		if (!(parm->ifbmp & BIT(i)) || !pdvobj->padapters[i])
			continue;

		/* update beacon content only if bstart_bss is _TRUE */
		if (pdvobj->padapters[i]->mlmeextpriv.bstart_bss != _TRUE)
			continue;

		mlme = &(pdvobj->padapters[i]->mlmepriv);

		#ifdef CONFIG_80211N_HT
		if ((ATOMIC_READ(&mlme->olbc) == _TRUE) || (ATOMIC_READ(&mlme->olbc_ht) == _TRUE)) {
			/* AP is not starting a 40 MHz BSS in presence of an 802.11g BSS. */
			mlme->ht_op_mode &= (~HT_INFO_OPERATION_MODE_OP_MODE_MASK);
			mlme->ht_op_mode |= OP_MODE_MAY_BE_LEGACY_STAS;
			update_beacon(pdvobj->padapters[i], _HT_ADD_INFO_IE_, NULL, _FALSE, 0);
		}
		#endif

		update_beacon(pdvobj->padapters[i], _TIM_IE_, NULL, _FALSE, 0);
	}

	if (mlme_act != MLME_OPCH_SWITCH
		&& pmlmeext->bstart_bss == _TRUE
	) {
#ifdef CONFIG_SUPPORT_MULTI_BCN
		_irqL irqL;

		_enter_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
		if (rtw_is_list_empty(&padapter->list)) {
			#ifdef CONFIG_FW_HANDLE_TXBCN
			padapter->vap_id = rtw_ap_allocate_vapid(pdvobj);
			#endif
			rtw_list_insert_tail(&padapter->list, get_list_head(&pdvobj->ap_if_q));
			pdvobj->nr_ap_if++;
			pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL / pdvobj->nr_ap_if;
		}
		_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);

		#ifdef CONFIG_SWTIMER_BASED_TXBCN
		rtw_ap_set_mbid_num(padapter, pdvobj->nr_ap_if);
		rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pdvobj->inter_bcn_space));
		#endif /*CONFIG_SWTIMER_BASED_TXBCN*/

#endif /*CONFIG_SUPPORT_MULTI_BCN*/

		#ifdef CONFIG_HW_P0_TSF_SYNC
		correct_TSF(padapter, mlme_act);
		#endif
	}

	rtw_scan_wait_completed(padapter);

	_rtw_iface_undersurvey_chk(__func__, padapter);
	/* send beacon */
	ResumeTxBeacon(padapter);
	{
#if !defined(CONFIG_INTERRUPT_BASED_TXBCN)
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI) || defined(CONFIG_PCI_BCN_POLLING)
#ifdef CONFIG_SWTIMER_BASED_TXBCN
		if (pdvobj->nr_ap_if == 1
			&& mlme_act != MLME_OPCH_SWITCH
		) {
			RTW_INFO("start SW BCN TIMER!\n");
			_set_timer(&pdvobj->txbcn_timer, bcn_interval);
		}
#else
		for (i = 0; i < pdvobj->iface_nums; i++) {
			if (!(parm->ifbmp & BIT(i)) || !pdvobj->padapters[i])
				continue;

			if (send_beacon(pdvobj->padapters[i]) == _FAIL)
				RTW_INFO(ADPT_FMT" issue_beacon, fail!\n", ADPT_ARG(pdvobj->padapters[i]));
		}
#endif
#endif
#endif /* !defined(CONFIG_INTERRUPT_BASED_TXBCN) */

#ifdef CONFIG_FW_HANDLE_TXBCN
		if (mlme_act != MLME_OPCH_SWITCH)
			rtw_ap_mbid_bcn_en(padapter, padapter->vap_id);
#endif
	}
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
	u32 akm;
	u8 mfp_opt = MFP_NO;
	u8	channel, network_type;
	u8 OUI1[] = {0x00, 0x50, 0xf2, 0x01};
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
	HT_CAP_AMPDU_DENSITY best_ampdu_density;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pbss_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	u8 *ie = pbss_network->IEs;
	u8 vht_cap = _FALSE;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	u8 rf_num = 0;
	int ret_rm;
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


	if (pbss_network->InfrastructureMode != Ndis802_11APMode
		&& pbss_network->InfrastructureMode != Ndis802_11_mesh
	) {
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

#ifdef CONFIG_RTW_MESH
	/* Mesh ID */
	if (MLME_IS_MESH(padapter)) {
		p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, WLAN_EID_MESH_ID, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
		if (p && ie_len > 0) {
			_rtw_memset(&pbss_network->mesh_id, 0, sizeof(NDIS_802_11_SSID));
			_rtw_memcpy(pbss_network->mesh_id.Ssid, (p + 2), ie_len);
			pbss_network->mesh_id.SsidLength = ie_len;
		}
	}
#endif

	/* chnnel */
	channel = 0;
	pbss_network->Configuration.Length = 0;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _DSSET_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)
		channel = *(p + 2);

	pbss_network->Configuration.DSConfig = channel;

	/*	support rate ie & ext support ie & IElen & SupportedRates	*/
	network_type = rtw_update_rate_bymode(pbss_network, pregistrypriv->wireless_mode);

	/* parsing ERP_IE */
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0)  {
		if(padapter->registrypriv.wireless_mode == WIRELESS_11B ) {

			pbss_network->IELength = pbss_network->IELength - *(p+1) - 2;
			ret_rm = rtw_ies_remove_ie(ie , &len, _BEACON_IE_OFFSET_, _ERPINFO_IE_,NULL,0);
			RTW_DBG("%s, remove_ie of ERP_IE=%d\n", __FUNCTION__, ret_rm);
		} else 
			ERP_IE_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)p);

	}

	/* update privacy/security */
	if (cap & BIT(4))
		pbss_network->Privacy = 1;
	else
		pbss_network->Privacy = 0;

	psecuritypriv->wpa_psk = 0;

	/* wpa2 */
	akm = 0;
	group_cipher = 0;
	pairwise_cipher = 0;
	psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if (p && ie_len > 0) {
		if (rtw_parse_wpa2_ie(p, ie_len + 2, &group_cipher, &pairwise_cipher, &akm, &mfp_opt) == _SUCCESS) {
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
			psecuritypriv->dot8021xalg = 1;/* psk,  todo:802.1x */
			psecuritypriv->wpa_psk |= BIT(1);

			psecuritypriv->wpa2_group_cipher = group_cipher;
			psecuritypriv->wpa2_pairwise_cipher = pairwise_cipher;

#ifdef CONFIG_IOCTL_CFG80211
			/*
			Kernel < v5.1, the auth_type set as NL80211_AUTHTYPE_AUTOMATIC 
			in cfg80211_rtw_start_ap().
			if the AKM SAE in the RSN IE, we have to update the auth_type for SAE
			in rtw_check_beacon_data().
			*/
			if (CHECK_BIT(WLAN_AKM_TYPE_SAE, akm))
				psecuritypriv->auth_type = NL80211_AUTHTYPE_SAE;
#endif
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
				psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPAPSK;
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

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)) {
		/* MFP is mandatory for secure mesh */
		if (padapter->mesh_info.mesh_auth_id)
			mfp_opt = MFP_REQUIRED;
	} else
#endif
	if (mfp_opt == MFP_INVALID) {
		RTW_INFO(FUNC_ADPT_FMT" invalid MFP setting\n", FUNC_ADPT_ARG(padapter));
		return _FAIL;
	}
	psecuritypriv->mfp_opt = mfp_opt;

	/* wmm */
	ie_len = 0;
	pmlmepriv->qospriv.qos_option = 0;
#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter))
		pmlmepriv->qospriv.qos_option = 1;
#endif
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

				WMM_param_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)p);

				break;
			}

			if ((p == NULL) || (ie_len == 0))
				break;
		}
	}
#ifdef CONFIG_80211N_HT
	if(padapter->registrypriv.ht_enable &&
		is_supported_ht(padapter->registrypriv.wireless_mode)) {
		/* parsing HT_CAP_IE */
		p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
		if (p && ie_len > 0) {
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
				(psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP)) {
				rtw_hal_get_def_var(padapter, HW_VAR_BEST_AMPDU_DENSITY, &best_ampdu_density);
				pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & (best_ampdu_density << 2));
			} else
				pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);

			rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_FACTOR & max_rx_ampdu_factor); /* set  Max Rx AMPDU size  to 64K */

			_rtw_memcpy(&(pmlmeinfo->HT_caps), pht_cap, sizeof(struct HT_caps_element));

			/* Update Supported MCS Set field */
			{
				u8 rx_nss = 0;
				int i;

				rx_nss = GET_HAL_RX_NSS(padapter);

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
					RTW_WARN("rf_type:%d or rx_nss:%u is not expected\n", GET_HAL_RFPATH(padapter), rx_nss);
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
		if (p && ie_len > 0) {
			pHT_info_ie = p;
			if (channel == 0)
				pbss_network->Configuration.DSConfig = GET_HT_OP_ELE_PRI_CHL(pHT_info_ie + 2);
			else if (channel != GET_HT_OP_ELE_PRI_CHL(pHT_info_ie + 2)) {
				RTW_INFO(FUNC_ADPT_FMT" ch inconsistent, DSSS:%u, HT primary:%u\n"
					, FUNC_ADPT_ARG(padapter), channel, GET_HT_OP_ELE_PRI_CHL(pHT_info_ie + 2));
			}
		}
	}
#endif /* CONFIG_80211N_HT */
	pmlmepriv->cur_network.network_type = network_type;

#ifdef CONFIG_80211N_HT
	pmlmepriv->htpriv.ht_option = _FALSE;

	if ((psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_TKIP) ||
	    (psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_TKIP)) {
		/* todo: */
		/* ht_cap = _FALSE; */
	}

	/* ht_cap	 */
	if (padapter->registrypriv.ht_enable &&
		is_supported_ht(padapter->registrypriv.wireless_mode) && ht_cap == _TRUE) {

		pmlmepriv->htpriv.ht_option = _TRUE;
		pmlmepriv->qospriv.qos_option = 1;

		pmlmepriv->htpriv.ampdu_enable = pregistrypriv->ampdu_enable ? _TRUE : _FALSE;

		HT_caps_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)pHT_caps_ie);

		HT_info_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)pHT_info_ie);
	}
#endif

#ifdef CONFIG_80211AC_VHT
	pmlmepriv->ori_vht_en = 0;
	pmlmepriv->vhtpriv.vht_option = _FALSE;

	if (pmlmepriv->htpriv.ht_option == _TRUE
		&& pbss_network->Configuration.DSConfig > 14
		&& REGSTY_IS_11AC_ENABLE(pregistrypriv)
		&& is_supported_vht(pregistrypriv->wireless_mode)
		&& (!rfctl->country_ent || COUNTRY_CHPLAN_EN_11AC(rfctl->country_ent))
	) {
		/* Parsing VHT CAP IE */
		p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTCapability, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
		if (p && ie_len > 0)
			vht_cap = _TRUE;

		/* Parsing VHT OPERATION IE */

		if (vht_cap == _TRUE
			&& MLME_IS_MESH(padapter) /* allow only mesh temporarily before VHT IE checking is ready */
		) {
			rtw_check_for_vht20(padapter, ie + _BEACON_IE_OFFSET_, pbss_network->IELength - _BEACON_IE_OFFSET_);
			pmlmepriv->ori_vht_en = 1;
			pmlmepriv->vhtpriv.vht_option = _TRUE;
		} else if (REGSTY_IS_11AC_AUTO(pregistrypriv)) {
			rtw_vht_ies_detach(padapter, pbss_network);
			rtw_vht_ies_attach(padapter, pbss_network);
		}
	}

	if (pmlmepriv->vhtpriv.vht_option == _FALSE)
		rtw_vht_ies_detach(padapter, pbss_network);
#endif /* CONFIG_80211AC_VHT */

#ifdef CONFIG_80211N_HT
	if(padapter->registrypriv.ht_enable &&
					is_supported_ht(padapter->registrypriv.wireless_mode) &&
		pbss_network->Configuration.DSConfig <= 14 && padapter->registrypriv.wifi_spec == 1 &&
		pbss_network->IELength + 10 <= MAX_IE_SZ) {
		uint len = 0;

		SET_EXT_CAPABILITY_ELE_BSS_COEXIST(pmlmepriv->ext_capab_ie_data, 1);
		pmlmepriv->ext_capab_ie_len = 10;
		rtw_set_ie(pbss_network->IEs + pbss_network->IELength, EID_EXTCapability, 8, pmlmepriv->ext_capab_ie_data, &len);
		pbss_network->IELength += pmlmepriv->ext_capab_ie_len;
	}
#endif /* CONFIG_80211N_HT */

	pbss_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX *)pbss_network);

	rtw_ies_get_chbw(pbss_network->IEs + _BEACON_IE_OFFSET_, pbss_network->IELength - _BEACON_IE_OFFSET_
		, &pmlmepriv->ori_ch, &pmlmepriv->ori_bw, &pmlmepriv->ori_offset, 1, 1);
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
void rtw_macaddr_acl_init(_adapter *adapter, u8 period)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl;
	_queue *acl_node_q;
	int i;
	_irqL irqL;

	if (period >= RTW_ACL_PERIOD_NUM) {
		rtw_warn_on(1);
		return;
	}

	acl = &stapriv->acl_list[period];
	acl_node_q = &acl->acl_node_q;

	_rtw_spinlock_init(&(acl_node_q->lock));

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

static void _rtw_macaddr_acl_deinit(_adapter *adapter, u8 period, bool clear_only)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl;
	_queue *acl_node_q;
	_irqL irqL;
	_list *head, *list;
	struct rtw_wlan_acl_node *acl_node;

	if (period >= RTW_ACL_PERIOD_NUM) {
		rtw_warn_on(1);
		return;
	}

	acl = &stapriv->acl_list[period];
	acl_node_q = &acl->acl_node_q;

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

	if (!clear_only)
		_rtw_spinlock_free(&(acl_node_q->lock));

	rtw_warn_on(acl->num);
	acl->mode = RTW_ACL_MODE_DISABLED;
}

void rtw_macaddr_acl_deinit(_adapter *adapter, u8 period)
{
	_rtw_macaddr_acl_deinit(adapter, period, 0);
}

void rtw_macaddr_acl_clear(_adapter *adapter, u8 period)
{
	_rtw_macaddr_acl_deinit(adapter, period, 1);
}

void rtw_set_macaddr_acl(_adapter *adapter, u8 period, int mode)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl;

	if (period >= RTW_ACL_PERIOD_NUM) {
		rtw_warn_on(1);
		return;
	}

	acl = &stapriv->acl_list[period];

	RTW_INFO(FUNC_ADPT_FMT" p=%u, mode=%d\n"
		, FUNC_ADPT_ARG(adapter), period, mode);

	acl->mode = mode;
}

int rtw_acl_add_sta(_adapter *adapter, u8 period, const u8 *addr)
{
	_irqL irqL;
	_list *list, *head;
	u8 existed = 0;
	int i = -1, ret = 0;
	struct rtw_wlan_acl_node *acl_node;
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl;
	_queue *acl_node_q;

	if (period >= RTW_ACL_PERIOD_NUM) {
		rtw_warn_on(1);
		ret = -1;
		goto exit;
	}

	acl = &stapriv->acl_list[period];
	acl_node_q = &acl->acl_node_q;

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

	RTW_INFO(FUNC_ADPT_FMT" p=%u "MAC_FMT" %s (acl_num=%d)\n"
		 , FUNC_ADPT_ARG(adapter), period, MAC_ARG(addr)
		, (existed ? "existed" : ((i < 0 || i >= NUM_ACL) ? "no room" : "added"))
		 , acl->num);
exit:
	return ret;
}

int rtw_acl_remove_sta(_adapter *adapter, u8 period, const u8 *addr)
{
	_irqL irqL;
	_list *list, *head;
	int ret = 0;
	struct rtw_wlan_acl_node *acl_node;
	struct sta_priv *stapriv = &adapter->stapriv;
	struct wlan_acl_pool *acl;
	_queue	*acl_node_q;
	u8 is_baddr = is_broadcast_mac_addr(addr);
	u8 match = 0;

	if (period >= RTW_ACL_PERIOD_NUM) {
		rtw_warn_on(1);
		goto exit;
	}

	acl = &stapriv->acl_list[period];
	acl_node_q = &acl->acl_node_q;

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

	RTW_INFO(FUNC_ADPT_FMT" p=%u "MAC_FMT" %s (acl_num=%d)\n"
		 , FUNC_ADPT_ARG(adapter), period, MAC_ARG(addr)
		 , is_baddr ? "clear all" : (match ? "match" : "no found")
		 , acl->num);

exit:
	return ret;
}
#endif /* CONFIG_RTW_MACADDR_ACL */

u8 rtw_ap_set_sta_key(_adapter *adapter, const u8 *addr, u8 alg, const u8 *key, u8 keyid, u8 gk)
{
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	struct cmd_obj *cmd;
	struct set_stakey_parm *param;
	u8	res = _SUCCESS;

	cmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	param = (struct set_stakey_parm *)rtw_zmalloc(sizeof(struct set_stakey_parm));
	if (param == NULL) {
		rtw_mfree((u8 *) cmd, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(cmd, param, _SetStaKey_CMD_);

	_rtw_memcpy(param->addr, addr, ETH_ALEN);
	param->algorithm = alg;
	param->keyid = keyid;
	_rtw_memcpy(param->key, key, 16);
	param->gk = gk;

	res = rtw_enqueue_cmd(cmdpriv, cmd);

exit:
	return res;
}

u8 rtw_ap_set_pairwise_key(_adapter *padapter, struct sta_info *psta)
{
	return rtw_ap_set_sta_key(padapter
		, psta->cmn.mac_addr
		, psta->dot118021XPrivacy
		, psta->dot118021x_UncstKey.skey
		, 0
		, 0
	);
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

	if ((rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, 0)) && (psta_bmc->sleepq_len > 0)) {
		int tx_counts = 0;

		_update_beacon(padapter, _TIM_IE_, NULL, _FALSE, 0, "update TIM with TIB=1");

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

			if (rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, 0))
				update_tim = _TRUE;

			rtw_tim_map_clear(padapter, pstapriv->tim_bitmap, 0);
			rtw_tim_map_clear(padapter, pstapriv->sta_dz_bitmap, 0);

			if (update_tim == _TRUE) {
				RTW_INFO("clear TIB\n");
				_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, 0, "bmc sleepq and HIQ empty");
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
#ifdef CONFIG_80211N_HT
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
#endif /* CONFIG_80211N_HT */
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
#ifdef CONFIG_80211N_HT
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
#endif /* CONFIG_80211N_HT */
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

void _update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx, u8 flags, const char *tag)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	bool updated = 1; /* treat as upadated by default */

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

#ifdef CONFIG_RTW_MESH
	case WLAN_EID_MESH_CONFIG:
		updated = rtw_mesh_update_bss_peering_status(padapter, &(pmlmeext->mlmext_info.network));
		updated |= rtw_mesh_update_bss_formation_info(padapter, &(pmlmeext->mlmext_info.network));
		updated |= rtw_mesh_update_bss_forwarding_state(padapter, &(pmlmeext->mlmext_info.network));
		break;
#endif

	case _VENDOR_SPECIFIC_IE_:
		update_bcn_vendor_spec_ie(padapter, oui);
		break;

	case 0xFF:
	default:
		break;
	}

	if (updated)
		pmlmepriv->update_bcn = _TRUE;

	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);

#ifndef CONFIG_INTERRUPT_BASED_TXBCN
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI) || defined(CONFIG_PCI_BCN_POLLING)
	if (tx && updated) {
		/* send_beacon(padapter); */ /* send_beacon must execute on TSR level */
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" ie_id:%u - %s\n", FUNC_ADPT_ARG(padapter), ie_id, tag);
		if(flags == RTW_CMDF_WAIT_ACK)
			set_tx_beacon_cmd(padapter, RTW_CMDF_WAIT_ACK);
		else
			set_tx_beacon_cmd(padapter, 0);
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

		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE, 0);

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

	e_field = (ctrl_field & BIT(0)) ? 1 : 0; /*SM Power Save Enabled*/
	m_field = (ctrl_field & BIT(1)) ? 1 : 0; /*SM Mode, 0:static SMPS, 1:dynamic SMPS*/

	if (e_field) {
		if (m_field) { /*mode*/
			psta->htpriv.smps_cap = WLAN_HT_CAP_SM_PS_DYNAMIC;
			RTW_ERR("Don't support dynamic SMPS\n");
		}
		else
			psta->htpriv.smps_cap = WLAN_HT_CAP_SM_PS_STATIC;
	} else {
		/*disable*/
		psta->htpriv.smps_cap = WLAN_HT_CAP_SM_PS_DISABLED;
	}

	if (psta->htpriv.smps_cap != WLAN_HT_CAP_SM_PS_DYNAMIC)
		rtw_ssmps_wk_cmd(padapter, psta, e_field, 1);
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
				update_beacon(padapter, _ERPINFO_IE_, NULL, _FALSE, 0);
			}
		}

	} else {
		if (psta->nonerp_set) {
			psta->nonerp_set = 0;

			pmlmepriv->num_sta_non_erp--;

			if (pmlmepriv->num_sta_non_erp == 0) {
				beacon_updated = _TRUE;
				update_beacon(padapter, _ERPINFO_IE_, NULL, _FALSE, 0);
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
	if(padapter->registrypriv.ht_enable &&
		is_supported_ht(padapter->registrypriv.wireless_mode)) {
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

			if (((ht_capab & RTW_IEEE80211_HT_CAP_40MHZ_INTOLERANT) != 0) &&
				(psta->ht_40mhz_intolerant == 0)) {
				psta->ht_40mhz_intolerant = 1;
				pmlmepriv->num_sta_40mhz_intolerant++;
				RTW_INFO("%s STA " MAC_FMT " - 40MHZ_INTOLERANT, ",
					   __FUNCTION__, MAC_ARG(psta->cmn.mac_addr));
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
			update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE, 0);
			update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _FALSE, 0);
			beacon_updated = _TRUE;
		}
	}
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)) {
		struct sta_priv *pstapriv = &padapter->stapriv;

		update_beacon(padapter, WLAN_EID_MESH_CONFIG, NULL, _FALSE, 0);
		if (pstapriv->asoc_list_cnt == 1)
			_set_timer(&padapter->mesh_atlm_param_req_timer, 0);
		beacon_updated = _TRUE;
	}
#endif

	if (beacon_updated)
		update_beacon(padapter, 0xFF, NULL, _TRUE, 0);

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

	if (rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, psta->cmn.aid)) {
		rtw_tim_map_clear(padapter, pstapriv->tim_bitmap, psta->cmn.aid);
		beacon_updated = _TRUE;
		update_beacon(padapter, _TIM_IE_, NULL, _FALSE, 0);
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
			update_beacon(padapter, _ERPINFO_IE_, NULL, _FALSE, 0);
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

	if (psta->ht_40mhz_intolerant) {
		psta->ht_40mhz_intolerant = 0;
		if (pmlmepriv->num_sta_40mhz_intolerant > 0)
			pmlmepriv->num_sta_40mhz_intolerant--;
		else
			rtw_warn_on(1);
	}

	if (rtw_ht_operation_update(padapter) > 0) {
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE, 0);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _FALSE, 0);
	}
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)) {
		update_beacon(padapter, WLAN_EID_MESH_CONFIG, NULL, _FALSE, 0);
		if (pstapriv->asoc_list_cnt == 0)
			_cancel_timer_ex(&padapter->mesh_atlm_param_req_timer);
		beacon_updated = _TRUE;
	}
#endif

	if (beacon_updated == _TRUE)
		update_beacon(padapter, 0xFF, NULL, _TRUE, 0);

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

	if (!psta)
		return beacon_updated;

	if (active == _TRUE) {
#ifdef CONFIG_80211N_HT
		/* tear down Rx AMPDU */
		send_delba(padapter, 0, psta->cmn.mac_addr);/* recipient */

		/* tear down TX AMPDU */
		send_delba(padapter, 1, psta->cmn.mac_addr);/*  */ /* originator */

#endif /* CONFIG_80211N_HT */

		if (!MLME_IS_MESH(padapter))
			issue_deauth(padapter, psta->cmn.mac_addr, reason);
	}

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter))
		rtw_mesh_path_flush_by_nexthop(psta);
#endif

#ifdef CONFIG_BEAMFORMING
	beamforming_wk_cmd(padapter, BEAMFORMING_CTRL_LEAVE, psta->cmn.mac_addr, ETH_ALEN, 1);
#endif

#ifdef CONFIG_80211N_HT
	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */
#endif

	/* clear cam entry / key */
	rtw_clearstakey_cmd(padapter, psta, enqueue);


	_enter_critical_bh(&psta->lock, &irqL);
	psta->state &= ~(_FW_LINKED | WIFI_UNDER_KEY_HANDSHAKE);

#ifdef CONFIG_IOCTL_CFG80211
	if ((psta->auth_len != 0) && (psta->pauth_frame != NULL)) {
		rtw_mfree(psta->pauth_frame, psta->auth_len);
		psta->pauth_frame = NULL;
		psta->auth_len = 0;
	}
#endif /* CONFIG_IOCTL_CFG80211 */
	_exit_critical_bh(&psta->lock, &irqL);

	if (!MLME_IS_MESH(padapter)) {
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

	beacon_updated = bss_cap_update_on_sta_leave(padapter, psta);

	report_del_sta_event(padapter, psta->cmn.mac_addr, reason, enqueue, _FALSE);

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
		STA_SET_MESH_PLINK(psta, NULL);

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset))
			flush_list[flush_num++] = stainfo_offset;
		else
			rtw_warn_on(1);
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	/* call ap_free_sta() for each sta picked */
	for (i = 0; i < flush_num; i++) {
		u8 sta_addr[ETH_ALEN];

		psta = rtw_get_stainfo_by_offset(pstapriv, flush_list[i]);
		_rtw_memcpy(sta_addr, psta->cmn.mac_addr, ETH_ALEN);

		ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_DEAUTH_LEAVING, enqueue);
		#ifdef CONFIG_RTW_MESH
		if (MLME_IS_MESH(padapter))
			rtw_mesh_expire_peer(padapter, sta_addr);
		#endif
	}

	if (!MLME_IS_MESH(padapter))
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
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	_irqL irqL;
	_list	*phead, *plist;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	rtw_setopmode_cmd(padapter
		, MLME_IS_AP(padapter) ? Ndis802_11APMode : Ndis802_11_mesh
		, RTW_CMDF_DIRECTLY
	);

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
#ifdef CONFIG_CONCURRENT_MODE
	struct security_priv *psecuritypriv = &padapter->securitypriv;
#endif

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

	for (i = 0 ;  i < pstapriv->max_aid; i++)
		pstapriv->sta_aid[i] = NULL;

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
	struct sta_info *psta = NULL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_SUPPORT_MULTI_BCN
	struct dvobj_priv *pdvobj = padapter->dvobj;
	_irqL irqL;
#endif

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
	rtw_dfs_rd_en_decision(padapter, self_action, 0);
#endif

	/* free scan queue */
	rtw_free_network_queue(padapter, _TRUE);

#if CONFIG_RTW_MACADDR_ACL
	rtw_macaddr_acl_clear(padapter, RTW_ACL_PERIOD_BSS);
#endif

	rtw_sta_flush(padapter, _TRUE);
	rtw_ap_bcmc_sta_flush(padapter);

	/* free_assoc_sta_resources	 */
	rtw_free_all_stainfo(padapter);

	psta = rtw_get_bcmc_stainfo(padapter);
	if (psta) {
		rtw_sta_mstatus_disc_rpt(padapter, psta->cmn.mac_id);
		/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */
		rtw_free_stainfo(padapter, psta);
		/*_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);*/
	}

	pmlmepriv->ap_isolate = 0;
	rtw_free_mlme_priv_ie_data(pmlmepriv);

#ifdef CONFIG_SUPPORT_MULTI_BCN
	if (pmlmeext->bstart_bss == _TRUE) {
		#ifdef CONFIG_FW_HANDLE_TXBCN
		u8 free_apid = CONFIG_LIMITED_AP_NUM;
		#endif

		_enter_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
		pdvobj->nr_ap_if--;
		if (pdvobj->nr_ap_if > 0)
			pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL / pdvobj->nr_ap_if;
		else
			pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL;
		#ifdef CONFIG_FW_HANDLE_TXBCN
		rtw_ap_release_vapid(pdvobj, padapter->vap_id);
		free_apid = padapter->vap_id;
		padapter->vap_id = CONFIG_LIMITED_AP_NUM;
		#endif
		rtw_list_delete(&padapter->list);
		_exit_critical_bh(&pdvobj->ap_if_q.lock, &irqL);
		#ifdef CONFIG_FW_HANDLE_TXBCN
		rtw_ap_mbid_bcn_dis(padapter, free_apid);
		#endif

		#ifdef CONFIG_SWTIMER_BASED_TXBCN
		rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pdvobj->inter_bcn_space));

		if (pdvobj->nr_ap_if == 0)
			_cancel_timer_ex(&pdvobj->txbcn_timer);
		#endif
	}
#endif

	pmlmeext->bstart_bss = _FALSE;

	rtw_hal_rcr_set_chk_bssid(padapter, self_action);

#ifdef CONFIG_HW_P0_TSF_SYNC
	correct_TSF(padapter, self_action);
#endif

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

static u8 rtw_ap_update_chbw_by_ifbmp(struct dvobj_priv *dvobj, u8 ifbmp
	, u8 cur_ie_ch[], u8 cur_ie_bw[], u8 cur_ie_offset[]
	, u8 dec_ch[], u8 dec_bw[], u8 dec_offset[]
	, const char *caller)
{
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	WLAN_BSSID_EX *network;
	u8 ifbmp_ch_changed = 0;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (!(ifbmp & BIT(i)) || !dvobj->padapters)
			continue;

		iface = dvobj->padapters[i];
		mlmeext = &(iface->mlmeextpriv);

		if (MLME_IS_ASOC(iface)) {
			RTW_INFO(FUNC_ADPT_FMT" %u,%u,%u => %u,%u,%u%s\n", caller, ADPT_ARG(iface)
				, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset
				, dec_ch[i], dec_bw[i], dec_offset[i]
				, MLME_IS_OPCH_SW(iface) ? " OPCH_SW" : "");
		} else {
			RTW_INFO(FUNC_ADPT_FMT" %u,%u,%u => %u,%u,%u%s\n", caller, ADPT_ARG(iface)
				, cur_ie_ch[i], cur_ie_bw[i], cur_ie_offset[i]
				, dec_ch[i], dec_bw[i], dec_offset[i]
				, MLME_IS_OPCH_SW(iface) ? " OPCH_SW" : "");
		}
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (!(ifbmp & BIT(i)) || !dvobj->padapters)
			continue;

		iface = dvobj->padapters[i];
		mlmeext = &(iface->mlmeextpriv);
		network = &(mlmeext->mlmext_info.network);

		/* ch setting differs from mlmeext.network IE */
		if (cur_ie_ch[i] != dec_ch[i]
			|| cur_ie_bw[i] != dec_bw[i]
			|| cur_ie_offset[i] != dec_offset[i])
			ifbmp_ch_changed |= BIT(i);

		/* ch setting differs from existing one */
		if (MLME_IS_ASOC(iface)
			&& (mlmeext->cur_channel != dec_ch[i]
				|| mlmeext->cur_bwmode != dec_bw[i]
				|| mlmeext->cur_ch_offset != dec_offset[i])
		) {
			if (rtw_linked_check(iface) == _TRUE) {
				#ifdef CONFIG_SPCT_CH_SWITCH
				if (1)
					rtw_ap_inform_ch_switch(iface, dec_ch[i], dec_offset[i]);
				else
				#endif
					rtw_sta_flush(iface, _FALSE);
			}
		}

		mlmeext->cur_channel = dec_ch[i];
		mlmeext->cur_bwmode = dec_bw[i];
		mlmeext->cur_ch_offset = dec_offset[i];

		rtw_ap_update_bss_chbw(iface, network, dec_ch[i], dec_bw[i], dec_offset[i]);
	}

	return ifbmp_ch_changed;
}

static u8 rtw_ap_ch_specific_chk(_adapter *adapter, u8 ch, u8 *bw, u8 *offset, const char *caller)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *chset = rfctl->channel_set;
	int ch_idx;
	u8 ret = _SUCCESS;

	ch_idx = rtw_chset_search_ch(chset, ch);
	if (ch_idx < 0) {
		RTW_WARN("%s ch:%u doesn't fit in chplan\n", caller, ch);
		ret = _FAIL;
		goto exit;
	}
	if (chset[ch_idx].ScanType == SCAN_PASSIVE) {
		RTW_WARN("%s ch:%u is passive\n", caller, ch);
		ret = _FAIL;
		goto exit;
	}

	rtw_adjust_chbw(adapter, ch, bw, offset);

	if (!rtw_get_offset_by_chbw(ch, *bw, offset)) {
		RTW_WARN("%s %u,%u has no valid offset\n", caller, ch, *bw);
		ret = _FAIL;
		goto exit;
	}

	while (!rtw_chset_is_chbw_valid(chset, ch, *bw, *offset, 0, 0)
		|| (rtw_rfctl_dfs_domain_unknown(rfctl) && rtw_chset_is_dfs_chbw(chset, ch, *bw, *offset))
	) {
		if (*bw > CHANNEL_WIDTH_20)
			(*bw)--;
		if (*bw == CHANNEL_WIDTH_20) {
			*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		}
	}

	if (rtw_rfctl_dfs_domain_unknown(rfctl) && rtw_chset_is_dfs_chbw(chset, ch, *bw, *offset)) {
		RTW_WARN("%s DFS channel %u can't be used\n", caller, ch);
		ret = _FAIL;
		goto exit;
	}

exit:
	return ret;
}

static bool rtw_ap_choose_chbw(_adapter *adapter, u8 sel_ch, u8 max_bw, u8 cur_ch
	, u8 *ch, u8 *bw, u8 *offset, bool by_int_info, u8 mesh_only, const char *caller)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	bool ch_avail = _FALSE;

#if defined(CONFIG_DFS_MASTER)
	if (!rtw_rfctl_dfs_domain_unknown(rfctl)) {
		if (rfctl->radar_detected
			&& rfctl->dbg_dfs_choose_dfs_ch_first
		) {
			ch_avail = rtw_choose_shortest_waiting_ch(rfctl, sel_ch, max_bw
						, ch, bw, offset
						, RTW_CHF_2G | RTW_CHF_NON_DFS
						, cur_ch, by_int_info, mesh_only);
			if (ch_avail == _TRUE) {
				RTW_INFO("%s choose 5G DFS channel for debug\n", caller);
				goto exit;
			}
		}

		if (rfctl->radar_detected
			&& rfctl->dfs_ch_sel_d_flags
		) {
			ch_avail = rtw_choose_shortest_waiting_ch(rfctl, sel_ch, max_bw
						, ch, bw, offset
						, rfctl->dfs_ch_sel_d_flags
						, cur_ch, by_int_info, mesh_only);
			if (ch_avail == _TRUE) {
				RTW_INFO("%s choose with dfs_ch_sel_d_flags:0x%02x for debug\n"
					, caller, rfctl->dfs_ch_sel_d_flags);
				goto exit;
			}
		}

		ch_avail = rtw_choose_shortest_waiting_ch(rfctl, sel_ch, max_bw
					, ch, bw, offset
					, 0
					, cur_ch, by_int_info, mesh_only);
	} else
#endif /* defined(CONFIG_DFS_MASTER) */
	{
		ch_avail = rtw_choose_shortest_waiting_ch(rfctl, sel_ch, max_bw
					, ch, bw, offset
					, RTW_CHF_DFS
					, cur_ch, by_int_info, mesh_only);
	}
#if defined(CONFIG_DFS_MASTER)
exit:
#endif
	if (ch_avail == _FALSE)
		RTW_WARN("%s no available channel\n", caller);

	return ch_avail;
}

u8 rtw_ap_chbw_decision(_adapter *adapter, u8 ifbmp, u8 excl_ifbmp
	, s16 req_ch, s8 req_bw, s8 req_offset
	, u8 *ch, u8 *bw, u8 *offset, u8 *chbw_allow)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	RT_CHANNEL_INFO *chset = adapter_to_chset(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	bool ch_avail = _FALSE;
	u8 cur_ie_ch[CONFIG_IFACE_NUMBER] = {0};
	u8 cur_ie_bw[CONFIG_IFACE_NUMBER] = {0};
	u8 cur_ie_offset[CONFIG_IFACE_NUMBER] = {0};
	u8 dec_ch[CONFIG_IFACE_NUMBER] = {0};
	u8 dec_bw[CONFIG_IFACE_NUMBER] = {0};
	u8 dec_offset[CONFIG_IFACE_NUMBER] = {0};
	u8 u_ch = 0, u_bw = 0, u_offset = 0;
	struct mlme_ext_priv *mlmeext;
	WLAN_BSSID_EX *network;
	struct mi_state mstate;
	struct mi_state mstate_others;
	bool set_u_ch = _FALSE;
	u8 ifbmp_others = 0xFF & ~ifbmp & ~excl_ifbmp;
	u8 ifbmp_ch_changed = 0;
	bool ifbmp_all_mesh = 0;
	_adapter *iface;
	int i;

#ifdef CONFIG_RTW_MESH
	for (i = 0; i < dvobj->iface_nums; i++)
		if ((ifbmp & BIT(i)) && dvobj->padapters)
			if (!MLME_IS_MESH(dvobj->padapters[i]))
				break;
	ifbmp_all_mesh = i >= dvobj->iface_nums ? 1 : 0;
#endif

	RTW_INFO("%s ifbmp:0x%02x excl_ifbmp:0x%02x req:%d,%d,%d\n", __func__
		, ifbmp, excl_ifbmp, req_ch, req_bw, req_offset);
	rtw_mi_status_by_ifbmp(dvobj, ifbmp, &mstate);
	rtw_mi_status_by_ifbmp(dvobj, ifbmp_others, &mstate_others);
	RTW_INFO("%s others ld_sta_num:%u, lg_sta_num:%u, ap_num:%u, mesh_num:%u\n"
		, __func__, MSTATE_STA_LD_NUM(&mstate_others), MSTATE_STA_LG_NUM(&mstate_others)
		, MSTATE_AP_NUM(&mstate_others), MSTATE_MESH_NUM(&mstate_others));

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (!(ifbmp & BIT(i)) || !dvobj->padapters[i])
			continue;
		iface = dvobj->padapters[i];
		mlmeext = &(iface->mlmeextpriv);
		network = &(mlmeext->mlmext_info.network);

		/* get current IE channel settings */
		rtw_ies_get_chbw(BSS_EX_TLV_IES(network), BSS_EX_TLV_IES_LEN(network)
			, &cur_ie_ch[i], &cur_ie_bw[i], &cur_ie_offset[i], 1, 1);

		/* prepare temporary channel setting decision */
		if (req_ch == 0) {
			/* request comes from upper layer, use cur_ie values */
			dec_ch[i] = cur_ie_ch[i];
			dec_bw[i] = cur_ie_bw[i];
			dec_offset[i] = cur_ie_offset[i];
		} else {
			/* use chbw of cur_ie updated with specifying req as temporary decision */
			dec_ch[i] = (req_ch <= REQ_CH_NONE) ? cur_ie_ch[i] : req_ch;
			if (req_bw <= REQ_BW_NONE) {
				if (req_bw == REQ_BW_ORI)
					dec_bw[i] = iface->mlmepriv.ori_bw;
				else
					dec_bw[i] = cur_ie_bw[i];
			} else
				dec_bw[i] = req_bw;
			dec_offset[i] = (req_offset <= REQ_OFFSET_NONE) ? cur_ie_offset[i] : req_offset;
		}
	}

	if (MSTATE_STA_LD_NUM(&mstate_others) || MSTATE_STA_LG_NUM(&mstate_others)
		|| MSTATE_AP_NUM(&mstate_others) || MSTATE_MESH_NUM(&mstate_others)
	) {
		/* has linked/linking STA or has AP/Mesh mode */
		rtw_warn_on(!rtw_mi_get_ch_setting_union_by_ifbmp(dvobj, ifbmp_others, &u_ch, &u_bw, &u_offset));
		RTW_INFO("%s others union:%u,%u,%u\n", __func__, u_ch, u_bw, u_offset);
	}

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter) && req_ch == 0) {
		if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC)) {
			u8 if_id = adapter->iface_id;

			mlmeext = &(adapter->mlmeextpriv);

			/* check channel settings are the same */
			if (cur_ie_ch[if_id] == mlmeext->cur_channel
				&& cur_ie_bw[if_id] == mlmeext->cur_bwmode
				&& cur_ie_offset[if_id] == mlmeext->cur_ch_offset) {

				RTW_INFO(FUNC_ADPT_FMT"req ch settings are the same as current ch setting, go to exit\n"
					, FUNC_ADPT_ARG(adapter));

				*chbw_allow = _FALSE;
				goto exit;
			} else {
				RTW_INFO(FUNC_ADPT_FMT"request channel settings are not the same as current channel setting(%d,%d,%d,%d,%d,%d), restart MCC\n"
					, FUNC_ADPT_ARG(adapter)
					, cur_ie_ch[if_id], cur_ie_bw[if_id], cur_ie_offset[if_id]
					, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

				rtw_hal_set_mcc_setting_disconnect(adapter);
			}
		}	
	}
#endif /* CONFIG_MCC_MODE */

	if (MSTATE_STA_LG_NUM(&mstate_others) && !MSTATE_STA_LD_NUM(&mstate_others)) {
		/* has linking STA but no linked STA */

		for (i = 0; i < dvobj->iface_nums; i++) {
			if (!(ifbmp & BIT(i)) || !dvobj->padapters[i])
				continue;
			iface = dvobj->padapters[i];

			rtw_adjust_chbw(iface, dec_ch[i], &dec_bw[i], &dec_offset[i]);
			#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(iface))
				rtw_mesh_adjust_chbw(dec_ch[i], &dec_bw[i], &dec_offset[i]);
			#endif

			if (rtw_is_chbw_grouped(u_ch, u_bw, u_offset, dec_ch[i], dec_bw[i], dec_offset[i])) {
				rtw_chset_sync_chbw(chset
					, &dec_ch[i], &dec_bw[i], &dec_offset[i]
					, &u_ch, &u_bw, &u_offset, 1, 0);
				set_u_ch = _TRUE;

				/* channel bw offset can be allowed, not need MCC */
				*chbw_allow = _TRUE;
			} else {
				#ifdef CONFIG_MCC_MODE
				if (MCC_EN(iface)) {
					mlmeext = &(iface->mlmeextpriv);
					mlmeext->cur_channel = *ch = dec_ch[i];
					mlmeext->cur_bwmode = *bw = dec_bw[i];
					mlmeext->cur_ch_offset = *offset = dec_offset[i];

					/* channel bw offset can not be allowed, need MCC */
					*chbw_allow = _FALSE;
					RTW_INFO(FUNC_ADPT_FMT" enable mcc: %u,%u,%u\n", FUNC_ADPT_ARG(iface)
						 , *ch, *bw, *offset);
					goto exit;
				}
				#endif /* CONFIG_MCC_MODE */

				/* set this for possible ch change when join down*/
				set_fwstate(&iface->mlmepriv, WIFI_OP_CH_SWITCHING);
			}
		}

	} else if (MSTATE_STA_LD_NUM(&mstate_others)
		|| MSTATE_AP_NUM(&mstate_others) || MSTATE_MESH_NUM(&mstate_others)
	) {
		/* has linked STA mode or AP/Mesh mode */

		for (i = 0; i < dvobj->iface_nums; i++) {
			if (!(ifbmp & BIT(i)) || !dvobj->padapters[i])
				continue;
			iface = dvobj->padapters[i];

			rtw_adjust_chbw(iface, u_ch, &dec_bw[i], &dec_offset[i]);
			#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(iface))
				rtw_mesh_adjust_chbw(u_ch, &dec_bw[i], &dec_offset[i]);
			#endif

			#ifdef CONFIG_MCC_MODE
			if (MCC_EN(iface)) {
				if (!rtw_is_chbw_grouped(u_ch, u_bw, u_offset, dec_ch[i], dec_bw[i], dec_offset[i])) {
					mlmeext = &(iface->mlmeextpriv);
					mlmeext->cur_channel = *ch = dec_ch[i] = cur_ie_ch[i];
					mlmeext->cur_bwmode = *bw = dec_bw[i] = cur_ie_bw[i];
					mlmeext->cur_ch_offset = *offset = dec_offset[i] = cur_ie_offset[i];
					/* channel bw offset can not be allowed, need MCC */
					*chbw_allow = _FALSE;
					RTW_INFO(FUNC_ADPT_FMT" enable mcc: %u,%u,%u\n", FUNC_ADPT_ARG(iface)
						 , *ch, *bw, *offset);
					goto exit;
				} else
					/* channel bw offset can be allowed, not need MCC */
					*chbw_allow = _TRUE;
			}
			#endif /* CONFIG_MCC_MODE */

			if (req_ch == 0 && dec_bw[i] > u_bw
				&& rtw_chset_is_dfs_chbw(chset, u_ch, u_bw, u_offset)
			) {
				/* request comes from upper layer, prevent from additional channel waiting */
				dec_bw[i] = u_bw;
				if (dec_bw[i] == CHANNEL_WIDTH_20)
					dec_offset[i] = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			}

			/* follow */
			rtw_chset_sync_chbw(chset
				, &dec_ch[i], &dec_bw[i], &dec_offset[i]
				, &u_ch, &u_bw, &u_offset, 1, 0);
		}

		set_u_ch = _TRUE;

	} else {
		/* autonomous decision */
		u8 ori_ch = 0;
		u8 max_bw;
		bool by_int_info;

		/* autonomous decision, not need MCC */
		*chbw_allow = _TRUE;

		if (req_ch <= REQ_CH_NONE) /* channel is not specified */
			goto choose_chbw;

		/* get tmp dec union of ifbmp */
		for (i = 0; i < dvobj->iface_nums; i++) {
			if (!(ifbmp & BIT(i)) || !dvobj->padapters[i])
				continue;
			if (u_ch == 0) {
				u_ch = dec_ch[i];
				u_bw = dec_bw[i];
				u_offset = dec_offset[i];
				rtw_adjust_chbw(adapter, u_ch, &u_bw, &u_offset);
				rtw_get_offset_by_chbw(u_ch, u_bw, &u_offset);
			} else {
				u8 tmp_ch = dec_ch[i];
				u8 tmp_bw = dec_bw[i];
				u8 tmp_offset = dec_offset[i];
				
				rtw_adjust_chbw(adapter, tmp_ch, &tmp_bw, &tmp_offset);
				rtw_get_offset_by_chbw(tmp_ch, tmp_bw, &tmp_offset);

				rtw_warn_on(!rtw_is_chbw_grouped(u_ch, u_bw, u_offset, tmp_ch, tmp_bw, tmp_offset));
				rtw_sync_chbw(&tmp_ch, &tmp_bw, &tmp_offset, &u_ch, &u_bw, &u_offset);
			}
		}

		#ifdef CONFIG_RTW_MESH
		/* if ifbmp are all mesh, apply bw restriction */
		if (ifbmp_all_mesh)
			rtw_mesh_adjust_chbw(u_ch, &u_bw, &u_offset);
		#endif

		RTW_INFO("%s ifbmp:0x%02x tmp union:%u,%u,%u\n", __func__, ifbmp, u_ch, u_bw, u_offset);

		/* check if tmp dec union is usable */
		if (rtw_ap_ch_specific_chk(adapter, u_ch, &u_bw, &u_offset, __func__) == _FAIL) {
			/* channel can't be used */
			if (req_ch > 0) {
				/* specific channel and not from IE => don't change channel setting */
				goto exit;
			}
			goto choose_chbw;
		} else if (rtw_chset_is_chbw_non_ocp(chset, u_ch, u_bw, u_offset)) {
			RTW_WARN("%s DFS channel %u,%u under non ocp\n", __func__, u_ch, u_bw);
			if (req_ch > 0 && req_bw > REQ_BW_NONE) {
				/* change_chbw with specific channel and specific bw, goto update_bss_chbw directly */
				goto update_bss_chbw;
			}
		} else
			goto update_bss_chbw;

choose_chbw:
		by_int_info = req_ch == REQ_CH_INT_INFO ? 1 : 0;
		req_ch = req_ch > 0 ? req_ch : 0;
		max_bw = req_bw > REQ_BW_NONE ? req_bw : CHANNEL_WIDTH_20;
		for (i = 0; i < dvobj->iface_nums; i++) {
			if (!(ifbmp & BIT(i)) || !dvobj->padapters[i])
				continue;
			iface = dvobj->padapters[i];
			mlmeext = &(iface->mlmeextpriv);

			if (req_bw <= REQ_BW_NONE) {
				if (req_bw == REQ_BW_ORI) {
					if (max_bw < iface->mlmepriv.ori_bw)
						max_bw = iface->mlmepriv.ori_bw;
				} else {
					if (max_bw < cur_ie_bw[i])
						max_bw = cur_ie_bw[i];
				}
			}

			if (MSTATE_AP_NUM(&mstate) || MSTATE_MESH_NUM(&mstate)) {
				if (ori_ch == 0)
					ori_ch = mlmeext->cur_channel;
				else if (ori_ch != mlmeext->cur_channel)
					rtw_warn_on(1);
			} else {
				if (ori_ch == 0)
					ori_ch = cur_ie_ch[i];
				else if (ori_ch != cur_ie_ch[i])
					rtw_warn_on(1);
			}
		}

		ch_avail = rtw_ap_choose_chbw(adapter, req_ch, max_bw
			, ori_ch, &u_ch, &u_bw, &u_offset, by_int_info, ifbmp_all_mesh, __func__);
		if (ch_avail == _FALSE)
			goto exit;

update_bss_chbw:
		for (i = 0; i < dvobj->iface_nums; i++) {
			if (!(ifbmp & BIT(i)) || !dvobj->padapters[i])
				continue;
			iface = dvobj->padapters[i];

			dec_ch[i] = u_ch;
			if (dec_bw[i] > u_bw)
				dec_bw[i] = u_bw;
			if (dec_bw[i] == CHANNEL_WIDTH_20)
				dec_offset[i] = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			else
				dec_offset[i] = u_offset;

			#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(iface))
				rtw_mesh_adjust_chbw(dec_ch[i], &dec_bw[i], &dec_offset[i]);
			#endif
		}

		set_u_ch = _TRUE;
	}

	ifbmp_ch_changed = rtw_ap_update_chbw_by_ifbmp(dvobj, ifbmp
							, cur_ie_ch, cur_ie_bw, cur_ie_offset
							, dec_ch, dec_bw, dec_offset
							, __func__);

	if (u_ch != 0)
		RTW_INFO("%s union:%u,%u,%u\n", __func__, u_ch, u_bw, u_offset);

	if (rtw_mi_check_fwstate(adapter, _FW_UNDER_SURVEY)) {
		/* scanning, leave ch setting to scan state machine */
		set_u_ch = _FALSE;
	}

	if (set_u_ch == _TRUE) {
		*ch = u_ch;
		*bw = u_bw;
		*offset = u_offset;
	}
exit:
	return ifbmp_ch_changed;
}

u8 rtw_ap_sta_states_check(_adapter *adapter)
{
	struct sta_info *psta;
	struct sta_priv *pstapriv = &adapter->stapriv;
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

		if (!(psta->state & _FW_LINKED)) {
			RTW_INFO(ADPT_FMT"- SoftAP/Mesh - sta under linking, its state = 0x%x\n", ADPT_ARG(adapter), psta->state);
			rst = _TRUE;
			break;
		} else if (psta->state & WIFI_UNDER_KEY_HANDSHAKE) {
			RTW_INFO(ADPT_FMT"- SoftAP/Mesh - sta under key handshaking, its state = 0x%x\n", ADPT_ARG(adapter), psta->state);
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
			&& !IS_CH_WAITING(adapter_to_rfctl(padapter))
		) {
			/*update_beacon(padapter, _TIM_IE_, NULL, _FALSE, 0);*/
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
		set_tx_beacon_cmd(padapter, 0);
}
#endif

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
	u8 rate_set[12];
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
	u32 akm = 0;
	u8 mfp_opt = MFP_NO;
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

		if (rtw_parse_wpa2_ie(wpa_ie - 2, wpa_ie_len + 2, &group_cipher, &pairwise_cipher, &akm, &mfp_opt) == _SUCCESS) {
			sta->dot8021xalg = 1;/* psk, todo:802.1x */
			sta->wpa_psk |= BIT(1);

			sta->wpa2_group_cipher = group_cipher & sec->wpa2_group_cipher;
			sta->wpa2_pairwise_cipher = pairwise_cipher & sec->wpa2_pairwise_cipher;

			sta->akm_suite_type = akm;
			if (MLME_IS_AP(adapter) && (CHECK_BIT(WLAN_AKM_TYPE_SAE, akm)) && (MFP_NO == mfp_opt))
				status = WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION;

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

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(adapter)) {
		/* MFP is mandatory for secure mesh */
		if (adapter->mesh_info.mesh_auth_id)
			sta->flags |= WLAN_STA_MFP;
	} else
#endif
	if ((sec->mfp_opt == MFP_REQUIRED && mfp_opt == MFP_NO) || mfp_opt == MFP_INVALID) 
		status = WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION;
	else if (sec->mfp_opt >= MFP_OPTIONAL && mfp_opt >= MFP_OPTIONAL)
		sta->flags |= WLAN_STA_MFP;

#ifdef CONFIG_IOCTL_CFG80211
	if (MLME_IS_AP(adapter) &&
		(sec->auth_type == NL80211_AUTHTYPE_SAE) &&
		(CHECK_BIT(WLAN_AKM_TYPE_SAE, sta->akm_suite_type)) &&
		(WLAN_AUTH_OPEN == sta->authalg)) {
		/* WPA3-SAE, PMK caching */
		if (rtw_cached_pmkid(adapter, sta->cmn.mac_addr) == -1) {
			RTW_INFO("SAE: No PMKSA cache entry found\n");
			status = WLAN_STATUS_INVALID_PMKID;
		} else {
			RTW_INFO("SAE: PMKSA cache entry found\n");
		}
	}
#endif

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

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(adapter)) {
		/* QoS is mandatory in mesh */
		sta->flags |= WLAN_STA_WME;
	}
#endif

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

		if (elems->ht_operation && elems->ht_operation_len == HT_OP_IE_LEN) {
			_rtw_memcpy(sta->htpriv.ht_op, elems->ht_operation, HT_OP_IE_LEN);
			sta->htpriv.op_present = 1;
		}
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
	if (elems->vht_capabilities && elems->vht_capabilities_len == VHT_CAP_IE_LEN) {
		sta->flags |= WLAN_STA_VHT;
		_rtw_memcpy(sta->vhtpriv.vht_cap, elems->vht_capabilities, VHT_CAP_IE_LEN);

		if (elems->vht_operation && elems->vht_operation_len== VHT_OP_IE_LEN) {
			_rtw_memcpy(sta->vhtpriv.vht_op, elems->vht_operation, VHT_OP_IE_LEN);
			sta->vhtpriv.op_present = 1;
		}

		if (elems->vht_op_mode_notify && elems->vht_op_mode_notify_len == 1) {
			_rtw_memcpy(&sta->vhtpriv.vht_op_mode_notify, elems->vht_op_mode_notify, 1);
			sta->vhtpriv.notify_present = 1;
		}
	}
exit:
#endif

	return;
}
#endif /* CONFIG_AP_MODE */

