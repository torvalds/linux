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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>


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
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
	rtw_free_stainfo(padapter, psta);
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
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

		if((pstapriv->tim_bitmap&0xff00) && (pstapriv->tim_bitmap&0x00fc))			
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
			*dst_ie++ = *(u8*)&tim_bitmap_le;
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

#ifndef CONFIG_INTERRUPT_BASED_TXBCN 
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	set_tx_beacon_cmd(padapter);
#endif
#endif //!CONFIG_INTERRUPT_BASED_TXBCN


}

void rtw_add_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index, u8 *data, u8 len)
{
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8	bmatch = _FALSE;
	u8	*pie = pnetwork->IEs;
	u8	*p, *dst_ie, *premainder_ie=NULL, *pbackup_remainder_ie=NULL;
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
	u8 *p, *dst_ie, *premainder_ie=NULL, *pbackup_remainder_ie=NULL;
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
	u8 updated;
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
				
				_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
				rtw_free_stainfo(padapter, psta);
				_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
				
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
#ifdef CONFIG_TX_MCAST2UNI
#ifdef CONFIG_80211N_HT
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
#endif //CONFIG_80211N_HT
#endif // CONFIG_TX_MCAST2UNI
#endif //CONFIG_ACTIVE_KEEP_ALIVE_CHECK

		if (psta->expire_to <= 0)
		{
			#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			if (padapter->registrypriv.wifi_spec == 1)
			{
				psta->expire_to = pstapriv->expire_to;
				continue;
			}

			if (psta->state & WIFI_SLEEP_STATE) {
				if (!(psta->state & WIFI_STA_ALIVE_CHK_STATE)) {
					//to check if alive by another methods if staion is at ps mode.					
					psta->expire_to = pstapriv->expire_to;
					psta->state |= WIFI_STA_ALIVE_CHK_STATE;

					//DBG_871X("alive chk, sta:" MAC_FMT " is at ps mode!\n", MAC_ARG(psta->hwaddr));

					//to update bcn with tim_bitmap for this station
					pstapriv->tim_bitmap |= BIT(psta->aid);
					update_beacon(padapter, _TIM_IE_, NULL, _FALSE);

					if(!pmlmeext->active_keep_alive_check)
						continue;
				}
			}

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
	u32 init_rate=0;
	unsigned char sta_band = 0, raid, shortGIrate = _FALSE;
	unsigned char limit;	
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
	
	//b/g mode ra_bitmap  
	for (i=0; i<sizeof(psta->bssrateset); i++)
	{
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
	}
#ifdef CONFIG_80211N_HT
	//n mode ra_bitmap
	if(psta_ht->ht_option) 
	{
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if(rf_type == RF_2T2R)
			limit=16;// 2R
		else
			limit=8;//  1R

		for (i=0; i<limit; i++) {
			if (psta_ht->ht_cap.supp_mcs_set[i/8] & BIT(i%8))
				tx_ra_bitmap |= BIT(i+12);
		}

		//max short GI rate
		shortGIrate = psta_ht->sgi;
	}
#endif //CONFIG_80211N_HT

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

	if ( pcur_network->Configuration.DSConfig > 14 ) {
		// 5G band
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_5N ;

		if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11A;
		
	} else {
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_24N;

		if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11G;

		if (tx_ra_bitmap & 0x0f)
			sta_band |= WIRELESS_11B;
	}

	psta->wireless_mode = sta_band;

	raid = networktype_to_raid(sta_band);	
	init_rate = get_highest_rate_idx(tx_ra_bitmap&0x0fffffff)&0x3f;
	
	if (psta->aid < NUM_STA) 
	{
		u8 arg = 0;

		arg = psta->mac_id&0x1f;
		
		arg |= BIT(7);//support entry 2~31
		
		if (shortGIrate==_TRUE)
			arg |= BIT(5);

		tx_ra_bitmap |= ((raid<<28)&0xf0000000);

		DBG_871X("%s=> mac_id:%d , raid:%d , bitmap=0x%x, arg=0x%x\n", 
			__FUNCTION__ , psta->mac_id, raid ,tx_ra_bitmap, arg);

		//bitmap[0:27] = tx_rate_bitmap
		//bitmap[28:31]= Rate Adaptive id
		//arg[0:4] = macid
		//arg[5] = Short GI
		rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, rssi_level);
		

		if (shortGIrate==_TRUE)
			init_rate |= BIT(6);
		
		//set ra_id, init_rate
		psta->raid = raid;
		psta->init_rate = init_rate;
		
	}
	else 
	{
		DBG_871X("station aid %d exceed the max number\n", psta->aid);
	}

}

