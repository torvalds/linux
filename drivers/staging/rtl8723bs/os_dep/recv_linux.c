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
