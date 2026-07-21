// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/skbuff.h>
#include <linux/jiffies.h>
#include "hif.h"
#include "skbq.h"
#include "mac.h"
#include "command.h"
#include "bus.h"

/* Returns number of bytes needed to word align */
#define BYTES_NEEDED_TO_WORD_ALIGN(bytes) \
	((bytes) & 0x3 ? (4 - ((bytes) & 0x3)) : 0)

/* Rounds down to the nearest word boundary */
#define ROUND_DOWN_TO_WORD(bytes)                                  \
	(BYTES_NEEDED_TO_WORD_ALIGN(bytes) ?                       \
		 bytes - (4 - BYTES_NEEDED_TO_WORD_ALIGN(bytes)) : \
		 bytes)

#define MM81X_SKBQ_MAX_TXQ_LEN 32
#define MM81X_SKBQ_TX_QUEUED_LIFETIME_MS 1000
#define MM81X_SKBQ_TX_STATUS_LIFETIME_MS (15 * 1000)

/* Returns padding needed to align x up to a 4-byte boundary */
#define MM81X_PAD4(x) (((x) & 0x3) ? (4 - ((x) & 0x3)) : 0)

struct mm81x_tx_status_priv {
	/*
	 * Time (jiffies) at which this packet has spent too long the pending
	 * queue, waiting for status notification from the firmware, and
	 * should be considered lost.
	 */
	unsigned long tx_status_expiry;
};

static struct mm81x_tx_status_priv *
__mm81x_skbq_tx_status_priv(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct mm81x_tx_status_priv) >
		     sizeof(tx_info->status.status_driver_data));
	return (struct mm81x_tx_status_priv *)&tx_info->status
		.status_driver_data[0];
}

static bool __mm81x_skbq_has_pending_tx_skb_timed_out(struct sk_buff *skb)
{
	struct mm81x_tx_status_priv *info = __mm81x_skbq_tx_status_priv(skb);

	/* If our timestamp value is in the past then we have timed out. */
	return time_is_before_jiffies(info->tx_status_expiry);
}

static u32 __mm81x_skbq_size(const struct mm81x_skbq *mq)
{
	return mq->skbq_size;
}

static u32 __mm81x_skbq_space(const struct mm81x_skbq *mq)
{
	return MM81X_SKBQ_SIZE - __mm81x_skbq_size(mq);
}

static bool __mm81x_skbq_over_threshold(struct mm81x_skbq *mq)
{
	return skb_queue_len(&mq->skbq) >= MM81X_SKBQ_MAX_TXQ_LEN;
}

static bool __mm81x_skbq_under_threshold(struct mm81x_skbq *mq)
{
	return skb_queue_len(&mq->skbq) < (MM81X_SKBQ_MAX_TXQ_LEN - 2);
}

static void __mm81x_skbq_unlink(struct mm81x_skbq *mq,
				struct sk_buff_head *queue, struct sk_buff *skb)
{
	if (queue == &mq->skbq) {
		WARN_ON(skb->len > mq->skbq_size);
		mq->skbq_size -= min(skb->len, mq->skbq_size);
	}

	__skb_unlink(skb, queue);
}

static int __mm81x_skbq_put(struct mm81x_skbq *mq, struct sk_buff_head *queue,
			    struct sk_buff *skb, bool queue_at_head,
			    struct sk_buff *queue_before)
{
	/* Limit the size of the Tx queue, but not the pending queue */
	if (queue == &mq->skbq) {
		if (skb->len > __mm81x_skbq_space(mq))
			return -ENOMEM;

		mq->skbq_size += skb->len;
	}

	if (queue_before)
		__skb_queue_before(queue, queue_before, skb);
	else if (queue_at_head)
		__skb_queue_head(queue, skb);
	else
		__skb_queue_tail(queue, skb);

	return 0;
}

static void __mm81x_skbq_pkt_id(struct mm81x_skbq *mq, struct sk_buff *skb)
{
	struct mm81x_skb_hdr *hdr = (struct mm81x_skb_hdr *)skb->data;

	hdr->tx_info.pkt_id = cpu_to_le32(mq->pkt_seq++);
}

static struct mm81x_skbq *
__mm81x_skbq_tx_status_to_skbq(struct mm81x *mors,
			       const struct mm81x_skb_tx_status *tx_sts)
{
	int aci;
	struct mm81x_skbq *mq = NULL;

	switch (tx_sts->channel) {
	case MM81X_SKB_CHAN_DATA:
	case MM81X_SKB_CHAN_DATA_NOACK:
		aci = dot11_tid_to_ac(tx_sts->tid);
		mq = mm81x_hif_get_tx_data_queue(mors, aci);
		break;
	case MM81X_SKB_CHAN_MGMT:
		mq = mm81x_hif_get_tx_mgmt_queue(mors);
		break;
	case MM81X_SKB_CHAN_BEACON:
		mq = mm81x_hif_get_tx_beacon_queue(mors);
		break;
	default:
		dev_err(mors->dev,
			"unexpected channel on reported tx status [%d]",
			tx_sts->channel);
	}

	return mq;
}