void update_bmc_sta(_adapter *padapter)
{
	_irqL	irqL;
	u32 init_rate=0;
	unsigned char	network_type, raid;
	int i, supportRateNum = 0;	
	unsigned int tx_ra_bitmap=0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;	
	struct sta_info *psta = rtw_get_bcmc_stainfo(padapter);

	if(psta)
	{
		psta->aid = 0;//default set to 0
		//psta->mac_id = psta->aid+4;	
		psta->mac_id = psta->aid + 1;

		psta->qos_option = 0;
#ifdef CONFIG_80211N_HT	
		psta->htpriv.ht_option = _FALSE;
#endif //CONFIG_80211N_HT

		psta->ieee8021x_blocked = 0;

		_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		//psta->dot118021XPrivacy = _NO_PRIVACY_;//!!! remove it, because it has been set before this.



		//prepare for add_RATid		
		supportRateNum = rtw_get_rateset_len((u8*)&pcur_network->SupportedRates);
		network_type = rtw_check_network_type((u8*)&pcur_network->SupportedRates, supportRateNum, 1);
		
		_rtw_memcpy(psta->bssrateset, &pcur_network->SupportedRates, supportRateNum);
		psta->bssratelen = supportRateNum;

		//b/g mode ra_bitmap  
		for (i=0; i<supportRateNum; i++)
		{	
			if (psta->bssrateset[i])
				tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
		}

		if ( pcur_network->Configuration.DSConfig > 14 ) {
			//force to A mode. 5G doesn't support CCK rates
			network_type = WIRELESS_11A;
			tx_ra_bitmap = 0x150; // 6, 12, 24 Mbps		
		} else {
			//force to b mode 
			network_type = WIRELESS_11B;
			tx_ra_bitmap = 0xf;		
		}

		//tx_ra_bitmap = update_basic_rate(pcur_network->SupportedRates, supportRateNum);

		raid = networktype_to_raid(network_type);
		init_rate = get_highest_rate_idx(tx_ra_bitmap&0x0fffffff)&0x3f;
				
		//DBG_871X("Add id %d val %08x to ratr for bmc sta\n", psta->aid, tx_ra_bitmap);
		//ap mode
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

		//if(pHalData->fw_ractrl == _TRUE)
		{
			u8 arg = 0;

			arg = psta->mac_id&0x1f;
		
			arg |= BIT(7);
		
			//if (shortGIrate==_TRUE)
			//	arg |= BIT(5);
			
			tx_ra_bitmap |= ((raid<<28)&0xf0000000);			

			DBG_871X("update_bmc_sta, mask=0x%x, arg=0x%x\n", tx_ra_bitmap, arg);

			//bitmap[0:27] = tx_rate_bitmap
			//bitmap[28:31]= Rate Adaptive id
			//arg[0:4] = macid
			//arg[5] = Short GI
			rtw_hal_add_ra_tid(padapter, tx_ra_bitmap, arg, 0);			
		
		}

		//set ra_id, init_rate
		psta->raid = raid;
		psta->init_rate = init_rate;

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
	//set intf_tag to if1
	//psta->intf_tag = 0;

	//psta->mac_id = psta->aid+4;
	psta->mac_id = psta->aid+1; 
	DBG_871X("%s\n",__FUNCTION__);	

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

		//check if sta support s Short GI
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40))
		{
			phtpriv_sta->sgi = _TRUE;
		}

		// bwmode
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH))
		{
			//phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_40;
			phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
			phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;
			
		}		

		psta->qos_option = _TRUE;
		
	}
	else
	{
		phtpriv_sta->ampdu_enable = _FALSE;
		
		phtpriv_sta->sgi = _FALSE;
		phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	//Rx AMPDU
	send_delba(padapter, 0, psta->hwaddr);// recipient
	
	//TX AMPDU
	send_delba(padapter, 1, psta->hwaddr);// // originator
	phtpriv_sta->agg_enable_bitmap = 0x0;//reset
	phtpriv_sta->candidate_tid_bitmap = 0x0;//reset
#endif //CONFIG_80211N_HT

	//todo: init other variables
	
	_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));


	//add ratid
	//add_RATid(padapter, psta);//move to ap_sta_info_defer_update()


	_enter_critical_bh(&psta->lock, &irqL);
	psta->state |= _FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL);
	

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

