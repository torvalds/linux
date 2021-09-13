// SPDX-License-Identifier: GPL-2.0-only
/*
 * O(1) TX queue with built-in allocator.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/sched.h>
#include <net/mac80211.h>

#include "queue.h"
#include "wfx.h"
#include "sta.h"
#include "data_tx.h"
#include "traces.h"

void wfx_tx_lock(struct wfx_dev *wdev)
{
	atomic_inc(&wdev->tx_lock);
}

void wfx_tx_unlock(struct wfx_dev *wdev)
{
	int tx_lock = atomic_dec_return(&wdev->tx_lock);

	WARN(tx_lock < 0, "inconsistent tx_lock value");
	if (!tx_lock)
		wfx_bh_request_tx(wdev);
}

void wfx_tx_flush(struct wfx_dev *wdev)
{
	int ret;

	// Do not wait for any reply if chip is frozen
	if (wdev->chip_frozen)
		return;

	wfx_tx_lock(wdev);
	mutex_lock(&wdev->hif_cmd.lock);
	ret = wait_event_timeout(wdev->hif.tx_buffers_empty,
				 !wdev->hif.tx_buffers_used,
				 msecs_to_jiffies(3000));
	if (!ret) {
		dev_warn(wdev->dev, "cannot flush tx buffers (%d still busy)\n",
			 wdev->hif.tx_buffers_used);
		wfx_pending_dump_old_frames(wdev, 3000);
		// FIXME: drop pending frames here
		wdev->chip_frozen = true;
	}
	mutex_unlock(&wdev->hif_cmd.lock);
	wfx_tx_unlock(wdev);
}

void wfx_tx_lock_flush(struct wfx_dev *wdev)
{
	wfx_tx_lock(wdev);
	wfx_tx_flush(wdev);
}

void wfx_tx_queues_init(struct wfx_vif *wvif)
{
	// The device is in charge to respect the details of the QoS parameters.
	// The driver just ensure that it roughtly respect the priorities to
	// avoid any shortage.
	const int priorities[IEEE80211_NUM_ACS] = { 1, 2, 64, 128 };
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		skb_queue_head_init(&wvif->tx_queue[i].normal);
		skb_queue_head_init(&wvif->tx_queue[i].cab);
		wvif->tx_queue[i].priority = priorities[i];
	}
}

void wfx_tx_queues_check_empty(struct wfx_vif *wvif)
{
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		WARN_ON(atomic_read(&wvif->tx_queue[i].pending_frames));
		WARN_ON(!skb_queue_empty_lockless(&wvif->tx_queue[i].normal));
		WARN_ON(!skb_queue_empty_lockless(&wvif->tx_queue[i].cab));
	}
}

bool wfx_tx_queue_empty(struct wfx_vif *wvif, struct wfx_queue *queue)
{
	return skb_queue_empty_lockless(&queue->normal) &&
	       skb_queue_empty_lockless(&queue->cab);
}

static void __wfx_tx_queue_drop(struct wfx_vif *wvif,
				struct sk_buff_head *skb_queue,
				struct sk_buff_head *dropped)
{
	struct sk_buff *skb, *tmp;

	spin_lock_bh(&skb_queue->lock);
	skb_queue_walk_safe(skb_queue, skb, tmp) {
		__skb_unlink(skb, skb_queue);
		skb_queue_head(dropped, skb);
	}
	spin_unlock_bh(&skb_queue->lock);
}

void wfx_tx_queue_drop(struct wfx_vif *wvif, struct wfx_queue *queue,
		       struct sk_buff_head *dropped)
{
	__wfx_tx_queue_drop(wvif, &queue->cab, dropped);
	__wfx_tx_queue_drop(wvif, &queue->normal, dropped);
	wake_up(&wvif->wdev->tx_dequeue);
}

void wfx_tx_queues_put(struct wfx_vif *wvif, struct sk_buff *skb)
{
	struct wfx_queue *queue = &wvif->tx_queue[skb_get_queue_mapping(skb)];
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		skb_queue_tail(&queue->cab, skb);
	else
		skb_queue_tail(&queue->normal, skb);
}

void wfx_pending_drop(struct wfx_dev *wdev, struct sk_buff_head *dropped)
{
	struct wfx_queue *queue;
	struct wfx_vif *wvif;
	struct hif_msg *hif;
	struct sk_buff *skb;

	WARN(!wdev->chip_frozen, "%s should only be used to recover a frozen device",
	     __func__);
	while ((skb = skb_dequeue(&wdev->tx_pending)) != NULL) {
		hif = (struct hif_msg *)skb->data;
		wvif = wdev_to_wvif(wdev, hif->interface);
		if (wvif) {
			queue = &wvif->tx_queue[skb_get_queue_mapping(skb)];
			WARN_ON(skb_get_queue_mapping(skb) > 3);
			WARN_ON(!atomic_read(&queue->pending_frames));
			atomic_dec(&queue->pending_frames);
		}
		skb_queue_head(dropped, skb);
	}
}

struct sk_buff *wfx_pending_get(struct wfx_dev *wdev, u32 packet_id)
{
	struct wfx_queue *queue;
	struct hif_req_tx *req;
	struct wfx_vif *wvif;
	struct hif_msg *hif;
	struct sk_buff *skb;

	spin_lock_bh(&wdev->tx_pending.lock);
	skb_queue_walk(&wdev->tx_pending, skb) {
		hif = (struct hif_msg *)skb->data;
		req = (struct hif_req_tx *)hif->body;
		if (req->packet_id != packet_id)
			continue;
		spin_unlock_bh(&wdev->tx_pending.lock);
		wvif = wdev_to_wvif(wdev, hif->interface);
		if (wvif) {
			queue = &wvif->tx_queue[skb_get_queue_mapping(skb)];
			WARN_ON(skb_get_queue_mapping(skb) > 3);
			WARN_ON(!atomic_read(&queue->pending_frames));
			atomic_dec(&queue->pending_frames);
		}
		skb_unlink(skb, &wdev->tx_pending);
		return skb;
	}
	spin_unlock_bh(&wdev->tx_pending.lock);
	WARN(1, "cannot find packet in pending queue");
	return NULL;
}

void wfx_pending_dump_old_frames(struct wfx_dev *wdev, unsigned int limit_ms)
{
	ktime_t now = ktime_get();
	struct wfx_tx_priv *tx_priv;
	struct hif_req_tx *req;
	struct sk_buff *skb;
	bool first = true;

	spin_lock_bh(&wdev->tx_pending.lock);
	skb_queue_walk(&wdev->tx_pending, skb) {
		tx_priv = wfx_skb_tx_priv(skb);
		req = wfx_skb_txreq(skb);
		if (ktime_after(now, ktime_add_ms(tx_priv->xmit_timestamp,
						  limit_ms))) {
			if (first) {
				dev_info(wdev->dev, "frames stuck in firmware since %dms or more:\n",
					 limit_ms);
				first = false;
			}
			dev_info(wdev->dev, "   id %08x sent %lldms ago\n",
				 req->packet_id,
				 ktime_ms_delta(now, tx_priv->xmit_timestamp));
		}
	}
	spin_unlock_bh(&wdev->tx_pending.lock);
}

unsigned int wfx_pending_get_pkt_us_delay(struct wfx_dev *wdev,
					  struct sk_buff *skb)
{
	ktime_t now = ktime_get();
	struct wfx_tx_priv *tx_priv = wfx_skb_tx_priv(skb);

	return ktime_us_delta(now, tx_priv->xmit_timestamp);
}

bool wfx_tx_queues_has_cab(struct wfx_vif *wvif)
{
	int i;

	if (wvif->vif->type != NL80211_IFTYPE_AP)
		return false;
	for (i = 0; i < IEEE80211_NUM_ACS; ++i)
		// Note: since only AP can have mcast frames in queue and only
		// one vif can be AP, all queued frames has same interface id
		if (!skb_queue_empty_lockless(&wvif->tx_queue[i].cab))
			return true;
	return false;
}

static int wfx_tx_queue_get_weight(struct wfx_queue *queue)
{
	return atomic_read(&queue->pending_frames) * queue->priority;
}

static struct sk_buff *wfx_tx_queues_get_skb(struct wfx_dev *wdev)
{
	struct wfx_queue *queues[IEEE80211_NUM_ACS * ARRAY_SIZE(wdev->vif)];
	int i, j, num_queues = 0;
	struct wfx_vif *wvif;
	struct hif_msg *hif;
	struct sk_buff *skb;

	// sort the queues
	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			WARN_ON(num_queues >= ARRAY_SIZE(queues));
			queues[num_queues] = &wvif->tx_queue[i];
			for (j = num_queues; j > 0; j--)
				if (wfx_tx_queue_get_weight(queues[j]) <
				    wfx_tx_queue_get_weight(queues[j - 1]))
					swap(queues[j - 1], queues[j]);
			num_queues++;
		}
	}

	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		if (!wvif->after_dtim_tx_allowed)
			continue;
		for (i = 0; i < num_queues; i++) {
			skb = skb_dequeue(&queues[i]->cab);
			if (!skb)
				continue;
			// Note: since only AP can have mcast frames in queue
			// and only one vif can be AP, all queued frames has
			// same interface id
			hif = (struct hif_msg *)skb->data;
			WARN_ON(hif->interface != wvif->id);
			WARN_ON(queues[i] !=
				&wvif->tx_queue[skb_get_queue_mapping(skb)]);
			atomic_inc(&queues[i]->pending_frames);
			trace_queues_stats(wdev, queues[i]);
			return skb;
		}
		// No more multicast to sent
		wvif->after_dtim_tx_allowed = false;
		schedule_work(&wvif->update_tim_work);
	}

	for (i = 0; i < num_queues; i++) {
		skb = skb_dequeue(&queues[i]->normal);
		if (skb) {
			atomic_inc(&queues[i]->pending_frames);
			trace_queues_stats(wdev, queues[i]);
			return skb;
		}
	}
	return NULL;
}

struct hif_msg *wfx_tx_queues_get(struct wfx_dev *wdev)
{
	struct wfx_tx_priv *tx_priv;
	struct sk_buff *skb;

	if (atomic_read(&wdev->tx_lock))
		return NULL;
	skb = wfx_tx_queues_get_skb(wdev);
	if (!skb)
		return NULL;
	skb_queue_tail(&wdev->tx_pending, skb);
	wake_up(&wdev->tx_dequeue);
	tx_priv = wfx_skb_tx_priv(skb);
	tx_priv->xmit_timestamp = ktime_get();
	return (struct hif_msg *)skb->data;
}