void mm81x_skbq_pull_hdr_post_tx(struct sk_buff *skb)
{
	skb_pull(skb, sizeof(struct mm81x_skb_hdr) +
			      ((struct mm81x_skb_hdr *)skb->data)->offset);
}

static void mm81x_skbq_insert_pending(struct mm81x_skbq *mq,
				      struct sk_buff *skb, __le32 insertion_id)
{
	struct sk_buff *pfirst, *pnext;
	struct mm81x_skb_hdr *mhdr;
	struct sk_buff *tail = skb_peek_tail(&mq->skbq);

	__mm81x_skbq_unlink(mq, &mq->pending, skb);

	if (!tail) {
		__mm81x_skbq_put(mq, &mq->skbq, skb, false, NULL);
		return;
	}

	/* Check if it should just be inserted on to the end */
	mhdr = (struct mm81x_skb_hdr *)tail->data;
	WARN_ON(insertion_id == mhdr->tx_info.pkt_id);
	if (le32_to_cpu(insertion_id) >= le32_to_cpu(mhdr->tx_info.pkt_id)) {
		__mm81x_skbq_put(mq, &mq->skbq, skb, false, NULL);
		return;
	}

	/* Otherwise, re-insert to correct spot in skbq */
	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		mhdr = (struct mm81x_skb_hdr *)pfirst->data;

		WARN_ON(insertion_id == mhdr->tx_info.pkt_id);
		if (le32_to_cpu(insertion_id) <=
		    le32_to_cpu(mhdr->tx_info.pkt_id)) {
			__mm81x_skbq_put(mq, &mq->skbq, skb, false, pfirst);
			return;
		}
	}

	WARN_ON_ONCE(1);
}

static void mm81x_skbq_sta_eosp(struct mm81x *mors, struct sk_buff *skb)
{
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = txi->control.vif;

	mm81x_skbq_pull_hdr_post_tx(skb);

	/*
	 * If this frame is the last frame in a PS-Poll or u-APSD SP,
	 * then mac80211 must be informed that the SP is now over.
	 */
	if (txi->flags & IEEE80211_TX_STATUS_EOSP) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		struct ieee80211_sta *sta;

		scoped_guard(rcu) {
			sta = ieee80211_find_sta(vif, hdr->addr1);
			if (sta)
				ieee80211_sta_eosp(sta);
		}
	}
}

static void __mm81x_skbq_drop_pending_skb(struct mm81x_skbq *mq,
					  struct sk_buff *skb)
{
	__mm81x_skbq_unlink(mq, &mq->pending, skb);
	mm81x_skbq_sta_eosp(mq->mors, skb);
	ieee80211_free_txskb(mq->mors->hw, skb);
}

static bool mm81x_tx_h_is_ps_filtered(struct mm81x_skbq *mq,
				      struct sk_buff *skb,
				      struct mm81x_skb_tx_status *tx_sts)
{
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = txi->control.vif;

	WARN_ON_ONCE(!(le32_to_cpu(tx_sts->flags) &
		       MM81X_TX_STATUS_FLAGS_PS_FILTERED));

	if (vif->type == NL80211_IFTYPE_AP) {
		__mm81x_skbq_drop_pending_skb(mq, skb);
		return true;
	}

	if (vif->type == NL80211_IFTYPE_STATION) {
		mm81x_skbq_insert_pending(mq, skb, tx_sts->pkt_id);
		return true;
	}

	return false;
}

/*
 * Get a pending frame by its ID. This will also drop frames with
 * older packet ids that are in the list
 */
static struct sk_buff *__mm81x_skbq_get_pending_by_id(struct mm81x *mors,
						      struct mm81x_skbq *mq,
						      u32 pkt_id)
{
	struct sk_buff *pfirst, *pnext;
	struct sk_buff *ret = NULL;

	/* Move sent packets to pending list waiting for feedback */
	skb_queue_walk_safe(&mq->pending, pfirst, pnext) {
		struct mm81x_skb_hdr *hdr =
			(struct mm81x_skb_hdr *)pfirst->data;

		if (le32_to_cpu(hdr->tx_info.pkt_id) == pkt_id) {
			ret = pfirst;
			break;

		} else if (le32_to_cpu(hdr->tx_info.pkt_id) < pkt_id &&
			   __mm81x_skbq_has_pending_tx_skb_timed_out(pfirst)) {
			__mm81x_skbq_drop_pending_skb(mq, pfirst);
		}
	}

	return ret;
}

