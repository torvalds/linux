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

#include <drv_types.h>


#ifdef CONFIG_AP_MODE

extern unsigned char	RTW_WPA_OUI[];
extern unsigned char 	WMM_OUI[];
extern unsigned char	WPS_OUI[];
extern unsigned char	P2P_OUI[];
extern unsigned char	WFD_OUI[];

void init_mlme_ap_info(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;	
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	

	_rtw_spinlock_init(&pmlmepriv->bcn_update_lock);	

	//for ACL 
	_rtw_init_queue(&pacl_list->acl_node_q);

	//pmlmeext->bstart_bss = _FALSE;

	start_ap_mode(padapter);
}

void free_mlme_ap_info(_adapter *padapter)
{
	_irqL irqL;
	struct sta_info *psta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//stop_ap_mode(padapter);

	pmlmepriv->update_bcn = _FALSE;
	pmlmeext->bstart_bss = _FALSE;	
	
	rtw_sta_flush(padapter);

	pmlmeinfo->state = _HW_STATE_NOLINK_;

	//free_assoc_sta_resources
	rtw_free_all_stainfo(padapter);

	//free bc/mc sta_info
	psta = rtw_get_bcmc_stainfo(padapter);	
	//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	rtw_free_stainfo(padapter, psta);
	//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	

	_rtw_spinlock_free(&pmlmepriv->bcn_update_lock);
	
}

static void update_BCNTIM(_adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork_mlmeext = &(pmlmeinfo->network);
	unsigned char *pie = pnetwork_mlmeext->IEs;

	//DBG_871X("%s\n", __FUNCTION__);
	
	//update TIM IE
	//if(pstapriv->tim_bitmap)
	if(_TRUE)
	{
		u8 *p, *dst_ie, *premainder_ie=NULL, *pbackup_remainder_ie=NULL;
		u16 tim_bitmap_le;
		uint offset, tmp_len, tim_ielen, tim_ie_offset, remainder_ielen;	
	
		tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);

		p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, _TIM_IE_, &tim_ielen, pnetwork_mlmeext->IELength - _FIXED_IE_LENGTH_);
		if (p != NULL && tim_ielen>0)
		{
			tim_ielen += 2;
			
			premainder_ie = p+tim_ielen;

			tim_ie_offset = (sint)(p -pie);
			
			remainder_ielen = pnetwork_mlmeext->IELength - tim_ie_offset - tim_ielen;

			//append TIM IE from dst_ie offset
			dst_ie = p;
		}
		else
		{
			tim_ielen = 0;

			//calucate head_len		
			offset = _FIXED_IE_LENGTH_;

			/* get ssid_ie len */
			p = rtw_get_ie(pie + _BEACON_IE_OFFSET_, _SSID_IE_, &tmp_len, (pnetwork_mlmeext->IELength - _BEACON_IE_OFFSET_));
			if (p != NULL)
				offset += tmp_len+2;

			// get supported rates len
			p = rtw_get_ie(pie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &tmp_len, (pnetwork_mlmeext->IELength - _BEACON_IE_OFFSET_));	
			if (p !=  NULL) 
			{			
				offset += tmp_len+2;
			}

			//DS Parameter Set IE, len=3	
			offset += 3;

			premainder_ie = pie + offset;

			remainder_ielen = pnetwork_mlmeext->IELength - offset - tim_ielen;	

			//append TIM IE from offset
			dst_ie = pie + offset;
			
		}

		
		if(remainder_ielen>0)
		{
			pbackup_remainder_ie = rtw_malloc(remainder_ielen);
			if(pbackup_remainder_ie && premainder_ie)
				_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
		}		
		
		*dst_ie++=_TIM_IE_;

		if((pstapriv->tim_bitmap&0xff00) && (pstapriv->tim_bitmap&0x00fe))			
			tim_ielen = 5;
		else
			tim_ielen = 4;

		*dst_ie++= tim_ielen;
		
		*dst_ie++=0;//DTIM count
		*dst_ie++=1;//DTIM peroid
		
		if(pstapriv->tim_bitmap&BIT(0))//for bc/mc frames
			*dst_ie++ = BIT(0);//bitmap ctrl 
		else
			*dst_ie++ = 0;

		if(tim_ielen==4)
		{
			u8 pvb=0;
			
			if(pstapriv->tim_bitmap&0x00fe)
				pvb = (u8)tim_bitmap_le;
			else if(pstapriv->tim_bitmap&0xff00)			
				pvb = (u8)(tim_bitmap_le>>8);
			else
				pvb = (u8)tim_bitmap_le;

			*dst_ie++ = pvb;
			
		}	
		else if(tim_ielen==5)
		{
			_rtw_memcpy(dst_ie, &tim_bitmap_le, 2);
			dst_ie+=2;				
		}	
		
		//copy remainder IE
		if(pbackup_remainder_ie)
		{
			_rtw_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

			rtw_mfree(pbackup_remainder_ie, remainder_ielen);
		}	

		offset =  (uint)(dst_ie - pie);
		pnetwork_mlmeext->IELength = offset + remainder_ielen;
	
	}
}

void rtw_add_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index, u8 *data, u8 len)
{
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8	bmatch = _FALSE;
	u8	*pie = pnetwork->IEs;
	u8	*p=NULL, *dst_ie=NULL, *premainder_ie=NULL, *pbackup_remainder_ie=NULL;
	u32	i, offset, ielen, ie_offset, remainder_ielen = 0;

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pnetwork->IELength;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pnetwork->IEs + i);

		if (pIE->ElementID > index)
		{
			break;
		}
		else if(pIE->ElementID == index) // already exist the same IE
		{
			p = (u8 *)pIE;
			ielen = pIE->Length;
			bmatch = _TRUE;
			break;
		}

		p = (u8 *)pIE;
		ielen = pIE->Length;
		i += (pIE->Length + 2);
	}

	if (p != NULL && ielen>0)
	{
		ielen += 2;
		
		premainder_ie = p+ielen;

		ie_offset = (sint)(p -pie);
		
		remainder_ielen = pnetwork->IELength - ie_offset - ielen;

		if(bmatch)
			dst_ie = p;
		else
			dst_ie = (p+ielen);
	}

	if(dst_ie == NULL)
		return;

	if(remainder_ielen>0)
	{
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if(pbackup_remainder_ie && premainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	*dst_ie++=index;
	*dst_ie++=len;

	_rtw_memcpy(dst_ie, data, len);
	dst_ie+=len;

	//copy remainder IE
	if(pbackup_remainder_ie)
	{
		_rtw_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		rtw_mfree(pbackup_remainder_ie, remainder_ielen);
	}

	offset =  (uint)(dst_ie - pie);
	pnetwork->IELength = offset + remainder_ielen;
}

void rtw_remove_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index)
{
	u8 *p, *dst_ie=NULL, *premainder_ie=NULL, *pbackup_remainder_ie=NULL;
	uint offset, ielen, ie_offset, remainder_ielen = 0;
	u8	*pie = pnetwork->IEs;

	p = rtw_get_ie(pie + _FIXED_IE_LENGTH_, index, &ielen, pnetwork->IELength - _FIXED_IE_LENGTH_);
	if (p != NULL && ielen>0)
	{
		ielen += 2;
		
		premainder_ie = p+ielen;

		ie_offset = (sint)(p -pie);
		
		remainder_ielen = pnetwork->IELength - ie_offset - ielen;

		dst_ie = p;
	}
	else {
		return;
	}

	if(remainder_ielen>0)
	{
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if(pbackup_remainder_ie && premainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	//copy remainder IE
	if(pbackup_remainder_ie)
	{
		_rtw_memcpy(dst_ie, pbackup_remainder_ie, remainder_ielen);

		rtw_mfree(pbackup_remainder_ie, remainder_ielen);
	}

	offset =  (uint)(dst_ie - pie);
	pnetwork->IELength = offset + remainder_ielen;
}


u8 chk_sta_is_alive(struct sta_info *psta);
u8 chk_sta_is_alive(struct sta_info *psta)
{
	u8 ret = _FALSE;
	#ifdef DBG_EXPIRATION_CHK
	DBG_871X("sta:"MAC_FMT", rssi:%d, rx:"STA_PKTS_FMT", expire_to:%u, %s%ssq_len:%u\n"
		, MAC_ARG(psta->hwaddr)
		, psta->rssi_stat.UndecoratedSmoothedPWDB
		//, STA_RX_PKTS_ARG(psta)
		, STA_RX_PKTS_DIFF_ARG(psta)
		, psta->expire_to
		, psta->state&WIFI_SLEEP_STATE?"PS, ":""
		, psta->state&WIFI_STA_ALIVE_CHK_STATE?"SAC, ":""
		, psta->sleepq_len
	);
	#endif

	//if(sta_last_rx_pkts(psta) == sta_rx_pkts(psta))
	if((psta->sta_stats.last_rx_data_pkts + psta->sta_stats.last_rx_ctrl_pkts) == (psta->sta_stats.rx_data_pkts + psta->sta_stats.rx_ctrl_pkts))
	{
		#if 0
		if(psta->state&WIFI_SLEEP_STATE)
			ret = _TRUE;
		#endif
	}
	else
	{
		ret = _TRUE;
	}

	sta_update_last_rx_pkts(psta);

	return ret;
}

void	expire_timeout_chk(_adapter *padapter)
{
	_irqL irqL;
	_list	*phead, *plist;
	u8 updated = _FALSE;
	struct sta_info *psta=NULL;	
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;


	_enter_critical_bh(&pstapriv->auth_list_lock, &irqL);
	
	phead = &pstapriv->auth_list;
	plist = get_next(phead);
	
	//check auth_queue
	#ifdef DBG_EXPIRATION_CHK
	if (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		DBG_871X(FUNC_NDEV_FMT" auth_list, cnt:%u\n"
			, FUNC_NDEV_ARG(padapter->pnetdev), pstapriv->auth_list_cnt);
	}
	#endif
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, auth_list);

		plist = get_next(plist);


#ifdef CONFIG_ATMEL_RC_PATCH
		if (_TRUE == _rtw_memcmp((void *)(pstapriv->atmel_rc_pattern), (void *)(psta->hwaddr), ETH_ALEN))
			continue;
		if (psta->flag_atmel_rc)
			continue;
#endif
		if(psta->expire_to>0)
		{
			psta->expire_to--;
			if (psta->expire_to == 0)
			{
				rtw_list_delete(&psta->auth_list);
				pstapriv->auth_list_cnt--;
				
				DBG_871X("auth expire %02X%02X%02X%02X%02X%02X\n",
					psta->hwaddr[0],psta->hwaddr[1],psta->hwaddr[2],psta->hwaddr[3],psta->hwaddr[4],psta->hwaddr[5]);
				
				_exit_critical_bh(&pstapriv->auth_list_lock, &irqL);
				
				//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
				rtw_free_stainfo(padapter, psta);
				//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
				
				_enter_critical_bh(&pstapriv->auth_list_lock, &irqL);
			}	
		}	
		
	}

	_exit_critical_bh(&pstapriv->auth_list_lock, &irqL);
	psta = NULL;
	

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	//check asoc_queue
	#ifdef DBG_EXPIRATION_CHK
	if (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		DBG_871X(FUNC_NDEV_FMT" asoc_list, cnt:%u\n"
			, FUNC_NDEV_ARG(padapter->pnetdev), pstapriv->asoc_list_cnt);
	}
	#endif
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);
#ifdef CONFIG_ATMEL_RC_PATCH
		DBG_871X("%s:%d  psta=%p, %02x,%02x||%02x,%02x  \n\n", __func__,  __LINE__,
			psta,pstapriv->atmel_rc_pattern[0], pstapriv->atmel_rc_pattern[5], psta->hwaddr[0], psta->hwaddr[5]);
		if (_TRUE == _rtw_memcmp((void *)pstapriv->atmel_rc_pattern, (void *)(psta->hwaddr), ETH_ALEN))
			continue;		
		if (psta->flag_atmel_rc)
			continue;
		DBG_871X("%s: debug line:%d \n", __func__, __LINE__);
