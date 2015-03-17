/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#define _RTW_TDLS_C_

#include <drv_types.h>

#ifdef CONFIG_TDLS
extern unsigned char MCS_rate_2R[16];
extern unsigned char MCS_rate_1R[16];
extern void process_wmmps_data(_adapter *padapter, union recv_frame *precv_frame);

void rtw_reset_tdls_info(_adapter* padapter)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	ptdlsinfo->ap_prohibited = _FALSE;
	ptdlsinfo->link_established = _FALSE;
	ptdlsinfo->sta_cnt = 0;
	ptdlsinfo->sta_maximum = _FALSE;
	ptdlsinfo->ch_sensing = 0;
	ptdlsinfo->cur_channel = 0;
	ptdlsinfo->candidate_ch = 1;	//when inplement channel switching, default candidate channel is 1
	ptdlsinfo->watchdog_count = 0;
	ptdlsinfo->dev_discovered = 0;

#ifdef CONFIG_WFD
	ptdlsinfo->wfd_info = &padapter->wfd_info;
#endif //CONFIG_WFD
}

int rtw_init_tdls_info(_adapter* padapter)
{
	int	res = _SUCCESS;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	ptdlsinfo->tdls_enable = _TRUE;
	rtw_reset_tdls_info(padapter);

	_rtw_spinlock_init(&ptdlsinfo->cmd_lock);
	_rtw_spinlock_init(&ptdlsinfo->hdl_lock);

	return res;

}

void rtw_free_tdls_info(struct tdls_info *ptdlsinfo)
{
	_rtw_spinlock_free(&ptdlsinfo->cmd_lock);
	_rtw_spinlock_free(&ptdlsinfo->hdl_lock);

	_rtw_memset(ptdlsinfo, 0, sizeof(struct tdls_info) );

}

int _issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, unsigned char *da, unsigned int power_mode, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = _FALSE;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//	SetToDs(fctrl);

	if (power_mode)
	{
		SetPwrMgt(fctrl);
	}

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if(wait_ack)
	{
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	}
	else
	{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;

}

int issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	u32 start = rtw_get_current_time();
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//[TDLS] UAPSD : merge this from issue_nulldata() and mark it first.
	#if 0
	psta = rtw_get_stainfo(&padapter->stapriv, da);
	if (psta) {
		if (power_mode)
			rtw_hal_macid_sleep(padapter, psta->mac_id);
		else
			rtw_hal_macid_wakeup(padapter, psta->mac_id);
	} else {
		DBG_871X(FUNC_ADPT_FMT ": Can't find sta info for " MAC_FMT ", skip macid %s!!\n",
			FUNC_ADPT_ARG(padapter), MAC_ARG(da), power_mode?"sleep":"wakeup");
		rtw_warn_on(1);
	}
	#endif

	do
	{
		ret = _issue_nulldata_to_TDLS_peer_STA(padapter, da, power_mode, wait_ms>0 ? _TRUE : _FALSE);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

void free_tdls_sta(_adapter *padapter, struct sta_info *ptdls_sta)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
   	struct sta_priv *pstapriv = &padapter->stapriv;
	_irqL irqL;
	
	//free peer sta_info
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	if(ptdlsinfo->sta_cnt != 0)
		ptdlsinfo->sta_cnt--;
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	if( ptdlsinfo->sta_cnt < (NUM_STA - 2 - 4) )	// -2: AP + BC/MC sta, -4: default key
	{
		ptdlsinfo->sta_maximum = _FALSE;
		_rtw_memset( &ptdlsinfo->ss_record, 0x00, sizeof(struct tdls_ss_record) );
	}

	//clear cam
	rtw_clearstakey_cmd(padapter, ptdls_sta, _TRUE);

	if(ptdlsinfo->sta_cnt==0){
		rtw_tdls_cmd(padapter, myid(&(padapter->eeprompriv)), TDLS_RS_RCR);
		ptdlsinfo->link_established = _FALSE;
	}
	else
		DBG_871X("Remain tdls sta:%02x\n", ptdlsinfo->sta_cnt);

	rtw_free_stainfo(padapter,  ptdls_sta);
	
}


//TDLS encryption(if needed) will always be CCMP
void rtw_tdls_set_key(_adapter *padapter, struct rx_pkt_attrib *prx_pkt_attrib, struct sta_info *ptdls_sta)
{
	if(prx_pkt_attrib->encrypt)
	{
		ptdls_sta->dot118021XPrivacy=_AES_;
		rtw_setstakey_cmd(padapter, ptdls_sta, _TRUE, _TRUE);
	}
}

void rtw_tdls_process_ht_cap(_adapter *padapter, struct sta_info *ptdls_sta, u8 *data, u8 Length)
{
	/* save HT capabilities in the sta object */
	_rtw_memset(&ptdls_sta->htpriv.ht_cap, 0, sizeof(struct rtw_ieee80211_ht_cap));
	if (data && Length >= sizeof(struct rtw_ieee80211_ht_cap) )
	{
		ptdls_sta->flags |= WLAN_STA_HT;
		
		ptdls_sta->flags |= WLAN_STA_WME;
		
		_rtw_memcpy(&ptdls_sta->htpriv.ht_cap, data, sizeof(struct rtw_ieee80211_ht_cap));			
		
	} else
		ptdls_sta->flags &= ~WLAN_STA_HT;

	if(ptdls_sta->flags & WLAN_STA_HT)
	{
		if(padapter->registrypriv.ht_enable == _TRUE)
		{
			ptdls_sta->htpriv.ht_option = _TRUE;
		}
		else
		{
			ptdls_sta->htpriv.ht_option = _FALSE;
			ptdls_sta->stat_code = _STATS_FAILURE_;
		}
	}

	//HT related cap
	if(ptdls_sta->htpriv.ht_option)
	{
		//check if sta supports rx ampdu
		if(padapter->registrypriv.ampdu_enable==1)
			ptdls_sta->htpriv.ampdu_enable = _TRUE;

		//check if sta support s Short GI 20M
		if(ptdls_sta->htpriv.ht_cap.cap_info & cpu_to_le16(IEEE80211_HT_CAP_SGI_20))
		{
			ptdls_sta->htpriv.sgi_20m = _TRUE;
		}
		//check if sta support s Short GI 40M
		if(ptdls_sta->htpriv.ht_cap.cap_info & cpu_to_le16(IEEE80211_HT_CAP_SGI_40))
		{
			ptdls_sta->htpriv.sgi_40m = _TRUE;
		}

		// bwmode would still followed AP's setting
		if(ptdls_sta->htpriv.ht_cap.cap_info & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH))
		{
			if (padapter->mlmeextpriv.cur_bwmode >= CHANNEL_WIDTH_40)
				ptdls_sta->bw_mode = CHANNEL_WIDTH_40;
			ptdls_sta->htpriv.ch_offset = padapter->mlmeextpriv.cur_ch_offset;
		}
	}

}

int rtw_tdls_set_ht_cap(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	int tmplen;

	rtw_ht_use_default_setting(padapter);

	tmplen = pattrib->pktlen;
	rtw_restructure_ht_ie(padapter, NULL, pframe, 0, &(pattrib->pktlen), padapter->mlmeextpriv.cur_channel);

	return (pattrib->pktlen - tmplen);

}

u8 *rtw_tdls_set_sup_ch(struct mlme_ext_priv *pmlmeext, u8 *pframe, struct pkt_attrib *pattrib)
{
	u8 sup_ch[ 30 * 2 ] = { 0x00 }, ch_set_idx = 0;	//For supported channel
	u8 ch_24g = 0, b1 = 0, b4 = 0;
	u8 bit_table = 0, sup_ch_idx = 0;

	do{
		if( pmlmeext->channel_set[ch_set_idx].ChannelNum >= 1 &&
			pmlmeext->channel_set[ch_set_idx].ChannelNum <= 14 )
		{
			ch_24g = 1;	// 2.4 G channels
		}
		else if( pmlmeext->channel_set[ch_set_idx].ChannelNum >= 36 && 
			pmlmeext->channel_set[ch_set_idx].ChannelNum <= 48)
		{
			b1 = 1;	// 5 G band1
		}
		else if( pmlmeext->channel_set[ch_set_idx].ChannelNum >= 149 && 
			pmlmeext->channel_set[ch_set_idx].ChannelNum <= 165)
		{
			b4 = 1;	// 5 G band4
		}
		else
		{
			ch_set_idx++;	// We don't claim that we support DFS channels.
			continue;
		}

		sup_ch_idx = (ch_24g + b1 + b4 - 1) * 2;
		if( sup_ch_idx >= 0)
		{
			if(sup_ch[sup_ch_idx] == 0)
				sup_ch[sup_ch_idx] = pmlmeext->channel_set[ch_set_idx].ChannelNum;
			sup_ch[sup_ch_idx+1]++;	//Number of channel
		}

		ch_set_idx++;
	}
	while( pmlmeext->channel_set[ch_set_idx].ChannelNum != 0 && ch_set_idx < MAX_CHANNEL_NUM );

	return(rtw_set_ie(pframe, _SUPPORTED_CH_IE_, sup_ch_idx+2, sup_ch, &(pattrib->pktlen)));
}

