// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RECV_OSDEP_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <linux/jiffies.h>
#include <net/cfg80211.h>
#include <asm/unaligned.h>

void rtw_os_free_recvframe(union recv_frame *precvframe)
{
	if (precvframe->u.hdr.pkt) {
		dev_kfree_skb_any(precvframe->u.hdr.pkt);/* free skb by driver */

		precvframe->u.hdr.pkt = NULL;
	}
}

/* alloc os related resource in union recv_frame */
void rtw_os_recv_resource_alloc(struct adapter *padapter, union recv_frame *precvframe)
{
	precvframe->u.hdr.pkt_newalloc = precvframe->u.hdr.pkt = NULL;
}

/* free os related resource in union recv_frame */
void rtw_os_recv_resource_free(struct recv_priv *precvpriv)
{
	sint i;
	union recv_frame *precvframe;

	precvframe = (union recv_frame *) precvpriv->precv_frame_buf;

	for (i = 0; i < NR_RECVFRAME; i++) {
		if (precvframe->u.hdr.pkt) {
			/* free skb by driver */
			dev_kfree_skb_any(precvframe->u.hdr.pkt);
			precvframe->u.hdr.pkt = NULL;
		}
		precvframe++;
	}
}

/* free os related resource in struct recv_buf */
void rtw_os_recvbuf_resource_free(struct adapter *padapter, struct recv_buf *precvbuf)
{
	if (precvbuf->pskb) {
		dev_kfree_skb_any(precvbuf->pskb);
	}
}

_pkt *rtw_os_alloc_msdu_pkt(union recv_frame *prframe, u16 nSubframe_Length, u8 *pdata)
{
	u16 eth_type;
	_pkt *sub_skb;
	struct rx_pkt_attrib *pattrib;

	pattrib = &prframe->u.hdr.attrib;

	sub_skb = rtw_skb_alloc(nSubframe_Length + 12);
	if (!sub_skb) {
		DBG_871X("%s(): rtw_skb_alloc() Fail!!!\n", __func__);
		return NULL;
	}

	skb_reserve(sub_skb, 12);
	skb_put_data(sub_skb, (pdata + ETH_HLEN), nSubframe_Length);

	eth_type = get_unaligned_be16(&sub_skb->data[6]);

	if (sub_skb->len >= 8 &&
		((!memcmp(sub_skb->data, rfc1042_header, SNAP_SIZE) &&
		  eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		 !memcmp(sub_skb->data, bridge_tunnel_header, SNAP_SIZE))) {
		/*
		 * remove RFC1042 or Bridge-Tunnel encapsulation and replace
		 * EtherType
		 */
		skb_pull(sub_skb, SNAP_SIZE);
		memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
		memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
	} else {
		__be16 len;
		/* Leave Ethernet header part of hdr and full payload */
		len = htons(sub_skb->len);
		memcpy(skb_push(sub_skb, 2), &len, 2);
		memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
		memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
	}

	return sub_skb;
}

void rtw_os_recv_indicate_pkt(struct adapter *padapter, _pkt *pkt, struct rx_pkt_attrib *pattrib)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret;

	/* Indicate the packets to upper layer */
	if (pkt) {
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
			_pkt *pskb2 = NULL;
			struct sta_info *psta = NULL;
			struct sta_priv *pstapriv = &padapter->stapriv;
			int bmcast = IS_MCAST(pattrib->dst);

			if (memcmp(pattrib->dst, myid(&padapter->eeprompriv), ETH_ALEN)) {
				if (bmcast) {
					psta = rtw_get_bcmc_stainfo(padapter);
					pskb2 = skb_clone(pkt, GFP_ATOMIC);
				} else {
					psta = rtw_get_stainfo(pstapriv, pattrib->dst);
				}

				if (psta) {
					struct net_device *pnetdev = (struct net_device *)padapter->pnetdev;
					/* skb->ip_summed = CHECKSUM_NONE; */
					pkt->dev = pnetdev;
					skb_set_queue_mapping(pkt, rtw_recv_select_queue(pkt));

					_rtw_xmit_entry(pkt, pnetdev);

					if (bmcast && pskb2)
						pkt = pskb2;
					else
						return;
				}
			} else {
				/*  to APself */
				/* DBG_871X("to APSelf\n"); */
			}
		}

		pkt->protocol = eth_type_trans(pkt, padapter->pnetdev);
		pkt->dev = padapter->pnetdev;

#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
		if ((pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1))
			pkt->ip_summed = CHECKSUM_UNNECESSARY;
		else
			pkt->ip_summed = CHECKSUM_NONE;

#else /* !CONFIG_TCP_CSUM_OFFLOAD_RX */
		pkt->ip_summed = CHECKSUM_NONE;
#endif /* CONFIG_TCP_CSUM_OFFLOAD_RX */

		ret = rtw_netif_rx(padapter->pnetdev, pkt);
	}
}

