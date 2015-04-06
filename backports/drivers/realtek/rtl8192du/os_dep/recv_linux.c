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

#include <usb_ops.h>

/* alloc os related resource in struct recv_frame_hdr */
int rtw_os_recv_resource_alloc(struct rtw_adapter *padapter, struct recv_frame_hdr *precvframe)
{
	int	res=_SUCCESS;

	precvframe->pkt_newalloc = precvframe->pkt = NULL;

	return res;
}

/* free os related resource in struct recv_frame_hdr */
void rtw_os_recv_resource_free(struct recv_priv *precvpriv)
{
}

/* alloc os related resource in struct recv_buf */
int rtw_os_recvbuf_resource_alloc(struct rtw_adapter *padapter, struct recv_buf *precvbuf)
{
	int res=_SUCCESS;

	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;

	precvbuf->irp_pending = false;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if (precvbuf->purb == NULL) {
		res = _FAIL;
	}

	precvbuf->pskb = NULL;

	precvbuf->reuse = false;

	precvbuf->pallocated_buf  = precvbuf->pbuf = NULL;

	precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	return res;
}

/* free os related resource in struct recv_buf */
int rtw_os_recvbuf_resource_free(struct rtw_adapter *padapter, struct recv_buf *precvbuf)
{
	int ret = _SUCCESS;

	if (precvbuf->purb)
		usb_free_urb(precvbuf->purb);

	if (precvbuf->pskb)
		dev_kfree_skb_any(precvbuf->pskb);

	return ret;
}

void rtw_handle_tkip_mic_err(struct rtw_adapter *padapter,u8 bgroup)
{
	enum nl80211_key_type key_type;
	union iwreq_data wrqu;
	struct iw_michaelmicfailure    ev;
	struct mlme_priv*              pmlmepriv  = &padapter->mlmepriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	u32 cur_time = 0;

	if (psecuritypriv->last_mic_err_time == 0)
	{
		psecuritypriv->last_mic_err_time = rtw_get_current_time();
	}
	else
	{
		cur_time = rtw_get_current_time();

		if (cur_time - psecuritypriv->last_mic_err_time < 60*HZ)
		{
			psecuritypriv->btkip_countermeasure = true;
			psecuritypriv->last_mic_err_time = 0;
			psecuritypriv->btkip_countermeasure_time = cur_time;
		}
		else
		{
			psecuritypriv->last_mic_err_time = rtw_get_current_time();
		}
	}

	if (bgroup)
		key_type |= NL80211_KEYTYPE_GROUP;
	else
		key_type |= NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(padapter->pnetdev, (u8 *)&pmlmepriv->assoc_bssid[0], key_type, -1,
		NULL, GFP_ATOMIC);

	memset(&ev, 0x00, sizeof(ev));
	if (bgroup)
	    ev.flags |= IW_MICFAILURE_GROUP;
	else
	    ev.flags |= IW_MICFAILURE_PAIRWISE;

	ev.src_addr.sa_family = ARPHRD_ETHER;
	memcpy(ev.src_addr.sa_data, &pmlmepriv->assoc_bssid[0], ETH_ALEN);

	memset(&wrqu, 0x00, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);

	wireless_send_event(padapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu, (char*) &ev);
}

void rtw_hostapd_mlme_rx(struct rtw_adapter *padapter, struct recv_frame_hdr *precv_frame)
{
#ifdef CONFIG_HOSTAPD_MLME
	struct sk_buff *skb;
	struct hostapd_priv *phostapdpriv  = padapter->phostapdpriv;
	struct net_device *pmgnt_netdev = phostapdpriv->pmgnt_netdev;

	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("+rtw_hostapd_mlme_rx\n"));

	skb = precv_frame->pkt;

	if (skb == NULL)
		return;

	skb->data = precv_frame->rx_data;
	skb->tail = precv_frame->rx_tail;
	skb->len = precv_frame->len;

	skb->dev = pmgnt_netdev;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = __constant_htons(0x0003); /*ETH_P_80211_RAW*/

	skb_reset_mac_header(skb);

       memset(skb->cb, 0, sizeof(skb->cb));

	netif_rx(skb);

	precv_frame->pkt = NULL; /*  set pointer to NULL before rtw_free_recvframe() if call netif_rx() */
