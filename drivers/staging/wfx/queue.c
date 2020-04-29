// SPDX-License-Identifier: GPL-2.0-only
/*
 * O(1) TX queue with built-in allocator.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/sched.h>
#include <net/mac80211.h>

#include "queue.h"
#include "wfx.h"
#include "sta.h"
#include "data_tx.h"

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

	mutex_lock(&wdev->hif_cmd.lock);
	ret = wait_event_timeout(wdev->hif.tx_buffers_empty,
				 !wdev->hif.tx_buffers_used,
				 msecs_to_jiffies(3000));
	if (!ret) {
		dev_warn(wdev->dev, "cannot flush tx buffers (%d still busy)\n",
			 wdev->hif.tx_buffers_used);
		wfx_pending_dump_old_frames(wdev, 3000);
		// FIXME: drop pending frames here
		wdev->chip_frozen = 1;
	}
	mutex_unlock(&wdev->hif_cmd.lock);
}

void wfx_tx_lock_flush(struct wfx_dev *wdev)
{
	wfx_tx_lock(wdev);
	wfx_tx_flush(wdev);
}

void wfx_tx_queues_lock(struct wfx_dev *wdev)
{
	int i;
	struct wfx_queue *queue;

	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		queue = &wdev->tx_queue[i];
		spin_lock_bh(&queue->queue.lock);
		if (queue->tx_locked_cnt++ == 0)
			ieee80211_stop_queue(wdev->hw, queue->queue_id);
		spin_unlock_bh(&queue->queue.lock);
	}
}

void wfx_tx_queues_unlock(struct wfx_dev *wdev)
{
	int i;
	struct wfx_queue *queue;

	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		queue = &wdev->tx_queue[i];
		spin_lock_bh(&queue->queue.lock);
		WARN(!queue->tx_locked_cnt, "queue already unlocked");
		if (--queue->tx_locked_cnt == 0)
			ieee80211_wake_queue(wdev->hw, queue->queue_id);
		spin_unlock_bh(&queue->queue.lock);
	}
}

/* If successful, LOCKS the TX queue! */
void wfx_tx_queues_wait_empty_vif(struct wfx_vif *wvif)
{
	int i;
	bool done;
	struct wfx_queue *queue;
	struct sk_buff *item;
	struct wfx_dev *wdev = wvif->wdev;
	struct hif_msg *hif;

	if (wvif->wdev->chip_frozen) {
		wfx_tx_lock_flush(wdev);
		wfx_tx_queues_clear(wdev);
		return;
	}

	do {
		done = true;
		wfx_tx_lock_flush(wdev);
		for (i = 0; i < IEEE80211_NUM_ACS && done; ++i) {
			queue = &wdev->tx_queue[i];
			spin_lock_bh(&queue->queue.lock);
			skb_queue_walk(&queue->queue, item) {
				hif = (struct hif_msg *) item->data;
				if (hif->interface == wvif->id)
					done = false;
			}
			spin_unlock_bh(&queue->queue.lock);
		}
		if (!done) {
			wfx_tx_unlock(wdev);
			msleep(20);
		}
	} while (!done);
}

static void wfx_tx_queue_clear(struct wfx_dev *wdev, struct wfx_queue *queue,
			       struct sk_buff_head *gc_list)
{
	int i;
	struct sk_buff *item;
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;

	spin_lock_bh(&queue->queue.lock);
	while ((item = __skb_dequeue(&queue->queue)) != NULL)
		skb_queue_head(gc_list, item);
	spin_lock_nested(&stats->pending.lock, 1);
	for (i = 0; i < ARRAY_SIZE(stats->link_map_cache); ++i) {
		stats->link_map_cache[i] -= queue->link_map_cache[i];
		queue->link_map_cache[i] = 0;
	}
	spin_unlock(&stats->pending.lock);
	spin_unlock_bh(&queue->queue.lock);
}

void wfx_tx_queues_clear(struct wfx_dev *wdev)
{
	int i;
	struct sk_buff *item;
	struct sk_buff_head gc_list;
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;

	skb_queue_head_init(&gc_list);
	for (i = 0; i < IEEE80211_NUM_ACS; ++i)
		wfx_tx_queue_clear(wdev, &wdev->tx_queue[i], &gc_list);
	wake_up(&stats->wait_link_id_empty);
	while ((item = skb_dequeue(&gc_list)) != NULL)
		wfx_skb_dtor(wdev, item);
}

