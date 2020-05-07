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
#define _RECV_OSDEP_C_

#include <drv_types.h>

int rtw_os_recvframe_duplicate_skb(_adapter *padapter, union recv_frame *pcloneframe, _pkt *pskb)
{
	int res = _SUCCESS;
	_pkt	*pkt_copy = NULL;

	if (pskb == NULL) {
		RTW_INFO("%s [WARN] skb == NULL, drop frag frame\n", __func__);
		return _FAIL;
	}
#if 1
	pkt_copy = rtw_skb_copy(pskb);

	if (pkt_copy == NULL) {
		RTW_INFO("%s [WARN] rtw_skb_copy fail , drop frag frame\n", __func__);
		return _FAIL;
	}
#else
	pkt_copy = rtw_skb_clone(pskb);

	if (pkt_copy == NULL) {
		RTW_INFO("%s [WARN] rtw_skb_clone fail , drop frag frame\n", __func__);
		return _FAIL;
	}
#endif
	pkt_copy->dev = padapter->pnetdev;

	pcloneframe->u.hdr.pkt = pkt_copy;
	pcloneframe->u.hdr.rx_head = pkt_copy->head;
	pcloneframe->u.hdr.rx_data = pkt_copy->data;
	pcloneframe->u.hdr.rx_end = skb_end_pointer(pkt_copy);
	pcloneframe->u.hdr.rx_tail = skb_tail_pointer(pkt_copy);
	pcloneframe->u.hdr.len = pkt_copy->len;

	return res;
}

int rtw_os_alloc_recvframe(_adapter *padapter, union recv_frame *precvframe, u8 *pdata, _pkt *pskb)
{
	int res = _SUCCESS;
	u8	shift_sz = 0;
	u32	skb_len, alloc_sz;
	_pkt	*pkt_copy = NULL;
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;


	if (pdata == NULL) {
		precvframe->u.hdr.pkt = NULL;
		res = _FAIL;
		return res;
	}


	/*	Modified by Albert 20101213 */
	/*	For 8 bytes IP header alignment. */
	shift_sz = pattrib->qos ? 6 : 0; /*	Qos data, wireless lan header length is 26 */

	skb_len = pattrib->pkt_len;

	/* for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet. */
	/* modify alloc_sz for recvive crc error packet by thomas 2011-06-02 */
	if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
		/* alloc_sz = 1664;	 */ /* 1664 is 128 alignment. */
		alloc_sz = (skb_len <= 1650) ? 1664 : (skb_len + 14);
	} else {
		alloc_sz = skb_len;
		/*	6 is for IP header 8 bytes alignment in QoS packet case. */
		/*	8 is for skb->data 4 bytes alignment. */
		alloc_sz += 14;
	}

	pkt_copy = rtw_skb_alloc(alloc_sz);

	if (pkt_copy) {
		pkt_copy->dev = padapter->pnetdev;
		pkt_copy->len = skb_len;
		precvframe->u.hdr.pkt = pkt_copy;
		precvframe->u.hdr.rx_head = pkt_copy->head;
		precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		skb_reserve(pkt_copy, 8 - ((SIZE_PTR)(pkt_copy->data) & 7));  /* force pkt_copy->data at 8-byte alignment address */
		skb_reserve(pkt_copy, shift_sz);/* force ip_hdr at 8-byte alignment address according to shift_sz. */
		_rtw_memcpy(pkt_copy->data, pdata, skb_len);
		precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
	} else {
#if 0
		{
			rtw_free_recvframe(precvframe_if2, &precvpriv->free_recv_queue);
			rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

			/* The case of can't allocate skb is serious and may never be recovered,
			 once bDriverStopped is enable, this task should be stopped.*/
			if (!rtw_is_drv_stopped(secondary_padapter))
#ifdef PLATFORM_LINUX
				tasklet_schedule(&precvpriv->recv_tasklet);
#endif
			return ret;
		}

#endif

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
		RTW_INFO("%s:can not allocate memory for skb copy\n", __func__);

		precvframe->u.hdr.pkt = NULL;

		/* rtw_free_recvframe(precvframe, pfree_recv_queue); */
		/*exit_rtw_os_recv_resource_alloc;*/

		res = _FAIL;
#else
		if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
			RTW_INFO("%s: alloc_skb fail , drop frag frame\n", __FUNCTION__);
			/* rtw_free_recvframe(precvframe, pfree_recv_queue); */
			res = _FAIL;
			goto exit_rtw_os_recv_resource_alloc;
		}

		if (pskb == NULL) {
			res = _FAIL;
			goto exit_rtw_os_recv_resource_alloc;
		}

		precvframe->u.hdr.pkt = rtw_skb_clone(pskb);
		if (precvframe->u.hdr.pkt) {
			precvframe->u.hdr.pkt->dev = padapter->pnetdev;
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pdata;
			precvframe->u.hdr.rx_end =  pdata + alloc_sz;
		} else {
			RTW_INFO("%s: rtw_skb_clone fail\n", __FUNCTION__);
			/* rtw_free_recvframe(precvframe, pfree_recv_queue); */
			/*exit_rtw_os_recv_resource_alloc;*/
			res = _FAIL;
		}