static void mm81x_skbq_check_tx_empty(struct mm81x *mors, struct mm81x_skbq *mq)
{
	lockdep_assert_held(&mq->lock);

	if (mq->flags & MM81X_HIF_FLAGS_BEACON)
		return;

	if (skb_queue_len(&mq->skbq) + skb_queue_len(&mq->pending) == 0)
		wake_up(&mq->mors->tx_empty_waitq);
}

static void __mm81x_skbq_tx_status_process(struct mm81x *mors,
					   struct mm81x_skbq *mq,
					   struct mm81x_skb_tx_status *tx_sts)
{
	struct sk_buff *skb;

	lockdep_assert_held(&mq->lock);

	skb = __mm81x_skbq_get_pending_by_id(mors, mq,
					     le32_to_cpu(tx_sts->pkt_id));
	if (!skb) {
		dev_dbg(mors->dev,
			"No pending pkt match found [pktid:%d chan:%d]",
			tx_sts->pkt_id, tx_sts->channel);
		goto out;
	}

	if (le32_to_cpu(tx_sts->flags) & MM81X_TX_STATUS_PAGE_INVALID) {
		__mm81x_skbq_drop_pending_skb(mq, skb);
		goto out;
	}

	if (le32_to_cpu(tx_sts->flags) & MM81X_TX_STATUS_FLAGS_PS_FILTERED &&
	    mm81x_tx_h_is_ps_filtered(mq, skb, tx_sts))
		/* Has been consumed by mm81x_tx_h_is_ps_filtered */
		goto out;

	mm81x_skbq_pull_hdr_post_tx(skb);
	mm81x_skbq_skb_finish(mq, skb, tx_sts);

out:
	mm81x_skbq_check_tx_empty(mors, mq);
}

static void mm81x_skbq_tx_status_process(struct mm81x *mors,
					 struct sk_buff *skb)
{
	int i;
	struct mm81x_skb_tx_status *tx_sts =
		(struct mm81x_skb_tx_status *)skb->data;
	int count = skb->len / sizeof(*tx_sts);

	for (i = 0; i < count; tx_sts++, i++) {
		struct mm81x_skbq *mq =
			__mm81x_skbq_tx_status_to_skbq(mors, tx_sts);

		if (mq) {
			spin_lock_bh(&mq->lock);
			__mm81x_skbq_tx_status_process(mors, mq, tx_sts);
			spin_unlock_bh(&mq->lock);
		}
	}

	if (mors->ps.enable && !mors->ps.suspended &&
	    (mm81x_hif_get_tx_buffered_count(mors) == 0)) {
		/* Evaluate ps, check if it was gated on a pending tx status */
		queue_delayed_work(mors->chip_wq, &mors->ps.delayed_eval_work,
				   0);
	}
}

static void mm81x_skbq_dispatch_work(struct work_struct *dispatch_work)
{
	struct mm81x_skbq *mq =
		container_of(dispatch_work, struct mm81x_skbq, dispatch_work);
	struct mm81x *mors = mq->mors;
	struct mm81x_skb_hdr *hdr;
	struct sk_buff_head skbq;
	struct sk_buff *pfirst, *pnext;
	u8 channel;

	__skb_queue_head_init(&skbq);

	mm81x_skbq_deq_num_skb(mq, &skbq, mm81x_skbq_count(mq));

	skb_queue_walk_safe(&skbq, pfirst, pnext) {
		__skb_unlink(pfirst, &skbq);
		/* Header endianness has already be adjusted */
		hdr = (struct mm81x_skb_hdr *)pfirst->data;
		channel = hdr->channel;
		/* Remove mm81x header and padding */
		__skb_pull(pfirst, sizeof(*hdr) + hdr->offset);

		switch (channel) {
		case MM81X_SKB_CHAN_COMMAND:
			mm81x_cmd_resp_process(mors, pfirst);
			break;
		case MM81X_SKB_CHAN_TX_STATUS:
			mm81x_skbq_tx_status_process(mors, pfirst);
			dev_kfree_skb_any(pfirst);
			break;
		default:
			mm81x_mac_rx_skb(mors, pfirst, &hdr->rx_status);
			break;
		}
	}

	if (mm81x_skbq_count(mq))
		queue_work(mors->net_wq, &mq->dispatch_work);
}

