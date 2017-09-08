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
#define _RECV_OSDEP_C_

#include <drv_types.h>

int rtw_os_alloc_recvframe(_adapter *padapter, union recv_frame *precvframe, u8 *pdata, _pkt *pskb)
{
	int res = _SUCCESS;
	u8	shift_sz = 0;
	u32	skb_len, alloc_sz;
	_pkt	 *pkt_copy = NULL;	
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;


	if(pdata == NULL)
	{		
		precvframe->u.hdr.pkt = NULL;
		res = _FAIL;
		return res;
	}	


	//	Modified by Albert 20101213
	//	For 8 bytes IP header alignment.
	shift_sz = pattrib->qos ? 6:0;//	Qos data, wireless lan header length is 26

	skb_len = pattrib->pkt_len;

	// for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
	// modify alloc_sz for recvive crc error packet by thomas 2011-06-02
	if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0))
	{
		//alloc_sz = 1664;	//1664 is 128 alignment.
		alloc_sz = (skb_len <= 1650) ? 1664:(skb_len + 14);		
	}
	else 
	{
		alloc_sz = skb_len;
		//	6 is for IP header 8 bytes alignment in QoS packet case.
		//	8 is for skb->data 4 bytes alignment.
		alloc_sz += 14;
	}	

	pkt_copy = rtw_skb_alloc(alloc_sz);

	if(pkt_copy)
	{
		pkt_copy->dev = padapter->pnetdev;
		precvframe->u.hdr.pkt = pkt_copy;
		precvframe->u.hdr.rx_head = pkt_copy->data;
		precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		skb_reserve(pkt_copy, 8 - ((SIZE_PTR)( pkt_copy->data) & 7 ));//force pkt_copy->data at 8-byte alignment address
		skb_reserve(pkt_copy, shift_sz);//force ip_hdr at 8-byte alignment address according to shift_sz.
		_rtw_memcpy(pkt_copy->data, pdata, skb_len);
		precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
	}
	else
	{
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
		DBG_871X("%s:can not allocate memory for skb copy\n", __FUNCTION__);

		precvframe->u.hdr.pkt = NULL;

		//rtw_free_recvframe(precvframe, pfree_recv_queue);
		//goto _exit_recvbuf2recvframe;

		res = _FAIL;	
#else
		if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0))
		{				
			DBG_871X("%s: alloc_skb fail , drop frag frame \n", __FUNCTION__);
			//rtw_free_recvframe(precvframe, pfree_recv_queue);
			res = _FAIL;
			goto exit_rtw_os_recv_resource_alloc;
		}

		if(pskb == NULL)
		{
			res = _FAIL;
			goto exit_rtw_os_recv_resource_alloc;
		}
			
		precvframe->u.hdr.pkt = rtw_skb_clone(pskb);
		if(precvframe->u.hdr.pkt)
		{
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pdata;
			precvframe->u.hdr.rx_end =  pdata + alloc_sz;
		}
		else
		{
			DBG_871X("%s: rtw_skb_clone fail\n", __FUNCTION__);
			//rtw_free_recvframe(precvframe, pfree_recv_queue);
			//goto _exit_recvbuf2recvframe;
			res = _FAIL;
		}
#endif			
	}		

exit_rtw_os_recv_resource_alloc:

	return res;

}

void rtw_os_free_recvframe(union recv_frame *precvframe)
{
	if(precvframe->u.hdr.pkt)
	{
		rtw_skb_free(precvframe->u.hdr.pkt);//free skb by driver

		precvframe->u.hdr.pkt = NULL;
	}
}

//init os related resource in struct recv_priv
int rtw_os_recv_resource_init(struct recv_priv *precvpriv, _adapter *padapter)
{
	int	res=_SUCCESS;

	return res;
}

//alloc os related resource in union recv_frame
int rtw_os_recv_resource_alloc(_adapter *padapter, union recv_frame *precvframe)
{
	int	res=_SUCCESS;
	
	precvframe->u.hdr.pkt_newalloc = precvframe->u.hdr.pkt = NULL;

	return res;
}