#endif
#ifdef CONFIG_AUTO_AP_MODE
		if(psta->isrc)
			continue;
#endif
		if (chk_sta_is_alive(psta) || !psta->expire_to) {
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
			#ifdef CONFIG_TX_MCAST2UNI
			psta->under_exist_checking = 0;
			#endif	// CONFIG_TX_MCAST2UNI
		} else {
			psta->expire_to--;
		}

#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#ifdef CONFIG_80211N_HT
#ifdef CONFIG_TX_MCAST2UNI
		if ( (psta->flags & WLAN_STA_HT) && (psta->htpriv.agg_enable_bitmap || psta->under_exist_checking) ) {
			// check sta by delba(addba) for 11n STA 
			// ToDo: use CCX report to check for all STAs
			//DBG_871X("asoc check by DELBA/ADDBA! (pstapriv->expire_to=%d s)(psta->expire_to=%d s), [%02x, %d]\n", pstapriv->expire_to*2, psta->expire_to*2, psta->htpriv.agg_enable_bitmap, psta->under_exist_checking);
			
				if ( psta->expire_to <= (pstapriv->expire_to - 50 ) ) {
				DBG_871X("asoc expire by DELBA/ADDBA! (%d s)\n", (pstapriv->expire_to-psta->expire_to)*2);
				psta->under_exist_checking = 0;
				psta->expire_to = 0;
			} else if ( psta->expire_to <= (pstapriv->expire_to - 3) && (psta->under_exist_checking==0)) {
				DBG_871X("asoc check by DELBA/ADDBA! (%d s)\n", (pstapriv->expire_to-psta->expire_to)*2);
				psta->under_exist_checking = 1;
				//tear down TX AMPDU
				send_delba(padapter, 1, psta->hwaddr);// // originator
				psta->htpriv.agg_enable_bitmap = 0x0;//reset
				psta->htpriv.candidate_tid_bitmap = 0x0;//reset
			}
		}
#endif //CONFIG_TX_MCAST2UNI
#endif //CONFIG_80211N_HT
#endif //CONFIG_ACTIVE_KEEP_ALIVE_CHECK

		if (psta->expire_to <= 0)
		{
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			if (padapter->registrypriv.wifi_spec == 1)
			{
				psta->expire_to = pstapriv->expire_to;
				continue;
			}

#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#ifdef CONFIG_80211N_HT

#define KEEP_ALIVE_TRYCNT (3)

			if(psta->keep_alive_trycnt > 0 && psta->keep_alive_trycnt <= KEEP_ALIVE_TRYCNT)
			{				
				if(psta->state & WIFI_STA_ALIVE_CHK_STATE)
					psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
				else
					psta->keep_alive_trycnt = 0;
				
			}
			else if((psta->keep_alive_trycnt > KEEP_ALIVE_TRYCNT) && !(psta->state & WIFI_STA_ALIVE_CHK_STATE))
			{
				psta->keep_alive_trycnt = 0;
			}			
			if((psta->htpriv.ht_option==_TRUE) && (psta->htpriv.ampdu_enable==_TRUE)) 
			{
				uint priority = 1; //test using BK
				u8 issued=0;				
		
				//issued = (psta->htpriv.agg_enable_bitmap>>priority)&0x1;
				issued |= (psta->htpriv.candidate_tid_bitmap>>priority)&0x1;

				if(0==issued)
				{
					if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE))
					{
						psta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);

						if (psta->state & WIFI_SLEEP_STATE) 
							psta->expire_to = 2; // 2x2=4 sec
						else
							psta->expire_to = 1; // 2 sec
					
						psta->state |= WIFI_STA_ALIVE_CHK_STATE;
					
						//add_ba_hdl(padapter, (u8*)paddbareq_parm);

						DBG_871X("issue addba_req to check if sta alive, keep_alive_trycnt=%d\n", psta->keep_alive_trycnt);
						
						issue_action_BA(padapter, psta->hwaddr, RTW_WLAN_ACTION_ADDBA_REQ, (u16)priority);		
		
						_set_timer(&psta->addba_retry_timer, ADDBA_TO);
						
						psta->keep_alive_trycnt++;						

						continue;
					}			
				}					
			}
			if(psta->keep_alive_trycnt > 0 && psta->state & WIFI_STA_ALIVE_CHK_STATE)
			{
				psta->keep_alive_trycnt = 0;
				psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
				DBG_871X("change to another methods to check alive if staion is at ps mode\n");
			}	
			
#endif //CONFIG_80211N_HT
#endif //CONFIG_ACTIVE_KEEP_ALIVE_CHECK	
			if (psta->state & WIFI_SLEEP_STATE) {
				if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
					//to check if alive by another methods if staion is at ps mode.					
					psta->expire_to = pstapriv->expire_to;
					psta->state |= WIFI_STA_ALIVE_CHK_STATE;

					//DBG_871X("alive chk, sta:" MAC_FMT " is at ps mode!\n", MAC_ARG(psta->hwaddr));

					//to update bcn with tim_bitmap for this station
					pstapriv->tim_bitmap |= BIT(psta->aid);
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE);

					if(!pmlmeext->active_keep_alive_check)
						continue;
				}
			}
			#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
			if (pmlmeext->active_keep_alive_check) {
				int stainfo_offset;

				stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
				if (stainfo_offset_valid(stainfo_offset)) {
					chk_alive_list[chk_alive_num++] = stainfo_offset;
				}

				continue;
			}
			#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */
			rtw_list_delete(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			DBG_871X("asoc expire "MAC_FMT", state=0x%x\n", MAC_ARG(psta->hwaddr), psta->state);
			updated = ap_free_sta(padapter, psta, _FALSE, WLAN_REASON_DEAUTH_LEAVING);
		}	
		else
		{
			/* TODO: Aging mechanism to digest frames in sleep_q to avoid running out of xmitframe */
			if (psta->sleepq_len > (NR_XMITFRAME/pstapriv->asoc_list_cnt)
				&& padapter->xmitpriv.free_xmitframe_cnt < ((NR_XMITFRAME/pstapriv->asoc_list_cnt)/2)
			){
				DBG_871X("%s sta:"MAC_FMT", sleepq_len:%u, free_xmitframe_cnt:%u, asoc_list_cnt:%u, clear sleep_q\n", __func__
					, MAC_ARG(psta->hwaddr)
					, psta->sleepq_len, padapter->xmitpriv.free_xmitframe_cnt, pstapriv->asoc_list_cnt);
				wakeup_sta_to_xmit(padapter, psta);
			}
		}
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
if (chk_alive_num) {

	u8 backup_oper_channel=0;
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
#ifdef CONFIG_ATMEL_RC_PATCH
		if (_TRUE == _rtw_memcmp(  pstapriv->atmel_rc_pattern, psta->hwaddr, ETH_ALEN))
			continue;
		if (psta->flag_atmel_rc)
			continue;
#endif
		if(!(psta->state &_FW_LINKED))
			continue;		
	
		if (psta->state & WIFI_SLEEP_STATE)
			ret = issue_nulldata(padapter, psta->hwaddr, 0, 1, 50);
		else
			ret = issue_nulldata(padapter, psta->hwaddr, 0, 3, 50);

		psta->keep_alive_trycnt++;
		if (ret == _SUCCESS)
		{
			DBG_871X("asoc check, sta(" MAC_FMT ") is alive\n", MAC_ARG(psta->hwaddr));
			psta->expire_to = pstapriv->expire_to;
			psta->keep_alive_trycnt = 0;
			continue;
		}
		else if (psta->keep_alive_trycnt <= 3)
		{
			DBG_871X("ack check for asoc expire, keep_alive_trycnt=%d\n", psta->keep_alive_trycnt);
			psta->expire_to = 1;
			continue;
		}

		psta->keep_alive_trycnt = 0;
		DBG_871X("asoc expire "MAC_FMT", state=0x%x\n", MAC_ARG(psta->hwaddr), psta->state);
		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		if (rtw_is_list_empty(&psta->asoc_list)==_FALSE) {
			rtw_list_delete(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta(padapter, psta, _FALSE, WLAN_REASON_DEAUTH_LEAVING);
		}
		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	}

	if (backup_oper_channel>0) /* back to the original operation channel */
		SelectChannel(padapter, backup_oper_channel);
}
#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */

	associated_clients_update(padapter, updated);
}