int mm81x_skbq_put(struct mm81x_skbq *mq, struct sk_buff *skb)
{
	int ret;

	spin_lock_bh(&mq->lock);
	ret = __mm81x_skbq_put(mq, &mq->skbq, skb, false, NULL);
	spin_unlock_bh(&mq->lock);
	return ret;
}

static void mm81x_skbq_set_queued_tx_skb_expiry(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);

	if (ieee80211_is_probe_req(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control) ||
	    ieee80211_is_auth(hdr->frame_control)) {
		txi->control.enqueue_time = (u32)jiffies;
	} else {
		txi->control.enqueue_time = 0;
	}
}

static bool mm81x_skbq_has_queued_tx_skb_expired(struct sk_buff *skb)
{
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);

	if (txi->control.enqueue_time > 0) {
		u32 expiry_time =
			txi->control.enqueue_time +
			msecs_to_jiffies(MM81X_SKBQ_TX_QUEUED_LIFETIME_MS);

		return (s32)((u32)jiffies - expiry_time) > 0;
	}

	return false;
}

/*
 * Drop selected frames (those with an expiry time set) that could not
 * be sent within a reasonable timeframe due to congestion. These would
 * only be rejected or ignored by the peer, so are only contributing to
 * the problem.
 */
void mm81x_skbq_purge_aged(struct mm81x *mors, struct mm81x_skbq *mq)
{
	struct sk_buff *pfirst;
	struct sk_buff *pnext;

	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		if (!mm81x_skbq_has_queued_tx_skb_expired(pfirst))
			break;
		__mm81x_skbq_unlink(mq, &mq->skbq, pfirst);
		ieee80211_free_txskb(mors->hw, pfirst);
	}

	spin_unlock_bh(&mq->lock);
}

void mm81x_skbq_purge(struct mm81x_skbq *mq, struct sk_buff_head *skbq)
{
	struct sk_buff *skb;

	spin_lock_bh(&mq->lock);
	while ((skb = __skb_dequeue(skbq)))
		dev_kfree_skb_any(skb);
	spin_unlock_bh(&mq->lock);
}

void mm81x_skbq_enq(struct mm81x_skbq *mq, struct sk_buff_head *skbq)
{
	int size;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	size = __mm81x_skbq_space(mq);
	skb_queue_walk_safe(skbq, pfirst, pnext) {
		if (pfirst->len > size)
			break;
		__skb_unlink(pfirst, skbq);
		__mm81x_skbq_put(mq, &mq->skbq, pfirst, false, NULL);
		size -= pfirst->len;
	}

	spin_unlock_bh(&mq->lock);
}

int mm81x_skbq_deq_num_skb(struct mm81x_skbq *mq, struct sk_buff_head *skbq,
			   int num_skb)
{
	int count = 0;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		if (count >= num_skb)
			break;
		__mm81x_skbq_unlink(mq, &mq->skbq, pfirst);
		__skb_queue_tail(skbq, pfirst);
		++count;
	}

	spin_unlock_bh(&mq->lock);
	return count;
}

void mm81x_skbq_enq_prepend(struct mm81x_skbq *mq, struct sk_buff_head *skbq)
{
	int size;
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	size = __mm81x_skbq_space(mq);

	/*
	 * We are doing a reverse walk here to ensure the order remains the
	 * same. This means the last member of the queue goes in, on top of
	 * the queue first and gets pushed down as more members get added to
	 * the top of the queue.
	 */
	skb_queue_reverse_walk_safe(skbq, pfirst, pnext) {
		if (pfirst->len > size)
			break;
		__skb_unlink(pfirst, skbq);
		__mm81x_skbq_put(mq, &mq->skbq, pfirst, true, NULL);
		size -= pfirst->len;
	}

	spin_unlock_bh(&mq->lock);
}

static void mm81x_skbq_stop_tx_queues(struct mm81x *mors)
{
	int queue;

	if (!mors->started)
		return;
	for (queue = IEEE80211_AC_VO; queue <= IEEE80211_AC_BK; queue++)
		ieee80211_stop_queue(mors->hw, queue);

	set_bit(MM81X_STATE_DATA_QS_STOPPED, &mors->state_flags);
}

/* Wake all Tx queues if all queues are below threshold */
void mm81x_skbq_may_wake_tx_queues(struct mm81x *mors)
{
	int queue;
	struct mm81x_skbq *qs;
	int num_qs;
	bool could_wake;

	if (!mors->started)
		return;

	could_wake = true;
	mm81x_hif_skbq_get_tx_qs(mors, &qs, &num_qs);
	for (queue = 0; queue < num_qs; queue++) {
		struct mm81x_skbq *mq = &qs[queue];

		if (!could_wake)
			break;

		spin_lock_bh(&mq->lock);
		could_wake &= (__mm81x_skbq_under_threshold(mq));
		spin_unlock_bh(&mq->lock);
	}

	if (!could_wake)
		return;

	for (queue = IEEE80211_AC_VO; queue <= IEEE80211_AC_BK; queue++)
		ieee80211_wake_queue(mors->hw, queue);

	clear_bit(MM81X_STATE_DATA_QS_STOPPED, &mors->state_flags);
}