#ifdef CONFIG_WFD
void rtw_tdls_process_wfd_ie(struct tdls_info *ptdlsinfo, u8 *ptr, u8 length)
{
	u8	wfd_ie[ 128 ] = { 0x00 };
	u32	wfd_ielen = 0;
	u32	wfd_offset = 0;
	//	Try to get the TCP port information when receiving the negotiation response.
	//

	wfd_offset = 0;
	wfd_offset = rtw_get_wfd_ie( ptr + wfd_offset, length - wfd_offset, wfd_ie, &wfd_ielen );
	while( wfd_offset )
	{
		u8	attr_content[ 10 ] = { 0x00 };
		u32	attr_contentlen = 0;
		int	i;

		DBG_871X( "[%s] WFD IE Found!!\n", __FUNCTION__ );
		rtw_get_wfd_attr_content( wfd_ie, wfd_ielen, WFD_ATTR_DEVICE_INFO, attr_content, &attr_contentlen);
		if ( attr_contentlen )
		{
			ptdlsinfo->wfd_info->peer_rtsp_ctrlport = RTW_GET_BE16( attr_content + 2 );
			DBG_871X( "[%s] Peer PORT NUM = %d\n", __FUNCTION__, ptdlsinfo->wfd_info->peer_rtsp_ctrlport );
		}

		_rtw_memset( attr_content, 0x00, 10);
		attr_contentlen = 0;
		rtw_get_wfd_attr_content( wfd_ie, wfd_ielen, WFD_ATTR_LOCAL_IP_ADDR, attr_content, &attr_contentlen);
		if ( attr_contentlen )
		{
			_rtw_memcpy(ptdlsinfo->wfd_info->peer_ip_address, ( attr_content + 1 ), 4);
			DBG_871X( "[%s] Peer IP = %02u.%02u.%02u.%02u \n", __FUNCTION__, 
				ptdlsinfo->wfd_info->peer_ip_address[0], ptdlsinfo->wfd_info->peer_ip_address[1],
				ptdlsinfo->wfd_info->peer_ip_address[2], ptdlsinfo->wfd_info->peer_ip_address[3]
				);
		}
		wfd_offset = rtw_get_wfd_ie( ptr + wfd_offset, length - wfd_offset, wfd_ie, &wfd_ielen );
	}
}

int issue_tunneled_probe_req(_adapter *padapter)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	u8 baddr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; 
	struct tdls_txmgmt txmgmt;
	int ret = _FAIL;

	DBG_871X("[%s]\n", __FUNCTION__);

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = TUNNELED_PROBE_REQ;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, baddr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}
	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;
exit:

	return ret;
}

int issue_tunneled_probe_rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _FAIL;

	DBG_871X("[%s]\n", __FUNCTION__);

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = TUNNELED_PROBE_RSP;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, precv_frame->u.hdr.attrib.src, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}
	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;
exit:

	return ret;
}
#endif //CONFIG_WFD

int issue_tdls_setup_req(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, int wait_ack)
{
	struct tdls_info	*ptdlsinfo = &padapter->tdlsinfo;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
   	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *ptdls_sta= NULL;
	_irqL irqL;
	static u8 dialogtoken = 0;
	int ret = _FAIL;
	u32 timeout_interval= TPK_RESEND_COUNT * 1000;	//retry timer should set at least 301 sec, using TPK_count counting 301 times.

	ptxmgmt->action_code = TDLS_SETUP_REQUEST;
	if(ptdlsinfo->ap_prohibited == _TRUE)
		goto exit;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);

	//init peer sta_info
	ptdls_sta = rtw_get_stainfo(pstapriv, ptxmgmt->peer);
	if(ptdls_sta==NULL)
	{
		ptdls_sta = rtw_alloc_stainfo(pstapriv, ptxmgmt->peer);
		if(ptdls_sta==NULL)
		{
			DBG_871X("[%s] rtw_alloc_stainfo fail\n", __FUNCTION__);	
			rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
			rtw_free_xmitframe(pxmitpriv, pmgntframe);
			goto exit;
		}
	}
	
	if(!(ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE))
		ptdlsinfo->sta_cnt++;
	if( ptdlsinfo->sta_cnt == (NUM_STA - 2 - 4) )	// -2: AP + BC/MC sta, -4: default key
	{
		ptdlsinfo->sta_maximum  = _TRUE;
	}

	ptdls_sta->tdls_sta_state |= TDLS_RESPONDER_STATE;
	//for tdls; ptdls_sta->aid is used to fill dialogtoken
	ptdls_sta->dialog = dialogtoken;
	dialogtoken = (dialogtoken+1)%256;
	ptdls_sta->TDLS_PeerKey_Lifetime = timeout_interval;
	_set_timer( &ptdls_sta->handshake_timer, TDLS_HANDSHAKE_TIME );

	pattrib->qsel = pattrib->priority;

	if(rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

	ret = _SUCCESS;

exit:

	return ret;
}

int issue_tdls_teardown(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, u8 wait_ack)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info	*ptdls_sta=NULL;
	_irqL irqL;
	int ret = _FAIL;

	ptxmgmt->action_code = TDLS_TEARDOWN;
	ptdls_sta = rtw_get_stainfo(pstapriv, ptxmgmt->peer);
	if(ptdls_sta==NULL){
		DBG_871X("Np tdls_sta for tearing down\n");
		goto exit;
	}

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

	if(ret == _SUCCESS)
	{
		if(ptdls_sta->tdls_sta_state & TDLS_CH_SWITCH_ON_STATE){
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CS_OFF);
		}
		
		if( ptdls_sta->timer_flag == 1 )
		{
			_enter_critical_bh(&(padapter->tdlsinfo.hdl_lock), &irqL);
			ptdls_sta->timer_flag = 2;
			_exit_critical_bh(&(padapter->tdlsinfo.hdl_lock), &irqL);
		}
		else
			rtw_tdls_cmd(padapter, ptxmgmt->peer, TDLS_TEAR_STA );
	}

exit:

	return ret;
}

int issue_tdls_dis_req(_adapter *padapter, struct tdls_txmgmt *ptxmgmt)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	int ret = _FAIL;
	
	ptxmgmt->action_code = TDLS_DISCOVERY_REQUEST;
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}
	dump_mgntframe(padapter, pmgntframe);
	DBG_871X("issue tdls dis req\n");

	ret = _SUCCESS;
exit:

	return ret;
}

int issue_tdls_setup_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	int ret = _FAIL;

	ptxmgmt->action_code = TDLS_SETUP_RESPONSE;		
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(&(padapter->mlmepriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);

	ret = _SUCCESS;
exit:

	return ret;

}

int issue_tdls_setup_cfm(_adapter *padapter, struct tdls_txmgmt *ptxmgmt)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	int ret = _FAIL;
	
	ptxmgmt->action_code = TDLS_SETUP_CONFIRM;
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&padapter->eeprompriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(&padapter->mlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;		
	}

	dump_mgntframe(padapter, pmgntframe);

	ret = _SUCCESS;
exit:

	return ret;

}

//TDLS Discovery Response frame is a management action frame
int issue_tdls_dis_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, u8 privacy)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	int ret = _FAIL;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//	unicast probe request frame
	_rtw_memcpy(pwlanhdr->addr1, ptxmgmt->peer, ETH_ALEN);
	_rtw_memcpy(pattrib->dst, pwlanhdr->addr1, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
	_rtw_memcpy(pattrib->src, pwlanhdr->addr2, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_bssid(&padapter->mlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, pwlanhdr->addr3, ETH_ALEN);
	
	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof (struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);

	rtw_build_tdls_dis_rsp_ies(padapter, pmgntframe, pframe, ptxmgmt->dialog_token, privacy);

	pattrib->nr_frags = 1;
	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;

exit:
	return ret;
}

int issue_tdls_peer_traffic_rsp(_adapter *padapter, struct sta_info *ptdls_sta)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _FAIL;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = TDLS_PEER_TRAFFIC_RESPONSE;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptdls_sta->hwaddr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;

	if(rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;	
	}

	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;

exit:

	return ret;
}

int issue_tdls_peer_traffic_indication(_adapter *padapter, struct sta_info *ptdls_sta)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _FAIL;
	static u8 dialogtoken=0;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = TDLS_PEER_TRAFFIC_INDICATION;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptdls_sta->hwaddr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	//for tdls; pattrib->nr_frags is used to fill dialogtoken
	ptdls_sta->dialog = dialogtoken;
	dialogtoken = (dialogtoken+1)%256;
	//PTI frame's priority should be AC_VO
	pattrib->priority = 7; 

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;
	
exit:

	return ret;
}

int issue_tdls_ch_switch_req(_adapter *padapter, u8 *mac_addr)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _FAIL;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = TDLS_CHANNEL_SWITCH_REQUEST;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if(rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;
exit:

	return ret;
}

int issue_tdls_ch_switch_rsp(_adapter *padapter, u8 *mac_addr)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _FAIL;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = TDLS_CHANNEL_SWITCH_RESPONSE;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
/*
	_enter_critical_bh(&pxmitpriv->lock, &irqL);
	if(xmitframe_enqueue_for_tdls_sleeping_sta(padapter, pmgntframe)==_TRUE){
		_exit_critical_bh(&pxmitpriv->lock, &irqL);
		return _FALSE;
	}
*/
	if(rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;	
	}
	dump_mgntframe(padapter, pmgntframe);
	ret = _SUCCESS;
