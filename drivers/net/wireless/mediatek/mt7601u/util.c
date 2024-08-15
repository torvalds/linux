// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 */

#include "mt7601u.h"

void mt76_remove_hdr_pad(struct sk_buff *skb)
{
	int len = ieee80211_get_hdrlen_from_skb(skb);

	memmove(skb->data + 2, skb->data, len);
	skb_pull(skb, 2);
}

int mt76_insert_hdr_pad(struct sk_buff *skb)
{
	int len = ieee80211_get_hdrlen_from_skb(skb);
	int ret;

	if (len % 4 == 0)
		return 0;

	ret = skb_cow(skb, 2);
	if (ret)
		return ret;

	skb_push(skb, 2);
	memmove(skb->data, skb->data + 2, len);

	skb->data[len] = 0;
	skb->data[len + 1] = 0;
	return 0;
}