static int mm81x_skbq_tx(struct mm81x_skbq *mq, struct sk_buff *skb, u8 channel)
{
	int rc;
	bool mq_over_threshold;
	struct mm81x *mors = mq->mors;

	spin_lock_bh(&mq->lock);
	rc = __mm81x_skbq_put(mq, &mq->skbq, skb, false, NULL);
	if (rc) {
		dev_err(mors->dev, "skb put chan %d failed (%d)", channel, rc);
		if (channel == MM81X_SKB_CHAN_DATA) {
			u16 queue = skb_get_queue_mapping(skb);

			dev_err(mors->dev, "skb put queue %d status %d", queue,
				ieee80211_queue_stopped(mors->hw, queue));
		}
	}

	/* Fill packet ID in TX info */
	__mm81x_skbq_pkt_id(mq, skb);

	mq_over_threshold = __mm81x_skbq_over_threshold(mq);
	spin_unlock_bh(&mq->lock);

	/* For data packets stop queues */
	if (channel == MM81X_SKB_CHAN_DATA && mq_over_threshold)
		mm81x_skbq_stop_tx_queues(mors);

	switch (channel) {
	case MM81X_SKB_CHAN_DATA:
	case MM81X_SKB_CHAN_DATA_NOACK:
		if (mm81x_is_data_tx_allowed(mors)) {
			set_bit(MM81X_HIF_EVT_TX_DATA_PEND,
				&mors->hif.event_flags);
			queue_work(mors->chip_wq, &mors->hif_work);
		}
		break;
	case MM81X_SKB_CHAN_MGMT:
		set_bit(MM81X_HIF_EVT_TX_MGMT_PEND, &mors->hif.event_flags);
		queue_work(mors->chip_wq, &mors->hif_work);
		break;
	case MM81X_SKB_CHAN_BEACON:
		set_bit(MM81X_HIF_EVT_TX_BEACON_PEND, &mors->hif.event_flags);
		queue_work(mors->chip_wq, &mors->hif_work);
		break;
	case MM81X_SKB_CHAN_COMMAND:
		set_bit(MM81X_HIF_EVT_TX_COMMAND_PEND, &mors->hif.event_flags);
		queue_work(mors->chip_wq, &mors->hif_work);
		break;
	default:
		dev_err(mors->dev, "Invalid skb channel: %d", channel);
		break;
	}

	return rc;
}

static void __mm81x_skbq_tx_move_to_pending(struct mm81x_skbq *mq,
					    struct sk_buff *skb)
{
	struct mm81x_tx_status_priv *pend_info =
		__mm81x_skbq_tx_status_priv(skb);

	pend_info->tx_status_expiry =
		jiffies + msecs_to_jiffies(MM81X_SKBQ_TX_STATUS_LIFETIME_MS);
	__mm81x_skbq_put(mq, &mq->pending, skb, false, NULL);
}

void mm81x_skbq_tx_complete(struct mm81x_skbq *mq, struct sk_buff_head *skbq)
{
	bool skb_awaits_tx_status = false;
	struct mm81x *mors = mq->mors;
	struct sk_buff *pfirst, *pnext;
	struct sk_buff *peek = skb_peek(skbq);
	struct mm81x_skb_hdr *hdr;
	const bool fw_reports_bcn_tx_status =
		mors->fw_flags & MM81X_FW_FLAGS_REPORTS_TX_BEACON_COMPLETION;

	if (!peek)
		return;

	/* Move sent packets to pending list waiting for feedback */
	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(skbq, pfirst, pnext) {
		__skb_unlink(pfirst, skbq);
		hdr = (struct mm81x_skb_hdr *)pfirst->data;
		/*
		 * If firmware doesn't give status on beacons just free
		 * them, otherwise queue and wait for response.
		 */
		switch (hdr->channel) {
		case MM81X_SKB_CHAN_BEACON:
			if (fw_reports_bcn_tx_status) {
				__mm81x_skbq_tx_move_to_pending(mq, pfirst);
				skb_awaits_tx_status = true;
				break;
			}
			/*
			 * If the FW doesn't give statuses on beacon's,
			 * then mark them as done.
			 */
			mm81x_skbq_pull_hdr_post_tx(pfirst);
			dev_kfree_skb_any(pfirst);
			break;
		default:
			if (le32_to_cpu(hdr->tx_info.flags) &
			    MM81X_TX_STATUS_FLAGS_NO_REPORT) {
				dev_kfree_skb_any(pfirst);
			} else {
				/*
				 * skb has been given to the chip. Store the
				 * time and queue the skb onto the pending
				 * queue while we wait for the tx_status.
				 */
				__mm81x_skbq_tx_move_to_pending(mq, pfirst);
				skb_awaits_tx_status = true;
			}
			break;
		}
	}
	spin_unlock_bh(&mq->lock);

	if (skb_awaits_tx_status) {
		spin_lock_bh(&mors->stale_status.lock);
		mod_timer(&mors->stale_status.timer,
			  jiffies + msecs_to_jiffies(
					    MM81X_SKBQ_TX_STATUS_LIFETIME_MS));
		spin_unlock_bh(&mors->stale_status.lock);
	}
}