void add_RATid(_adapter *padapter, struct sta_info *psta, u8 rssi_level)
{	
	int i;
	u8 rf_type;
	unsigned char sta_band = 0, shortGIrate = _FALSE;
	unsigned int tx_ra_bitmap=0;
	struct ht_priv	*psta_ht = NULL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;

#ifdef CONFIG_80211N_HT
	if(psta)
		psta_ht = &psta->htpriv;
	else
		return;
#endif //CONFIG_80211N_HT

	if(!(psta->state & _FW_LINKED))
		return;

#if 0//gtest
	if(get_rf_mimo_mode(padapter) == RTL8712_RF_2T2R)
	{
		//is this a 2r STA?
		if((pstat->tx_ra_bitmap & 0x0ff00000) != 0 && !(priv->pshare->has_2r_sta & BIT(pstat->aid)))
		{
			priv->pshare->has_2r_sta |= BIT(pstat->aid);
			if(rtw_read16(padapter, 0x102501f6) != 0xffff)
			{
				rtw_write16(padapter, 0x102501f6, 0xffff);
				reset_1r_sta_RA(priv, 0xffff);
				Switch_1SS_Antenna(priv, 3);
			}
		}
		else// bg or 1R STA? 
		{ 
			if((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N) && pstat->ht_cap_len && priv->pshare->has_2r_sta == 0)
			{
				if(rtw_read16(padapter, 0x102501f6) != 0x7777)
				{ // MCS7 SGI
					rtw_write16(padapter, 0x102501f6,0x7777);
					reset_1r_sta_RA(priv, 0x7777);
					Switch_1SS_Antenna(priv, 2);
				}
			}
		}
		
	}

	if ((pstat->rssi_level < 1) || (pstat->rssi_level > 3)) 
	{
		if (pstat->rssi >= priv->pshare->rf_ft_var.raGoDownUpper)
			pstat->rssi_level = 1;
		else if ((pstat->rssi >= priv->pshare->rf_ft_var.raGoDown20MLower) ||
			((priv->pshare->is_40m_bw) && (pstat->ht_cap_len) &&
			(pstat->rssi >= priv->pshare->rf_ft_var.raGoDown40MLower) &&
			(pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_SUPPORT_CH_WDTH_))))
			pstat->rssi_level = 2;
		else
			pstat->rssi_level = 3;
	}

	// rate adaptive by rssi
	if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N) && pstat->ht_cap_len)
	{
		if ((get_rf_mimo_mode(priv) == MIMO_1T2R) || (get_rf_mimo_mode(priv) == MIMO_1T1R))
		{
			switch (pstat->rssi_level) {
				case 1:
					pstat->tx_ra_bitmap &= 0x100f0000;
					break;
				case 2:
					pstat->tx_ra_bitmap &= 0x100ff000;
					break;
				case 3:
					if (priv->pshare->is_40m_bw)
						pstat->tx_ra_bitmap &= 0x100ff005;
					else
						pstat->tx_ra_bitmap &= 0x100ff001;

					break;
			}
		}
		else 
		{
			switch (pstat->rssi_level) {
				case 1:
					pstat->tx_ra_bitmap &= 0x1f0f0000;
					break;
				case 2:
					pstat->tx_ra_bitmap &= 0x1f0ff000;
					break;
				case 3:
					if (priv->pshare->is_40m_bw)
						pstat->tx_ra_bitmap &= 0x000ff005;
					else
						pstat->tx_ra_bitmap &= 0x000ff001;

					break;
			}

			// Don't need to mask high rates due to new rate adaptive parameters
			//if (pstat->is_broadcom_sta)		// use MCS12 as the highest rate vs. Broadcom sta
			//	pstat->tx_ra_bitmap &= 0x81ffffff;

			// NIC driver will report not supporting MCS15 and MCS14 in asoc req
			//if (pstat->is_rtl8190_sta && !pstat->is_2t_mimo_sta)
			//	pstat->tx_ra_bitmap &= 0x83ffffff;		// if Realtek 1x2 sta, don't use MCS15 and MCS14
		}
	}
	else if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11G) && isErpSta(pstat))
	{
		switch (pstat->rssi_level) {
			case 1:
				pstat->tx_ra_bitmap &= 0x00000f00;
				break;
			case 2:
				pstat->tx_ra_bitmap &= 0x00000ff0;
				break;
			case 3:
				pstat->tx_ra_bitmap &= 0x00000ff5;
				break;
		}
	}
	else 
	{
		pstat->tx_ra_bitmap &= 0x0000000d;
	}

	// disable tx short GI when station cannot rx MCS15(AP is 2T2R)
	// disable tx short GI when station cannot rx MCS7 (AP is 1T2R or 1T1R)
	// if there is only 1r STA and we are 2T2R, DO NOT mask SGI rate
	if ((!(pstat->tx_ra_bitmap & 0x8000000) && (priv->pshare->has_2r_sta > 0) && (get_rf_mimo_mode(padapter) == RTL8712_RF_2T2R)) ||
		 (!(pstat->tx_ra_bitmap & 0x80000) && (get_rf_mimo_mode(padapter) != RTL8712_RF_2T2R)))
	{
		pstat->tx_ra_bitmap &= ~BIT(28);	
	}
#endif

	rtw_hal_update_sta_rate_mask(padapter, psta);
	tx_ra_bitmap = psta->ra_mask;

	shortGIrate = query_ra_short_GI(psta);

	if ( pcur_network->Configuration.DSConfig > 14 ) {
		
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_5N ;

		if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11A;

		// 5G band
		#ifdef CONFIG_80211AC_VHT
		if (psta->vhtpriv.vht_option)  {
			sta_band = WIRELESS_11_5AC;
		}		
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
	psta->raid = rtw_hal_networktype_to_raid(padapter, psta);
	
	if (psta->aid < NUM_STA) 
	{
		u8	arg[4] = {0};

		arg[0] = psta->mac_id;
		arg[1] = psta->raid;
		arg[2] = shortGIrate;
		arg[3] = psta->init_rate;

		DBG_871X("%s=> mac_id:%d , raid:%d , shortGIrate=%d, bitmap=0x%x\n", 
			__FUNCTION__ , psta->mac_id, psta->raid ,shortGIrate, tx_ra_bitmap);

		rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, rssi_level);
	}
	else 
	{
		DBG_871X("station aid %d exceed the max number\n", psta->aid);
	}

}

void update_bmc_sta(_adapter *padapter)
{
	_irqL	irqL;
	unsigned char	network_type;
	int supportRateNum = 0;
	unsigned int tx_ra_bitmap=0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;	
	struct sta_info *psta = rtw_get_bcmc_stainfo(padapter);

	if(psta)
	{
		psta->aid = 0;//default set to 0
		//psta->mac_id = psta->aid+4;	
		psta->mac_id = psta->aid + 1;//mac_id=1 for bc/mc stainfo

		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

		psta->qos_option = 0;
#ifdef CONFIG_80211N_HT	
		psta->htpriv.ht_option = _FALSE;
#endif //CONFIG_80211N_HT

		psta->ieee8021x_blocked = 0;

		_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		//psta->dot118021XPrivacy = _NO_PRIVACY_;//!!! remove it, because it has been set before this.

		//prepare for add_RATid		
		supportRateNum = rtw_get_rateset_len((u8*)&pcur_network->SupportedRates);
		network_type = rtw_check_network_type((u8*)&pcur_network->SupportedRates, supportRateNum, pcur_network->Configuration.DSConfig);
		if (IsSupportedTxCCK(network_type)) {
			network_type = WIRELESS_11B;
		}
		else if (network_type == WIRELESS_INVALID) { // error handling
			if ( pcur_network->Configuration.DSConfig > 14 )
				network_type = WIRELESS_11A;
			else
				network_type = WIRELESS_11B;
		}
		update_sta_basic_rate(psta, network_type);
		psta->wireless_mode = network_type;

		rtw_hal_update_sta_rate_mask(padapter, psta);
		tx_ra_bitmap = psta->ra_mask;

		psta->raid = rtw_hal_networktype_to_raid(padapter,psta);

		//ap mode
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

		//if(pHalData->fw_ractrl == _TRUE)
		{
			u8	arg[4] = {0};

			arg[0] = psta->mac_id;
			arg[1] = psta->raid;
			arg[2] = 0;
			arg[3] = psta->init_rate;

			DBG_871X("%s=> mac_id:%d , raid:%d , bitmap=0x%x\n", 
				__FUNCTION__ , psta->mac_id, psta->raid , tx_ra_bitmap);

			rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, 0);
		}

		rtw_sta_media_status_rpt(padapter, psta, 1);

		_enter_critical_bh(&psta->lock, &irqL);
		psta->state = _FW_LINKED;
		_exit_critical_bh(&psta->lock, &irqL);

	}
	else
	{
		DBG_871X("add_RATid_bmc_sta error!\n");
	}
		
}

//notes:
//AID: 1~MAX for sta and 0 for bc/mc in ap/adhoc mode 
//MAC_ID = AID+1 for sta in ap/adhoc mode 
//MAC_ID = 1 for bc/mc for sta/ap/adhoc
//MAC_ID = 0 for bssid for sta/ap/adhoc
//CAM_ID = //0~3 for default key, cmd_id=macid + 3, macid=aid+1;

void update_sta_info_apmode(_adapter *padapter, struct sta_info *psta)
{	
	_irqL	irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
#ifdef CONFIG_80211N_HT
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv	*phtpriv_sta = &psta->htpriv;
#endif //CONFIG_80211N_HT
	u8	cur_ldpc_cap=0, cur_stbc_cap=0, cur_beamform_cap=0;
	//set intf_tag to if1
	//psta->intf_tag = 0;

        DBG_871X("%s\n",__FUNCTION__);

	//psta->mac_id = psta->aid+4;
	//psta->mac_id = psta->aid+1;//alloc macid when call rtw_alloc_stainfo(),
		                                       //release macid when call rtw_free_stainfo()

	//ap mode
	rtw_hal_set_odm_var(padapter,HAL_ODM_STA_INFO,psta,_TRUE);
	
	if(psecuritypriv->dot11AuthAlgrthm==dot11AuthAlgrthm_8021X)
		psta->ieee8021x_blocked = _TRUE;
	else
		psta->ieee8021x_blocked = _FALSE;
	

	//update sta's cap
	
	//ERP
	VCS_update(padapter, psta);
#ifdef CONFIG_80211N_HT	
	//HT related cap
	if(phtpriv_sta->ht_option)
	{
		//check if sta supports rx ampdu
		phtpriv_sta->ampdu_enable = phtpriv_ap->ampdu_enable;

		//check if sta support s Short GI 20M
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
		{
			phtpriv_sta->sgi_20m = _TRUE;
		}
		//check if sta support s Short GI 40M
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_40))
		{
			phtpriv_sta->sgi_40m = _TRUE;
		}

		// bwmode
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH))
		{
			phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;
			psta->bw_mode = CHANNEL_WIDTH_40;
		}
		else
		{
			psta->bw_mode = CHANNEL_WIDTH_20;
		}

		psta->qos_option = _TRUE;

		// B0 Config LDPC Coding Capability
		if (TEST_FLAG(phtpriv_ap->ldpc_cap, LDPC_HT_ENABLE_TX) && 
			GET_HT_CAPABILITY_ELE_LDPC_CAP((u8 *)(&phtpriv_sta->ht_cap)))
		{
			SET_FLAG(cur_ldpc_cap, (LDPC_HT_ENABLE_TX | LDPC_HT_CAP_TX));
			DBG_871X("Enable HT Tx LDPC for STA(%d)\n",psta->aid);
		}

		// B7 B8 B9 Config STBC setting
		if (TEST_FLAG(phtpriv_ap->stbc_cap, STBC_HT_ENABLE_TX) &&
			GET_HT_CAPABILITY_ELE_RX_STBC((u8 *)(&phtpriv_sta->ht_cap)))
		{
			SET_FLAG(cur_stbc_cap, (STBC_HT_ENABLE_TX | STBC_HT_CAP_TX) );
			DBG_871X("Enable HT Tx STBC for STA(%d)\n",psta->aid);
		}

#ifdef CONFIG_BEAMFORMING
		// Config Tx beamforming setting
		if (TEST_FLAG(phtpriv_ap->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) && 
			GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP((u8 *)(&phtpriv_sta->ht_cap)))
		{
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
		}

		if (TEST_FLAG(phtpriv_ap->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
			GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP((u8 *)(&phtpriv_sta->ht_cap)))
		{
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
		}

		if (cur_beamform_cap) {
			DBG_871X("Client STA(%d) HT Beamforming Cap = 0x%02X\n", psta->aid, cur_beamform_cap);
		}
#endif //CONFIG_BEAMFORMING
	}
	else
	{
		phtpriv_sta->ampdu_enable = _FALSE;
		
		phtpriv_sta->sgi_20m = _FALSE;
		phtpriv_sta->sgi_40m = _FALSE;
		psta->bw_mode = CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	phtpriv_sta->ldpc_cap = cur_ldpc_cap;
	phtpriv_sta->stbc_cap = cur_stbc_cap;
	phtpriv_sta->beamform_cap = cur_beamform_cap;

	//Rx AMPDU
	send_delba(padapter, 0, psta->hwaddr);// recipient
	
	//TX AMPDU
	send_delba(padapter, 1, psta->hwaddr);// // originator
	phtpriv_sta->agg_enable_bitmap = 0x0;//reset
	phtpriv_sta->candidate_tid_bitmap = 0x0;//reset
#endif //CONFIG_80211N_HT

#ifdef CONFIG_80211AC_VHT
	update_sta_vht_info_apmode(padapter, psta);
#endif

	update_ldpc_stbc_cap(psta);

	//todo: init other variables
	
	_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));


	//add ratid
	//add_RATid(padapter, psta);//move to ap_sta_info_defer_update()


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
#endif //CONFIG_80211N_HT


	psta->wireless_mode = pmlmeext->cur_wireless_mode;

	psta->bssratelen = rtw_get_rateset_len(pnetwork->SupportedRates);
	_rtw_memcpy(psta->bssrateset, pnetwork->SupportedRates, psta->bssratelen);