static void start_bss_network(_adapter *padapter, u8 *pbuf)
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
	
	//DBG_871X("%s\n", __FUNCTION__);

	bcn_interval = (u16)pnetwork->Configuration.BeaconPeriod;	
	cur_channel = pnetwork->Configuration.DSConfig;
	cur_bwmode = HT_CHANNEL_WIDTH_20;;
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

	
	UpdateBrateTbl(padapter, pnetwork->SupportedRates);
	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, pnetwork->SupportedRates);

	if(pmlmepriv->cur_network.join_res != _TRUE) //setting only at  first time
	{
		//u32 initialgain;

		//initialgain = 0x1e;


		//disable dynamic functions, such as high power, DIG
		//Save_DM_Func_Flag(padapter);
		//Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);
		
#ifdef CONFIG_CONCURRENT_MODE	
		if(padapter->adapter_type > PRIMARY_ADAPTER)
		{
			if(rtw_buddy_adapter_up(padapter))
			{
				_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
		
				//turn on all dynamic functions on PRIMARY_ADAPTER, dynamic functions only runs at PRIMARY_ADAPTER
				Switch_DM_Func(pbuddy_adapter, DYNAMIC_ALL_FUNC_ENABLE, _TRUE);
	
				//rtw_hal_set_hwreg(pbuddy_adapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
			}
		}
		else
#endif
		{
			//turn on all dynamic functions	
			Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, _TRUE);

			//rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
		}	
	
	}