#endif
	}

exit_rtw_os_recv_resource_alloc:

	return res;

}

void rtw_os_free_recvframe(union recv_frame *precvframe)
{
	if (precvframe->u.hdr.pkt) {
		rtw_os_pkt_free(precvframe->u.hdr.pkt);
		precvframe->u.hdr.pkt = NULL;
	}
}

/* init os related resource in struct recv_priv */
int rtw_os_recv_resource_init(struct recv_priv *precvpriv, _adapter *padapter)
{
	int	res = _SUCCESS;


#ifdef CONFIG_RTW_NAPI
	skb_queue_head_init(&precvpriv->rx_napi_skb_queue);
#endif /* CONFIG_RTW_NAPI */

	return res;
}

/* alloc os related resource in union recv_frame */
int rtw_os_recv_resource_alloc(_adapter *padapter, union recv_frame *precvframe)
{
	int	res = _SUCCESS;

	precvframe->u.hdr.pkt = NULL;

	return res;
}

/* free os related resource in union recv_frame */
void rtw_os_recv_resource_free(struct recv_priv *precvpriv)
{
	sint i;
	union recv_frame *precvframe;
	precvframe = (union recv_frame *) precvpriv->precv_frame_buf;


#ifdef CONFIG_RTW_NAPI
	if (skb_queue_len(&precvpriv->rx_napi_skb_queue))
		RTW_WARN("rx_napi_skb_queue not empty\n");
	rtw_skb_queue_purge(&precvpriv->rx_napi_skb_queue);
#endif /* CONFIG_RTW_NAPI */

	for (i = 0; i < NR_RECVFRAME; i++) {
		rtw_os_free_recvframe(precvframe);
		precvframe++;
	}
}

/* alloc os related resource in struct recv_buf */
int rtw_os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf)
{
	int res = _SUCCESS;

#ifdef CONFIG_USB_HCI
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;
#endif

	precvbuf->irp_pending = _FALSE;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if (precvbuf->purb == NULL)
		res = _FAIL;

	precvbuf->pskb = NULL;

	precvbuf->pallocated_buf  = precvbuf->pbuf = NULL;

	precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	precvbuf->pallocated_buf = rtw_usb_buffer_alloc(pusbd, (size_t)precvbuf->alloc_sz, &precvbuf->dma_transfer_addr);
	precvbuf->pbuf = precvbuf->pallocated_buf;
	if (precvbuf->pallocated_buf == NULL)
		return _FAIL;
#endif /* CONFIG_USE_USB_BUFFER_ALLOC_RX */

#endif /* CONFIG_USB_HCI */

	return res;
}

/* free os related resource in struct recv_buf */
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

#endif /* CONFIG_USE_USB_BUFFER_ALLOC_RX */

	if (precvbuf->purb) {
		/* usb_kill_urb(precvbuf->purb); */
		usb_free_urb(precvbuf->purb);
	}

#endif /* CONFIG_USB_HCI */


	if (precvbuf->pskb) {
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
		if (rtw_free_skb_premem(precvbuf->pskb) != 0)
#endif
			rtw_skb_free(precvbuf->pskb);
	}
	return ret;

}