#ifdef CONFIG_80211N_HT	
	//HT related cap
	if(phtpriv_ap->ht_option)
	{
		//check if sta supports rx ampdu
		//phtpriv_ap->ampdu_enable = phtpriv_ap->ampdu_enable;

		//check if sta support s Short GI 20M
		if((phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
		{
			phtpriv_ap->sgi_20m = _TRUE;
		}
		//check if sta support s Short GI 40M
		if((phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_40))
		{
			phtpriv_ap->sgi_40m = _TRUE;
		}

		psta->qos_option = _TRUE;
	}
	else
	{
		phtpriv_ap->ampdu_enable = _FALSE;
		
		phtpriv_ap->sgi_20m = _FALSE;
		phtpriv_ap->sgi_40m = _FALSE;
	}

	psta->bw_mode = pmlmeext->cur_bwmode;
	phtpriv_ap->ch_offset = pmlmeext->cur_ch_offset;

	phtpriv_ap->agg_enable_bitmap = 0x0;//reset
	phtpriv_ap->candidate_tid_bitmap = 0x0;//reset

	_rtw_memcpy(&psta->htpriv, &pmlmepriv->htpriv, sizeof(struct ht_priv));

#ifdef CONFIG_80211AC_VHT
	_rtw_memcpy(&psta->vhtpriv, &pmlmepriv->vhtpriv, sizeof(struct vht_priv));
#endif //CONFIG_80211AC_VHT

#endif //CONFIG_80211N_HT
}

static void update_hw_ht_param(_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	DBG_871X("%s\n", __FUNCTION__);
	

	//handle A-MPDU parameter field
	/* 	
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing	
	*/
	max_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;	
	
	min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) >> 2;	

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MIN_SPACE, (u8 *)(&min_MPDU_spacing));

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));

	//
	// Config SM Power Save setting
	//
	pmlmeinfo->SM_PS = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & 0x0C) >> 2;
	if(pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
	{
		/*u8 i;
		//update the MCS rates
		for (i = 0; i < 16; i++)
		{
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		}*/
		DBG_871X("%s(): WLAN_HT_CAP_SM_PS_STATIC\n",__FUNCTION__);
	}

	//
	// Config current HT Protection mode.
	//
	//pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;

}

void start_bss_network(_adapter *padapter, u8 *pbuf)
{
	u8 *p;
	u8 val8, cur_channel, cur_bwmode, cur_ch_offset;
	u16 bcn_interval;
	u32	acparm;	
	int	ie_len;	
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv* psecuritypriv=&(padapter->securitypriv);	
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork_mlmeext = &(pmlmeinfo->network);
	struct HT_info_element *pht_info=NULL;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif //CONFIG_P2P
	u8	cbw40_enable=0;
	u8	change_band = _FALSE;
	
	//DBG_871X("%s\n", __FUNCTION__);

	bcn_interval = (u16)pnetwork->Configuration.BeaconPeriod;	
	cur_channel = pnetwork->Configuration.DSConfig;
	cur_bwmode = CHANNEL_WIDTH_20;
	cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	

	//check if there is wps ie, 
	//if there is wpsie in beacon, the hostapd will update beacon twice when stating hostapd,
	//and at first time the security ie ( RSN/WPA IE) will not include in beacon.
	if(NULL == rtw_get_wps_ie(pnetwork->IEs+_FIXED_IE_LENGTH_, pnetwork->IELength-_FIXED_IE_LENGTH_, NULL, NULL))
	{
		pmlmeext->bstart_bss = _TRUE;
	}

	//todo: update wmm, ht cap
	//pmlmeinfo->WMM_enable;
	//pmlmeinfo->HT_enable;
	if(pmlmepriv->qospriv.qos_option)
		pmlmeinfo->WMM_enable = _TRUE;
#ifdef CONFIG_80211N_HT
	if(pmlmepriv->htpriv.ht_option)
	{
		pmlmeinfo->WMM_enable = _TRUE;
		pmlmeinfo->HT_enable = _TRUE;
		//pmlmeinfo->HT_info_enable = _TRUE;
		//pmlmeinfo->HT_caps_enable = _TRUE;

		update_hw_ht_param(padapter);
	}
#endif //#CONFIG_80211N_HT

#ifdef CONFIG_80211AC_VHT
	if(pmlmepriv->vhtpriv.vht_option) {
		pmlmeinfo->VHT_enable = _TRUE;
		update_hw_vht_param(padapter);
	}
#endif //CONFIG_80211AC_VHT

	if(pmlmepriv->cur_network.join_res != _TRUE) //setting only at  first time
	{
		//WEP Key will be set before this function, do not clear CAM.
		if ((psecuritypriv->dot11PrivacyAlgrthm != _WEP40_) && (psecuritypriv->dot11PrivacyAlgrthm != _WEP104_))
			flush_all_cam_entry(padapter);	//clear CAM
	}	

	//set MSR to AP_Mode		
	Set_MSR(padapter, _HW_STATE_AP_);	
		
	//Set BSSID REG
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pnetwork->MacAddress);

	//Set EDCA param reg
#ifdef CONFIG_CONCURRENT_MODE
	acparm = 0x005ea42b;
#else
	acparm = 0x002F3217; // VO
#endif
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
	acparm = 0x005E4317; // VI
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
	//acparm = 0x00105320; // BE
	acparm = 0x005ea42b;
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
	acparm = 0x0000A444; // BK
	rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));

	//Set Security
	val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)? 0xcc: 0xcf;
	rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

	//Beacon Control related register
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&bcn_interval));

	rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, NULL);

	if(pmlmepriv->cur_network.join_res != _TRUE) //setting only at  first time
	{
		//u32 initialgain;

		//initialgain = 0x1e;


		//disable dynamic functions, such as high power, DIG
		//Save_DM_Func_Flag(padapter);
		//Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);
		
		//turn on all dynamic functions	
		Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, _TRUE);

		//rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
	
	}
#ifdef CONFIG_80211N_HT
	//set channel, bwmode	
	p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
	if( p && ie_len)
	{
		pht_info = (struct HT_info_element *)(p+2);

		if (cur_channel > 14) {
			if ((pregpriv->bw_mode & 0xf0) > 0)
				cbw40_enable = 1;
		} else {
			if ((pregpriv->bw_mode & 0x0f) > 0)
				cbw40_enable = 1;
		}

		if ((cbw40_enable) &&	 (pht_info->infos[0] & BIT(2)))
		{
			//switch to the 40M Hz mode
			//pmlmeext->cur_bwmode = CHANNEL_WIDTH_40;
			cur_bwmode = CHANNEL_WIDTH_40;
			switch (pht_info->infos[0] & 0x3)
			{
				case 1:
					//pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
					break;
			
				case 3:
					//pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;					
					break;
				
				default:
					//pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
					break;
			}		
						
		}
					
	}
#endif //CONFIG_80211N_HT

#ifdef CONFIG_80211AC_VHT
	p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_VHTOperation, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
	if( p && ie_len)
	{
		if(GET_VHT_OPERATION_ELE_CHL_WIDTH(p+2) >= 1) {
			cur_bwmode = CHANNEL_WIDTH_80;
		}
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_set_ap_channel_bandwidth(padapter, cur_channel, cur_ch_offset, cur_bwmode);
#else //!CONFIG_DUALMAC_CONCURRENT	
#ifdef CONFIG_CONCURRENT_MODE
	//TODO: need to judge the phy parameters on concurrent mode for single phy
	concurrent_set_ap_chbw(padapter, cur_channel, cur_ch_offset, cur_bwmode);
#else //!CONFIG_CONCURRENT_MODE
	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
	DBG_871X("CH=%d, BW=%d, offset=%d\n", cur_channel, cur_bwmode, cur_ch_offset);
	pmlmeext->cur_channel = cur_channel;	
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;
#endif //!CONFIG_CONCURRENT_MODE
#endif //!CONFIG_DUALMAC_CONCURRENT

	pmlmeext->cur_wireless_mode = pmlmepriv->cur_network.network_type;

	//let pnetwork_mlmeext == pnetwork_mlme.
	_rtw_memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);

	//update cur_wireless_mode
	update_wireless_mode(padapter);

	//update RRSR after set channel and bandwidth
	UpdateBrateTbl(padapter, pnetwork->SupportedRates);
	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, pnetwork->SupportedRates);

	//udpate capability after cur_wireless_mode updated
	update_capinfo(padapter, rtw_get_capability((WLAN_BSSID_EX *)pnetwork));


#ifdef CONFIG_P2P
	_rtw_memcpy(pwdinfo->p2p_group_ssid, pnetwork->Ssid.Ssid, pnetwork->Ssid.SsidLength);	
	pwdinfo->p2p_group_ssid_len = pnetwork->Ssid.SsidLength;
#endif //CONFIG_P2P

	if(_TRUE == pmlmeext->bstart_bss)
	{
		update_beacon(padapter, _TIM_IE_, NULL, _TRUE);

#ifndef CONFIG_INTERRUPT_BASED_TXBCN //other case will  tx beacon when bcn interrupt coming in.
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		//issue beacon frame
		if(send_beacon(padapter)==_FAIL)
		{
			DBG_871X("issue_beacon, fail!\n");
		}
#endif 
#endif //!CONFIG_INTERRUPT_BASED_TXBCN
		
	}


	//update bc/mc sta_info
	update_bmc_sta(padapter);
	
	//pmlmeext->bstart_bss = _TRUE;
	
}

