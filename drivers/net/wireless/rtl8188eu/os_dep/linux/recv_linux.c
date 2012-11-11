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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <wifi.h>
#include <recv_osdep.h>

#include <osdep_intf.h>
#include <ethernet.h>

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif

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

}


//alloc os related resource in struct recv_buf
int rtw_os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf)
{
	int res=_SUCCESS;

#ifdef CONFIG_USB_HCI
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;

	precvbuf->irp_pending = _FALSE;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if(precvbuf->purb == NULL){
		res = _FAIL;
	}

	precvbuf->pskb = NULL;

	precvbuf->reuse = _FALSE;

	precvbuf->pallocated_buf  = precvbuf->pbuf = NULL;

	precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	precvbuf->pallocated_buf = rtw_usb_buffer_alloc(pusbd, (size_t)precvbuf->alloc_sz, GFP_ATOMIC, &precvbuf->dma_transfer_addr);
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

	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;
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
		dev_kfree_skb_any(precvbuf->pskb);


	return ret;

}

void rtw_handle_tkip_mic_err(_adapter *padapter,u8 bgroup)
{
#ifdef CONFIG_IOCTL_CFG80211
	enum nl80211_key_type key_type;
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

	cfg80211_michael_mic_failure(padapter->pnetdev, (u8 *)&pmlmepriv->assoc_bssid[ 0 ], key_type, -1,
		NULL, GFP_ATOMIC);
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
	_rtw_memcpy( ev.src_addr.sa_data, &pmlmepriv->assoc_bssid[ 0 ], ETH_ALEN );

	_rtw_memset( &wrqu, 0x00, sizeof( wrqu ) );
	wrqu.data.length = sizeof( ev );

	wireless_send_event( padapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu, (char*) &ev );
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

	//pskb_copy = skb_copy(skb, GFP_ATOMIC);
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

	netif_rx(skb);

	precv_frame->u.hdr.pkt = NULL; // set pointer to NULL before rtw_free_recvframe() if call netif_rx()
#endif
}

int rtw_recv_indicatepkt(_adapter *padapter, union recv_frame *precv_frame)
{
	struct recv_priv *precvpriv;
	_queue	*pfree_recv_queue;
	_pkt *skb;
	struct mlme_priv*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
#endif

#ifdef CONFIG_BR_EXT
	void *br_port = NULL;
#endif

_func_enter_;

	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

#ifdef CONFIG_DRVEXT_MODULE
	if (drvext_rx_handler(padapter, precv_frame->u.hdr.rx_data, precv_frame->u.hdr.len) == _SUCCESS)
	{
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

#ifdef NET_SKBUFF_DATA_USES_OFFSET
	skb_set_tail_pointer(skb, precv_frame->u.hdr.len);
#else
	skb->tail = precv_frame->u.hdr.rx_tail;
#endif

	skb->len = precv_frame->u.hdr.len;

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n skb->head=%p skb->data=%p skb->tail=%p skb->end=%p skb->len=%d\n", skb->head, skb->data, skb->tail, skb->end, skb->len));

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
	 	_pkt *pskb2=NULL;
	 	struct sta_info *psta = NULL;
	 	struct sta_priv *pstapriv = &padapter->stapriv;
		struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
		int bmcast = IS_MCAST(pattrib->dst);

		//DBG_871X("bmcast=%d\n", bmcast);

		if(_rtw_memcmp(pattrib->dst, myid(&padapter->eeprompriv), ETH_ALEN)==_FALSE)
		{
			//DBG_871X("not ap psta=%p, addr=%pM\n", psta, pattrib->dst);

			if(bmcast)
			{
				psta = rtw_get_bcmc_stainfo(padapter);
				pskb2 = skb_clone(skb, GFP_ATOMIC);
			} else {
				psta = rtw_get_stainfo(pstapriv, pattrib->dst);
			}

			if(psta)
			{
				//DBG_871X("directly forwarding to the rtw_xmit_entry\n");

				//skb->ip_summed = CHECKSUM_NONE;
				//skb->protocol = eth_type_trans(skb, pnetdev);

				skb->dev = padapter->pnetdev;
				rtw_xmit_entry(skb, padapter->pnetdev);

				if(bmcast)
					skb = pskb2;
				else
					goto _recv_indicatepkt_end;
			}


		}
		else// to APself
		{
			//DBG_871X("to APSelf\n");
		}
	}
	

#ifdef CONFIG_BR_EXT

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	br_port = padapter->pnetdev->br_port;
#else   // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
	br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
	rcu_read_unlock();
#endif  // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))

	if( br_port	&& (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) )	 	
	{
		int nat25_handle_frame(_adapter *priv, struct sk_buff *skb);
		if (nat25_handle_frame(padapter, skb) == -1) {
			//priv->ext_stats.rx_data_drops++;
			//DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n");
			//return FAIL;
#if 1			
			// bypass this frame to upper layer!!
#else
			goto _recv_indicatepkt_drop;
#endif
		}	
	}

#endif	// CONFIG_BR_EXT


#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
	if ( (pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1) ) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		//DBG_871X("CHECKSUM_UNNECESSARY \n");
	} else {
		skb->ip_summed = CHECKSUM_NONE;
		//DBG_871X("CHECKSUM_NONE(%d, %d) \n", pattrib->tcpchk_valid, pattrib->tcp_chkrpt);
	}
#else /* !CONFIG_TCP_CSUM_OFFLOAD_RX */

	skb->ip_summed = CHECKSUM_NONE;

#endif

	skb->dev = padapter->pnetdev;
	skb->protocol = eth_type_trans(skb, padapter->pnetdev);

	netif_rx(skb);

_recv_indicatepkt_end:

	precv_frame->u.hdr.pkt = NULL; // pointers to NULL before rtw_free_recvframe()

	rtw_free_recvframe(precv_frame, pfree_recv_queue);

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n rtw_recv_indicatepkt :after netif_rx!!!!\n"));

_func_exit_;

        return _SUCCESS;

_recv_indicatepkt_drop:

	 //enqueue back to free_recv_queue
	 if(precv_frame)
		 rtw_free_recvframe(precv_frame, pfree_recv_queue);


 	 precvpriv->rx_drop++;

	 return _FAIL;

_func_exit_;

}

void rtw_os_read_port(_adapter *padapter, struct recv_buf *precvbuf)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;

#ifdef CONFIG_USB_HCI

	precvbuf->ref_cnt--;

	//free skb in recv_buf
	dev_kfree_skb_any(precvbuf->pskb);

	precvbuf->pskb = NULL;
	precvbuf->reuse = _FALSE;

	if(precvbuf->irp_pending == _FALSE)
	{
		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
	}


#endif
#ifdef CONFIG_SDIO_HCI
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