exit:

	return ret;
}

int On_TDLS_Dis_Rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta = NULL, *psta = rtw_get_stainfo(&(padapter->stapriv), get_bssid(&(padapter->mlmepriv)));
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	u8 *ptr = precv_frame->u.hdr.rx_data, *psa;
	struct rx_pkt_attrib *pattrib = &(precv_frame->u.hdr.attrib);
	struct tdls_info *ptdlsinfo = &(padapter->tdlsinfo);
	u8 empty_addr[ETH_ALEN] = { 0x00 };
	int UndecoratedSmoothedPWDB;
	struct tdls_txmgmt txmgmt;	
	int ret = _SUCCESS;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	//WFDTDLS: for sigma test, not to setup direct link automatically
	ptdlsinfo->dev_discovered = 1;

#ifdef CONFIG_TDLS_AUTOSETUP
	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), psa);

	if(ptdls_sta != NULL)
	{
		ptdls_sta->tdls_sta_state |= TDLS_ALIVE_STATE;

		//Record the tdls sta with lowest signal strength
		if( (ptdlsinfo->sta_maximum == _TRUE) && (ptdls_sta->alive_count >= 1) )
		{
			if( _rtw_memcmp(ptdlsinfo->ss_record.macaddr, empty_addr, ETH_ALEN) )
			{
				_rtw_memcpy(ptdlsinfo->ss_record.macaddr, psa, ETH_ALEN);
				ptdlsinfo->ss_record.RxPWDBAll = pattrib->RxPWDBAll;
			}
			else
			{
				if( ptdlsinfo->ss_record.RxPWDBAll < pattrib->RxPWDBAll )
				{
					_rtw_memcpy(ptdlsinfo->ss_record.macaddr, psa, ETH_ALEN);
					ptdlsinfo->ss_record.RxPWDBAll = pattrib->RxPWDBAll;
				}
			}
		}

	}
	else
	{
		if( ptdlsinfo->sta_maximum == _TRUE)
		{
			if( _rtw_memcmp( ptdlsinfo->ss_record.macaddr, empty_addr, ETH_ALEN ) )
			{
				//All traffics are busy, do not set up another direct link.
				ret = _FAIL;
				goto exit;
			}
			else
			{
				if( pattrib->RxPWDBAll > ptdlsinfo->ss_record.RxPWDBAll )
				{
					_rtw_memcpy(txmgmt.peer, ptdlsinfo->ss_record.macaddr, ETH_ALEN);
					//issue_tdls_teardown(padapter, ptdlsinfo->ss_record.macaddr, _FALSE);
				}
				else
				{
					ret = _FAIL;
					goto exit;
				}
			}
		}

		padapter->HalFunc.GetHalDefVarHandler(padapter, HAL_DEF_UNDERCORATEDSMOOTHEDPWDB, &UndecoratedSmoothedPWDB);

		if( pattrib->RxPWDBAll + TDLS_SIGNAL_THRESH >= UndecoratedSmoothedPWDB);
		{
			DBG_871X("pattrib->RxPWDBAll=%d, pdmpriv->UndecoratedSmoothedPWDB=%d\n", pattrib->RxPWDBAll, UndecoratedSmoothedPWDB);
			_rtw_memcpy(txmgmt.peer, psa, ETH_ALEN);
			issue_tdls_setup_req(padapter, &txmgmt, _FALSE);
		}
	}
#endif //CONFIG_TDLS_AUTOSETUP

exit:
	return ret;

}

sint On_TDLS_Setup_Req(_adapter *padapter, union recv_frame *precv_frame)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 *psa, *pmyid;
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *prsnie, *ppairwise_cipher;
	u8 i, k;
	u8 ccmp_included=0, rsnie_included=0;
	u16 j, pairwise_count;
	u8 SNonce[32];
	u32 *timeout_interval=NULL;
	sint parsing_length;	//frame body length, without icv_len
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 5;
	unsigned char		supportRate[16];
	int				supportRateNum = 0;
	struct tdls_txmgmt txmgmt;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	pmyid=myid(&(padapter->eeprompriv));
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-ETH_TYPE_LEN
			-PAYLOAD_TYPE_LEN
			-FIXED_IE;

	if(ptdlsinfo->ap_prohibited == _TRUE)
	{
		goto exit;
	}

	if(ptdls_sta==NULL){
		ptdls_sta = rtw_alloc_stainfo(pstapriv, psa);
	}else{
		if(ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE){
			//If the direct link is already set up
			//Process as re-setup after tear down
			DBG_871X("re-setup a direct link\n");
		}
		//already receiving TDLS setup request
		else if(ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE){
			DBG_871X("receive duplicated TDLS setup request frame in handshaking\n");
			goto exit;
		}
		//When receiving and sending setup_req to the same link at the same time, STA with higher MAC_addr would be initiator
		//following is to check out MAC_addr
		else if(ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE){
			DBG_871X("receive setup_req after sending setup_req\n");
			for (i=0;i<6;i++){
				if(*(pmyid+i)==*(psa+i)){
				}
				else if(*(pmyid+i)>*(psa+i)){
					ptdls_sta->tdls_sta_state=TDLS_INITIATOR_STATE;
					break;
				}else if(*(pmyid+i)<*(psa+i)){
					goto exit;
				}
			}
		}
	}

	if(ptdls_sta) 
	{
		ptdls_sta->dialog = *(ptr+2);	//copy dialog token
		ptdls_sta->stat_code = 0;

		//parsing information element
		for(j=FIXED_IE; j<parsing_length;){

			pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

			switch (pIE->ElementID)
			{
				case _SUPPORTEDRATES_IE_:
					_rtw_memcpy(supportRate, pIE->data, pIE->Length);
					supportRateNum = pIE->Length;
					break;
				case _COUNTRY_IE_:
					break;
				case _EXT_SUPPORTEDRATES_IE_:
					if(supportRateNum<=sizeof(supportRate))
					{
						_rtw_memcpy(supportRate+supportRateNum, pIE->data, pIE->Length);
						supportRateNum += pIE->Length;
					}
					break;
				case _SUPPORTED_CH_IE_:
					break;
				case _RSN_IE_2_:
					rsnie_included=1;
					if(prx_pkt_attrib->encrypt){
						prsnie=(u8*)pIE;
						//check whether initiator STA has CCMP pairwise_cipher.
						ppairwise_cipher=prsnie+10;
						_rtw_memcpy(ptdls_sta->TDLS_RSNIE, pIE->data, pIE->Length);
						pairwise_count = *(u16*)(ppairwise_cipher-2);
						for(k=0;k<pairwise_count;k++){
							if(_rtw_memcmp( ppairwise_cipher+4*k, RSN_CIPHER_SUITE_CCMP, 4)==_TRUE)
								ccmp_included=1;
						}
						if(ccmp_included==0){
							//invalid contents of RSNIE
							ptdls_sta->stat_code=72;
						}
					}
					break;
				case _EXT_CAP_IE_:
					break;
				case _VENDOR_SPECIFIC_IE_:
					break;
				case _FTIE_:
					if(prx_pkt_attrib->encrypt)
						_rtw_memcpy(SNonce, (ptr+j+52), 32);
					break;
				case _TIMEOUT_ITVL_IE_:
					if(prx_pkt_attrib->encrypt)
						timeout_interval = (u32 *)(ptr+j+3);
					break;
				case _RIC_Descriptor_IE_:
					break;
				case _HT_CAPABILITY_IE_:
					rtw_tdls_process_ht_cap(padapter, ptdls_sta, pIE->data, pIE->Length);
					break;
				case EID_BSSCoexistence:
					break;
				case _LINK_ID_IE_:
					if(_rtw_memcmp(get_bssid(pmlmepriv), pIE->data, 6) == _FALSE)
					{
						//not in the same BSS
						ptdls_sta->stat_code=7;
					}
					break;
				default:
					break;
			}

			j += (pIE->Length + 2);
			
		}

		//check status code
		//if responder STA has/hasn't security on AP, but request hasn't/has RSNIE, it should reject
		if(ptdls_sta->stat_code == 0 )
		{
			if(rsnie_included && (prx_pkt_attrib->encrypt==0)){
				//security disabled
				ptdls_sta->stat_code = 5;
			}else if(rsnie_included==0 && (prx_pkt_attrib->encrypt)){
				//request haven't RSNIE
				ptdls_sta->stat_code = 38;
			}

#ifdef CONFIG_WFD
			//WFD test plan version 0.18.2 test item 5.1.5
			//SoUT does not use TDLS if AP uses weak security
			if ( padapter->wdinfo.wfd_tdls_enable )
			{
				if(rsnie_included && (prx_pkt_attrib->encrypt != _AES_))
				{
					ptdls_sta->stat_code = 5;
				}
			}
#endif //CONFIG_WFD
		}

		ptdls_sta->tdls_sta_state|= TDLS_INITIATOR_STATE;
		if(prx_pkt_attrib->encrypt){
			_rtw_memcpy(ptdls_sta->SNonce, SNonce, 32);
			_rtw_memcpy(&(ptdls_sta->TDLS_PeerKey_Lifetime), timeout_interval, 4);
		}

		//update station supportRate	
		ptdls_sta->bssratelen = supportRateNum;
		_rtw_memcpy(ptdls_sta->bssrateset, supportRate, supportRateNum);

		if(!(ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE))
			ptdlsinfo->sta_cnt++;
		if( ptdlsinfo->sta_cnt == (NUM_STA - 2 - 4) )	// -2: AP + BC/MC sta, -4: default key
		{
			ptdlsinfo->sta_maximum = _TRUE;
		}

#ifdef CONFIG_WFD
		rtw_tdls_process_wfd_ie(ptdlsinfo, ptr + FIXED_IE, parsing_length - FIXED_IE);
#endif // CONFIG_WFD

	}
	else
	{
		goto exit;
	}

	_rtw_memcpy(txmgmt.peer, prx_pkt_attrib->src, ETH_ALEN);
	issue_tdls_setup_rsp(padapter, &txmgmt);

	if(ptdls_sta->stat_code==0)
	{
		_set_timer( &ptdls_sta->handshake_timer, TDLS_HANDSHAKE_TIME);
	}
	else		//status code!=0 ; setup unsuccess
	{
		free_tdls_sta(padapter, ptdls_sta);
	}
		