#endif
}

int rtw_recv_indicatepkt(struct rtw_adapter *padapter, struct recv_frame_hdr *precv_frame)
{
	struct recv_priv *precvpriv;
	struct __queue *pfree_recv_queue;
	struct sk_buff *skb;
	struct mlme_priv*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
#endif

	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

#ifdef CONFIG_DRVEXT_MODULE
	if (drvext_rx_handler(padapter, precv_frame->rx_data, precv_frame->len) == _SUCCESS)
		goto _recv_indicatepkt_drop;
#endif

	skb = precv_frame->pkt;
	if (skb == NULL) {
		RT_TRACE(_module_recv_osdep_c_,_drv_err_,("rtw_recv_indicatepkt():skb==NULL something wrong!!!!\n"));
		goto _recv_indicatepkt_drop;
	}

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,
		 ("rtw_recv_indicatepkt():skb != NULL !!!\n"));
	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 ("rtw_recv_indicatepkt():precv_frame->rx_head=%p  precv_frame->hdr.rx_data=%p\n",
		 precv_frame->rx_head, precv_frame->rx_data));
	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 ("precv_frame->hdr.rx_tail=%p precv_frame->rx_end=%p precv_frame->hdr.len=%d\n",
		 precv_frame->rx_tail,
		 precv_frame->rx_end,
		 precv_frame->len));

	skb->data = precv_frame->rx_data;

	skb_set_tail_pointer(skb, precv_frame->len);

	skb->len = precv_frame->len;

	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 ("\n skb->head=%p skb->data=%p skb->tail=%p skb->end=%p skb->len=%d\n",
		 skb->head, skb->data, skb_tail_pointer(skb),
		 skb_end_pointer(skb), skb->len));

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		struct sk_buff *pskb2 = NULL;
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &padapter->stapriv;
		struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
		int bmcast = IS_MCAST(pattrib->dst);

		if (_rtw_memcmp(pattrib->dst, myid(&padapter->eeprompriv), ETH_ALEN)==false) {
			if (bmcast) {
				psta = rtw_get_bcmc_stainfo(padapter);
				pskb2 = skb_clone(skb, GFP_ATOMIC);
			} else {
				psta = rtw_get_stainfo(pstapriv, pattrib->dst);
			}

			if (psta) {
				struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;

				skb->dev = pnetdev;
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
				skb_set_queue_mapping(skb, rtw_recv_select_queue(skb));
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35) */

				rtw_xmit_entry(skb, pnetdev);

				if (bmcast)
					skb = pskb2;
				else
					goto _recv_indicatepkt_end;
			}

		}
	}

#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
	if ((pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}
#else /* !CONFIG_TCP_CSUM_OFFLOAD_RX */

	skb->ip_summed = CHECKSUM_NONE;

#endif

	skb->dev = padapter->pnetdev;
	skb->protocol = eth_type_trans(skb, padapter->pnetdev);

	netif_rx(skb);

_recv_indicatepkt_end:

	precv_frame->pkt = NULL; /*  pointers to NULL before rtw_free_recvframe() */

	rtw_free_recvframe(precv_frame, pfree_recv_queue);

	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n rtw_recv_indicatepkt :after netif_rx!!!!\n"));

        return _SUCCESS;

_recv_indicatepkt_drop:

	 /* enqueue back to free_recv_queue */
	 rtw_free_recvframe(precv_frame, pfree_recv_queue);

	 return _FAIL;
}

void rtw_os_read_port(struct rtw_adapter *padapter, struct recv_buf *precvbuf)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;

	precvbuf->ref_cnt--;

	/* free skb in recv_buf */
	dev_kfree_skb_any(precvbuf->pskb);

	precvbuf->pskb = NULL;
	precvbuf->reuse = false;

	if (precvbuf->irp_pending == false)
		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
}

static void _rtw_reordering_ctrl_timeout_handler (void *FunctionContext)
{
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)FunctionContext;
	rtw_reordering_ctrl_timeout_handler(preorder_ctrl);
}

void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	struct rtw_adapter *padapter = preorder_ctrl->padapter;

	_init_timer(&(preorder_ctrl->reordering_ctrl_timer), padapter->pnetdev, _rtw_reordering_ctrl_timeout_handler, preorder_ctrl);
}