void rtw_handle_tkip_mic_err(struct adapter *padapter, u8 bgroup)
{
	enum nl80211_key_type key_type = 0;
	union iwreq_data wrqu;
	struct iw_michaelmicfailure    ev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	unsigned long cur_time = 0;

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

	if (bgroup) {
		key_type |= NL80211_KEYTYPE_GROUP;
	} else {
		key_type |= NL80211_KEYTYPE_PAIRWISE;
	}

	cfg80211_michael_mic_failure(padapter->pnetdev, (u8 *)&pmlmepriv->assoc_bssid[0], key_type, -1,
		NULL, GFP_ATOMIC);

	memset(&ev, 0x00, sizeof(ev));
	if (bgroup) {
		ev.flags |= IW_MICFAILURE_GROUP;
	} else {
		ev.flags |= IW_MICFAILURE_PAIRWISE;
	}

	ev.src_addr.sa_family = ARPHRD_ETHER;
	memcpy(ev.src_addr.sa_data, &pmlmepriv->assoc_bssid[0], ETH_ALEN);

	memset(&wrqu, 0x00, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);
}

#ifdef CONFIG_AUTO_AP_MODE
static void rtw_os_ksocket_send(struct adapter *padapter, union recv_frame *precv_frame)
{
	_pkt *skb = precv_frame->u.hdr.pkt;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_info *psta = precv_frame->u.hdr.psta;

	DBG_871X("eth rx: got eth_type = 0x%x\n", pattrib->eth_type);

	if (psta && psta->isrc && psta->pid > 0) {
		u16 rx_pid;

		rx_pid = *(u16 *)(skb->data+ETH_HLEN);

		DBG_871X("eth rx(pid = 0x%x): sta(%pM) pid = 0x%x\n",
			rx_pid, MAC_ARG(psta->hwaddr), psta->pid);

		if (rx_pid == psta->pid) {
			int i;
			u16 len = *(u16 *)(skb->data+ETH_HLEN+2);
			DBG_871X("eth, RC: len = 0x%x\n", len);

			for (i = 0; i < len; i++)
				DBG_871X("0x%x\n", *(skb->data+ETH_HLEN+4+i));

			DBG_871X("eth, RC-end\n");
		}

	}

}
#endif /* CONFIG_AUTO_AP_MODE */

int rtw_recv_indicatepkt(struct adapter *padapter, union recv_frame *precv_frame)
{
	struct recv_priv *precvpriv;
	struct __queue	*pfree_recv_queue;
	_pkt *skb;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;

	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);

	skb = precv_frame->u.hdr.pkt;
	if (skb == NULL) {
		RT_TRACE(_module_recv_osdep_c_, _drv_err_, ("rtw_recv_indicatepkt():skb == NULL something wrong!!!!\n"));
		goto _recv_indicatepkt_drop;
	}

	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("rtw_recv_indicatepkt():skb != NULL !!!\n"));
	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("rtw_recv_indicatepkt():precv_frame->u.hdr.rx_head =%p  precv_frame->hdr.rx_data =%p\n", precv_frame->u.hdr.rx_head, precv_frame->u.hdr.rx_data));
	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("precv_frame->hdr.rx_tail =%p precv_frame->u.hdr.rx_end =%p precv_frame->hdr.len =%d\n", precv_frame->u.hdr.rx_tail, precv_frame->u.hdr.rx_end, precv_frame->u.hdr.len));

	skb->data = precv_frame->u.hdr.rx_data;

	skb_set_tail_pointer(skb, precv_frame->u.hdr.len);

	skb->len = precv_frame->u.hdr.len;

	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("\n skb->head =%p skb->data =%p skb->tail =%p skb->end =%p skb->len =%d\n", skb->head, skb->data, skb_tail_pointer(skb), skb_end_pointer(skb), skb->len));

#ifdef CONFIG_AUTO_AP_MODE
	if (0x8899 == pattrib->eth_type) {
		rtw_os_ksocket_send(padapter, precv_frame);

		/* goto _recv_indicatepkt_drop; */
	}
#endif /* CONFIG_AUTO_AP_MODE */

	rtw_os_recv_indicate_pkt(padapter, skb, pattrib);

	/* pointers to NULL before rtw_free_recvframe() */
	precv_frame->u.hdr.pkt = NULL;

	rtw_free_recvframe(precv_frame, pfree_recv_queue);

	RT_TRACE(_module_recv_osdep_c_, _drv_info_, ("\n rtw_recv_indicatepkt :after rtw_os_recv_indicate_pkt!!!!\n"));

	return _SUCCESS;

_recv_indicatepkt_drop:

	/* enqueue back to free_recv_queue */
	rtw_free_recvframe(precv_frame, pfree_recv_queue);

	return _FAIL;
}

void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	timer_setup(&preorder_ctrl->reordering_ctrl_timer,
		    rtw_reordering_ctrl_timeout_handler, 0);

}