void wfx_tx_queues_init(struct wfx_dev *wdev)
{
	int i;

	memset(&wdev->tx_queue_stats, 0, sizeof(wdev->tx_queue_stats));
	memset(wdev->tx_queue, 0, sizeof(wdev->tx_queue));
	skb_queue_head_init(&wdev->tx_queue_stats.pending);
	init_waitqueue_head(&wdev->tx_queue_stats.wait_link_id_empty);

	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		wdev->tx_queue[i].queue_id = i;
		skb_queue_head_init(&wdev->tx_queue[i].queue);
	}
}

void wfx_tx_queues_deinit(struct wfx_dev *wdev)
{
	WARN_ON(!skb_queue_empty(&wdev->tx_queue_stats.pending));
	wfx_tx_queues_clear(wdev);
}

int wfx_tx_queue_get_num_queued(struct wfx_queue *queue, u32 link_id_map)
{
	int ret, i;

	if (!link_id_map)
		return 0;

	spin_lock_bh(&queue->queue.lock);
	if (link_id_map == (u32)-1) {
		ret = skb_queue_len(&queue->queue);
	} else {
		ret = 0;
		for (i = 0; i < ARRAY_SIZE(queue->link_map_cache); i++)
			if (link_id_map & BIT(i))
				ret += queue->link_map_cache[i];
	}
	spin_unlock_bh(&queue->queue.lock);
	return ret;
}

void wfx_tx_queue_put(struct wfx_dev *wdev, struct wfx_queue *queue,
		      struct sk_buff *skb)
{
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;
	struct wfx_tx_priv *tx_priv = wfx_skb_tx_priv(skb);

	WARN(tx_priv->link_id >= ARRAY_SIZE(stats->link_map_cache), "invalid link-id value");
	spin_lock_bh(&queue->queue.lock);
	__skb_queue_tail(&queue->queue, skb);

	++queue->link_map_cache[tx_priv->link_id];

	spin_lock_nested(&stats->pending.lock, 1);
	++stats->link_map_cache[tx_priv->link_id];
	spin_unlock(&stats->pending.lock);
	spin_unlock_bh(&queue->queue.lock);
}

static struct sk_buff *wfx_tx_queue_get(struct wfx_dev *wdev,
					struct wfx_queue *queue,
					u32 link_id_map)
{
	struct sk_buff *skb = NULL;
	struct sk_buff *item;
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;
	struct wfx_tx_priv *tx_priv;
	bool wakeup_stats = false;

	spin_lock_bh(&queue->queue.lock);
	skb_queue_walk(&queue->queue, item) {
		tx_priv = wfx_skb_tx_priv(item);
		if (link_id_map & BIT(tx_priv->link_id)) {
			skb = item;
			break;
		}
	}
	if (skb) {
		tx_priv = wfx_skb_tx_priv(skb);
		tx_priv->xmit_timestamp = ktime_get();
		__skb_unlink(skb, &queue->queue);
		--queue->link_map_cache[tx_priv->link_id];

		spin_lock_nested(&stats->pending.lock, 1);
		__skb_queue_tail(&stats->pending, skb);
		if (!--stats->link_map_cache[tx_priv->link_id])
			wakeup_stats = true;
		spin_unlock(&stats->pending.lock);
	}
	spin_unlock_bh(&queue->queue.lock);
	if (wakeup_stats)
		wake_up(&stats->wait_link_id_empty);
	return skb;
}

int wfx_pending_requeue(struct wfx_dev *wdev, struct sk_buff *skb)
{
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;
	struct wfx_tx_priv *tx_priv = wfx_skb_tx_priv(skb);
	struct wfx_queue *queue = &wdev->tx_queue[skb_get_queue_mapping(skb)];

	WARN_ON(skb_get_queue_mapping(skb) > 3);
	spin_lock_bh(&queue->queue.lock);
	++queue->link_map_cache[tx_priv->link_id];

	spin_lock_nested(&stats->pending.lock, 1);
	++stats->link_map_cache[tx_priv->link_id];
	__skb_unlink(skb, &stats->pending);
	spin_unlock(&stats->pending.lock);
	__skb_queue_tail(&queue->queue, skb);
	spin_unlock_bh(&queue->queue.lock);
	return 0;
}

int wfx_pending_remove(struct wfx_dev *wdev, struct sk_buff *skb)
{
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;

	spin_lock_bh(&stats->pending.lock);
	__skb_unlink(skb, &stats->pending);
	spin_unlock_bh(&stats->pending.lock);
	wfx_skb_dtor(wdev, skb);

	return 0;
}