//free os related resource in union recv_frame
void rtw_os_recv_resource_free(struct recv_priv *precvpriv)
{
	sint i;
	union recv_frame *precvframe;
	precvframe = (union recv_frame*) precvpriv->precv_frame_buf;

	for(i=0; i < NR_RECVFRAME; i++)
	{
		if(precvframe->u.hdr.pkt)
		{
			rtw_skb_free(precvframe->u.hdr.pkt);//free skb by driver
			precvframe->u.hdr.pkt = NULL;
		}
		precvframe++;
	}
}

//alloc os related resource in struct recv_buf
int rtw_os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf)
{
	int res=_SUCCESS;

#ifdef CONFIG_USB_HCI
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;

	precvbuf->irp_pending = _FALSE;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if(precvbuf->purb == NULL){
		res = _FAIL;
	}

	precvbuf->pskb = NULL;

	precvbuf->pallocated_buf  = precvbuf->pbuf = NULL;

	precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	precvbuf->pallocated_buf = rtw_usb_buffer_alloc(pusbd, (size_t)precvbuf->alloc_sz, &precvbuf->dma_transfer_addr);
	precvbuf->pbuf = precvbuf->pallocated_buf;
	if(precvbuf->pallocated_buf == NULL)
		return _FAIL;
	#endif //CONFIG_USE_USB_BUFFER_ALLOC_RX
	
#endif //CONFIG_USB_HCI

	return res;
}

//free os related resource in struct recv_buf
int rtw_os_recvbuf_resource_free(_adapter *padapter, struct recv_buf *precvbuf)
{
	int ret = _SUCCESS;

#ifdef CONFIG_USB_HCI

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX

	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;

	rtw_usb_buffer_free(pusbd, (size_t)precvbuf->alloc_sz, precvbuf->pallocated_buf, precvbuf->dma_transfer_addr);
	precvbuf->pallocated_buf =  NULL;
	precvbuf->dma_transfer_addr = 0;

#endif //CONFIG_USE_USB_BUFFER_ALLOC_RX

	if(precvbuf->purb)
	{
		//usb_kill_urb(precvbuf->purb);
		usb_free_urb(precvbuf->purb);
	}

#endif //CONFIG_USB_HCI


	if(precvbuf->pskb)
	{
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
		if(rtw_free_skb_premem(precvbuf->pskb)!=0)
#endif
		rtw_skb_free(precvbuf->pskb);
	}
	return ret;

}

_pkt *rtw_os_alloc_msdu_pkt(union recv_frame *prframe, u16 nSubframe_Length, u8 *pdata)
{
	u16	eth_type;
	u8	*data_ptr;
	_pkt *sub_skb;
	struct rx_pkt_attrib *pattrib;

	pattrib = &prframe->u.hdr.attrib;

#ifdef CONFIG_SKB_COPY
	sub_skb = rtw_skb_alloc(nSubframe_Length + 12);
	if(sub_skb)
	{
		skb_reserve(sub_skb, 12);
		data_ptr = (u8 *)skb_put(sub_skb, nSubframe_Length);
		_rtw_memcpy(data_ptr, (pdata + ETH_HLEN), nSubframe_Length);
	}
	else
#endif // CONFIG_SKB_COPY
	{
		sub_skb = rtw_skb_clone(prframe->u.hdr.pkt);
		if(sub_skb)
		{
			sub_skb->data = pdata + ETH_HLEN;
			sub_skb->len = nSubframe_Length;
			skb_set_tail_pointer(sub_skb, nSubframe_Length);
		}
		else
		{
			DBG_871X("%s(): rtw_skb_clone() Fail!!!\n",__FUNCTION__);
			return NULL;
		}
	}

	eth_type = RTW_GET_BE16(&sub_skb->data[6]);

	if (sub_skb->len >= 8 &&
		((_rtw_memcmp(sub_skb->data, rtw_rfc1042_header, SNAP_SIZE) &&
		  eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		 _rtw_memcmp(sub_skb->data, rtw_bridge_tunnel_header, SNAP_SIZE) )) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		skb_pull(sub_skb, SNAP_SIZE);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
	} else {
		u16 len;
		/* Leave Ethernet header part of hdr and full payload */
		len = htons(sub_skb->len);
		_rtw_memcpy(skb_push(sub_skb, 2), &len, 2);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
	}

	return sub_skb;
}