exit:
	
	return _SUCCESS;
}

int On_TDLS_Setup_Rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa;
	u16 stat_code;
	sint parsing_length;	//frame body length, without icv_len
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =7;
	u8 ANonce[32];
	u8  *pftie=NULL, *ptimeout_ie=NULL, *plinkid_ie=NULL, *prsnie=NULL, *pftie_mic=NULL, *ppairwise_cipher=NULL;
	u16 pairwise_count, j, k;
	u8 verify_ccmp=0;
	unsigned char		supportRate[16];
	int				supportRateNum = 0;
	struct tdls_txmgmt txmgmt;
	int ret = _SUCCESS;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	if ( NULL == ptdls_sta )
	{
		ret = _FAIL;
		goto exit;
	}

	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;

	_rtw_memcpy(&stat_code, ptr+2, 2);
	
	if(stat_code!=0)
	{
		DBG_871X( "[%s] status_code = %d, free_tdls_sta\n", __FUNCTION__, stat_code );
		free_tdls_sta(padapter, ptdls_sta);
		ret = _FAIL;
		goto exit;
	}

	stat_code = 0;

	//parsing information element
	for(j=FIXED_IE; j<parsing_length;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
			case _SUPPORTEDRATES_IE_:
				_rtw_memcpy(supportRate, pIE->data, pIE->Length);
				supportRateNum = pIE->Length;
				break;
			case _COUNTRY_IE_:
				break;
			case _EXT_SUPPORTEDRATES_IE_:
				if(supportRateNum<=sizeof(supportRate))
				{
					_rtw_memcpy(supportRate+supportRateNum, pIE->data, pIE->Length);
					supportRateNum += pIE->Length;
				}
				break;
			case _SUPPORTED_CH_IE_:
				break;
			case _RSN_IE_2_:
				prsnie=(u8*)pIE;
				//check whether responder STA has CCMP pairwise_cipher.
				ppairwise_cipher=prsnie+10;
				_rtw_memcpy(&pairwise_count, (u16*)(ppairwise_cipher-2), 2);
				for(k=0;k<pairwise_count;k++){
					if(_rtw_memcmp( ppairwise_cipher+4*k, RSN_CIPHER_SUITE_CCMP, 4)==_TRUE)
						verify_ccmp=1;
				}
			case _EXT_CAP_IE_:
				break;
			case _VENDOR_SPECIFIC_IE_:
				break;
			case _FTIE_:
				pftie=(u8*)pIE;
				//_rtw_memcpy(ptdls_sta->ANonce, (ptr+j+20), 32);
				_rtw_memcpy(ANonce, (ptr+j+20), 32);
				break;
			case _TIMEOUT_ITVL_IE_:
				ptimeout_ie=(u8*)pIE;
				break;
			case _RIC_Descriptor_IE_:
				break;
			case _HT_CAPABILITY_IE_:
				rtw_tdls_process_ht_cap(padapter, ptdls_sta, pIE->data, pIE->Length);
				break;
			case EID_BSSCoexistence:
				break;
			case _LINK_ID_IE_:
				plinkid_ie=(u8*)pIE;
				break;
			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	//update station's supportRate	
	ptdls_sta->bssratelen = supportRateNum;
	_rtw_memcpy(ptdls_sta->bssrateset, supportRate, supportRateNum);

	_rtw_memcpy(ptdls_sta->ANonce, ANonce, 32);

#ifdef CONFIG_WFD
	rtw_tdls_process_wfd_ie(ptdlsinfo, ptr + FIXED_IE, parsing_length - FIXED_IE);
#endif // CONFIG_WFD

	if(stat_code != 0)
	{
		ptdls_sta->stat_code = stat_code;
	}
	else
	{
		if(prx_pkt_attrib->encrypt)
		{
			if(verify_ccmp==1)
			{
				wpa_tdls_generate_tpk(padapter, ptdls_sta);
				ptdls_sta->stat_code=0;
				if(tdls_verify_mic(ptdls_sta->tpk.kck, 2, plinkid_ie, prsnie, ptimeout_ie, pftie)==0)	//0: Invalid, 1: valid
				{
					free_tdls_sta(padapter, ptdls_sta);
					ret = _FAIL;
					goto exit;
				}
			}
			else
			{
				ptdls_sta->stat_code=72;	//invalide contents of RSNIE
			}

		}else{
			ptdls_sta->stat_code=0;
		}
	}

	DBG_871X("issue_tdls_setup_cfm\n");
	_rtw_memcpy(txmgmt.peer, prx_pkt_attrib->src, ETH_ALEN);
	issue_tdls_setup_cfm(padapter, &txmgmt);

	if(ptdls_sta->stat_code==0)
	{
		ptdlsinfo->link_established = _TRUE;

		if( ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE )
		{
			ptdls_sta->tdls_sta_state |= TDLS_LINKED_STATE;
			_cancel_timer_ex( &ptdls_sta->handshake_timer);
		}

		rtw_tdls_set_key(padapter, prx_pkt_attrib, ptdls_sta);

		rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_ESTABLISHED);

	}

exit:
	return ret;

}

int On_TDLS_Setup_Cfm(_adapter *padapter, union recv_frame *precv_frame)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa; 
	u16 stat_code;
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =5;
	u8  *pftie=NULL, *ptimeout_ie=NULL, *plinkid_ie=NULL, *prsnie=NULL, *pftie_mic=NULL, *ppairwise_cipher=NULL;
	u16 j, pairwise_count;
	int ret = _SUCCESS;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	if(ptdls_sta == NULL)
	{
		DBG_871X( "[%s] Direct Link Peer = "MAC_FMT" not found\n", __FUNCTION__, MAC_ARG(psa) );
		ret = _FAIL;
		goto exit;
	}

	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-ETH_TYPE_LEN
			-PAYLOAD_TYPE_LEN
			-FIXED_IE;
	_rtw_memcpy(&stat_code, ptr+2, 2);

	if(stat_code!=0){
		DBG_871X( "[%s] stat_code = %d\n, free_tdls_sta", __FUNCTION__, stat_code );
		free_tdls_sta(padapter, ptdls_sta);
		ret = _FAIL;
		goto exit;
	}

	if(prx_pkt_attrib->encrypt){
		//parsing information element
		for(j=FIXED_IE; j<parsing_length;){

			pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

			switch (pIE->ElementID)
			{
				case _RSN_IE_2_:
					prsnie=(u8*)pIE;
					break;
				case _VENDOR_SPECIFIC_IE_:
					break;
	 			case _FTIE_:
					pftie=(u8*)pIE;
					break;
				case _TIMEOUT_ITVL_IE_:
					ptimeout_ie=(u8*)pIE;
					break;
				case _HT_EXTRA_INFO_IE_:
					break;
	 			case _LINK_ID_IE_:
					plinkid_ie=(u8*)pIE;
					break;
				default:
					break;
			}

			j += (pIE->Length + 2);
			
		}

		//verify mic in FTIE MIC field
		if(tdls_verify_mic(ptdls_sta->tpk.kck, 3, plinkid_ie, prsnie, ptimeout_ie, pftie)==0){	//0: Invalid, 1: Valid
			free_tdls_sta(padapter, ptdls_sta);
			ret = _FAIL;
			goto exit;
		}

	}

	ptdlsinfo->link_established = _TRUE;
	if( ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE )
	{
		ptdls_sta->tdls_sta_state|=TDLS_LINKED_STATE;
		_cancel_timer_ex( &ptdls_sta->handshake_timer);
	}

	rtw_tdls_set_key(padapter, prx_pkt_attrib, ptdls_sta);

	rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_ESTABLISHED);

exit:
	return ret;

}