/* Returns the first skb in the pending list. */
struct sk_buff *mm81x_skbq_tx_pending(struct mm81x_skbq *mq)
{
	struct sk_buff *pfirst;

	spin_lock_bh(&mq->lock);
	pfirst = skb_peek(&mq->pending);
	spin_unlock_bh(&mq->lock);
	return pfirst;
}

int mm81x_skbq_check_for_stale_tx(struct mm81x *mors, struct mm81x_skbq *mq)
{
	int flushed = 0;
	struct sk_buff *pfirst;
	struct sk_buff *pnext;

	if (!skb_queue_len(&mq->pending))
		return 0;

	/* Move sent packets to pending list waiting for feedback */
	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->pending, pfirst, pnext) {
		struct mm81x_skb_hdr *hdr =
			(struct mm81x_skb_hdr *)pfirst->data;

		if (__mm81x_skbq_has_pending_tx_skb_timed_out(pfirst)) {
			dev_dbg(mors->dev, "TX skb timed out [id:%d,chan:%d]",
				hdr->tx_info.pkt_id, hdr->channel);

			__mm81x_skbq_drop_pending_skb(mq, pfirst);
			flushed++;
		}
	}

	if (flushed)
		mm81x_skbq_check_tx_empty(mors, mq);

	spin_unlock_bh(&mq->lock);
	return flushed;
}

/* Remove commands from pending (or skbq if not sent) */
static void __skbq_cmd_finish(struct mm81x_skbq *mq, struct sk_buff *skb)
{
	struct mm81x *mors = mq->mors;

	if (skb_queue_len(&mq->pending)) {
		__mm81x_skbq_unlink(mq, &mq->pending, skb);
		dev_kfree_skb(skb);
	} else if (skb_queue_len(&mq->skbq)) {
		/* Command was probably timed out before being sent */
		dev_dbg(mors->dev,
			"Command pending queue empty. Removing from SKBQ.");
		__mm81x_skbq_unlink(mq, &mq->skbq, skb);
		dev_kfree_skb(skb);
	} else {
		dev_dbg(mors->dev, "Command Q not found");
	}
}

struct mm81x_update_sta_iter_data {
	struct mm81x *mors;
	struct sk_buff *skb;
	struct mm81x_skb_tx_status *tx_sts;
	int tx_attempts;
	bool updated;
};

static void mm81x_tx_h_update_sta_iter(void *data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct mm81x_update_sta_iter_data *iter = data;
	struct ieee80211_hdr *hdr;
	struct ieee80211_sta *sta;

	if (iter->updated || !iter->skb || !iter->skb->data)
		return;

	hdr = (struct ieee80211_hdr *)iter->skb->data;

	/*
	 * Note that each iteration via
	 * ieee80211_iterate_active_interfaces_atomic is under an RCU critical
	 * section so there is no need for a local critical section within here
	 * when looking up the station.
	 */
	sta = ieee80211_find_sta(vif, hdr->addr1);
	if (!sta)
		return;

	mm81x_rc_sta_feedback_rates(iter->mors, iter->skb, sta, iter->tx_sts,
				    iter->tx_attempts);
	mm81x_tx_h_check_aggr(sta, iter->skb);

	/*
	 * In situations with multiple virtual interfaces, finish iteration
	 * once we have found our STA to prevent further iteration.
	 */
	iter->updated = true;
}

/* TX status/Response received remove packet from pending TX finish */
static void __skbq_data_tx_finish(struct mm81x_skbq *mq, struct sk_buff *skb,
				  struct mm81x_skb_tx_status *tx_sts)
{
	struct mm81x *mors = mq->mors;
	struct mm81x_update_sta_iter_data iter = {};

