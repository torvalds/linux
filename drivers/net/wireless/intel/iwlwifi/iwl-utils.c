/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#include <net/gso.h>
#include <linux/ieee80211.h>
#include <net/ip.h>

#include "iwl-drv.h"
#include "iwl-utils.h"

#ifdef CONFIG_INET
int iwl_tx_tso_segment(struct sk_buff *skb, unsigned int num_subframes,
		       netdev_features_t netdev_flags,
		       struct sk_buff_head *mpdus_skbs)
{
	struct sk_buff *tmp, *next;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	char cb[sizeof(skb->cb)];
	u16 i = 0;
	unsigned int tcp_payload_len;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	bool ipv4 = (skb->protocol == htons(ETH_P_IP));
	bool qos = ieee80211_is_data_qos(hdr->frame_control);
	u16 ip_base_id = ipv4 ? ntohs(ip_hdr(skb)->id) : 0;

	skb_shinfo(skb)->gso_size = num_subframes * mss;
	memcpy(cb, skb->cb, sizeof(cb));

	next = skb_gso_segment(skb, netdev_flags);
	skb_shinfo(skb)->gso_size = mss;
	skb_shinfo(skb)->gso_type = ipv4 ? SKB_GSO_TCPV4 : SKB_GSO_TCPV6;

	if (IS_ERR(next) && PTR_ERR(next) == -ENOMEM)
		return -ENOMEM;

	if (WARN_ONCE(IS_ERR(next),
		      "skb_gso_segment error: %d\n", (int)PTR_ERR(next)))
		return PTR_ERR(next);

	if (next)
		consume_skb(skb);

	skb_list_walk_safe(next, tmp, next) {
		memcpy(tmp->cb, cb, sizeof(tmp->cb));
		/*
		 * Compute the length of all the data added for the A-MSDU.
		 * This will be used to compute the length to write in the TX
		 * command. We have: SNAP + IP + TCP for n -1 subframes and
		 * ETH header for n subframes.
		 */
		tcp_payload_len = skb_tail_pointer(tmp) -
			skb_transport_header(tmp) -
			tcp_hdrlen(tmp) + tmp->data_len;

		if (ipv4)
			ip_hdr(tmp)->id = htons(ip_base_id + i * num_subframes);

		if (tcp_payload_len > mss) {
			skb_shinfo(tmp)->gso_size = mss;
			skb_shinfo(tmp)->gso_type = ipv4 ? SKB_GSO_TCPV4 :
							   SKB_GSO_TCPV6;
		} else {
			if (qos) {
				u8 *qc;

				if (ipv4)
					ip_send_check(ip_hdr(tmp));

				qc = ieee80211_get_qos_ctl((void *)tmp->data);
				*qc &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;
			}
			skb_shinfo(tmp)->gso_size = 0;
		}

		skb_mark_not_on_list(tmp);
		__skb_queue_tail(mpdus_skbs, tmp);
		i++;
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_tx_tso_segment);
#endif /* CONFIG_INET */