_pkt *rtw_os_alloc_msdu_pkt(union recv_frame *prframe, const u8 *da, const u8 *sa, u8 *msdu ,u16 msdu_len)
{
	u16	eth_type;
	u8	*data_ptr;
	_pkt *sub_skb;
	struct rx_pkt_attrib *pattrib;

	pattrib = &prframe->u.hdr.attrib;

#ifdef CONFIG_SKB_COPY
	sub_skb = rtw_skb_alloc(msdu_len + 14);
	if (sub_skb) {
		skb_reserve(sub_skb, 14);
		data_ptr = (u8 *)skb_put(sub_skb, msdu_len);
		_rtw_memcpy(data_ptr, msdu, msdu_len);
	} else
#endif /* CONFIG_SKB_COPY */
	{
		sub_skb = rtw_skb_clone(prframe->u.hdr.pkt);
		if (sub_skb) {
			sub_skb->data = msdu;
			sub_skb->len = msdu_len;
			skb_set_tail_pointer(sub_skb, msdu_len);
		} else {
			RTW_INFO("%s(): rtw_skb_clone() Fail!!!\n", __FUNCTION__);
			return NULL;
		}
	}

	eth_type = RTW_GET_BE16(&sub_skb->data[6]);

	if (sub_skb->len >= 8
		&& ((_rtw_memcmp(sub_skb->data, rtw_rfc1042_header, SNAP_SIZE)
				&& eth_type != ETH_P_AARP && eth_type != ETH_P_IPX)
			|| _rtw_memcmp(sub_skb->data, rtw_bridge_tunnel_header, SNAP_SIZE))
	) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		skb_pull(sub_skb, SNAP_SIZE);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), sa, ETH_ALEN);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), da, ETH_ALEN);
	} else {
		/* Leave Ethernet header part of hdr and full payload */
		u16 len;

		len = htons(sub_skb->len);
		_rtw_memcpy(skb_push(sub_skb, 2), &len, 2);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), sa, ETH_ALEN);
		_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), da, ETH_ALEN);
	}

	return sub_skb;
}

#ifdef CONFIG_RTW_NAPI
static int napi_recv(_adapter *padapter, int budget)
{
	_pkt *pskb;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	int work_done = 0;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u8 rx_ok;


	while ((work_done < budget) &&
	       (!skb_queue_empty(&precvpriv->rx_napi_skb_queue))) {
		pskb = skb_dequeue(&precvpriv->rx_napi_skb_queue);
		if (!pskb)
			break;

		rx_ok = _FALSE;

#ifdef CONFIG_RTW_GRO
		if (pregistrypriv->en_gro) {
			if (rtw_napi_gro_receive(&padapter->napi, pskb) != GRO_DROP)
				rx_ok = _TRUE;
			goto next;
		}
#endif /* CONFIG_RTW_GRO */

		if (rtw_netif_receive_skb(padapter->pnetdev, pskb) == NET_RX_SUCCESS)
			rx_ok = _TRUE;

next:
		if (rx_ok == _TRUE) {
			work_done++;
			DBG_COUNTER(padapter->rx_logs.os_netif_ok);
		} else {
			DBG_COUNTER(padapter->rx_logs.os_netif_err);
		}
	}

	return work_done;
}

int rtw_recv_napi_poll(struct napi_struct *napi, int budget)
{
	_adapter *padapter = container_of(napi, _adapter, napi);
	int work_done = 0;
	struct recv_priv *precvpriv = &padapter->recvpriv;


	work_done = napi_recv(padapter, budget);
	if (work_done < budget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && defined(CONFIG_PCI_HCI)
		napi_complete_done(napi, work_done);
#else
		napi_complete(napi);
#endif
		if (!skb_queue_empty(&precvpriv->rx_napi_skb_queue))
			napi_schedule(napi);
	}

	return work_done;
}

#ifdef CONFIG_RTW_NAPI_DYNAMIC
void dynamic_napi_th_chk (_adapter *adapter)
{

	if (adapter->registrypriv.en_napi) {
		struct dvobj_priv *dvobj;
		struct registry_priv *registry;
	
		dvobj = adapter_to_dvobj(adapter);
		registry = &adapter->registrypriv;
		if (dvobj->traffic_stat.cur_rx_tp > registry->napi_threshold)
			dvobj->en_napi_dynamic = 1;
		else
			dvobj->en_napi_dynamic = 0;
	}

}
#endif /* CONFIG_RTW_NAPI_DYNAMIC */
#endif /* CONFIG_RTW_NAPI */

void rtw_os_recv_indicate_pkt(_adapter *padapter, _pkt *pkt, union recv_frame *rframe)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_BR_EXT
	void *br_port = NULL;