	__mm81x_skbq_unlink(mq, &mq->pending, skb);
	iter.mors = mors;
	iter.skb = skb;
	iter.tx_sts = tx_sts;
	iter.tx_attempts = mm81x_tx_h_get_attempts(mors, tx_sts);

	ieee80211_iterate_active_interfaces_atomic(mors->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   mm81x_tx_h_update_sta_iter,
						   &iter);

	ieee80211_tx_status_skb(mors->hw, skb);
}

void mm81x_skbq_skb_finish(struct mm81x_skbq *mq, struct sk_buff *skb,
			   struct mm81x_skb_tx_status *tx_sts)
{
	if (mq->flags & MM81X_HIF_FLAGS_COMMAND)
		__skbq_cmd_finish(mq, skb);
	else
		__skbq_data_tx_finish(mq, skb, tx_sts);
}

void mm81x_skbq_tx_flush(struct mm81x_skbq *mq)
{
	struct sk_buff *pfirst, *pnext;

	spin_lock_bh(&mq->lock);
	skb_queue_walk_safe(&mq->pending, pfirst, pnext) {
		__mm81x_skbq_unlink(mq, &mq->pending, pfirst);
		ieee80211_free_txskb(mq->mors->hw, pfirst);
	}

	skb_queue_walk_safe(&mq->skbq, pfirst, pnext) {
		__mm81x_skbq_unlink(mq, &mq->skbq, pfirst);
		ieee80211_free_txskb(mq->mors->hw, pfirst);
	}
	spin_unlock_bh(&mq->lock);
}

void mm81x_skbq_init(struct mm81x *mors, struct mm81x_skbq *mq, u16 flags)
{
	spin_lock_init(&mq->lock);
	__skb_queue_head_init(&mq->skbq);
	__skb_queue_head_init(&mq->pending);
	mq->mors = mors;
	mq->skbq_size = 0;
	mq->flags = flags;
	mq->pkt_seq = 0;
	if (flags & MM81X_HIF_FLAGS_DIR_TO_HOST)
		INIT_WORK(&mq->dispatch_work, mm81x_skbq_dispatch_work);
}

void mm81x_skbq_finish(struct mm81x_skbq *mq)
{
	if (mq->skbq_size > 0)
		dev_dbg(mq->mors->dev,
			"Purging a non empty MorseQ. Dropping data!");

	/* Clean up link to hif */
	if (mq->flags & MM81X_HIF_FLAGS_DIR_TO_HOST)
		cancel_work_sync(&mq->dispatch_work);
	mm81x_skbq_purge(mq, &mq->skbq);
	mm81x_skbq_purge(mq, &mq->pending);
	mq->skbq_size = 0;
}

u32 mm81x_skbq_size(struct mm81x_skbq *mq)
{
	u32 count;

	spin_lock_bh(&mq->lock);
	count = __mm81x_skbq_size(mq);
	spin_unlock_bh(&mq->lock);
	return count;
}

u32 mm81x_skbq_count(struct mm81x_skbq *mq)
{
	u32 count = 0;

	spin_lock_bh(&mq->lock);
	count += skb_queue_len(&mq->skbq);
	spin_unlock_bh(&mq->lock);
	return count;
}

u32 mm81x_skbq_pending_count(struct mm81x_skbq *mq)
{
	u32 count;

	spin_lock_bh(&mq->lock);
	count = skb_queue_len(&mq->pending);
	spin_unlock_bh(&mq->lock);
	return count;
}

u32 mm81x_skbq_count_tx_ready(struct mm81x_skbq *mq)
{
	struct mm81x *mors = mq->mors;

	if (!mm81x_is_data_tx_allowed(mors))
		return 0;

	return mm81x_skbq_count(mq);
}

u32 mm81x_skbq_space(struct mm81x_skbq *mq)
{
	u32 space;

	spin_lock_bh(&mq->lock);
	space = __mm81x_skbq_space(mq);
	spin_unlock_bh(&mq->lock);

	return space;
}

struct sk_buff *mm81x_skbq_alloc_skb(struct mm81x_skbq *mq, unsigned int length)
{
	struct sk_buff *skb;
	int tx_headroom = sizeof(struct mm81x_skb_hdr) +
			  mm81x_bus_get_alignment(mq->mors);
	int skb_len = tx_headroom + length + MM81X_PAD4(length);

	skb = dev_alloc_skb(skb_len);
	if (!skb)
		return NULL;

	skb_reserve(skb, tx_headroom);
	skb_put(skb, length);
	return skb;
}