int On_TDLS_Dis_Req(_adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta_ap;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	sint parsing_length;	//frame body length, without icv_len
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 3, *dst;
	u16 j;
	struct tdls_txmgmt txmgmt;
	int ret = _SUCCESS;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE + 1;
	txmgmt.dialog_token = *(ptr+2);
	_rtw_memcpy(&txmgmt.peer, precv_frame->u.hdr.attrib.src, ETH_ALEN);
	txmgmt.action_code = TDLS_DISCOVERY_RESPONSE;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;

	//parsing information element
	for(j=FIXED_IE; j<parsing_length;){

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
			case _LINK_ID_IE_:
				psta_ap = rtw_get_stainfo(pstapriv, pIE->data);
				if(psta_ap == NULL)
				{
					goto exit;
				}
				dst = pIE->data + 12;
				if( (MacAddr_isBcst(dst) == _FALSE) && (_rtw_memcmp(myid(&(padapter->eeprompriv)), dst, 6) == _FALSE) )
				{
					goto exit;
				}
				break;
			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	issue_tdls_dis_rsp(padapter, &txmgmt, prx_pkt_attrib->privacy);

exit:
	return ret;
	
}

int On_TDLS_Teardown(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *psa;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);	
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	struct sta_info *ptdls_sta= NULL;
	_irqL irqL;

	psa = get_sa(ptr);

	ptdls_sta = rtw_get_stainfo(pstapriv, psa);
	if(ptdls_sta!=NULL){
		if(ptdls_sta->tdls_sta_state & TDLS_CH_SWITCH_ON_STATE){
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CS_OFF);
		}
		free_tdls_sta(padapter, ptdls_sta);
	}
		
	return _SUCCESS;
	
}

u8 TDLS_check_ch_state(uint state){
	if(	(state & TDLS_CH_SWITCH_ON_STATE) &&
		(state & TDLS_AT_OFF_CH_STATE) &&
		(state & TDLS_PEER_AT_OFF_STATE) ){

		if(state & TDLS_PEER_SLEEP_STATE)
			return 2;	//U-APSD + ch. switch
		else
			return 1;	//ch. switch
	}else
		return 0;
}

int On_TDLS_Peer_Traffic_Indication(_adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	struct sta_info *ptdls_sta = rtw_get_stainfo(&padapter->stapriv, pattrib->src);	
	u8 *ptr = precv_frame->u.hdr.rx_data;

	ptr +=pattrib->hdrlen + pattrib->iv_len + LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE + 1;

	if(ptdls_sta != NULL)
	{
		ptdls_sta->dialog = *(ptr+2);
		issue_tdls_peer_traffic_rsp(padapter, ptdls_sta);

		//issue_nulldata_to_TDLS_peer_STA(padapter, ptdls_sta->hwaddr, 0, 0, 0);
	}
	else
	{
		DBG_871X("from unknown sta:"MAC_FMT"\n", MAC_ARG(pattrib->src));
		return _FAIL;
	}

	return _SUCCESS;
}

//we process buffered data for 1. U-APSD, 2. ch. switch, 3. U-APSD + ch. switch here
int On_TDLS_Peer_Traffic_Rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	//get peer sta infomation
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->src);
	u8 wmmps_ac=0, state=TDLS_check_ch_state(ptdls_sta->tdls_sta_state);
	int i;
	
	ptdls_sta->sta_stats.rx_data_pkts++;

	ptdls_sta->tdls_sta_state &= ~(TDLS_WAIT_PTR_STATE);

	//receive peer traffic response frame, sleeping STA wakes up
	//ptdls_sta->tdls_sta_state &= ~(TDLS_PEER_SLEEP_STATE);
	//process_wmmps_data( padapter, precv_frame);

	// if noticed peer STA wakes up by receiving peer traffic response
	// and we want to do channel swtiching, then we will transmit channel switch request first
	if(ptdls_sta->tdls_sta_state & TDLS_APSD_CHSW_STATE){
		issue_tdls_ch_switch_req(padapter, pattrib->src);
		ptdls_sta->tdls_sta_state &= ~(TDLS_APSD_CHSW_STATE);
		return  _SUCCESS;
	}

	//check 4-AC queue bit
	if(ptdls_sta->uapsd_vo || ptdls_sta->uapsd_vi || ptdls_sta->uapsd_be || ptdls_sta->uapsd_bk)
		wmmps_ac=1;

	//if it's a direct link and have buffered frame
	if(ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE){
		//[TDLS] UAPSD
		//if(wmmps_ac && state)
		if(wmmps_ac && 1)
		{
			_irqL irqL;	 
			_list	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe=NULL;
		
			_enter_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);	

			xmitframe_phead = get_list_head(&ptdls_sta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			//transmit buffered frames
			while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
			{			
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);
				xmitframe_plist = get_next(xmitframe_plist);
				rtw_list_delete(&pxmitframe->list);

				ptdls_sta->sleepq_len--;
				ptdls_sta->sleepq_ac_len--;
				if(ptdls_sta->sleepq_len>0){
					pxmitframe->attrib.mdata = 1;
					pxmitframe->attrib.eosp = 0;
				}else{
					pxmitframe->attrib.mdata = 0;
					pxmitframe->attrib.eosp = 1;
				}
				pxmitframe->attrib.triggered = 1;

				rtw_hal_xmitframe_enqueue(padapter, pxmitframe);


			}

			if(ptdls_sta->sleepq_len==0)
			{
				DBG_871X("no buffered packets for tdls to xmit\n");
				//on U-APSD + CH. switch state, when there is no buffered date to xmit,
				// we should go back to base channel
				if(state==2){
					rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CS_OFF);
				}else if(ptdls_sta->tdls_sta_state&TDLS_SW_OFF_STATE){
						ptdls_sta->tdls_sta_state &= ~(TDLS_SW_OFF_STATE);
						ptdlsinfo->candidate_ch= pmlmeext->cur_channel;
						issue_tdls_ch_switch_req(padapter, pattrib->src);
						DBG_871X("issue tdls ch switch req back to base channel\n");
				}
				
			}
			else
			{
				DBG_871X("error!psta->sleepq_len=%d\n", ptdls_sta->sleepq_len);
				ptdls_sta->sleepq_len=0;						
			}

			_exit_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);			
		
		}

	}

	return _SUCCESS;
}

sint On_TDLS_Ch_Switch_Req(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa; 
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =3;
	u16 j;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);
	
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-ETH_TYPE_LEN
			-PAYLOAD_TYPE_LEN
			-FIXED_IE;

	ptdls_sta->off_ch = *(ptr+2);
	
	//parsing information element
	for(j=FIXED_IE; j<parsing_length;){

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
			case _COUNTRY_IE_:
				break;
			case _CH_SWTICH_ANNOUNCE_:
				break;
 			case _LINK_ID_IE_:
				break;
			case _CH_SWITCH_TIMING_:
				_rtw_memcpy(&ptdls_sta->ch_switch_time, pIE->data, 2);
				_rtw_memcpy(&ptdls_sta->ch_switch_timeout, pIE->data+2, 2);
			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	//todo: check status
	ptdls_sta->stat_code=0;
	ptdls_sta->tdls_sta_state |= TDLS_CH_SWITCH_ON_STATE;

	issue_nulldata(padapter, NULL, 1, 0, 0);

	issue_tdls_ch_switch_rsp(padapter, psa);

	DBG_871X("issue tdls channel switch response\n");

	if((ptdls_sta->tdls_sta_state & TDLS_CH_SWITCH_ON_STATE) && ptdls_sta->off_ch==pmlmeext->cur_channel){
		DBG_871X("back to base channel %x\n", pmlmeext->cur_channel);
		ptdls_sta->option=TDLS_BASE_CH;
		rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_BASE_CH);
	}else{		
		ptdls_sta->option=TDLS_OFF_CH;
		rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_OFF_CH);
	}
	return _SUCCESS;
}

sint On_TDLS_Ch_Switch_Rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa; 
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =4;
	u16 stat_code, j, switch_time, switch_timeout;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	int ret = _SUCCESS;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	//if channel switch is running and receiving Unsolicited TDLS Channel Switch Response,
	//it will go back to base channel and terminate this channel switch procedure
	if(ptdls_sta->tdls_sta_state & TDLS_CH_SWITCH_ON_STATE ){
		if(pmlmeext->cur_channel==ptdls_sta->off_ch){
			DBG_871X("back to base channel %x\n", pmlmeext->cur_channel);
			ptdls_sta->option=TDLS_BASE_CH;
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_BASE_CH);
		}else{
			DBG_871X("receive unsolicited channel switch response \n");
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CS_OFF);
		}
		ret = _FAIL;
		goto exit;
	}

	//avoiding duplicated or unconditional ch. switch. rsp
	if(!(ptdls_sta->tdls_sta_state & TDLS_CH_SW_INITIATOR_STATE))
	{
		ret = _FAIL;
		goto exit;
	}

	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-ETH_TYPE_LEN
			-PAYLOAD_TYPE_LEN
			-FIXED_IE;

	_rtw_memcpy(&stat_code, ptr+2, 2);

	if(stat_code!=0){
		ret = _FAIL;
		goto exit;
	}
	
	//parsing information element
	for(j=FIXED_IE; j<parsing_length;){

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
 			case _LINK_ID_IE_:
				break;
			case _CH_SWITCH_TIMING_:
				_rtw_memcpy(&switch_time, pIE->data, 2);
				if(switch_time > ptdls_sta->ch_switch_time)
					_rtw_memcpy(&ptdls_sta->ch_switch_time, &switch_time, 2);

				_rtw_memcpy(&switch_timeout, pIE->data+2, 2);
				if(switch_timeout > ptdls_sta->ch_switch_timeout)
					_rtw_memcpy(&ptdls_sta->ch_switch_timeout, &switch_timeout, 2);

			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	ptdls_sta->tdls_sta_state &= ~(TDLS_CH_SW_INITIATOR_STATE);
	ptdls_sta->tdls_sta_state |=TDLS_CH_SWITCH_ON_STATE;

	//goto set_channel_workitem_callback()
	ptdls_sta->option=TDLS_OFF_CH;
	rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_OFF_CH);

