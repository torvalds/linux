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
 ******************************************************************************/
#define _RECV_OSDEP_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <wifi.h>
#include <recv_osdep.h>

#include <osdep_intf.h>

#include <usb_ops.h>

void rtw_handle_tkip_mic_err23a(struct rtw_adapter *padapter, u8 bgroup)
{
	enum nl80211_key_type key_type = 0;
	union iwreq_data wrqu;
	struct iw_michaelmicfailure ev;
	struct mlme_priv *pmlmepriv  = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	unsigned long cur_time;

	if (psecuritypriv->last_mic_err_time == 0) {
		psecuritypriv->last_mic_err_time = jiffies;
	} else {
		cur_time = jiffies;

		if (cur_time - psecuritypriv->last_mic_err_time < 60*HZ) {
			psecuritypriv->btkip_countermeasure = true;
			psecuritypriv->last_mic_err_time = 0;
			psecuritypriv->btkip_countermeasure_time = cur_time;
		} else {
			psecuritypriv->last_mic_err_time = jiffies;
		}
	}

	if (bgroup)
		key_type |= NL80211_KEYTYPE_GROUP;
	else
		key_type |= NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(padapter->pnetdev,
				     (u8 *)&pmlmepriv->assoc_bssid[0],
				     key_type, -1, NULL, GFP_ATOMIC);

	memset(&ev, 0x00, sizeof(ev));
	if (bgroup)
		ev.flags |= IW_MICFAILURE_GROUP;
	else
		ev.flags |= IW_MICFAILURE_PAIRWISE;

	ev.src_addr.sa_family = ARPHRD_ETHER;
	ether_addr_copy(ev.src_addr.sa_data, &pmlmepriv->assoc_bssid[0]);

	memset(&wrqu, 0x00, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);
}

int rtw_recv_indicatepkt23a(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame)
{
	struct recv_priv *precvpriv;
	struct sk_buff *skb;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	precvpriv = &padapter->recvpriv;

	skb = precv_frame->pkt;
	if (!skb) {
		RT_TRACE(_module_recv_osdep_c_, _drv_err_,
			 "rtw_recv_indicatepkt23a():skb == NULL!!!!\n");
		goto _recv_indicatepkt_drop;
	}

	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 "rtw_recv_indicatepkt23a():skb != NULL !!!\n");
	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 "rtw_recv_indicatepkt23a():precv_frame->hdr.rx_data =%p\n",
		 precv_frame->pkt->data);
	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 "skb->head =%p skb->data =%p skb->tail =%p skb->end =%p skb->len =%d\n",
		 skb->head, skb->data,
		 skb_tail_pointer(skb), skb_end_pointer(skb), skb->len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		struct sk_buff *pskb2 = NULL;
		struct sta_info *psta = NULL;
		struct sta_priv *pstapriv = &padapter->stapriv;
		struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
		int bmcast = is_multicast_ether_addr(pattrib->dst);

		/* DBG_8723A("bmcast =%d\n", bmcast); */

		if (!ether_addr_equal(pattrib->dst,
				      myid(&padapter->eeprompriv))) {
			/* DBG_8723A("not ap psta =%p, addr =%pM\n", psta, pattrib->dst); */
			if (bmcast) {
				psta = rtw_get_bcmc_stainfo23a(padapter);
				pskb2 = skb_clone(skb, GFP_ATOMIC);
			} else {
				psta = rtw_get_stainfo23a(pstapriv, pattrib->dst);
			}

			if (psta) {
				struct net_device *pnetdev = padapter->pnetdev;

				/* DBG_8723A("directly forwarding to the rtw_xmit23a_entry23a\n"); */

				/* skb->ip_summed = CHECKSUM_NONE; */
				skb->dev = pnetdev;
				skb_set_queue_mapping(skb, rtw_recv_select_queue23a(skb));

				rtw_xmit23a_entry23a(skb, pnetdev);

				if (bmcast)
					skb = pskb2;
				else
					goto _recv_indicatepkt_end;
			}
		} else { /*  to APself */
			/* DBG_8723A("to APSelf\n"); */
		}
	}

	skb->ip_summed = CHECKSUM_NONE;
	skb->dev = padapter->pnetdev;
	skb->protocol = eth_type_trans(skb, padapter->pnetdev);

	netif_rx(skb);

_recv_indicatepkt_end:

	precv_frame->pkt = NULL; /*  pointers to NULL before rtw_free_recvframe23a() */

	rtw_free_recvframe23a(precv_frame);

	RT_TRACE(_module_recv_osdep_c_, _drv_info_,
		 "rtw_recv_indicatepkt23a :after netif_rx!!!!\n");
	return _SUCCESS;

_recv_indicatepkt_drop:

	 rtw_free_recvframe23a(precv_frame);
	 return _FAIL;
}

void rtw_init_recv_timer23a(struct recv_reorder_ctrl *preorder_ctrl)
{
	setup_timer(&preorder_ctrl->reordering_ctrl_timer,
		    rtw_reordering_ctrl_timeout_handler23a,
		    (unsigned long)preorder_ctrl);
}