#endif
	int ret;

	/* Indicat the packets to upper layer */
	if (pkt) {
		struct ethhdr *ehdr = (struct ethhdr *)pkt->data;

		DBG_COUNTER(padapter->rx_logs.os_indicate);

		if (MLME_IS_AP(padapter)) {
			_pkt *pskb2 = NULL;
			struct sta_info *psta = NULL;
			struct sta_priv *pstapriv = &padapter->stapriv;
			int bmcast = IS_MCAST(ehdr->h_dest);

			/* RTW_INFO("bmcast=%d\n", bmcast); */

			if (_rtw_memcmp(ehdr->h_dest, adapter_mac_addr(padapter), ETH_ALEN) == _FALSE) {
				/* RTW_INFO("not ap psta=%p, addr=%pM\n", psta, ehdr->h_dest); */

				if (bmcast) {
					psta = rtw_get_bcmc_stainfo(padapter);
					pskb2 = rtw_skb_clone(pkt);
				} else
					psta = rtw_get_stainfo(pstapriv, ehdr->h_dest);

				if (psta) {
					struct net_device *pnetdev = (struct net_device *)padapter->pnetdev;

					/* RTW_INFO("directly forwarding to the rtw_xmit_entry\n"); */

					/* skb->ip_summed = CHECKSUM_NONE; */
					pkt->dev = pnetdev;
					#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
					skb_set_queue_mapping(pkt, rtw_recv_select_queue(pkt));
					#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35) */

					_rtw_xmit_entry(pkt, pnetdev);

					if (bmcast && (pskb2 != NULL)) {
						pkt = pskb2;
						DBG_COUNTER(padapter->rx_logs.os_indicate_ap_mcast);
					} else {
						DBG_COUNTER(padapter->rx_logs.os_indicate_ap_forward);
						return;
					}
				}
			} else { /* to APself */
				/* RTW_INFO("to APSelf\n"); */
				DBG_COUNTER(padapter->rx_logs.os_indicate_ap_self);
			}
		}

#ifdef CONFIG_BR_EXT
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE) {
			/* Insert NAT2.5 RX here! */
			#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
			br_port = padapter->pnetdev->br_port;
			#else
			rcu_read_lock();
			br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
			rcu_read_unlock();
			#endif

			if (br_port) {
				int nat25_handle_frame(_adapter *priv, struct sk_buff *skb);

				if (nat25_handle_frame(padapter, pkt) == -1) {
					/* priv->ext_stats.rx_data_drops++; */
					/* DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n"); */
					/* return FAIL; */

					#if 1
					/* bypass this frame to upper layer!! */
					#else
					rtw_skb_free(sub_skb);
					continue;
					#endif
				}
			}
		}
#endif /* CONFIG_BR_EXT */

		/* After eth_type_trans process , pkt->data pointer will move from ethrnet header to ip header */
		pkt->protocol = eth_type_trans(pkt, padapter->pnetdev);
		pkt->dev = padapter->pnetdev;
		pkt->ip_summed = CHECKSUM_NONE; /* CONFIG_TCP_CSUM_OFFLOAD_RX */
#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
		if ((rframe->u.hdr.attrib.csum_valid == 1)
		    && (rframe->u.hdr.attrib.csum_err == 0))
			pkt->ip_summed = CHECKSUM_UNNECESSARY;
#endif /* CONFIG_TCP_CSUM_OFFLOAD_RX */

#ifdef CONFIG_RTW_NAPI
#ifdef CONFIG_RTW_NAPI_DYNAMIC
		if (!skb_queue_empty(&precvpriv->rx_napi_skb_queue)
			&& !adapter_to_dvobj(padapter)->en_napi_dynamic			
			)
			napi_recv(padapter, RTL_NAPI_WEIGHT);
#endif

		if (pregistrypriv->en_napi
			#ifdef CONFIG_RTW_NAPI_DYNAMIC
			&& adapter_to_dvobj(padapter)->en_napi_dynamic
			#endif
		) {
			skb_queue_tail(&precvpriv->rx_napi_skb_queue, pkt);
			#ifndef CONFIG_RTW_NAPI_V2
			napi_schedule(&padapter->napi);
			#endif
			return;
		}
#endif /* CONFIG_RTW_NAPI */

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
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	systime cur_time = 0;

	if (psecuritypriv->last_mic_err_time == 0)
		psecuritypriv->last_mic_err_time = rtw_get_current_time();
	else {
		cur_time = rtw_get_current_time();

		if (cur_time - psecuritypriv->last_mic_err_time < 60 * HZ) {
			psecuritypriv->btkip_countermeasure = _TRUE;
			psecuritypriv->last_mic_err_time = 0;
			psecuritypriv->btkip_countermeasure_time = cur_time;
		} else
			psecuritypriv->last_mic_err_time = rtw_get_current_time();
	}

#ifdef CONFIG_IOCTL_CFG80211
	if (bgroup)
		key_type |= NL80211_KEYTYPE_GROUP;
	else
		key_type |= NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(padapter->pnetdev, sta->cmn.mac_addr, key_type, -1, NULL, GFP_ATOMIC);