exit:
	return ret;
}

#ifdef CONFIG_WFD
void wfd_ie_tdls(_adapter * padapter, u8 *pframe, u32 *pktlen )
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->tdlsinfo.wfd_info;
	u8 wfdie[ MAX_WFD_IE_LEN] = { 0x00 };
	u32 wfdielen = 0;

	//	WFD OUI
	wfdielen = 0;
	wfdie[ wfdielen++ ] = 0x50;
	wfdie[ wfdielen++ ] = 0x6F;
	wfdie[ wfdielen++ ] = 0x9A;
	wfdie[ wfdielen++ ] = 0x0A;	//	WFA WFD v1.0

	//	Commented by Albert 20110825
	//	According to the WFD Specification, the negotiation request frame should contain 3 WFD attributes
	//	1. WFD Device Information
	//	2. Associated BSSID ( Optional )
	//	3. Local IP Adress ( Optional )

	//	WFD Device Information ATTR
	//	Type:
	wfdie[ wfdielen++ ] = WFD_ATTR_DEVICE_INFO;

	//	Length:
	//	Note: In the WFD specification, the size of length field is 2.
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	//	Value1:
	//	WFD device information
	//	available for WFD session + Preferred TDLS + WSD ( WFD Service Discovery )
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL 
								| WFD_DEVINFO_PC_TDLS | WFD_DEVINFO_WSD);
	wfdielen += 2;

	//	Value2:
	//	Session Management Control Port
	//	Default TCP port for RTSP messages is 554
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport );
	wfdielen += 2;

	//	Value3:
	//	WFD Device Maximum Throughput
	//	300Mbps is the maximum throughput
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	//	Associated BSSID ATTR
	//	Type:
	wfdie[ wfdielen++ ] = WFD_ATTR_ASSOC_BSSID;

	//	Length:
	//	Note: In the WFD specification, the size of length field is 2.
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	//	Value:
	//	Associated BSSID
	if ( check_fwstate( pmlmepriv, _FW_LINKED) == _TRUE )
	{
		_rtw_memcpy( wfdie + wfdielen, &pmlmepriv->assoc_bssid[ 0 ], ETH_ALEN );
	}
	else
	{
		_rtw_memset( wfdie + wfdielen, 0x00, ETH_ALEN );
	}

	//	Local IP Address ATTR
	wfdie[ wfdielen++ ] = WFD_ATTR_LOCAL_IP_ADDR;

	//	Length:
	//	Note: In the WFD specification, the size of length field is 2.
	RTW_PUT_BE16(wfdie + wfdielen, 0x0005);
	wfdielen += 2;

	//	Version:
	//	0x01: Version1;IPv4
	wfdie[ wfdielen++ ] = 0x01;	

	//	IPv4 Address
	_rtw_memcpy( wfdie + wfdielen, pwfd_info->ip_address, 4 );
	wfdielen += 4;
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, pktlen);
	
}
#endif //CONFIG_WFD

void rtw_build_tdls_setup_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct sta_info *ptdls_sta=rtw_get_stainfo( (&padapter->stapriv) , pattrib->dst);

	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_TDLS;
	u8 action = TDLS_SETUP_REQUEST;
	u8 bssrate[NDIS_802_11_LENGTH_RATES_EX]; //Use NDIS_802_11_LENGTH_RATES_EX in order to call func.rtw_set_supported_rate
 	int	bssrate_len = 0, i = 0 ;
	u8 more_supportedrates = 0;
	unsigned int ie_len;
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8 link_id_addr[18] = {0};
	u8 iedata=0;
	u8 sup_ch[ 30 * 2 ] = {0x00 }, sup_ch_idx = 0, idx_5g = 2;	//For supported channel
	u8 timeout_itvl[5];	//set timeout interval to maximum value
	u32 time;
	u8 *pframe_head;

	//SNonce	
	if(pattrib->encrypt){
		for(i=0;i<8;i++){
			time=rtw_get_current_time();
			_rtw_memcpy(&ptdls_sta->SNonce[4*i], (u8 *)&time, 4);
		}
	}

	pframe_head = pframe;	// For rtw_tdls_set_ht_cap()

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));
	//category, action, dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	//capability
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	if(pattrib->encrypt)
		*pframe =*pframe | cap_Privacy;
	pframe += 2;
	pattrib->pktlen += 2;

	//supported rates
	if(pmlmeext->cur_channel < 14 )
	{
		rtw_set_supported_rate(bssrate, WIRELESS_11BG_24N);
		bssrate_len = IEEE80211_CCK_RATE_LEN + IEEE80211_NUM_OFDM_RATESLEN;
	}
	else
	{
		rtw_set_supported_rate(bssrate, WIRELESS_11A_5N);
		bssrate_len = IEEE80211_NUM_OFDM_RATESLEN;
	}

	//country(optional)

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//extended supported rates
	if(more_supportedrates==1){
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}

	//supported channels
	pframe = rtw_tdls_set_sup_ch(pmlmeext, pframe, pattrib);
	
	//	SRC IE
	pframe = rtw_set_ie( pframe, _SRC_IE_, sizeof(TDLS_SRC), TDLS_SRC, &(pattrib->pktlen));
	
	//RSNIE
	if(pattrib->encrypt)
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, sizeof(TDLS_RSNIE), TDLS_RSNIE, &(pattrib->pktlen));
	
	//extended capabilities
	pframe = rtw_set_ie(pframe, _EXT_CAP_IE_ , sizeof(TDLS_EXT_CAPIE), TDLS_EXT_CAPIE, &(pattrib->pktlen));

	//QoS capability(WMM_IE)
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, sizeof(TDLS_WMMIE), TDLS_WMMIE,  &(pattrib->pktlen));


	if(pattrib->encrypt){
		//FTIE
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		_rtw_memcpy((pframe+52), ptdls_sta->SNonce, 32);
		pframe += 84;
		pattrib->pktlen += 84;

		//Timeout interval
		timeout_itvl[0]=0x02;
		_rtw_memcpy(timeout_itvl+1, (u8 *)(&ptdls_sta->TDLS_PeerKey_Lifetime), 4);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));
	}

	//Sup_reg_classes(optional)
	//HT capabilities
	pframe += rtw_tdls_set_ht_cap(padapter, pframe_head, pattrib);

	//20/40 BSS coexistence
	if(pmlmepriv->num_FortyMHzIntolerant>0)
		iedata |= BIT(2);//20 MHz BSS Width Request
	pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));
	
	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

#ifdef CONFIG_WFD
	wfd_ie_tdls( padapter, pframe, &(pattrib->pktlen) );
#endif //CONFIG_WFD

}

void rtw_build_tdls_setup_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info *ptdls_sta;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;

	u8 payload_type = 0x02;	
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_SETUP_RESPONSE;
	unsigned char	bssrate[NDIS_802_11_LENGTH_RATES_EX];	
	int	bssrate_len = 0;
	u8 more_supportedrates = 0;
	unsigned int ie_len;
	unsigned char *p;
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8 link_id_addr[18] = {0};
	u8 iedata=0;
	u8 timeout_itvl[5];	//setup response timeout interval will copy from request
	u8 ANonce[32];	//maybe it can put in ontdls_req
	u8 k;		//for random ANonce
	u8  *pftie=NULL, *ptimeout_ie=NULL, *plinkid_ie=NULL, *prsnie=NULL, *pftie_mic=NULL;
	u32 time;
	u8 *pframe_head;

	ptdls_sta = rtw_get_stainfo( &(padapter->stapriv) , pattrib->dst);

	if(ptdls_sta == NULL )
	{
		DBG_871X("[%s] %d\n", __FUNCTION__, __LINE__);
		return;
	}

	if(pattrib->encrypt){
		for(k=0;k<8;k++){
			time=rtw_get_current_time();
			_rtw_memcpy(&ptdls_sta->ANonce[4*k], (u8*)&time, 4);
		}
	}

	pframe_head = pframe;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, status code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));

	if(ptdls_sta->stat_code!=0)	//invalid setup request
	{
		DBG_871X("ptdls_sta->stat_code:%04x \n", ptdls_sta->stat_code);		
		return;
	}
	
	//dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	//capability
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	if(pattrib->encrypt )
		*pframe =*pframe | cap_Privacy;
	pframe += 2;
	pattrib->pktlen += 2;

	//supported rates
	//supported rates
	if(pmlmeext->cur_channel < 14 )
	{
		rtw_set_supported_rate(bssrate, WIRELESS_11BG_24N);
		bssrate_len = IEEE80211_CCK_RATE_LEN + IEEE80211_NUM_OFDM_RATESLEN;
	}
	else
	{
		rtw_set_supported_rate(bssrate, WIRELESS_11A_5N);
		bssrate_len = IEEE80211_NUM_OFDM_RATESLEN;
	}

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//country(optional)
	//extended supported rates
	if(more_supportedrates==1){
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}

	//supported channels
	pframe = rtw_tdls_set_sup_ch(pmlmeext, pframe, pattrib);
	
	// SRC IE
	pframe = rtw_set_ie(pframe, _SRC_IE_ , sizeof(TDLS_SRC), TDLS_SRC, &(pattrib->pktlen));

	//RSNIE
	if(pattrib->encrypt){
		prsnie = pframe;
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, sizeof(ptdls_sta->TDLS_RSNIE), ptdls_sta->TDLS_RSNIE, &(pattrib->pktlen));
	}

	//extended capabilities
	pframe = rtw_set_ie(pframe, _EXT_CAP_IE_ , sizeof(TDLS_EXT_CAPIE), TDLS_EXT_CAPIE, &(pattrib->pktlen));

	//QoS capability(WMM_IE)
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, sizeof(TDLS_WMMIE), TDLS_WMMIE,  &(pattrib->pktlen));

	if(pattrib->encrypt){
		wpa_tdls_generate_tpk(padapter, ptdls_sta);

		//FTIE
		pftie = pframe;
		pftie_mic = pframe+4;
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		_rtw_memcpy((pframe+20), ptdls_sta->ANonce, 32);
		_rtw_memcpy((pframe+52), ptdls_sta->SNonce, 32);
		pframe += 84;
		pattrib->pktlen += 84;

		//Timeout interval
		ptimeout_ie = pframe;
		timeout_itvl[0]=0x02;
		_rtw_memcpy(timeout_itvl+1, (u8 *)(&ptdls_sta->TDLS_PeerKey_Lifetime), 4);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));
	}

	//Sup_reg_classes(optional)
	//HT capabilities
	pframe += rtw_tdls_set_ht_cap(padapter, pframe_head, pattrib);

	//20/40 BSS coexistence
	if(pmlmepriv->num_FortyMHzIntolerant>0)
		iedata |= BIT(2);//20 MHz BSS Width Request
	pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));

	//Link identifier
	plinkid_ie = pframe;
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//fill FTIE mic
	if(pattrib->encrypt)
		wpa_tdls_ftie_mic(ptdls_sta->tpk.kck, 2, plinkid_ie, prsnie, ptimeout_ie, pftie, pftie_mic);