int rtw_check_beacon_data(_adapter *padapter, u8 *pbuf,  int len)
{
	int ret=_SUCCESS;
	u8 *p;
	u8 *pHT_caps_ie=NULL;
	u8 *pHT_info_ie=NULL;
	struct sta_info *psta = NULL;
	u16 cap, ht_cap=_FALSE;
	uint ie_len = 0;
	int group_cipher, pairwise_cipher;	
	u8	channel, network_type, supportRate[NDIS_802_11_LENGTH_RATES_EX];
	int supportRateNum = 0;
	u8 OUI1[] = {0x00, 0x50, 0xf2,0x01};
	u8 wps_oui[4]={0x0,0x50,0xf2,0x04};
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};	
	struct registry_priv *pregistrypriv = &padapter->registrypriv;	
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pbss_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;	
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ie = pbss_network->IEs;
	u8 vht_cap=_FALSE;

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

	DBG_871X("%s, len=%d\n", __FUNCTION__, len);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return _FAIL;


	if(len>MAX_IE_SZ)
		return _FAIL;
	
	pbss_network->IELength = len;

	_rtw_memset(ie, 0, MAX_IE_SZ);
	
	_rtw_memcpy(ie, pbuf, pbss_network->IELength);


	if(pbss_network->InfrastructureMode!=Ndis802_11APMode)
		return _FAIL;

	pbss_network->Rssi = 0;

	_rtw_memcpy(pbss_network->MacAddress, myid(&(padapter->eeprompriv)), ETH_ALEN);
	
	//beacon interval
	p = rtw_get_beacon_interval_from_ie(ie);//ie + 8;	// 8: TimeStamp, 2: Beacon Interval 2:Capability
	//pbss_network->Configuration.BeaconPeriod = le16_to_cpu(*(unsigned short*)p);
	pbss_network->Configuration.BeaconPeriod = RTW_GET_LE16(p);
	
	//capability
	//cap = *(unsigned short *)rtw_get_capability_from_ie(ie);
	//cap = le16_to_cpu(cap);
	cap = RTW_GET_LE16(ie);

	//SSID
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SSID_IE_, &ie_len, (pbss_network->IELength -_BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		_rtw_memset(&pbss_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
		_rtw_memcpy(pbss_network->Ssid.Ssid, (p + 2), ie_len);
		pbss_network->Ssid.SsidLength = ie_len;
	}	

	//chnnel
	channel = 0;
	pbss_network->Configuration.Length = 0;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _DSSET_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
		channel = *(p + 2);

	pbss_network->Configuration.DSConfig = channel;

	
	_rtw_memset(supportRate, 0, NDIS_802_11_LENGTH_RATES_EX);
	// get supported rates
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));	
	if (p !=  NULL) 
	{
		_rtw_memcpy(supportRate, p+2, ie_len);	
		supportRateNum = ie_len;
	}
	
	//get ext_supported rates
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ie_len, pbss_network->IELength - _BEACON_IE_OFFSET_);	
	if (p !=  NULL)
	{
		_rtw_memcpy(supportRate+supportRateNum, p+2, ie_len);
		supportRateNum += ie_len;
	
	}

	network_type = rtw_check_network_type(supportRate, supportRateNum, channel);

	rtw_set_supported_rate(pbss_network->SupportedRates, network_type);


	//parsing ERP_IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		ERP_IE_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)p);
	}

	//update privacy/security
	if (cap & BIT(4))
		pbss_network->Privacy = 1;
	else
		pbss_network->Privacy = 0;

	psecuritypriv->wpa_psk = 0;

	//wpa2
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;	
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));		
	if(p && ie_len>0)
	{
		if(rtw_parse_wpa2_ie(p, ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS)
		{
			psecuritypriv->dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
			
			psecuritypriv->dot8021xalg = 1;//psk,  todo:802.1x
			psecuritypriv->wpa_psk |= BIT(1);

			psecuritypriv->wpa2_group_cipher = group_cipher;
			psecuritypriv->wpa2_pairwise_cipher = pairwise_cipher;
#if 0
			switch(group_cipher)
			{
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

			switch(pairwise_cipher)
			{
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

	//wpa
	ie_len = 0;
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;	
	for (p = ie + _BEACON_IE_OFFSET_; ;p += (ie_len + 2))
	{
		p = rtw_get_ie(p, _SSN_IE_1_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));		
		if ((p) && (_rtw_memcmp(p+2, OUI1, 4)))
		{
			if(rtw_parse_wpa_ie(p, ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS)
			{
				psecuritypriv->dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
				
				psecuritypriv->dot8021xalg = 1;//psk,  todo:802.1x

				psecuritypriv->wpa_psk |= BIT(0);

				psecuritypriv->wpa_group_cipher = group_cipher;
				psecuritypriv->wpa_pairwise_cipher = pairwise_cipher;

#if 0
				switch(group_cipher)
				{
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

				switch(pairwise_cipher)
				{
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
		{
				break;
		}
		
	}

	//wmm
	ie_len = 0;
	pmlmepriv->qospriv.qos_option = 0;
	if(pregistrypriv->wmm_enable)
	{
		for (p = ie + _BEACON_IE_OFFSET_; ;p += (ie_len + 2))
		{			
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));	
			if((p) && _rtw_memcmp(p+2, WMM_PARA_IE, 6)) 
			{
				pmlmepriv->qospriv.qos_option = 1;	

				*(p+8) |= BIT(7);//QoS Info, support U-APSD
				
				/* disable all ACM bits since the WMM admission control is not supported */
				*(p + 10) &= ~BIT(4); /* BE */
				*(p + 14) &= ~BIT(4); /* BK */
				*(p + 18) &= ~BIT(4); /* VI */
				*(p + 22) &= ~BIT(4); /* VO */
				
				break;				
			}
			
			if ((p == NULL) || (ie_len == 0))
			{
				break;
			}			
		}		
	}
#ifdef CONFIG_80211N_HT
	//parsing HT_CAP_IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		u8 rf_type=0;
		u32 max_rx_ampdu_factor=0;
		struct rtw_ieee80211_ht_cap *pht_cap = (struct rtw_ieee80211_ht_cap *)(p+2);

		pHT_caps_ie=p;

		ht_cap = _TRUE;
		network_type |= WIRELESS_11_24N;

		rtw_ht_use_default_setting(padapter);

		if (pmlmepriv->htpriv.sgi_20m == _FALSE)
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_SGI_20);

		if (pmlmepriv->htpriv.sgi_40m == _FALSE)
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_SGI_40);

		if (!TEST_FLAG(pmlmepriv->htpriv.ldpc_cap, LDPC_HT_ENABLE_RX))
		{
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_LDPC_CODING);
		}

		if (!TEST_FLAG(pmlmepriv->htpriv.stbc_cap, STBC_HT_ENABLE_TX))
		{
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_TX_STBC);
		}		

		if (!TEST_FLAG(pmlmepriv->htpriv.stbc_cap, STBC_HT_ENABLE_RX))
		{
			pht_cap->cap_info &= ~(IEEE80211_HT_CAP_RX_STBC_3R);
		}

		pht_cap->ampdu_params_info &= ~(IEEE80211_HT_CAP_AMPDU_FACTOR|IEEE80211_HT_CAP_AMPDU_DENSITY);
		
		if((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
			(psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP))
		{
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2));
		}	
		else
		{
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);	
		}	

		rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);
		pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_FACTOR & max_rx_ampdu_factor); //set  Max Rx AMPDU size  to 64K

		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if(rf_type == RF_1T1R)
		{			
			pht_cap->supp_mcs_set[0] = 0xff;
			pht_cap->supp_mcs_set[1] = 0x0;
		}

#ifdef CONFIG_BEAMFORMING
		// Use registry value to enable HT Beamforming.
		// ToDo: use configure file to set these capability.
		pht_cap->tx_BF_cap_info = 0;

		// HT Beamformer
		if(TEST_FLAG(pmlmepriv->htpriv.beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE))
		{
			// Transmit NDP Capable
			SET_HT_CAP_TXBF_TRANSMIT_NDP_CAP(pht_cap, 1);
			// Explicit Compressed Steering Capable
			SET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pht_cap, 1);
			// Compressed Steering Number Antennas
			SET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pht_cap, 1);
		}

		// HT Beamformee
		if(TEST_FLAG(pmlmepriv->htpriv.beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE))
		{
			// Receive NDP Capable
			SET_HT_CAP_TXBF_RECEIVE_NDP_CAP(pht_cap, 1);
			// Explicit Compressed Beamforming Feedback Capable
			SET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pht_cap, 2);
		}
#endif //CONFIG_BEAMFORMING

		_rtw_memcpy(&pmlmepriv->htpriv.ht_cap, p+2, ie_len);
		
	}

	//parsing HT_INFO_IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		pHT_info_ie=p;
	}
#endif //CONFIG_80211N_HT
	switch(network_type)
	{
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
		default :
			pbss_network->NetworkTypeInUse = Ndis802_11OFDM24;
			break;
	}
	
	pmlmepriv->cur_network.network_type = network_type;

#ifdef CONFIG_80211N_HT
	pmlmepriv->htpriv.ht_option = _FALSE;

	if( (psecuritypriv->wpa2_pairwise_cipher&WPA_CIPHER_TKIP) ||
		      (psecuritypriv->wpa_pairwise_cipher&WPA_CIPHER_TKIP))
	{	
		//todo:
		//ht_cap = _FALSE;
	}
		      
	//ht_cap	
	if(pregistrypriv->ht_enable && ht_cap==_TRUE)
	{		
		pmlmepriv->htpriv.ht_option = _TRUE;
		pmlmepriv->qospriv.qos_option = 1;

		if(pregistrypriv->ampdu_enable==1)
		{
			pmlmepriv->htpriv.ampdu_enable = _TRUE;
		}

		HT_caps_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)pHT_caps_ie);
		
		HT_info_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)pHT_info_ie);
	}
#endif

#ifdef CONFIG_80211AC_VHT

	//Parsing VHT CAP IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTCapability, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{	
		vht_cap = _TRUE; 
	}
	//Parsing VHT OPERATION IE
	

	pmlmepriv->vhtpriv.vht_option = _FALSE;
	// if channel in 5G band, then add vht ie .
	if ((pbss_network->Configuration.DSConfig > 14) && 
		(pmlmepriv->htpriv.ht_option == _TRUE) &&
		(pregistrypriv->vht_enable)) 
	{
		if(vht_cap == _TRUE)
		{
			pmlmepriv->vhtpriv.vht_option = _TRUE;
		}
		else if(pregistrypriv->vht_enable == 2) // auto enabled
		{
			u8	cap_len, operation_len;

			rtw_vht_use_default_setting(padapter);

			// VHT Capabilities element
			cap_len = rtw_build_vht_cap_ie(padapter, pbss_network->IEs + pbss_network->IELength);
			pbss_network->IELength += cap_len;

			// VHT Operation element
			operation_len = rtw_build_vht_operation_ie(padapter, pbss_network->IEs + pbss_network->IELength, pbss_network->Configuration.DSConfig);
			pbss_network->IELength += operation_len;

			pmlmepriv->vhtpriv.vht_option = _TRUE;
		}		
	}
#endif //CONFIG_80211AC_VHT

	pbss_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX  *)pbss_network);

	//issue beacon to start bss network
	//start_bss_network(padapter, (u8*)pbss_network);
	rtw_startbss_cmd(padapter, RTW_CMDF_WAIT_ACK);
			

	//alloc sta_info for ap itself
	psta = rtw_get_stainfo(&padapter->stapriv, pbss_network->MacAddress);
	if(!psta)
	{
		psta = rtw_alloc_stainfo(&padapter->stapriv, pbss_network->MacAddress);
		if (psta == NULL) 
		{ 
			return _FAIL;
		}	
	}

	// update AP's sta info 
	update_ap_info(padapter, psta);
	
	psta->state |= WIFI_AP_STATE;		//Aries, add,fix bug of flush_cam_entry at STOP AP mode , 0724 	
	rtw_indicate_connect( padapter);

	pmlmepriv->cur_network.join_res = _TRUE;//for check if already set beacon
		
	//update bc/mc sta_info
	//update_bmc_sta(padapter);

	return ret;

}

void rtw_set_macaddr_acl(_adapter *padapter, int mode)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;

	DBG_871X("%s, mode=%d\n", __func__, mode);

	pacl_list->mode = mode;
}