struct sk_buff *wfx_pending_get(struct wfx_dev *wdev, u32 packet_id)
{
	struct sk_buff *skb;
	struct hif_req_tx *req;
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;

	spin_lock_bh(&stats->pending.lock);
	skb_queue_walk(&stats->pending, skb) {
		req = wfx_skb_txreq(skb);
		if (req->packet_id == packet_id) {
			spin_unlock_bh(&stats->pending.lock);
			return skb;
		}
	}
	spin_unlock_bh(&stats->pending.lock);
	WARN(1, "cannot find packet in pending queue");
	return NULL;
}

void wfx_pending_dump_old_frames(struct wfx_dev *wdev, unsigned int limit_ms)
{
	struct wfx_queue_stats *stats = &wdev->tx_queue_stats;
	ktime_t now = ktime_get();
	struct wfx_tx_priv *tx_priv;
	struct hif_req_tx *req;
	struct sk_buff *skb;
	bool first = true;

	spin_lock_bh(&stats->pending.lock);
	skb_queue_walk(&stats->pending, skb) {
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
	spin_unlock_bh(&stats->pending.lock);
}

unsigned int wfx_pending_get_pkt_us_delay(struct wfx_dev *wdev,
					  struct sk_buff *skb)
{
	ktime_t now = ktime_get();
	struct wfx_tx_priv *tx_priv = wfx_skb_tx_priv(skb);

	return ktime_us_delta(now, tx_priv->xmit_timestamp);
}

bool wfx_tx_queues_is_empty(struct wfx_dev *wdev)
{
	int i;
	struct sk_buff_head *queue;
	bool ret = true;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		queue = &wdev->tx_queue[i].queue;
		spin_lock_bh(&queue->lock);
		if (!skb_queue_empty(queue))
			ret = false;
		spin_unlock_bh(&queue->lock);
	}
	return ret;
}

static bool hif_handle_tx_data(struct wfx_vif *wvif, struct sk_buff *skb,
			       struct wfx_queue *queue)
{
	struct hif_req_tx *req = wfx_skb_txreq(skb);
	struct ieee80211_key_conf *hw_key = wfx_skb_tx_priv(skb)->hw_key;
	struct ieee80211_hdr *frame =
		(struct ieee80211_hdr *)(req->frame + req->data_flags.fc_offset);

	// FIXME: mac80211 is smart enough to handle BSS loss. Driver should not
	// try to do anything about that.
	if (ieee80211_is_nullfunc(frame->frame_control)) {
		mutex_lock(&wvif->bss_loss_lock);
		if (wvif->bss_loss_state) {
			wvif->bss_loss_confirm_id = req->packet_id;
			req->queue_id.queue_id = HIF_QUEUE_ID_VOICE;
		}
		mutex_unlock(&wvif->bss_loss_lock);
	}

	// FIXME: identify the exact scenario matched by this condition. Does it
	// happen yet?
	if (ieee80211_has_protected(frame->frame_control) &&
	    hw_key && hw_key->keyidx != wvif->wep_default_key_id &&
	    (hw_key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     hw_key->cipher == WLAN_CIPHER_SUITE_WEP104)) {
		wfx_tx_lock(wvif->wdev);
		WARN_ON(wvif->wep_pending_skb);
		wvif->wep_default_key_id = hw_key->keyidx;
		wvif->wep_pending_skb = skb;
		if (!schedule_work(&wvif->wep_key_work))
			wfx_tx_unlock(wvif->wdev);
		return true;
	} else {
		return false;
	}
}

static int wfx_get_prio_queue(struct wfx_vif *wvif,
				 u32 tx_allowed_mask, int *total)
{
	static const int urgent = BIT(WFX_LINK_ID_AFTER_DTIM) |
		BIT(WFX_LINK_ID_UAPSD);
	const struct ieee80211_tx_queue_params *edca;
	unsigned int score, best = -1;
	int winner = -1;
	int i;

	/* search for a winner using edca params */
	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		int queued;

		edca = &wvif->edca_params[i];
		queued = wfx_tx_queue_get_num_queued(&wvif->wdev->tx_queue[i],
				tx_allowed_mask);
		if (!queued)
			continue;
		*total += queued;
		score = ((edca->aifs + edca->cw_min) << 16) +
			((edca->cw_max - edca->cw_min) *
			 (get_random_int() & 0xFFFF));
		if (score < best && (winner < 0 || i != 3)) {
			best = score;
			winner = i;
		}
	}

	/* override winner if bursting */
	if (winner >= 0 && wvif->wdev->tx_burst_idx >= 0 &&
	    winner != wvif->wdev->tx_burst_idx &&
	    !wfx_tx_queue_get_num_queued(&wvif->wdev->tx_queue[winner],
					 tx_allowed_mask & urgent) &&
	    wfx_tx_queue_get_num_queued(&wvif->wdev->tx_queue[wvif->wdev->tx_burst_idx], tx_allowed_mask))
		winner = wvif->wdev->tx_burst_idx;

	return winner;
}