#ifdef DBG_UDP_PKT_LOSE_11AC
#define PAYLOAD_LEN_LOC_OF_IP_HDR 0x10 /*ethernet payload length location of ip header (DA+SA+eth_type+(version&hdr_len)) */	
#endif

void rtw_os_recv_indicate_pkt(_adapter *padapter, _pkt *pkt, struct rx_pkt_attrib *pattrib)
{
	struct mlme_priv*pmlmepriv = &padapter->mlmepriv;
	struct recv_priv *precvpriv = &(padapter->recvpriv);
#ifdef CONFIG_BR_EXT
	void *br_port = NULL;
#endif
	int ret;

	/* Indicat the packets to upper layer */
	if (pkt) {
		if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		{
		 	_pkt *pskb2=NULL;
		 	struct sta_info *psta = NULL;
		 	struct sta_priv *pstapriv = &padapter->stapriv;
			int bmcast = IS_MCAST(pattrib->dst);

			//DBG_871X("bmcast=%d\n", bmcast);

			if (_rtw_memcmp(pattrib->dst, adapter_mac_addr(padapter), ETH_ALEN) == _FALSE)
			{
				//DBG_871X("not ap psta=%p, addr=%pM\n", psta, pattrib->dst);

				if(bmcast)
				{
					psta = rtw_get_bcmc_stainfo(padapter);
					pskb2 = rtw_skb_clone(pkt);
				} else {
					psta = rtw_get_stainfo(pstapriv, pattrib->dst);
				}

				if(psta)
				{
					struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;			

					//DBG_871X("directly forwarding to the rtw_xmit_entry\n");

					//skb->ip_summed = CHECKSUM_NONE;
					pkt->dev = pnetdev;				
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
					skb_set_queue_mapping(pkt, rtw_recv_select_queue(pkt));
#endif //LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35)

					_rtw_xmit_entry(pkt, pnetdev);

					if(bmcast && (pskb2 != NULL) ) {
						pkt = pskb2;
						DBG_COUNTER(padapter->rx_logs.os_indicate_ap_mcast);
					} else {
						DBG_COUNTER(padapter->rx_logs.os_indicate_ap_forward);
						return;
					}
				}
			}
			else// to APself
			{
				//DBG_871X("to APSelf\n");
				DBG_COUNTER(padapter->rx_logs.os_indicate_ap_self);
			}
		}
		
#ifdef CONFIG_BR_EXT
		// Insert NAT2.5 RX here!
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		br_port = padapter->pnetdev->br_port;
#else   // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		rcu_read_lock();
		br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
		rcu_read_unlock();
#endif  // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))


		if( br_port && (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) )
		{
			int nat25_handle_frame(_adapter *priv, struct sk_buff *skb);
			if (nat25_handle_frame(padapter, pkt) == -1) {
				//priv->ext_stats.rx_data_drops++;
				//DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n");
				//return FAIL;
				
#if 1
				// bypass this frame to upper layer!!
#else
				rtw_skb_free(sub_skb);
				continue;
#endif
			}							
		}
#endif	// CONFIG_BR_EXT
		if( precvpriv->sink_udpport > 0)
			rtw_sink_rtp_seq_dbg(padapter,pkt);
#ifdef DBG_UDP_PKT_LOSE_11AC
		/* After eth_type_trans process , pkt->data pointer will move from ethrnet header to ip header ,  
		*	we have to check ethernet type , so this debug must be print before eth_type_trans
		*/
		if (*((unsigned short *)(pkt->data+ETH_ALEN*2)) == htons(ETH_P_ARP)) {
			/* ARP Payload length will be 42bytes or 42+18(tailer)=60bytes*/
			if (pkt->len != 42 && pkt->len != 60) 
				DBG_871X("Error !!%s,ARP Payload length %u not correct\n" , __func__ , pkt->len);
		} else if (*((unsigned short *)(pkt->data+ETH_ALEN*2)) == htons(ETH_P_IP)) { 
			if (be16_to_cpu(*((u16 *)(pkt->data+PAYLOAD_LEN_LOC_OF_IP_HDR))) != (pkt->len)-ETH_HLEN) {
				DBG_871X("Error !!%s,Payload length not correct\n" , __func__);
				DBG_871X("%s, IP header describe Total length=%u\n" , __func__ , be16_to_cpu(*((u16 *)(pkt->data+PAYLOAD_LEN_LOC_OF_IP_HDR))));
				DBG_871X("%s, Pkt real length=%u\n" , __func__ , (pkt->len)-ETH_HLEN);
			} 
		}
#endif
		/* After eth_type_trans process , pkt->data pointer will move from ethrnet header to ip header */
		pkt->protocol = eth_type_trans(pkt, padapter->pnetdev);
		pkt->dev = padapter->pnetdev;

#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
		if ( (pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1) ) {
			pkt->ip_summed = CHECKSUM_UNNECESSARY;
		} else {
			pkt->ip_summed = CHECKSUM_NONE;
		}
#else /* !CONFIG_TCP_CSUM_OFFLOAD_RX */
		pkt->ip_summed = CHECKSUM_NONE;
#endif //CONFIG_TCP_CSUM_OFFLOAD_RX

		ret = rtw_netif_rx(padapter->pnetdev, pkt);
		if (ret == NET_RX_SUCCESS)
			DBG_COUNTER(padapter->rx_logs.os_netif_ok);
		else
			DBG_COUNTER(padapter->rx_logs.os_netif_err);
	}
}