#ifdef CONFIG_WFD
	wfd_ie_tdls( padapter, pframe, &(pattrib->pktlen) );
#endif //CONFIG_WFD

}

void rtw_build_tdls_setup_cfm_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{

	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info *ptdls_sta=rtw_get_stainfo( (&padapter->stapriv) , pattrib->dst);

	u8 payload_type = 0x02;	
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_SETUP_CONFIRM;
	u8 more_supportedrates = 0;
	unsigned int ie_len;
	unsigned char *p;
	u8 timeout_itvl[5];	//set timeout interval to maximum value
	u8 wmm_param_ele[24] = {0};
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8	link_id_addr[18] = {0};
	u8  *pftie=NULL, *ptimeout_ie=NULL, *plinkid_ie=NULL, *prsnie=NULL, *pftie_mic=NULL;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));
	//category, action, status code, dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	if(ptdls_sta->stat_code!=0)	//invalid setup request
		return;
	
	//RSNIE
	if(pattrib->encrypt){
		prsnie = pframe;
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, sizeof(TDLS_RSNIE), TDLS_RSNIE, &(pattrib->pktlen));
	}
	
	//EDCA param set; WMM param ele.
	if(pattrib->encrypt){
		//FTIE
		pftie = pframe;
		pftie_mic = pframe+4;
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		_rtw_memcpy((pframe+20), ptdls_sta->ANonce, 32);
		_rtw_memcpy((pframe+52), ptdls_sta->SNonce, 32);
		pframe += 84;
		pattrib->pktlen += 84;

		//Timeout interval
		ptimeout_ie = pframe;
		timeout_itvl[0]=0x02;
		_rtw_memcpy(timeout_itvl+1, (u8 *)(&ptdls_sta->TDLS_PeerKey_Lifetime), 4);
		ptdls_sta->TPK_count=0;
		_set_timer(&ptdls_sta->TPK_timer, ptdls_sta->TDLS_PeerKey_Lifetime/TPK_RESEND_COUNT);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));
	}

	//HT operation; todo
	//Link identifier
	plinkid_ie = pframe;
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//FTIE mic
	if(pattrib->encrypt)
		wpa_tdls_ftie_mic(ptdls_sta->tpk.kck, 3, plinkid_ie, prsnie, ptimeout_ie, pftie, pftie_mic);

	//WMM Parameter Set
	if(&pmlmeinfo->WMM_param)
	{
		_rtw_memcpy(wmm_param_ele, WMM_PARA_OUI, 6);
		_rtw_memcpy(wmm_param_ele+6, (u8 *)&pmlmeinfo->WMM_param, sizeof(pmlmeinfo->WMM_param));
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_,  24, wmm_param_ele, &(pattrib->pktlen));		
	}

}

void rtw_build_tdls_teardown_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{

	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	u8 action = ptxmgmt->action_code;
	u8 link_id_addr[18] = {0};
	struct sta_info *ptdls_sta = rtw_get_stainfo( &(padapter->stapriv) , pattrib->dst);

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptxmgmt->status_code, &(pattrib->pktlen));

	//Link identifier
	if(ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE){	
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	}else  if(ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE){
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	}
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));
	
}

void rtw_build_tdls_dis_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_TDLS;
	u8 action = TDLS_DISCOVERY_REQUEST;
	u8	link_id_addr[18] = {0};
	static u8 dialogtoken=0;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));		
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	if(ptxmgmt->external_support == _TRUE) {
		pframe = rtw_set_fixed_ie(pframe, 1, &(ptxmgmt->dialog_token), &(pattrib->pktlen));
	} else {
		pframe = rtw_set_fixed_ie(pframe, 1, &(dialogtoken), &(pattrib->pktlen));
		dialogtoken = (dialogtoken+1)%256;
	}

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));
	
}

void rtw_build_tdls_dis_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, u8 dialog, u8 privacy)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;

	u8 category = RTW_WLAN_CATEGORY_PUBLIC;
	u8 action = TDLS_DISCOVERY_RESPONSE;
	u8 bssrate[NDIS_802_11_LENGTH_RATES_EX];
 	int bssrate_len = 0;
	u8 more_supportedrates = 0;
	u8 *p;
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8 link_id_addr[18] = {0};
	u8 iedata=0;
	u8 timeout_itvl[5];	//set timeout interval to maximum value
	u32 timeout_interval= TPK_RESEND_COUNT * 1000;
	u8 *pframe_head, pktlen_index;

	pktlen_index = pattrib->pktlen;	// For mgmt frame, pattrib->pktlen would count frame header
	pframe_head = pframe;

	//category, action, dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialog), &(pattrib->pktlen));

	//capability
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	if(privacy)
		*pframe =*pframe | cap_Privacy;
	pframe += 2;
	pattrib->pktlen += 2;

	//supported rates
	rtw_set_supported_rate(bssrate, WIRELESS_11BG_24N);
	bssrate_len = IEEE80211_CCK_RATE_LEN + IEEE80211_NUM_OFDM_RATESLEN;

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//extended supported rates
	if(more_supportedrates==1){
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}

	//supported channels
	pframe = rtw_tdls_set_sup_ch(pmlmeext, pframe, pattrib);

	//RSNIE
	if(privacy)
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, sizeof(TDLS_RSNIE), TDLS_RSNIE, &(pattrib->pktlen));

	//extended capability
	pframe = rtw_set_ie(pframe, _EXT_CAP_IE_ , sizeof(TDLS_EXT_CAPIE), TDLS_EXT_CAPIE, &(pattrib->pktlen));

	if(privacy){
		//FTIE
		_rtw_memset(pframe, 0, 84);	//All fields shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		pframe += 84;
		pattrib->pktlen += 84;

		//Timeout interval
		timeout_itvl[0]=0x02;
		_rtw_memcpy(timeout_itvl+1, &timeout_interval, 4);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));
	}

	//Sup_reg_classes(optional)
	//HT capabilities
	pframe += rtw_tdls_set_ht_cap(padapter, pframe_head - pktlen_index, pattrib);

	//20/40 BSS coexistence
	if(pmlmepriv->num_FortyMHzIntolerant>0)
		iedata |= BIT(2);//20 MHz BSS Width Request
	pframe = rtw_set_ie(pframe, EID_BSSCoexistence, 1, &iedata, &(pattrib->pktlen));

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_, 18, link_id_addr, &(pattrib->pktlen));

}

void rtw_build_tdls_peer_traffic_indication_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_PEER_TRAFFIC_INDICATION;

	u8	link_id_addr[18] = {0};
	u8 AC_queue=0;
	struct sta_info *ptdls_sta = rtw_get_stainfo(&padapter->stapriv, pattrib->dst);

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	//Link identifier
	if(ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE){	
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	}else  if(ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE){
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	}
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//PTI control
	//PU buffer status
	if(ptdls_sta->uapsd_bk&BIT(1))
		AC_queue=BIT(0);
	if(ptdls_sta->uapsd_be&BIT(1))
		AC_queue=BIT(1);
	if(ptdls_sta->uapsd_vi&BIT(1))
		AC_queue=BIT(2);
	if(ptdls_sta->uapsd_vo&BIT(1))
		AC_queue=BIT(3);
	pframe = rtw_set_ie(pframe, _PTI_BUFFER_STATUS_, 1, &AC_queue, &(pattrib->pktlen));
	
}

