/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
 */

#ifndef __MT76_UTIL_H
#define __MT76_UTIL_H

#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#define MT76_INCR(_var, _size) \
	(_var = (((_var) + 1) % (_size)))

int mt76_wcid_alloc(u32 *mask, int size);

static inline bool
mt76_wcid_mask_test(u32 *mask, int idx)
{
	return mask[idx / 32] & BIT(idx % 32);
}

static inline void
mt76_wcid_mask_set(u32 *mask, int idx)
{
	mask[idx / 32] |= BIT(idx % 32);
}

static inline void
mt76_wcid_mask_clear(u32 *mask, int idx)
{
	mask[idx / 32] &= ~BIT(idx % 32);
}

static inline void
mt76_skb_set_moredata(struct sk_buff *skb, bool enable)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (enable)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	else
		hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_MOREDATA);
}

#endif