void rtw_handle_tkip_mic_err(_adapter *padapter, struct sta_info *sta, u8 bgroup)
{
#ifdef CONFIG_IOCTL_CFG80211
	enum nl80211_key_type key_type = 0;
#endif
	union iwreq_data wrqu;
	struct iw_michaelmicfailure    ev;
	struct mlme_priv*              pmlmepriv  = &padapter->mlmepriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;	
	u32 cur_time = 0;

	if( psecuritypriv->last_mic_err_time == 0 )
	{
		psecuritypriv->last_mic_err_time = rtw_get_current_time();
	}
	else
	{
		cur_time = rtw_get_current_time();

		if( cur_time - psecuritypriv->last_mic_err_time < 60*HZ )
		{
			psecuritypriv->btkip_countermeasure = _TRUE;
			psecuritypriv->last_mic_err_time = 0;
			psecuritypriv->btkip_countermeasure_time = cur_time;
		}
		else
		{
			psecuritypriv->last_mic_err_time = rtw_get_current_time();
		}
	}

#ifdef CONFIG_IOCTL_CFG80211
	if ( bgroup )
	{
		key_type |= NL80211_KEYTYPE_GROUP;
	}
	else
	{
		key_type |= NL80211_KEYTYPE_PAIRWISE;
	}

	cfg80211_michael_mic_failure(padapter->pnetdev, sta->hwaddr, key_type, -1, NULL, GFP_ATOMIC);
#endif

	_rtw_memset( &ev, 0x00, sizeof( ev ) );
	if ( bgroup )
	{
	    ev.flags |= IW_MICFAILURE_GROUP;
	}
	else
	{
	    ev.flags |= IW_MICFAILURE_PAIRWISE;
	}

	ev.src_addr.sa_family = ARPHRD_ETHER;
	_rtw_memcpy(ev.src_addr.sa_data, sta->hwaddr, ETH_ALEN);

	_rtw_memset( &wrqu, 0x00, sizeof( wrqu ) );
	wrqu.data.length = sizeof( ev );

#ifndef CONFIG_IOCTL_CFG80211
	wireless_send_event( padapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu, (char*) &ev );
#endif
}

