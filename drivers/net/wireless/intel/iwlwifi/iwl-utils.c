/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
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

static u32 iwl_div_by_db(u32 value, u8 db)
{
	/*
	 * 2^32 * 10**(i / 10) for i = [1, 10], skipping 0 and simply stopping
	 * at 10 dB and looping instead of using a much larger table.
	 *
	 * Using 64 bit math is overkill, but means the helper does not require
	 * a limit on the input range.
	 */
	static const u32 db_to_val[] = {
		0xcb59185e, 0xa1866ba8, 0x804dce7a, 0x65ea59fe, 0x50f44d89,
		0x404de61f, 0x331426af, 0x2892c18b, 0x203a7e5b, 0x1999999a,
	};

	while (value && db > 0) {
		u8 change = min_t(u8, db, ARRAY_SIZE(db_to_val));

		value = (((u64)value) * db_to_val[change - 1]) >> 32;

		db -= change;
	}

	return value;
}

s8 iwl_average_neg_dbm(const u8 *neg_dbm_values, u8 len)
{
	int average_magnitude;
	u32 average_factor;
	int sum_magnitude = -128;
	u32 sum_factor = 0;
	int i, count = 0;

	/*
	 * To properly average the decibel values (signal values given in dBm)
	 * we need to do the math in linear space.  Doing a linear average of
	 * dB (dBm) values is a bit annoying though due to the large range of
	 * at least -10 to -110 dBm that will not fit into a 32 bit integer.
	 *
	 * A 64 bit integer should be sufficient, but then we still have the
	 * problem that there are no directly usable utility functions
	 * available.
	 *
	 * So, lets not deal with that and instead do much of the calculation
	 * with a 16.16 fixed point integer along with a base in dBm. 16.16 bit
	 * gives us plenty of head-room for adding up a few values and even
	 * doing some math on it. And the tail should be accurate enough too
	 * (1/2^16 is somewhere around -48 dB, so effectively zero).
	 *
	 * i.e. the real value of sum is:
	 *      sum = sum_factor / 2^16 * 10^(sum_magnitude / 10) mW
	 *
	 * However, that does mean we need to be able to bring two values to
	 * a common base, so we need a helper for that.
	 *
	 * Note that this function takes an input with unsigned negative dBm
	 * values but returns a signed dBm (i.e. a negative value).
	 */

	for (i = 0; i < len; i++) {
		int val_magnitude;
		u32 val_factor;

		/* Assume invalid */
		if (neg_dbm_values[i] == 0xff)
			continue;

		val_factor = 0x10000;
		val_magnitude = -neg_dbm_values[i];

		if (val_magnitude <= sum_magnitude) {
			u8 div_db = sum_magnitude - val_magnitude;

			val_factor = iwl_div_by_db(val_factor, div_db);
			val_magnitude = sum_magnitude;
		} else {
			u8 div_db = val_magnitude - sum_magnitude;

			sum_factor = iwl_div_by_db(sum_factor, div_db);
			sum_magnitude = val_magnitude;
		}

		sum_factor += val_factor;
		count++;
	}

	/* No valid noise measurement, return a very high noise level */
	if (count == 0)
		return 0;

	average_magnitude = sum_magnitude;
	average_factor = sum_factor / count;

	/*
	 * average_factor will be a number smaller than 1.0 (0x10000) at this
	 * point. What we need to do now is to adjust average_magnitude so that
	 * average_factor is between -0.5 dB and 0.5 dB.
	 *
	 * Just do -1 dB steps and find the point where
	 *   -0.5 dB * -i dB = 0x10000 * 10^(-0.5/10) / i dB
	 *                   = div_by_db(0xe429, i)
	 * is smaller than average_factor.
	 */
	for (i = 0; average_factor < iwl_div_by_db(0xe429, i); i++) {
		/* nothing */
	}

	return clamp(average_magnitude - i, -128, 0);
}
IWL_EXPORT_SYMBOL(iwl_average_neg_dbm);