#ifdef CONFIG_80211N_HT
	//set channel, bwmode	
	p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
	if( p && ie_len)
	{
		pht_info = (struct HT_info_element *)(p+2);
					
		if ((pregpriv->cbw40_enable) &&	 (pht_info->infos[0] & BIT(2)))
		{
			//switch to the 40M Hz mode
			//pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
			cur_bwmode = HT_CHANNEL_WIDTH_40;
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
#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_set_ap_channel_bandwidth(padapter, cur_channel, cur_ch_offset, cur_bwmode);
#else
	//TODO: need to judge the phy parameters on concurrent mode for single phy
	//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
#ifdef CONFIG_CONCURRENT_MODE
	if(!check_buddy_fwstate(padapter, _FW_LINKED|_FW_UNDER_LINKING|_FW_UNDER_SURVEY))
	{
	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
	}
	else if(check_buddy_fwstate(padapter, _FW_LINKED)==_TRUE)//only second adapter can enter AP Mode
	{
		_adapter *pbuddy_adapter = padapter->pbuddy_adapter;		
		struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
	
		//To sync cur_channel/cur_bwmode/cur_ch_offset with primary adapter
		DBG_871X("primary iface is at linked state, sync cur_channel/cur_bwmode/cur_ch_offset\n");
		DBG_871X("primary adapter, CH=%d, BW=%d, offset=%d\n", pbuddy_mlmeext->cur_channel, pbuddy_mlmeext->cur_bwmode, pbuddy_mlmeext->cur_ch_offset);
		DBG_871X("second adapter, CH=%d, BW=%d, offset=%d\n", cur_channel, cur_bwmode, cur_ch_offset);
		
		cur_channel = pbuddy_mlmeext->cur_channel;
		if(cur_bwmode == HT_CHANNEL_WIDTH_40)
		{
			if(pht_info)
				pht_info->infos[0] &= ~(BIT(0)|BIT(1));
			
			if(pbuddy_mlmeext->cur_bwmode == HT_CHANNEL_WIDTH_40)
			{
				cur_ch_offset = pbuddy_mlmeext->cur_ch_offset;

				//to update cur_ch_offset value in beacon
				if(pht_info)
				{				
					switch(cur_ch_offset)
					{
						case HAL_PRIME_CHNL_OFFSET_LOWER:
							pht_info->infos[0] |= 0x1;
							break;
						case HAL_PRIME_CHNL_OFFSET_UPPER:
							pht_info->infos[0] |= 0x3;
							break;
						case HAL_PRIME_CHNL_OFFSET_DONT_CARE:							
						default:							
							break;					
					}
				}		
				
			}
			else if(pbuddy_mlmeext->cur_bwmode == HT_CHANNEL_WIDTH_20)
			{
				cur_bwmode = HT_CHANNEL_WIDTH_20;
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			
				if(cur_channel>0 && cur_channel<5)
				{
					if(pht_info)
						pht_info->infos[0] |= 0x1;		

					cur_bwmode = HT_CHANNEL_WIDTH_40;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				}

				if(cur_channel>7 && cur_channel<(14+1))
				{
					if(pht_info)
						pht_info->infos[0] |= 0x3;
						
					cur_bwmode = HT_CHANNEL_WIDTH_40;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;						
				}					
			
				set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
			}
			
		}

		// to update channel value in beacon
		pnetwork->Configuration.DSConfig = cur_channel;		
		p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _DSSET_IE_, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		if(p && ie_len>0)
			*(p + 2) = cur_channel;
		
		if(pht_info)
			pht_info->primary_channel = cur_channel;
	}
#else
	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
#endif //CONFIG_CONCURRENT_MODE

	DBG_871X("CH=%d, BW=%d, offset=%d\n", cur_channel, cur_bwmode, cur_ch_offset);

	//
	pmlmeext->cur_channel = cur_channel;	
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;	
#endif //CONFIG_DUALMAC_CONCURRENT
	pmlmeext->cur_wireless_mode = pmlmepriv->cur_network.network_type;

	//let pnetwork_mlmeext == pnetwork_mlme.
	_rtw_memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);

	//update cur_wireless_mode
	update_wireless_mode(padapter);

	//udpate capability after cur_wireless_mode updated
	update_capinfo(padapter, rtw_get_capability((WLAN_BSSID_EX *)pnetwork));
	
#ifdef CONFIG_P2P
	_rtw_memcpy(pwdinfo->p2p_group_ssid, pnetwork->Ssid.Ssid, pnetwork->Ssid.SsidLength);	
	pwdinfo->p2p_group_ssid_len = pnetwork->Ssid.SsidLength;
#endif //CONFIG_P2P

	if(_TRUE == pmlmeext->bstart_bss)
	{
		update_beacon(padapter, _TIM_IE_, NULL, _FALSE);

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
		u8 rf_type;

		struct rtw_ieee80211_ht_cap *pht_cap = (struct rtw_ieee80211_ht_cap *)(p+2);

		pHT_caps_ie=p;
		
		
		ht_cap = _TRUE;
		network_type |= WIRELESS_11_24N;

	
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		if((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
			(psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP))
		{
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2));
		}	
		else
		{
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);	
		}	

		pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_FACTOR & 0x03); //set  Max Rx AMPDU size  to 64K

		if(rf_type == RF_1T1R)
		{			
			pht_cap->supp_mcs_set[0] = 0xff;
			pht_cap->supp_mcs_set[1] = 0x0;				
		}

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


	pbss_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX  *)pbss_network);

	//issue beacon to start bss network
	start_bss_network(padapter, (u8*)pbss_network);
			

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

	DBG_871X("%s(acl_num=%d)=" MAC_FMT "\n", __func__, pacl_list->num, MAC_ARG(addr));	

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

	wps_offset = (uint)(pwps_ie-ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if(remainder_ielen>0)
	{
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if(pbackup_remainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	
	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if(pwps_ie_src == NULL)
		return;


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
	//clear_cam_entry(padapter, (psta->mac_id + 3));
	rtw_clearstakey_cmd(padapter, (u8*)psta, (u8)(psta->mac_id + 3), _TRUE);


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

	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);					
	rtw_free_stainfo(padapter, psta);
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	

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
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	int i;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		int stainfo_offset;
		
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		/* Remove sta from asoc_list */
		rtw_list_delete(&psta->asoc_list);
		pstapriv->asoc_list_cnt--;

		/* Keep sta for ap_free_sta() beyond this asoc_list loop */
		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset)) {
			chk_alive_list[chk_alive_num++] = stainfo_offset;
		}
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);


	/* For each sta in chk_alive_list, call ap_free_sta */
	for (i = 0; i < chk_alive_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);
		ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_DEAUTH_LEAVING);
	}

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


	update_sta_info_apmode(padapter, psta);
		

}

/* called >= TSR LEVEL for USB or SDIO Interface*/
void ap_sta_info_defer_update(_adapter *padapter, struct sta_info *psta)
{
	if(psta->state & _FW_LINKED)
	{	
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
		}
		else if(psta->state &_FW_LINKED)
		{
			Update_RA_Entry(padapter, psta);
			//pairwise key
			/* per sta pairwise key and settings */
			if(	(padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
				(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_))
			{
				rtw_setstakey_cmd(padapter, (unsigned char *)psta, _TRUE,_FALSE);
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
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
	rtw_free_stainfo(padapter, psta);
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	
	rtw_init_bcmc_stainfo(padapter);	

	rtw_free_mlme_priv_ie_data(pmlmepriv);

}

#endif //CONFIG_NATIVEAP_MLME
#endif //CONFIG_AP_MODE