void rtw_build_tdls_peer_traffic_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_TDLS;
	u8 action = TDLS_PEER_TRAFFIC_RESPONSE;
	u8	link_id_addr[18] = {0};
	struct sta_info *ptdls_sta = rtw_get_stainfo(&padapter->stapriv, pattrib->dst);
	static u8 dialogtoken=0;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));		
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &ptdls_sta->dialog, &(pattrib->pktlen));

	//Link identifier
	if(ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE){	
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	}else  if(ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE){
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	}
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

}

void rtw_build_tdls_ch_switch_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_CHANNEL_SWITCH_REQUEST;
	u8	link_id_addr[18] = {0};
	struct sta_priv 	*pstapriv = &padapter->stapriv;	
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
	u8 ch_switch_timing[4] = {0};
	u16 switch_time= CH_SWITCH_TIME, switch_timeout=CH_SWITCH_TIMEOUT;	

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, target_ch
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdlsinfo->candidate_ch), &(pattrib->pktlen));

	//Link identifier
	if(ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE){	
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	}else  if(ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE){
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	}
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//ch switch timing
	_rtw_memcpy(ch_switch_timing, &switch_time, 2);
	_rtw_memcpy(ch_switch_timing+2, &switch_timeout, 2);
	pframe = rtw_set_ie(pframe, _CH_SWITCH_TIMING_,  4, ch_switch_timing, &(pattrib->pktlen));

	//update ch switch attrib to sta_info
	ptdls_sta->off_ch=ptdlsinfo->candidate_ch;
	ptdls_sta->ch_switch_time=switch_time;
	ptdls_sta->ch_switch_timeout=switch_timeout;

}

void rtw_build_tdls_ch_switch_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_CHANNEL_SWITCH_RESPONSE;
	u8	link_id_addr[18] = {0};
	struct sta_priv 	*pstapriv = &padapter->stapriv;	
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 ch_switch_timing[4] = {0};

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, status_code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));

	//Link identifier
	if(ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE){	
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	}else  if(ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE){
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	}
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//ch switch timing
	_rtw_memcpy(ch_switch_timing, &ptdls_sta->ch_switch_time, 2);
	_rtw_memcpy(ch_switch_timing+2, &ptdls_sta->ch_switch_timeout, 2);
	pframe = rtw_set_ie(pframe, _CH_SWITCH_TIMING_,  4, ch_switch_timing, &(pattrib->pktlen));

}

#ifdef CONFIG_WFD
void rtw_build_tunneled_probe_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct wifidirect_info *pbuddy_wdinfo = &padapter->pbuddy_adapter->wdinfo;
	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_P2P;
	u8 WFA_OUI[3] = { 0x50, 0x6f, 0x9a};
	u8 probe_req = 4;
	u8 wfdielen = 0;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));		
	//category, OUI, frame_body_type
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 3, WFA_OUI, &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(probe_req), &(pattrib->pktlen));

	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		wfdielen = build_probe_req_wfd_ie(pwdinfo, pframe);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	}
	else if(!rtw_p2p_chk_state(pbuddy_wdinfo, P2P_STATE_NONE))
	{
		wfdielen = build_probe_req_wfd_ie(pbuddy_wdinfo, pframe);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	}
	
}

void rtw_build_tunneled_probe_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct wifidirect_info *pbuddy_wdinfo = &padapter->pbuddy_adapter->wdinfo;
	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_P2P;
	u8 WFA_OUI[3] = { 0x50, 0x6f, 0x9a};
	u8 probe_rsp = 5;
	u8 wfdielen = 0;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));		
	//category, OUI, frame_body_type
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 3, WFA_OUI, &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(probe_rsp), &(pattrib->pktlen));

	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		wfdielen = build_probe_resp_wfd_ie(pwdinfo, pframe, 1);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	}
	else if(!rtw_p2p_chk_state(pbuddy_wdinfo, P2P_STATE_NONE))
	{
		wfdielen = build_probe_resp_wfd_ie(pbuddy_wdinfo, pframe, 1);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	}

}
#endif //CONFIG_WFD

void _tdls_tpk_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	struct tdls_txmgmt txmgmt;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	ptdls_sta->TPK_count++;
	//TPK_timer set 1000 as default
	//retry timer should set at least 301 sec.
	if(ptdls_sta->TPK_count==TPK_RESEND_COUNT){
		ptdls_sta->TPK_count=0;
		_rtw_memcpy(txmgmt.peer, ptdls_sta->hwaddr, ETH_ALEN);
		issue_tdls_setup_req(ptdls_sta->padapter, &txmgmt, _FALSE);
	}
	
	_set_timer(&ptdls_sta->TPK_timer, ptdls_sta->TDLS_PeerKey_Lifetime/TPK_RESEND_COUNT);
}

// TDLS_DONE_CH_SEN: channel sensing and report candidate channel
// TDLS_OFF_CH: first time set channel to off channel
// TDLS_BASE_CH: when go back to the channel linked with AP, send null data to peer STA as an indication
void _tdls_ch_switch_timer_hdl(void *FunctionContext)
{

	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	_adapter *padapter = ptdls_sta->padapter;
	
	if( ptdls_sta->option == TDLS_DONE_CH_SEN ){
		rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_DONE_CH_SEN);
	}else if( ptdls_sta->option == TDLS_OFF_CH ){
		issue_nulldata_to_TDLS_peer_STA(ptdls_sta->padapter, ptdls_sta->hwaddr, 0, 0, 0);
		_set_timer(&ptdls_sta->base_ch_timer, 500);
	}else if( ptdls_sta->option == TDLS_BASE_CH){
		issue_nulldata_to_TDLS_peer_STA(ptdls_sta->padapter, ptdls_sta->hwaddr, 0, 0, 0);
	}
}

void _tdls_base_ch_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	rtw_tdls_cmd(ptdls_sta->padapter, ptdls_sta->hwaddr, TDLS_P_OFF_CH);
}

void _tdls_off_ch_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	rtw_tdls_cmd(ptdls_sta->padapter, ptdls_sta->hwaddr, TDLS_P_BASE_CH );
}

void _tdls_handshake_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;

	if(ptdls_sta != NULL)
	{
		if( !(ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) )
		{
			DBG_871X("tdls handshake time out\n");
			rtw_tdls_cmd(ptdls_sta->padapter, ptdls_sta->hwaddr, TDLS_TEAR_STA );
		}
	}
}

void _tdls_pti_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	_adapter *padapter = ptdls_sta->padapter;
	struct tdls_txmgmt txmgmt;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	_rtw_memcpy(txmgmt.peer, ptdls_sta->hwaddr, ETH_ALEN);
	txmgmt.status_code = _RSON_TDLS_TEAR_TOOFAR_;

	if(ptdls_sta != NULL)
	{
		if( ptdls_sta->tdls_sta_state & TDLS_WAIT_PTR_STATE )
		{
			DBG_871X("Doesn't receive PTR from peer dev:"MAC_FMT"; Send TDLS Tear Down\n", MAC_ARG(ptdls_sta->hwaddr));
			issue_tdls_teardown(padapter, &txmgmt, _FALSE);
		}
	}
}

void rtw_init_tdls_timer(_adapter *padapter, struct sta_info *psta)
{
	psta->padapter=padapter;
	_init_timer(&psta->TPK_timer, padapter->pnetdev, _tdls_tpk_timer_hdl, psta);
	_init_timer(&psta->option_timer, padapter->pnetdev, _tdls_ch_switch_timer_hdl, psta);
	_init_timer(&psta->base_ch_timer, padapter->pnetdev, _tdls_base_ch_timer_hdl, psta);
	_init_timer(&psta->off_ch_timer, padapter->pnetdev, _tdls_off_ch_timer_hdl, psta);
	_init_timer(&psta->handshake_timer, padapter->pnetdev, _tdls_handshake_timer_hdl, psta);
	_init_timer(&psta->pti_timer, padapter->pnetdev, _tdls_pti_timer_hdl, psta);
}

void rtw_free_tdls_timer(struct sta_info *psta)
{
	_cancel_timer_ex(&psta->TPK_timer);
	_cancel_timer_ex(&psta->option_timer);
	_cancel_timer_ex(&psta->base_ch_timer);
	_cancel_timer_ex(&psta->off_ch_timer);
	_cancel_timer_ex(&psta->handshake_timer);
	_cancel_timer_ex(&psta->pti_timer);
}

u8	update_sgi_tdls(_adapter *padapter, struct sta_info *psta)
{
	return query_ra_short_GI(psta);
}

u32 update_mask_tdls(_adapter *padapter, struct sta_info *psta)
{
	unsigned char sta_band = 0;
	unsigned int tx_ra_bitmap=0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;

	rtw_hal_update_sta_rate_mask(padapter, psta);
	tx_ra_bitmap = psta->ra_mask;

	if ( pcur_network->Configuration.DSConfig > 14 ) {
		// 5G band
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

	psta->raid = rtw_hal_networktype_to_raid(padapter,psta);
	tx_ra_bitmap |= ((psta->raid<<28)&0xf0000000);
	return tx_ra_bitmap;
}

#endif //CONFIG_TDLS