#endif

	_rtw_memset(&ev, 0x00, sizeof(ev));
	if (bgroup)
		ev.flags |= IW_MICFAILURE_GROUP;
	else
		ev.flags |= IW_MICFAILURE_PAIRWISE;

	ev.src_addr.sa_family = ARPHRD_ETHER;
	_rtw_memcpy(ev.src_addr.sa_data, sta->cmn.mac_addr, ETH_ALEN);

	_rtw_memset(&wrqu, 0x00, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);

#ifndef CONFIG_IOCTL_CFG80211
	wireless_send_event(padapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu, (char *) &ev);
#endif
}

#ifdef CONFIG_HOSTAPD_MLME
void rtw_hostapd_mlme_rx(_adapter *padapter, union recv_frame *precv_frame)
{
	_pkt *skb;
	struct hostapd_priv *phostapdpriv  = padapter->phostapdpriv;
	struct net_device *pmgnt_netdev = phostapdpriv->pmgnt_netdev;


	skb = precv_frame->u.hdr.pkt;

	if (skb == NULL)
		return;

	skb->data = precv_frame->u.hdr.rx_data;
	skb->tail = precv_frame->u.hdr.rx_tail;
	skb->len = precv_frame->u.hdr.len;

	/* pskb_copy = rtw_skb_copy(skb);
	*	if(skb == NULL) goto _exit; */

	skb->dev = pmgnt_netdev;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	/* skb->protocol = __constant_htons(0x0019); ETH_P_80211_RAW */
	skb->protocol = __constant_htons(0x0003); /*ETH_P_80211_RAW*/

	/* RTW_INFO("(1)data=0x%x, head=0x%x, tail=0x%x, mac_header=0x%x, len=%d\n", skb->data, skb->head, skb->tail, skb->mac_header, skb->len); */

	/* skb->mac.raw = skb->data; */
	skb_reset_mac_header(skb);

	/* skb_pull(skb, 24); */
	_rtw_memset(skb->cb, 0, sizeof(skb->cb));

	rtw_netif_rx(pmgnt_netdev, skb);

	precv_frame->u.hdr.pkt = NULL; /* set pointer to NULL before rtw_free_recvframe() if call rtw_netif_rx() */
}
#endif /* CONFIG_HOSTAPD_MLME */

int rtw_recv_monitor(_adapter *padapter, union recv_frame *precv_frame)
{
	int ret = _FAIL;
	struct recv_priv *precvpriv;
	_queue	*pfree_recv_queue;
	_pkt *skb;
	struct rx_pkt_attrib *pattrib;

	if (NULL == precv_frame)
		goto _recv_drop;

	pattrib = &precv_frame->u.hdr.attrib;
	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

	skb = precv_frame->u.hdr.pkt;
	if (skb == NULL) {
		RTW_INFO("%s :skb==NULL something wrong!!!!\n", __func__);
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

inline void rtw_rframe_set_os_pkt(union recv_frame *rframe)
{
	_pkt *skb = rframe->u.hdr.pkt;

	skb->data = rframe->u.hdr.rx_data;
	skb_set_tail_pointer(skb, rframe->u.hdr.len);
	skb->len = rframe->u.hdr.len;
}

int rtw_recv_indicatepkt(_adapter *padapter, union recv_frame *precv_frame)
{
	struct recv_priv *precvpriv;
	_queue	*pfree_recv_queue;

	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

	if (precv_frame->u.hdr.pkt == NULL)
		goto _recv_indicatepkt_drop;

	rtw_os_recv_indicate_pkt(padapter, precv_frame->u.hdr.pkt, precv_frame);

	precv_frame->u.hdr.pkt = NULL;
	rtw_free_recvframe(precv_frame, pfree_recv_queue);
	return _SUCCESS;

_recv_indicatepkt_drop:
	rtw_free_recvframe(precv_frame, pfree_recv_queue);
	DBG_COUNTER(padapter->rx_logs.os_indicate_err);
	return _FAIL;
}

void rtw_os_read_port(_adapter *padapter, struct recv_buf *precvbuf)
{
#ifdef CONFIG_USB_HCI
	struct recv_priv *precvpriv = &padapter->recvpriv;

	precvbuf->ref_cnt--;

	/* free skb in recv_buf */
	rtw_skb_free(precvbuf->pskb);

	precvbuf->pskb = NULL;

	if (precvbuf->irp_pending == _FALSE)
		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);


#endif
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	precvbuf->pskb = NULL;
#endif

}