void rtw_hostapd_mlme_rx(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_HOSTAPD_MLME
	_pkt *skb;
	struct hostapd_priv *phostapdpriv  = padapter->phostapdpriv;
	struct net_device *pmgnt_netdev = phostapdpriv->pmgnt_netdev;

	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("+rtw_hostapd_mlme_rx\n"));

	skb = precv_frame->u.hdr.pkt;

	if (skb == NULL)
		return;

	skb->data = precv_frame->u.hdr.rx_data;
	skb->tail = precv_frame->u.hdr.rx_tail;
	skb->len = precv_frame->u.hdr.len;

	//pskb_copy = rtw_skb_copy(skb);
//	if(skb == NULL) goto _exit;

	skb->dev = pmgnt_netdev;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	//skb->protocol = __constant_htons(0x0019); /*ETH_P_80211_RAW*/
	skb->protocol = __constant_htons(0x0003); /*ETH_P_80211_RAW*/

	//DBG_871X("(1)data=0x%x, head=0x%x, tail=0x%x, mac_header=0x%x, len=%d\n", skb->data, skb->head, skb->tail, skb->mac_header, skb->len);

	//skb->mac.raw = skb->data;
	skb_reset_mac_header(skb);

       //skb_pull(skb, 24);
       _rtw_memset(skb->cb, 0, sizeof(skb->cb));

	rtw_netif_rx(pmgnt_netdev, skb);

	precv_frame->u.hdr.pkt = NULL; // set pointer to NULL before rtw_free_recvframe() if call rtw_netif_rx()
#endif
}

#ifdef CONFIG_AUTO_AP_MODE
static void rtw_os_ksocket_send(_adapter *padapter, union recv_frame *precv_frame)
{	
	_pkt *skb = precv_frame->u.hdr.pkt;	
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_info *psta = precv_frame->u.hdr.psta;
		
	DBG_871X("eth rx: got eth_type=0x%x\n", pattrib->eth_type);					
		
	if (psta && psta->isrc && psta->pid>0)
	{
		u16 rx_pid;

		rx_pid = *(u16*)(skb->data+ETH_HLEN);
			
		DBG_871X("eth rx(pid=0x%x): sta("MAC_FMT") pid=0x%x\n", 
			rx_pid, MAC_ARG(psta->hwaddr), psta->pid);

		if(rx_pid == psta->pid)
		{
			int i;
			u16 len = *(u16*)(skb->data+ETH_HLEN+2);
			//u16 ctrl_type = *(u16*)(skb->data+ETH_HLEN+4);

			//DBG_871X("eth, RC: len=0x%x, ctrl_type=0x%x\n", len, ctrl_type); 
			DBG_871X("eth, RC: len=0x%x\n", len);

			for(i=0;i<len;i++)
				DBG_871X("0x%x\n", *(skb->data+ETH_HLEN+4+i));
				//DBG_871X("0x%x\n", *(skb->data+ETH_HLEN+6+i));

			DBG_871X("eth, RC-end\n"); 

#if 0
			//send_sz = ksocket_send(padapter->ksock_send, &padapter->kaddr_send, (skb->data+ETH_HLEN+2), len);				
			rtw_recv_ksocket_send_cmd(padapter, (skb->data+ETH_HLEN+2), len);

			//DBG_871X("ksocket_send size=%d\n", send_sz); 
#endif			
		}
		
	}		

}
#endif //CONFIG_AUTO_AP_MODE