int rtw_acl_add_sta(_adapter *padapter, u8 *addr)
{
	_irqL irqL;
	_list	*plist, *phead;
	u8 added = _FALSE;
	int i, ret=0;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	_queue	*pacl_node_q =&pacl_list->acl_node_q;	

	DBG_871X("%s(acl_num=%d)=" MAC_FMT "\n", __func__, pacl_list->num, MAC_ARG(addr));	

	if((NUM_ACL-1) < pacl_list->num)
		return (-1);	


	_enter_critical_bh(&(pacl_node_q->lock), &irqL);

	phead = get_list_head(pacl_node_q);
	plist = get_next(phead);
		
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		paclnode = LIST_CONTAINOR(plist, struct rtw_wlan_acl_node, list);
		plist = get_next(plist);

		if(_rtw_memcmp(paclnode->addr, addr, ETH_ALEN))
		{
			if(paclnode->valid == _TRUE)
			{
				added = _TRUE;
				DBG_871X("%s, sta has been added\n", __func__);
				break;
			}
		}		
	}
	
	_exit_critical_bh(&(pacl_node_q->lock), &irqL);


	if(added == _TRUE)
		return ret;
	

	_enter_critical_bh(&(pacl_node_q->lock), &irqL);

	for(i=0; i< NUM_ACL; i++)
	{
		paclnode = &pacl_list->aclnode[i];

		if(paclnode->valid == _FALSE)
		{
			_rtw_init_listhead(&paclnode->list);
	
			_rtw_memcpy(paclnode->addr, addr, ETH_ALEN);
		
			paclnode->valid = _TRUE;

			rtw_list_insert_tail(&paclnode->list, get_list_head(pacl_node_q));
	
			pacl_list->num++;

			break;
		}
	}

	DBG_871X("%s, acl_num=%d\n", __func__, pacl_list->num);
	
	_exit_critical_bh(&(pacl_node_q->lock), &irqL);

	return ret;
}

int rtw_acl_remove_sta(_adapter *padapter, u8 *addr)
{
	_irqL irqL;
	_list	*plist, *phead;
	int i, ret=0;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	_queue	*pacl_node_q =&pacl_list->acl_node_q;
	u8 baddr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };	//Baddr is used for clearing acl_list

	DBG_871X("%s(acl_num=%d)=" MAC_FMT "\n", __func__, pacl_list->num, MAC_ARG(addr));	

	_enter_critical_bh(&(pacl_node_q->lock), &irqL);

	phead = get_list_head(pacl_node_q);
	plist = get_next(phead);
		
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		paclnode = LIST_CONTAINOR(plist, struct rtw_wlan_acl_node, list);
		plist = get_next(plist);

		if(_rtw_memcmp(paclnode->addr, addr, ETH_ALEN) || _rtw_memcmp(baddr, addr, ETH_ALEN))
		{
			if(paclnode->valid == _TRUE)
			{
				paclnode->valid = _FALSE;

				rtw_list_delete(&paclnode->list);
				
				pacl_list->num--;
			}
		}		
	}
	
	_exit_critical_bh(&(pacl_node_q->lock), &irqL);

	DBG_871X("%s, acl_num=%d\n", __func__, pacl_list->num);
	
	return ret;

}

u8 rtw_ap_set_pairwise_key(_adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj*			ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;	
	u8	res=_SUCCESS;

	ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj));
	if ( ph2c == NULL){
		res= _FAIL;
		goto exit;
	}

	psetstakey_para = (struct set_stakey_parm*)rtw_zmalloc(sizeof(struct set_stakey_parm));
	if(psetstakey_para==NULL){
		rtw_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res=_FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);


	psetstakey_para->algorithm = (u8)psta->dot118021XPrivacy;

	_rtw_memcpy(psetstakey_para->addr, psta->hwaddr, ETH_ALEN);	
	
	_rtw_memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);

	
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);	

exit:

	return res;
	
}

static int rtw_ap_set_key(_adapter *padapter, u8 *key, u8 alg, int keyid, u8 set_tx)
{
	u8 keylen;
	struct cmd_obj* pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv	*pcmdpriv=&(padapter->cmdpriv);	
	int res=_SUCCESS;

	//DBG_871X("%s\n", __FUNCTION__);
	
	pcmd = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj));
	if(pcmd==NULL){
		res= _FAIL;
		goto exit;
	}
	psetkeyparm=(struct setkey_parm*)rtw_zmalloc(sizeof(struct setkey_parm));
	if(psetkeyparm==NULL){
		rtw_mfree((unsigned char *)pcmd, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_rtw_memset(psetkeyparm, 0, sizeof(struct setkey_parm));
		
	psetkeyparm->keyid=(u8)keyid;
	if (is_wep_enc(alg))
		padapter->securitypriv.key_mask |= BIT(psetkeyparm->keyid);

	psetkeyparm->algorithm = alg;

	psetkeyparm->set_tx = set_tx;

	switch(alg)
	{
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
	pcmd->cmdsz =  (sizeof(struct setkey_parm));  
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;


	_rtw_init_listhead(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

int rtw_ap_set_group_key(_adapter *padapter, u8 *key, u8 alg, int keyid)
{
	DBG_871X("%s\n", __FUNCTION__);

	return rtw_ap_set_key(padapter, key, alg, keyid, 1);
}

int rtw_ap_set_wep_key(_adapter *padapter, u8 *key, u8 keylen, int keyid, u8 set_tx)
{
	u8 alg;

	switch(keylen)
	{
		case 5:
			alg =_WEP40_;			
			break;
		case 13:
			alg =_WEP104_;			
			break;
		default:
			alg =_NO_PRIVACY_;			
	}

	DBG_871X("%s\n", __FUNCTION__);

	return rtw_ap_set_key(padapter, key, alg, keyid, set_tx);
}

#ifdef CONFIG_NATIVEAP_MLME

static void update_bcn_fixed_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_erpinfo_ie(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	unsigned char *p, *ie = pnetwork->IEs;
	u32 len = 0;

	DBG_871X("%s, ERP_enable=%d\n", __FUNCTION__, pmlmeinfo->ERP_enable);

	if(!pmlmeinfo->ERP_enable)
		return;

	//parsing ERP_IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
	if(p && len>0)
	{
		PNDIS_802_11_VARIABLE_IEs pIE = (PNDIS_802_11_VARIABLE_IEs)p;

		if (pmlmepriv->num_sta_non_erp == 1)
			pIE->data[0] |= RTW_ERP_INFO_NON_ERP_PRESENT|RTW_ERP_INFO_USE_PROTECTION;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_NON_ERP_PRESENT|RTW_ERP_INFO_USE_PROTECTION);

		if(pmlmepriv->num_sta_no_short_preamble > 0)
			pIE->data[0] |= RTW_ERP_INFO_BARKER_PREAMBLE_MODE;
		else
			pIE->data[0] &= ~(RTW_ERP_INFO_BARKER_PREAMBLE_MODE);
	
		ERP_IE_handler(padapter, pIE);
	}
	
}

static void update_bcn_htcap_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_htinfo_ie(_adapter *padapter)
{	
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_rsn_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_wpa_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_wmm_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);
	
}

static void update_bcn_wps_ie(_adapter *padapter)
{
	u8 *pwps_ie=NULL, *pwps_ie_src, *premainder_ie, *pbackup_remainder_ie=NULL;
	uint wps_ielen=0, wps_offset, remainder_ielen;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	unsigned char *ie = pnetwork->IEs;
	u32 ielen = pnetwork->IELength;


	DBG_871X("%s\n", __FUNCTION__);

	pwps_ie = rtw_get_wps_ie(ie+_FIXED_IE_LENGTH_, ielen-_FIXED_IE_LENGTH_, NULL, &wps_ielen);
	
	if(pwps_ie==NULL || wps_ielen==0)
		return;

	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if(pwps_ie_src == NULL)
		return;

	wps_offset = (uint)(pwps_ie-ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if(remainder_ielen>0)
	{
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if(pbackup_remainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	wps_ielen = (uint)pwps_ie_src[1];//to get ie data len
	if((wps_offset+wps_ielen+2+remainder_ielen)<=MAX_IE_SZ)
	{
		_rtw_memcpy(pwps_ie, pwps_ie_src, wps_ielen+2);
		pwps_ie += (wps_ielen+2);

		if(pbackup_remainder_ie)
			_rtw_memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);

		//update IELength
		pnetwork->IELength = wps_offset + (wps_ielen+2) + remainder_ielen;
	}

	if(pbackup_remainder_ie)
		rtw_mfree(pbackup_remainder_ie, remainder_ielen);
	
	// deal with the case without set_tx_beacon_cmd() in update_beacon() 
#if defined( CONFIG_INTERRUPT_BASED_TXBCN ) || defined( CONFIG_PCI_HCI )
	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		u8 sr = 0;
		rtw_get_wps_attr_content(pwps_ie_src,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8*)(&sr), NULL);
	
		if( sr ) {
			set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			DBG_871X("%s, set WIFI_UNDER_WPS\n", __func__);
		}
	}
#endif
}

static void update_bcn_p2p_ie(_adapter *padapter)
{

}

static void update_bcn_vendor_spec_ie(_adapter *padapter, u8*oui)
{
	DBG_871X("%s\n", __FUNCTION__);

	if(_rtw_memcmp(RTW_WPA_OUI, oui, 4))
	{
		update_bcn_wpa_ie(padapter);
	}
	else if(_rtw_memcmp(WMM_OUI, oui, 4))
	{
		update_bcn_wmm_ie(padapter);
	}
	else if(_rtw_memcmp(WPS_OUI, oui, 4))
	{
		update_bcn_wps_ie(padapter);
	}
	else if(_rtw_memcmp(P2P_OUI, oui, 4))
	{
		update_bcn_p2p_ie(padapter);
	}
	else
	{
		DBG_871X("unknown OUI type!\n");
 	}
	
	
}

void update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv	*pmlmeext;
	//struct mlme_ext_info	*pmlmeinfo;
	
	//DBG_871X("%s\n", __FUNCTION__);

	if(!padapter)
		return;

	pmlmepriv = &(padapter->mlmepriv);
	pmlmeext = &(padapter->mlmeextpriv);
	//pmlmeinfo = &(pmlmeext->mlmext_info);

	if(_FALSE == pmlmeext->bstart_bss)
		return;

	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);

	switch(ie_id)
	{
		case 0xFF:

			update_bcn_fixed_ie(padapter);//8: TimeStamp, 2: Beacon Interval 2:Capability
			
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

	pmlmepriv->update_bcn = _TRUE;
	
	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);		
	
#ifndef CONFIG_INTERRUPT_BASED_TXBCN 
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	if(tx)
	{
		//send_beacon(padapter);//send_beacon must execute on TSR level
		set_tx_beacon_cmd(padapter);
	}
#else
	{	
		//PCI will issue beacon when BCN interrupt occurs.		
	}
#endif
#endif //!CONFIG_INTERRUPT_BASED_TXBCN
	
}