static int mm81x_skb_tx_h_validate_channel(const struct mm81x *mors, u8 channel)
{
	if (channel == MM81X_SKB_CHAN_COMMAND) {
		if (test_bit(MM81X_STATE_HOST_TO_CHIP_CMD_BLOCKED,
			     &mors->state_flags))
			return -EPERM;
	} else {
		if (test_bit(MM81X_STATE_HOST_TO_CHIP_TX_BLOCKED,
			     &mors->state_flags))
			return -EPERM;
	}

	return 0;
}

int mm81x_skbq_skb_tx(struct mm81x_skbq *mq, struct sk_buff **skb_orig,
		      struct mm81x_skb_tx_info *tx_info, u8 channel)
{
	int ret;
	struct mm81x_skb_hdr hdr;
	struct mm81x *mors = mq->mors;
	size_t end_of_skb_pad;
	struct sk_buff *skb = *skb_orig;
	u8 *aligned_head, *data;

	if (test_bit(MM81X_STATE_CHIP_UNRESPONSIVE, &mors->state_flags)) {
		dev_kfree_skb_any(skb);
		return -ENODEV;
	}

	ret = mm81x_skb_tx_h_validate_channel(mors, channel);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	mm81x_skbq_set_queued_tx_skb_expiry(skb);

	data = skb->data;
	aligned_head = PTR_ALIGN_DOWN((data - sizeof(hdr)),
				      mm81x_bus_get_alignment(mors));
	hdr.sync = MM81X_SKB_HEADER_SYNC;
	hdr.channel = channel;
	hdr.len = cpu_to_le16(skb->len);
	hdr.offset = data - (aligned_head + sizeof(hdr));
	hdr.checksum_upper = 0;
	hdr.checksum_lower = 0;
	if (tx_info)
		memcpy(&hdr.tx_info, tx_info, sizeof(*tx_info));
	else
		memset(&hdr.tx_info, 0, sizeof(hdr.tx_info));

	skb_push(skb, data - aligned_head);
	memcpy(skb->data, &hdr, sizeof(hdr));

	end_of_skb_pad = MM81X_PAD4(skb->len);
	if (end_of_skb_pad && skb_pad(skb, end_of_skb_pad))
		return -EINVAL;

	ret = mm81x_skbq_tx(mq, skb, channel);
	if (ret) {
		dev_err(mors->dev, "mm81x_skbq_tx fail: %d", ret);
		dev_kfree_skb_any(skb);
	}

	return ret;
}

void mm81x_skbq_data_traffic_pause(struct mm81x *mors)
{
	set_bit(MM81X_STATE_DATA_TX_STOPPED, &mors->state_flags);
	/* power-save requirements will be re-evaluated by the caller */
}

void mm81x_skbq_data_traffic_resume(struct mm81x *mors)
{
	clear_bit(MM81X_STATE_DATA_TX_STOPPED, &mors->state_flags);

	/* Set the TX_DATA_PEND bit. This will kick the transmission path to
	 * send any frames pending in the TX buffers, and wake the mac80211
	 * data Qs if they were previously stopped.
	 */
	set_bit(MM81X_HIF_EVT_TX_DATA_PEND, &mors->hif.event_flags);
}

bool mm81x_skbq_validate_checksum(u8 *data)
{
	int i;
	u32 xor = 0;
	struct mm81x_skb_hdr *skb_hdr = (struct mm81x_skb_hdr *)data;
	struct ieee80211_hdr *hdr =
		(struct ieee80211_hdr *)(data + sizeof(*skb_hdr));
	u16 len = le16_to_cpu(skb_hdr->len) + sizeof(*skb_hdr);
	u32 *data_to_xor = (u32 *)data;
	u32 header_xor = (le16_to_cpu(skb_hdr->checksum_upper) << 8) |
			 (skb_hdr->checksum_lower);

	/*
	 * For data frames the calculate the xor for skb header, mac header
	 * and ccmp header. For all other channel the xor is calculated for
	 * the full skb.
	 */
	if (skb_hdr->channel == MM81X_SKB_CHAN_DATA &&
	    (ieee80211_is_data(hdr->frame_control) ||
	     ieee80211_is_data_qos(hdr->frame_control))) {
		u16 data_len = sizeof(*skb_hdr) +
			       sizeof(struct ieee80211_qos_hdr) +
			       IEEE80211_CCMP_HDR_LEN;

		len = min(len, data_len);
		len = ROUND_DOWN_TO_WORD(len);
	}

	skb_hdr->checksum_upper = 0;
	skb_hdr->checksum_lower = 0;

	for (i = 0; i < len; i += 4) {
		xor ^= *data_to_xor;
		data_to_xor++;
	}

	xor &= 0x00FFFFFF;

	return xor == header_xor;
}