int rtw_recv_monitor(_adapter *padapter, union recv_frame *precv_frame)
{
	int ret = _FAIL;
	struct recv_priv *precvpriv;
	_queue	*pfree_recv_queue;
	_pkt *skb;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct rx_pkt_attrib *pattrib;

	if (NULL == precv_frame)
		goto _recv_drop;

	pattrib = &precv_frame->u.hdr.attrib;
	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

	skb = precv_frame->u.hdr.pkt;
	if (skb == NULL) {
		DBG_871X("%s :skb==NULL something wrong!!!!\n", __func__);
		goto _recv_drop;
	}

	skb->data = precv_frame->u.hdr.rx_data;
	skb_set_tail_pointer(skb, precv_frame->u.hdr.len);
	skb->len = precv_frame->u.hdr.len;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(0x0019); /* ETH_P_80211_RAW */

	rtw_netif_rx(padapter->pnetdev, skb);

	/* pointers to NULL before rtw_free_recvframe() */
	precv_frame->u.hdr.pkt = NULL;

	ret = _SUCCESS;

_recv_drop:

	/* enqueue back to free_recv_queue */
	if (precv_frame)
		rtw_free_recvframe(precv_frame, pfree_recv_queue);

	return ret;

}

int rtw_recv_indicatepkt(_adapter *padapter, union recv_frame *precv_frame)
{
	struct recv_priv *precvpriv;
	_queue	*pfree_recv_queue;
	_pkt *skb;
	struct mlme_priv*pmlmepriv = &padapter->mlmepriv;
	struct rx_pkt_attrib *pattrib;
	
	if(NULL == precv_frame)
		goto _recv_indicatepkt_drop;

	DBG_COUNTER(padapter->rx_logs.os_indicate);
	pattrib = &precv_frame->u.hdr.attrib;
	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

#ifdef CONFIG_DRVEXT_MODULE
	if (drvext_rx_handler(padapter, precv_frame->u.hdr.rx_data, precv_frame->u.hdr.len) == _SUCCESS)
	{
		goto _recv_indicatepkt_drop;
	}
#endif

#ifdef CONFIG_WAPI_SUPPORT
	if (rtw_wapi_check_for_drop(padapter,precv_frame))
	{
		WAPI_TRACE(WAPI_ERR, "%s(): Rx Reorder Drop case!!\n", __FUNCTION__);
		goto _recv_indicatepkt_drop;
	}
#endif

	skb = precv_frame->u.hdr.pkt;
	if(skb == NULL)
	{
		RT_TRACE(_module_recv_osdep_c_,_drv_err_,("rtw_recv_indicatepkt():skb==NULL something wrong!!!!\n"));
		goto _recv_indicatepkt_drop;
	}

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("rtw_recv_indicatepkt():skb != NULL !!!\n"));		
	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("rtw_recv_indicatepkt():precv_frame->u.hdr.rx_head=%p  precv_frame->hdr.rx_data=%p\n", precv_frame->u.hdr.rx_head, precv_frame->u.hdr.rx_data));
	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("precv_frame->hdr.rx_tail=%p precv_frame->u.hdr.rx_end=%p precv_frame->hdr.len=%d \n", precv_frame->u.hdr.rx_tail, precv_frame->u.hdr.rx_end, precv_frame->u.hdr.len));

	skb->data = precv_frame->u.hdr.rx_data;

	skb_set_tail_pointer(skb, precv_frame->u.hdr.len);

	skb->len = precv_frame->u.hdr.len;

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n skb->head=%p skb->data=%p skb->tail=%p skb->end=%p skb->len=%d\n", skb->head, skb->data, skb_tail_pointer(skb), skb_end_pointer(skb), skb->len));

	if (pattrib->eth_type == 0x888e)
		DBG_871X_LEVEL(_drv_always_, "recv eapol packet\n");

#ifdef CONFIG_AUTO_AP_MODE	
#if 1 //for testing
#if 1
	if (0x8899 == pattrib->eth_type)
	{
		rtw_os_ksocket_send(padapter, precv_frame);

		//goto _recv_indicatepkt_drop;
	}
#else
	if (0x8899 == pattrib->eth_type)
	{
		rtw_auto_ap_mode_rx(padapter, precv_frame);
		
		goto _recv_indicatepkt_end;
	}
