/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#ifndef _NET_OVPN_SKB_H_
#define _NET_OVPN_SKB_H_

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/types.h>

struct ovpn_cb {
	struct ovpn_peer *peer;
	struct ovpn_crypto_key_slot *ks;
	struct aead_request *req;
	struct scatterlist *sg;
	u8 *iv;
	unsigned int payload_offset;
	bool nosignal;
};

static inline struct ovpn_cb *ovpn_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ovpn_cb) > sizeof(skb->cb));
	return (struct ovpn_cb *)skb->cb;
}

/* Return IP protocol version from skb header.
 * Return 0 if protocol is not IPv4/IPv6 or cannot be read.
 */
static inline __be16 ovpn_ip_check_protocol(struct sk_buff *skb)
{
	__be16 proto = 0;

	/* skb could be non-linear,
	 * make sure IP header is in non-fragmented part
	 */
	if (!pskb_network_may_pull(skb, sizeof(struct iphdr)))
		return 0;

	if (ip_hdr(skb)->version == 4) {
		proto = htons(ETH_P_IP);
	} else if (ip_hdr(skb)->version == 6) {
		if (!pskb_network_may_pull(skb, sizeof(struct ipv6hdr)))
			return 0;
		proto = htons(ETH_P_IPV6);
	}

	return proto;
}

#endif /* _NET_OVPN_SKB_H_ */