static int wfx_tx_queue_mask_get(struct wfx_vif *wvif,
				     struct wfx_queue **queue_p,
				     u32 *tx_allowed_mask_p)
{
	int idx;
	u32 tx_allowed_mask;
	int total = 0;

	/* Search for unicast traffic */
	tx_allowed_mask = ~wvif->sta_asleep_mask;
	tx_allowed_mask |= BIT(WFX_LINK_ID_UAPSD);
	if (wvif->sta_asleep_mask)
		tx_allowed_mask &= ~BIT(WFX_LINK_ID_AFTER_DTIM);
	else
		tx_allowed_mask |= BIT(WFX_LINK_ID_AFTER_DTIM);
	idx = wfx_get_prio_queue(wvif, tx_allowed_mask, &total);
	if (idx < 0)
		return -ENOENT;

	*queue_p = &wvif->wdev->tx_queue[idx];
	*tx_allowed_mask_p = tx_allowed_mask;
	return 0;
}

struct hif_msg *wfx_tx_queues_get_after_dtim(struct wfx_vif *wvif)
{
	struct wfx_dev *wdev = wvif->wdev;
	struct ieee80211_tx_info *tx_info;
	struct hif_msg *hif;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		skb_queue_walk(&wdev->tx_queue[i].queue, skb) {
			tx_info = IEEE80211_SKB_CB(skb);
			hif = (struct hif_msg *)skb->data;
			if ((tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) &&
			    (hif->interface == wvif->id))
				return (struct hif_msg *)skb->data;
		}
	}
	return NULL;
}

struct hif_msg *wfx_tx_queues_get(struct wfx_dev *wdev)
{
	struct sk_buff *skb;
	struct hif_msg *hif = NULL;
	struct wfx_queue *queue = NULL;
	struct wfx_queue *vif_queue = NULL;
	u32 tx_allowed_mask = 0;
	u32 vif_tx_allowed_mask = 0;
	struct wfx_vif *wvif;
	int not_found;
	int burst;
	int i;

	if (atomic_read(&wdev->tx_lock))
		return NULL;

	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		if (wvif->after_dtim_tx_allowed) {
			for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
				skb = wfx_tx_queue_get(wvif->wdev,
						       &wdev->tx_queue[i],
						       BIT(WFX_LINK_ID_AFTER_DTIM));
				if (skb) {
					hif = (struct hif_msg *)skb->data;
					// Cannot happen since only one vif can
					// be AP at time
					WARN_ON(wvif->id != hif->interface);
					return hif;
				}
			}
			// No more multicast to sent
			wvif->after_dtim_tx_allowed = false;
			schedule_work(&wvif->update_tim_work);
		}
	}

	for (;;) {
		int ret = -ENOENT;
		int queue_num;

		wvif = NULL;
		while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
			spin_lock_bh(&wvif->ps_state_lock);

			not_found = wfx_tx_queue_mask_get(wvif, &vif_queue,
							  &vif_tx_allowed_mask);

			spin_unlock_bh(&wvif->ps_state_lock);

			if (!not_found) {
				if (queue && queue != vif_queue)
					dev_info(wdev->dev, "vifs disagree about queue priority\n");
				tx_allowed_mask |= vif_tx_allowed_mask;
				queue = vif_queue;
				ret = 0;
			}
		}

		if (ret)
			return NULL;

		queue_num = queue - wdev->tx_queue;

		skb = wfx_tx_queue_get(wdev, queue, tx_allowed_mask);
		if (!skb)
			continue;
		hif = (struct hif_msg *)skb->data;
		wvif = wdev_to_wvif(wdev, hif->interface);
		WARN_ON(!wvif);

		if (hif_handle_tx_data(wvif, skb, queue))
			continue;  /* Handled by WSM */

		/* allow bursting if txop is set */
		if (wvif->edca_params[queue_num].txop)
			burst = wfx_tx_queue_get_num_queued(queue, tx_allowed_mask) + 1;
		else
			burst = 1;

		/* store index of bursting queue */
		if (burst > 1)
			wdev->tx_burst_idx = queue_num;
		else
			wdev->tx_burst_idx = -1;

		return hif;
	}
}