#endif
#endif
#endif //CONFIG_AUTO_AP_MODE

	/* TODO: move to core */
	{
		_pkt *pkt = skb;
		struct ethhdr *etherhdr = (struct ethhdr *)pkt->data;
		struct sta_info *sta = precv_frame->u.hdr.psta;

		if (!sta)
			goto bypass_session_tracker;

		if (ntohs(etherhdr->h_proto) == ETH_P_IP) {
			u8 *ip = pkt->data + 14;

			if (GET_IPV4_PROTOCOL(ip) == 0x06  /* TCP */
				&& rtw_st_ctl_chk_reg_s_proto(&sta->st_ctl, 0x06) == _TRUE
			) {
				u8 *tcp = ip + GET_IPV4_IHL(ip) * 4;

				if (rtw_st_ctl_chk_reg_rule(&sta->st_ctl, padapter, IPV4_DST(ip), TCP_DST(tcp), IPV4_SRC(ip), TCP_SRC(tcp)) == _TRUE) {
					if (GET_TCP_SYN(tcp) && GET_TCP_ACK(tcp)) {
						session_tracker_add_cmd(padapter, sta
							, IPV4_DST(ip), TCP_DST(tcp)
							, IPV4_SRC(ip), TCP_SRC(tcp));
						if (DBG_SESSION_TRACKER)
							DBG_871X(FUNC_ADPT_FMT" local:"IP_FMT":"PORT_FMT", remote:"IP_FMT":"PORT_FMT" SYN-ACK\n"
								, FUNC_ADPT_ARG(padapter)
								, IP_ARG(IPV4_DST(ip)), PORT_ARG(TCP_DST(tcp))
								, IP_ARG(IPV4_SRC(ip)), PORT_ARG(TCP_SRC(tcp)));
					}
					if (GET_TCP_FIN(tcp)) {
						session_tracker_del_cmd(padapter, sta
							, IPV4_DST(ip), TCP_DST(tcp)
							, IPV4_SRC(ip), TCP_SRC(tcp));
						if (DBG_SESSION_TRACKER)
							DBG_871X(FUNC_ADPT_FMT" local:"IP_FMT":"PORT_FMT", remote:"IP_FMT":"PORT_FMT" FIN\n"
								, FUNC_ADPT_ARG(padapter)
								, IP_ARG(IPV4_DST(ip)), PORT_ARG(TCP_DST(tcp))
								, IP_ARG(IPV4_SRC(ip)), PORT_ARG(TCP_SRC(tcp)));
					}
				}

			}
		}
bypass_session_tracker:
		;
	}

	rtw_os_recv_indicate_pkt(padapter, skb, pattrib);

_recv_indicatepkt_end:

	precv_frame->u.hdr.pkt = NULL; // pointers to NULL before rtw_free_recvframe()

	rtw_free_recvframe(precv_frame, pfree_recv_queue);

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n rtw_recv_indicatepkt :after rtw_os_recv_indicate_pkt!!!!\n"));


        return _SUCCESS;

_recv_indicatepkt_drop:

	 //enqueue back to free_recv_queue
	 if(precv_frame)
		 rtw_free_recvframe(precv_frame, pfree_recv_queue);

	 DBG_COUNTER(padapter->rx_logs.os_indicate_err);

	 return _FAIL;

}

void rtw_os_read_port(_adapter *padapter, struct recv_buf *precvbuf)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;

#ifdef CONFIG_USB_HCI

	precvbuf->ref_cnt--;

	//free skb in recv_buf
	rtw_skb_free(precvbuf->pskb);

	precvbuf->pskb = NULL;

	if(precvbuf->irp_pending == _FALSE)
	{
		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
	}


#endif
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		precvbuf->pskb = NULL;
#endif

}
void _rtw_reordering_ctrl_timeout_handler (void *FunctionContext);
void _rtw_reordering_ctrl_timeout_handler (void *FunctionContext)
{
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)FunctionContext;
	rtw_reordering_ctrl_timeout_handler(preorder_ctrl);
}

void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	_adapter *padapter = preorder_ctrl->padapter;

	_init_timer(&(preorder_ctrl->reordering_ctrl_timer), padapter->pnetdev, _rtw_reordering_ctrl_timeout_handler, preorder_ctrl);

}