#ifdef CONFIG_80211N_HT

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
static int rtw_ht_operation_update(_adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;

	if(pmlmepriv->htpriv.ht_option == _TRUE) 
		return 0;
	
	//if (!iface->conf->ieee80211n || iface->conf->ht_op_mode_fixed)
	//	return 0;

	DBG_871X("%s current operation mode=0x%X\n",
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
	else if ((phtpriv_ap->ht_cap.cap_info & IEEE80211_HT_CAP_SUP_WIDTH)
		 && pmlmepriv->num_sta_ht_20mhz)
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

	DBG_871X("%s new operation mode=0x%X changes=%d\n",
		   __FUNCTION__, pmlmepriv->ht_op_mode, op_mode_changes);

	return op_mode_changes;
	
}

#endif /* CONFIG_80211N_HT */

void associated_clients_update(_adapter *padapter, u8 updated)
{
	//update associcated stations cap.
	if(updated == _TRUE)
	{
		_irqL irqL;
		_list	*phead, *plist;
		struct sta_info *psta=NULL;	
		struct sta_priv *pstapriv = &padapter->stapriv;
			
		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		
		phead = &pstapriv->asoc_list;
		plist = get_next(phead);
		
		//check asoc_queue
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
		{
			psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		
			plist = get_next(plist);

			VCS_update(padapter, psta);		
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


	if(!(psta->flags & WLAN_STA_SHORT_PREAMBLE))	
	{
		if(!psta->no_short_preamble_set)
		{
			psta->no_short_preamble_set = 1;
			
			pmlmepriv->num_sta_no_short_preamble++;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) && 
		     		(pmlmepriv->num_sta_no_short_preamble == 1))
			{
				beacon_updated = _TRUE;
				update_beacon(padapter, 0xFF, NULL, _TRUE);
			}	
			
		}
	}
	else
	{
		if(psta->no_short_preamble_set)
		{
			psta->no_short_preamble_set = 0;
			
			pmlmepriv->num_sta_no_short_preamble--;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) && 
		     		(pmlmepriv->num_sta_no_short_preamble == 0))
			{
				beacon_updated = _TRUE;
				update_beacon(padapter, 0xFF, NULL, _TRUE);
			}	
			
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

	if(psta->flags & WLAN_STA_NONERP)
	{
		if(!psta->nonerp_set)
		{
			psta->nonerp_set = 1;
			
			pmlmepriv->num_sta_non_erp++;
			
			if (pmlmepriv->num_sta_non_erp == 1)
			{
				beacon_updated = _TRUE;
				update_beacon(padapter, _ERPINFO_IE_, NULL, _TRUE);
			}	
		}
		
	}
	else
	{
		if(psta->nonerp_set)
		{
			psta->nonerp_set = 0;
			
			pmlmepriv->num_sta_non_erp--;
			
			if (pmlmepriv->num_sta_non_erp == 0)
			{
				beacon_updated = _TRUE;
				update_beacon(padapter, _ERPINFO_IE_, NULL, _TRUE);
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

	if(!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT))
	{
		if(!psta->no_short_slot_time_set)
		{
			psta->no_short_slot_time_set = 1;
			
			pmlmepriv->num_sta_no_short_slot_time++;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		   		 (pmlmepriv->num_sta_no_short_slot_time == 1))
			{
				beacon_updated = _TRUE;
				update_beacon(padapter, 0xFF, NULL, _TRUE);
			}			
			
		}
	}
	else
	{
		if(psta->no_short_slot_time_set)
		{
			psta->no_short_slot_time_set = 0;
			
			pmlmepriv->num_sta_no_short_slot_time--;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		   		 (pmlmepriv->num_sta_no_short_slot_time == 0))
			{
				beacon_updated = _TRUE;
				update_beacon(padapter, 0xFF, NULL, _TRUE);
			}			
		}
	}	
	
#ifdef CONFIG_80211N_HT

	if (psta->flags & WLAN_STA_HT) 
	{
		u16 ht_capab = le16_to_cpu(psta->htpriv.ht_cap.cap_info);
			
		DBG_871X("HT: STA " MAC_FMT " HT Capabilities "
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
			DBG_871X("%s STA " MAC_FMT " - no "
				   "greenfield, num of non-gf stations %d\n",
				   __FUNCTION__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_ht_no_gf);
		}
		
		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH) == 0) {
			if (!psta->ht_20mhz_set) {
				psta->ht_20mhz_set = 1;
				pmlmepriv->num_sta_ht_20mhz++;
			}
			DBG_871X("%s STA " MAC_FMT " - 20 MHz HT, "
				   "num of 20MHz HT STAs %d\n",
				   __FUNCTION__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_ht_20mhz);
		}
		
	} 
	else 
	{
		if (!psta->no_ht_set) {
			psta->no_ht_set = 1;
			pmlmepriv->num_sta_no_ht++;
		}
		if(pmlmepriv->htpriv.ht_option == _TRUE) {		
			DBG_871X("%s STA " MAC_FMT
				   " - no HT, num of non-HT stations %d\n",
				   __FUNCTION__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_no_ht);
		}
	}

	if (rtw_ht_operation_update(padapter) > 0)
	{
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE);
	}	
	
#endif /* CONFIG_80211N_HT */

	//update associcated stations cap.
	associated_clients_update(padapter,  beacon_updated);

	DBG_871X("%s, updated=%d\n", __func__, beacon_updated);

}

u8 bss_cap_update_on_sta_leave(_adapter *padapter, struct sta_info *psta)
{
	u8 beacon_updated = _FALSE;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	if(!psta)
		return beacon_updated;

	if (psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 0;
		pmlmepriv->num_sta_no_short_preamble--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_preamble == 0)
		{
			beacon_updated = _TRUE;
			update_beacon(padapter, 0xFF, NULL, _TRUE);
		}	
	}	

	if (psta->nonerp_set) {
		psta->nonerp_set = 0;		
		pmlmepriv->num_sta_non_erp--;
		if (pmlmepriv->num_sta_non_erp == 0)
		{
			beacon_updated = _TRUE;
			update_beacon(padapter, _ERPINFO_IE_, NULL, _TRUE);
		}	
	}

	if (psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 0;
		pmlmepriv->num_sta_no_short_slot_time--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_slot_time == 0)
		{
			beacon_updated = _TRUE;
			update_beacon(padapter, 0xFF, NULL, _TRUE);
		}	
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

	if (rtw_ht_operation_update(padapter) > 0)
	{
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE);
	}
	
#endif /* CONFIG_80211N_HT */

	//update associcated stations cap.
	//associated_clients_update(padapter,  beacon_updated); //move it to avoid deadlock

	DBG_871X("%s, updated=%d\n", __func__, beacon_updated);

	return beacon_updated;

}

u8 ap_free_sta(_adapter *padapter, struct sta_info *psta, bool active, u16 reason)
{
	_irqL irqL;
	u8 beacon_updated = _FALSE;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	if(!psta)
		return beacon_updated;

	if (active == _TRUE)
	{
#ifdef CONFIG_80211N_HT
		//tear down Rx AMPDU
		send_delba(padapter, 0, psta->hwaddr);// recipient
	
		//tear down TX AMPDU
		send_delba(padapter, 1, psta->hwaddr);// // originator
		
#endif //CONFIG_80211N_HT

		issue_deauth(padapter, psta->hwaddr, reason);
	}

	psta->htpriv.agg_enable_bitmap = 0x0;//reset
	psta->htpriv.candidate_tid_bitmap = 0x0;//reset


	//report_del_sta_event(padapter, psta->hwaddr, reason);

	//clear cam entry / key
	rtw_clearstakey_cmd(padapter, psta, _TRUE);


	_enter_critical_bh(&psta->lock, &irqL);
	psta->state &= ~_FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL);

	#ifdef CONFIG_IOCTL_CFG80211
	if (1) {
		#ifdef COMPAT_KERNEL_RELEASE
		rtw_cfg80211_indicate_sta_disassoc(padapter, psta->hwaddr, reason);
		#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
		rtw_cfg80211_indicate_sta_disassoc(padapter, psta->hwaddr, reason);
		#else //(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
		/* will call rtw_cfg80211_indicate_sta_disassoc() in cmd_thread for old API context */
		#endif //(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	} else
	#endif //CONFIG_IOCTL_CFG80211
	{
		rtw_indicate_sta_disassoc_event(padapter, psta);
	}

	report_del_sta_event(padapter, psta->hwaddr, reason);

	beacon_updated = bss_cap_update_on_sta_leave(padapter, psta);

	//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	rtw_free_stainfo(padapter, psta);
	//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	

	return beacon_updated;

}

int rtw_ap_inform_ch_switch(_adapter *padapter, u8 new_ch, u8 ch_offset)
{
	_irqL irqL;
	_list	*phead, *plist;
	int ret=0;	
	struct sta_info *psta = NULL;	
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	DBG_871X(FUNC_NDEV_FMT" with ch:%u, offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev), new_ch, ch_offset);

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	
	/* for each sta in asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{		
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		issue_action_spct_ch_switch(padapter, psta->hwaddr, new_ch, ch_offset);
		psta->expire_to = ((pstapriv->expire_to * 2) > 5) ? 5 : (pstapriv->expire_to * 2);
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	issue_action_spct_ch_switch(padapter, bc_addr, new_ch, ch_offset);

	return ret;
}

int rtw_sta_flush(_adapter *padapter)
{
	_irqL irqL;
	_list	*phead, *plist;
	int ret=0;	
	struct sta_info *psta = NULL;	
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));
	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	//free sta asoc_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		
		plist = get_next(plist);

		rtw_list_delete(&psta->asoc_list);
		pstapriv->asoc_list_cnt--;

		//_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_DEAUTH_LEAVING);
		//_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);


	issue_deauth(padapter, bc_addr, WLAN_REASON_DEAUTH_LEAVING);

	associated_clients_update(padapter, _TRUE);

	return ret;

}

/* called > TSR LEVEL for USB or SDIO Interface*/
void sta_info_update(_adapter *padapter, struct sta_info *psta)
{	
	int flags = psta->flags;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	
				
	//update wmm cap.
	if(WLAN_STA_WME&flags)
		psta->qos_option = 1;
	else
		psta->qos_option = 0;

	if(pmlmepriv->qospriv.qos_option == 0)	
		psta->qos_option = 0;

		
#ifdef CONFIG_80211N_HT		
	//update 802.11n ht cap.
	if(WLAN_STA_HT&flags)
	{
		psta->htpriv.ht_option = _TRUE;
		psta->qos_option = 1;	
	}
	else		
	{
		psta->htpriv.ht_option = _FALSE;
	}
		
	if(pmlmepriv->htpriv.ht_option == _FALSE)	
		psta->htpriv.ht_option = _FALSE;
#endif

#ifdef CONFIG_80211AC_VHT
	//update 802.11AC vht cap.
	if(WLAN_STA_VHT&flags)
	{
		psta->vhtpriv.vht_option = _TRUE;
	}
	else		
	{
		psta->vhtpriv.vht_option = _FALSE;
	}

	if(pmlmepriv->vhtpriv.vht_option == _FALSE)
		psta->vhtpriv.vht_option = _FALSE;
#endif	


	update_sta_info_apmode(padapter, psta);
		

}

/* called >= TSR LEVEL for USB or SDIO Interface*/
void ap_sta_info_defer_update(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(psta->state & _FW_LINKED)
	{
		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;
	
		//add ratid
		add_RATid(padapter, psta, 0);//DM_RATR_STA_INIT
	}	
}
/* restore hw setting from sw data structures */
void rtw_ap_restore_network(_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv * pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	_irqL irqL;
	_list	*phead, *plist;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	rtw_setopmode_cmd(padapter, Ndis802_11APMode,_FALSE);

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	start_bss_network(padapter, (u8*)&mlmepriv->cur_network.network);

	if((padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
		(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_))
	{
		/* restore group key, WEP keys is restored in ips_leave() */
		rtw_set_key(padapter, psecuritypriv, psecuritypriv->dot118021XGrpKeyid, 0,_FALSE);
	}

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		int stainfo_offset;

		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset)) {
			chk_alive_list[chk_alive_num++] = stainfo_offset;
		}
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	for (i = 0; i < chk_alive_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);

		if (psta == NULL) {
			DBG_871X(FUNC_ADPT_FMT" sta_info is null\n", FUNC_ADPT_ARG(padapter));
		} else if (psta->state &_FW_LINKED) {
			rtw_sta_media_status_rpt(padapter, psta, 1);
			Update_RA_Entry(padapter, psta);
			//pairwise key
			/* per sta pairwise key and settings */
			if(	(padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
				(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_))
			{
				rtw_setstakey_cmd(padapter, psta, _TRUE,_FALSE);
			}			
		}
	}

}

void start_ap_mode(_adapter *padapter)
{
	int i;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	
	pmlmepriv->update_bcn = _FALSE;
	
	//init_mlme_ap_info(padapter);
	pmlmeext->bstart_bss = _FALSE;

	pmlmepriv->num_sta_non_erp = 0;

	pmlmepriv->num_sta_no_short_slot_time = 0;

	pmlmepriv->num_sta_no_short_preamble = 0;

	pmlmepriv->num_sta_ht_no_gf = 0;
#ifdef CONFIG_80211N_HT
	pmlmepriv->num_sta_no_ht = 0;
#endif //CONFIG_80211N_HT
	pmlmepriv->num_sta_ht_20mhz = 0;

	pmlmepriv->olbc = _FALSE;

	pmlmepriv->olbc_ht = _FALSE;
	
#ifdef CONFIG_80211N_HT
	pmlmepriv->ht_op_mode = 0;
#endif

	for(i=0; i<NUM_STA; i++)
		pstapriv->sta_aid[i] = NULL;

	pmlmepriv->wps_beacon_ie = NULL;	
	pmlmepriv->wps_probe_resp_ie = NULL;
	pmlmepriv->wps_assoc_resp_ie = NULL;
	
	pmlmepriv->p2p_beacon_ie = NULL;
	pmlmepriv->p2p_probe_resp_ie = NULL;

	
	//for ACL 
	_rtw_init_listhead(&(pacl_list->acl_node_q.queue));
	pacl_list->num = 0;
	pacl_list->mode = 0;
	for(i = 0; i < NUM_ACL; i++)
	{		
		_rtw_init_listhead(&pacl_list->aclnode[i].list);
		pacl_list->aclnode[i].valid = _FALSE;
	}

}

void stop_ap_mode(_adapter *padapter)
{
	_irqL irqL;
	_list	*phead, *plist;
	struct rtw_wlan_acl_node *paclnode;
	struct sta_info *psta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;	
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	_queue	*pacl_node_q =&pacl_list->acl_node_q;	

	pmlmepriv->update_bcn = _FALSE;
	pmlmeext->bstart_bss = _FALSE;
	//_rtw_spinlock_free(&pmlmepriv->bcn_update_lock);
	
	//reset and init security priv , this can refine with rtw_reset_securitypriv
	_rtw_memset((unsigned char *)&padapter->securitypriv, 0, sizeof (struct security_priv));
	padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
	padapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

	//for ACL
	_enter_critical_bh(&(pacl_node_q->lock), &irqL);
	phead = get_list_head(pacl_node_q);
	plist = get_next(phead);		
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		paclnode = LIST_CONTAINOR(plist, struct rtw_wlan_acl_node, list);
		plist = get_next(plist);

		if(paclnode->valid == _TRUE)
		{
			paclnode->valid = _FALSE;

			rtw_list_delete(&paclnode->list);
				
			pacl_list->num--;		
		}		
	}	
	_exit_critical_bh(&(pacl_node_q->lock), &irqL);
	
	DBG_871X("%s, free acl_node_queue, num=%d\n", __func__, pacl_list->num);
	
	rtw_sta_flush(padapter);

	//free_assoc_sta_resources	
	rtw_free_all_stainfo(padapter);
	
	psta = rtw_get_bcmc_stainfo(padapter);
	//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	rtw_free_stainfo(padapter, psta);
	//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	
	rtw_init_bcmc_stainfo(padapter);	

	rtw_free_mlme_priv_ie_data(pmlmepriv);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_MediaStatusNotify(padapter, 0); //disconnect 
#endif	

}

