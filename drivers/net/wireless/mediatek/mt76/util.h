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
#include <net/mac80211.h>

struct mt76_worker
{
	struct task_struct *task;
	void (*fn)(struct mt76_worker *);
	unsigned long state;
};

enum {
	MT76_WORKER_SCHEDULED,
	MT76_WORKER_RUNNING,
};

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

int __mt76_worker_fn(void *ptr);

static inline int
mt76_worker_setup(struct ieee80211_hw *hw, struct mt76_worker *w,
		  void (*fn)(struct mt76_worker *),
		  const char *name)
{
	const char *dev_name = wiphy_name(hw->wiphy);
	int ret;

	if (fn)
		w->fn = fn;
	w->task = kthread_run(__mt76_worker_fn, w,
			      "mt76-%s %s", name, dev_name);

	if (IS_ERR(w->task)) {
		ret = PTR_ERR(w->task);
		w->task = NULL;
		return ret;
	}

	return 0;
}

static inline void mt76_worker_schedule(struct mt76_worker *w)
{
	if (!w->task)
		return;

	if (!test_and_set_bit(MT76_WORKER_SCHEDULED, &w->state) &&
	    !test_bit(MT76_WORKER_RUNNING, &w->state))
		wake_up_process(w->task);
}

static inline void mt76_worker_disable(struct mt76_worker *w)
{
	if (!w->task)
		return;

	kthread_park(w->task);
	WRITE_ONCE(w->state, 0);
}

static inline void mt76_worker_enable(struct mt76_worker *w)
{
	if (!w->task)
		return;

	kthread_unpark(w->task);
	mt76_worker_schedule(w);
}

static inline void mt76_worker_teardown(struct mt76_worker *w)
{
	if (!w->task)
		return;

	kthread_stop(w->task);
	w->task = NULL;
}

#endif
