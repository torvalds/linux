// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <linux/jiffies.h>
#include <net/cfg80211.h>
#include <linux/unaligned.h>

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
	signed int i;
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
	if (precvbuf->pskb)
		dev_kfree_skb_any(precvbuf->pskb);
}

struct sk_buff *rtw_os_alloc_msdu_pkt(union recv_frame *prframe, u16 nSubframe_Length, u8 *pdata)
{
	u16 eth_type;
	struct sk_buff *sub_skb;
	struct rx_pkt_attrib *pattrib;

	pattrib = &prframe->u.hdr.attrib;

	sub_skb = rtw_skb_alloc(nSubframe_Length + 12);
	if (!sub_skb)
		return NULL;

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

void rtw_os_recv_indicate_pkt(struct adapter *padapter, struct sk_buff *pkt, struct rx_pkt_attrib *pattrib)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	/* Indicate the packets to upper layer */
	if (pkt) {
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
			struct sk_buff *pskb2 = NULL;
			struct sta_info *psta = NULL;
			struct sta_priv *pstapriv = &padapter->stapriv;
			int bmcast = is_multicast_ether_addr(pattrib->dst);

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
			}
		}

		pkt->protocol = eth_type_trans(pkt, padapter->pnetdev);
		pkt->dev = padapter->pnetdev;

		pkt->ip_summed = CHECKSUM_NONE;

		rtw_netif_rx(padapter->pnetdev, pkt);
	}
}