#endif //CONFIG_NATIVEAP_MLME

#ifdef CONFIG_CONCURRENT_MODE
void concurrent_set_ap_chbw(_adapter *padapter, u8 channel, u8 channel_offset, u8 bwmode)
{
	u8 *p;
	int	ie_len=0;
	u8 cur_channel, cur_bwmode, cur_ch_offset, change_band;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct HT_info_element *pht_info=NULL;
	
	cur_channel = channel;
	cur_bwmode = bwmode;
	cur_ch_offset = channel_offset;
	change_band = _FALSE;
	
	p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
	if( p && ie_len)
	{
		pht_info = (struct HT_info_element *)(p+2);
	}

	
	if(!check_buddy_fwstate(padapter, _FW_LINKED|_FW_UNDER_LINKING|_FW_UNDER_SURVEY))
	{
		set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
	}
	else if(check_buddy_fwstate(padapter, _FW_LINKED)==_TRUE)
	{
		_adapter *pbuddy_adapter = padapter->pbuddy_adapter;		
		struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

		//To sync cur_channel/cur_bwmode/cur_ch_offset with buddy adapter
		DBG_871X(ADPT_FMT" is at linked state\n", ADPT_ARG(pbuddy_adapter));
		DBG_871X(ADPT_FMT": CH=%d, BW=%d, offset=%d\n", ADPT_ARG(pbuddy_adapter), pbuddy_mlmeext->cur_channel, pbuddy_mlmeext->cur_bwmode, pbuddy_mlmeext->cur_ch_offset);
		DBG_871X(ADPT_FMT": CH=%d, BW=%d, offset=%d\n", ADPT_ARG(padapter), cur_channel, cur_bwmode, cur_ch_offset);

		if((cur_channel <= 14 && pbuddy_mlmeext->cur_channel >= 36) ||
			(cur_channel >= 36 && pbuddy_mlmeext->cur_channel <= 14))
			change_band = _TRUE;

		cur_channel = pbuddy_mlmeext->cur_channel;

#ifdef CONFIG_80211AC_VHT
		if(cur_bwmode == CHANNEL_WIDTH_80)
		{
			u8 *pvht_cap_ie, *pvht_op_ie;
			int vht_cap_ielen, vht_op_ielen;
			
			pvht_cap_ie = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_VHTCapability, &vht_cap_ielen, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
			pvht_op_ie = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), EID_VHTOperation, &vht_op_ielen, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
						
			if(pbuddy_mlmeext->cur_channel <= 14) // downgrade to 20/40Mhz
			{
				//modify vht cap ie
				if( pvht_cap_ie && vht_cap_ielen)
				{
					SET_VHT_CAPABILITY_ELE_SHORT_GI80M(pvht_cap_ie+2, 0);
				}
				
				//modify vht op ie
				if( pvht_op_ie && vht_op_ielen)
				{
					SET_VHT_OPERATION_ELE_CHL_WIDTH(pvht_op_ie+2, 0); //change to 20/40Mhz
					SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(pvht_op_ie+2, 0);
					SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ2(pvht_op_ie+2, 0);
					//SET_VHT_OPERATION_ELE_BASIC_MCS_SET(p+2, 0xFFFF);					
					cur_bwmode = CHANNEL_WIDTH_40;
				}		
			}
			else
			{
				u8	center_freq;
				
				cur_bwmode = CHANNEL_WIDTH_80;
				
				if(pbuddy_mlmeext->cur_bwmode == CHANNEL_WIDTH_40 ||
					pbuddy_mlmeext->cur_bwmode == CHANNEL_WIDTH_80)
				{
					cur_ch_offset = pbuddy_mlmeext->cur_ch_offset;
				}
				else if(pbuddy_mlmeext->cur_bwmode == CHANNEL_WIDTH_20)
				{
					cur_ch_offset =  rtw_get_offset_by_ch(cur_channel);
				}	

				//modify ht info ie
				if(pht_info)
					pht_info->infos[0] &= ~HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
				
				switch(cur_ch_offset)
				{
					case HAL_PRIME_CHNL_OFFSET_LOWER:
						if(pht_info)
							pht_info->infos[0] |= HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE;		
						//cur_bwmode = CHANNEL_WIDTH_40;
						break;
					case HAL_PRIME_CHNL_OFFSET_UPPER:
						if(pht_info)
							pht_info->infos[0] |= HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW;						
						//cur_bwmode = CHANNEL_WIDTH_40;
						break;
					case HAL_PRIME_CHNL_OFFSET_DONT_CARE:							
					default:
						if(pht_info)
							pht_info->infos[0] &= ~HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
						cur_bwmode = CHANNEL_WIDTH_20;
						cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
						break;					
				}

				//modify vht op ie
				center_freq = rtw_get_center_ch(cur_channel, cur_bwmode, HAL_PRIME_CHNL_OFFSET_LOWER);
				if( pvht_op_ie && vht_op_ielen)
					SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(pvht_op_ie+2, center_freq);

				set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
				
			}
			
		}
#endif //CONFIG_80211AC_VHT

		if(cur_bwmode == CHANNEL_WIDTH_40)
		{
			if(pht_info)
				pht_info->infos[0] &= ~HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
			
			if(pbuddy_mlmeext->cur_bwmode == CHANNEL_WIDTH_40 ||
				pbuddy_mlmeext->cur_bwmode == CHANNEL_WIDTH_80)
			{
				cur_ch_offset = pbuddy_mlmeext->cur_ch_offset;

				//to update cur_ch_offset value in beacon
				if(pht_info)
				{				
					switch(cur_ch_offset)
					{
						case HAL_PRIME_CHNL_OFFSET_LOWER:
							pht_info->infos[0] |= HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE;
							break;
						case HAL_PRIME_CHNL_OFFSET_UPPER:
							pht_info->infos[0] |= HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW;
							break;
						case HAL_PRIME_CHNL_OFFSET_DONT_CARE:							
						default:							
							break;					
					}
				}		
				
			}
			else if(pbuddy_mlmeext->cur_bwmode == CHANNEL_WIDTH_20)
			{
				cur_ch_offset =  rtw_get_offset_by_ch(cur_channel);

				switch(cur_ch_offset)
				{
					case HAL_PRIME_CHNL_OFFSET_LOWER:
						if(pht_info)
							pht_info->infos[0] |= HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE;		
						cur_bwmode = CHANNEL_WIDTH_40;
						break;
					case HAL_PRIME_CHNL_OFFSET_UPPER:
						if(pht_info)
							pht_info->infos[0] |= HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW;						
						cur_bwmode = CHANNEL_WIDTH_40;
						break;
					case HAL_PRIME_CHNL_OFFSET_DONT_CARE:							
					default:
						if(pht_info)
							pht_info->infos[0] &= ~HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
						cur_bwmode = CHANNEL_WIDTH_20;
						cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
						break;					
				}						
				
			}

			set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
			
		}
		else
		{
			set_channel_bwmode(padapter, cur_channel, pbuddy_mlmeext->cur_ch_offset, pbuddy_mlmeext->cur_bwmode);
		}		

		// to update channel value in beacon
		pnetwork->Configuration.DSConfig = cur_channel;		
		p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _DSSET_IE_, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		if(p && ie_len>0)
			*(p + 2) = cur_channel;
		
		if(pht_info)
			pht_info->primary_channel = cur_channel;
	}

	DBG_871X(FUNC_ADPT_FMT" CH=%d, BW=%d, offset=%d\n", FUNC_ADPT_ARG(padapter), cur_channel, cur_bwmode, cur_ch_offset);

	pmlmeext->cur_channel = cur_channel;	
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;

	//buddy interface band is different from current interface, update ERP, support rate, ext support rate IE
	if(change_band == _TRUE)
		change_band_update_ie(padapter, pnetwork);

}
#endif //CONFIG_CONCURRENT_MODE

#endif //CONFIG_AP_MODE

