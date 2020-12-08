// SPDX-License-Identifier: GPL-2.0-only
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <net/mac80211.h>
#include <linux/etherdevice.h>

#include "data_tx.h"
#include "wfx.h"
#include "bh.h"
#include "sta.h"
#include "queue.h"
#include "debug.h"
#include "traces.h"
#include "hif_tx_mib.h"

static int wfx_get_hw_rate(struct wfx_dev *wdev,
			   const struct ieee80211_tx_rate *rate)
{
	struct ieee80211_supported_band *band;

	if (rate->idx < 0)
		return -1;
	if (rate->flags & IEEE80211_TX_RC_MCS) {
		if (rate->idx > 7) {
			WARN(1, "wrong rate->idx value: %d", rate->idx);
			return -1;
		}
		return rate->idx + 14;
	}
	// WFx only support 2GHz, else band information should be retrieved
	// from ieee80211_tx_info
	band = wdev->hw->wiphy->bands[NL80211_BAND_2GHZ];
	if (rate->idx >= band->n_bitrates) {
		WARN(1, "wrong rate->idx value: %d", rate->idx);
		return -1;
	}
	return band->bitrates[rate->idx].hw_value;
}

/* TX policy cache implementation */

static void wfx_tx_policy_build(struct wfx_vif *wvif, struct tx_policy *policy,
				struct ieee80211_tx_rate *rates)
{
	struct wfx_dev *wdev = wvif->wdev;
	int i, rateid;
	u8 count;

	WARN(rates[0].idx < 0, "invalid rate policy");
	memset(policy, 0, sizeof(*policy));
	for (i = 0; i < IEEE80211_TX_MAX_RATES; ++i) {
		if (rates[i].idx < 0)
			break;
		WARN_ON(rates[i].count > 15);
		rateid = wfx_get_hw_rate(wdev, &rates[i]);
		// Pack two values in each byte of policy->rates
		count = rates[i].count;
		if (rateid % 2)
			count <<= 4;
		policy->rates[rateid / 2] |= count;
	}
}

static bool tx_policy_is_equal(const struct tx_policy *a,
			       const struct tx_policy *b)
{
	return !memcmp(a->rates, b->rates, sizeof(a->rates));
}

static int wfx_tx_policy_find(struct tx_policy_cache *cache,
			      struct tx_policy *wanted)
{
	struct tx_policy *it;

	list_for_each_entry(it, &cache->used, link)
		if (tx_policy_is_equal(wanted, it))
			return it - cache->cache;
	list_for_each_entry(it, &cache->free, link)
		if (tx_policy_is_equal(wanted, it))
			return it - cache->cache;
	return -1;
}

static void wfx_tx_policy_use(struct tx_policy_cache *cache,
			      struct tx_policy *entry)
{
	++entry->usage_count;
	list_move(&entry->link, &cache->used);
}

static int wfx_tx_policy_release(struct tx_policy_cache *cache,
				 struct tx_policy *entry)
{
	int ret = --entry->usage_count;

	if (!ret)
		list_move(&entry->link, &cache->free);
	return ret;
}

static int wfx_tx_policy_get(struct wfx_vif *wvif,
			     struct ieee80211_tx_rate *rates, bool *renew)
{
	int idx;
	struct tx_policy_cache *cache = &wvif->tx_policy_cache;
	struct tx_policy wanted;

	wfx_tx_policy_build(wvif, &wanted, rates);

	spin_lock_bh(&cache->lock);
	if (list_empty(&cache->free)) {
		WARN(1, "unable to get a valid Tx policy");
		spin_unlock_bh(&cache->lock);
		return HIF_TX_RETRY_POLICY_INVALID;
	}
	idx = wfx_tx_policy_find(cache, &wanted);
	if (idx >= 0) {
		*renew = false;
	} else {
		struct tx_policy *entry;
		*renew = true;
		/* If policy is not found create a new one
		 * using the oldest entry in "free" list
		 */
		entry = list_entry(cache->free.prev, struct tx_policy, link);
		memcpy(entry->rates, wanted.rates, sizeof(entry->rates));
		entry->uploaded = false;
		entry->usage_count = 0;
		idx = entry - cache->cache;
	}
	wfx_tx_policy_use(cache, &cache->cache[idx]);
	if (list_empty(&cache->free))
		ieee80211_stop_queues(wvif->wdev->hw);
	spin_unlock_bh(&cache->lock);
	return idx;
}

static void wfx_tx_policy_put(struct wfx_vif *wvif, int idx)
{
	int usage, locked;
	struct tx_policy_cache *cache = &wvif->tx_policy_cache;

	if (idx == HIF_TX_RETRY_POLICY_INVALID)
		return;
	spin_lock_bh(&cache->lock);
	locked = list_empty(&cache->free);
	usage = wfx_tx_policy_release(cache, &cache->cache[idx]);
	if (locked && !usage)
		ieee80211_wake_queues(wvif->wdev->hw);
	spin_unlock_bh(&cache->lock);
}

static int wfx_tx_policy_upload(struct wfx_vif *wvif)
{
	struct tx_policy *policies = wvif->tx_policy_cache.cache;
	u8 tmp_rates[12];
	int i, is_used;

	do {
		spin_lock_bh(&wvif->tx_policy_cache.lock);
		for (i = 0; i < ARRAY_SIZE(wvif->tx_policy_cache.cache); ++i) {
			is_used = memzcmp(policies[i].rates,
					  sizeof(policies[i].rates));
			if (!policies[i].uploaded && is_used)
				break;
		}
		if (i < ARRAY_SIZE(wvif->tx_policy_cache.cache)) {
			policies[i].uploaded = true;
			memcpy(tmp_rates, policies[i].rates, sizeof(tmp_rates));
			spin_unlock_bh(&wvif->tx_policy_cache.lock);
			hif_set_tx_rate_retry_policy(wvif, i, tmp_rates);
		} else {
			spin_unlock_bh(&wvif->tx_policy_cache.lock);
		}
	} while (i < ARRAY_SIZE(wvif->tx_policy_cache.cache));
	return 0;
}

void wfx_tx_policy_upload_work(struct work_struct *work)
{
	struct wfx_vif *wvif =
		container_of(work, struct wfx_vif, tx_policy_upload_work);

	wfx_tx_policy_upload(wvif);
	wfx_tx_unlock(wvif->wdev);
}

void wfx_tx_policy_init(struct wfx_vif *wvif)
{
	struct tx_policy_cache *cache = &wvif->tx_policy_cache;
	int i;

	memset(cache, 0, sizeof(*cache));

	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->used);
	INIT_LIST_HEAD(&cache->free);

	for (i = 0; i < ARRAY_SIZE(cache->cache); ++i)
		list_add(&cache->cache[i].link, &cache->free);
}

/* Tx implementation */

static bool ieee80211_is_action_back(struct ieee80211_hdr *hdr)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)hdr;

	if (!ieee80211_is_action(mgmt->frame_control))
		return false;
	if (mgmt->u.action.category != WLAN_CATEGORY_BACK)
		return false;
	return true;
}

static u8 wfx_tx_get_link_id(struct wfx_vif *wvif, struct ieee80211_sta *sta,
			     struct ieee80211_hdr *hdr)
{
	struct wfx_sta_priv *sta_priv =
		sta ? (struct wfx_sta_priv *)&sta->drv_priv : NULL;
	const u8 *da = ieee80211_get_DA(hdr);

	if (sta_priv && sta_priv->link_id)
		return sta_priv->link_id;
	if (wvif->vif->type != NL80211_IFTYPE_AP)
		return 0;
	if (is_multicast_ether_addr(da))
		return 0;
	return HIF_LINK_ID_NOT_ASSOCIATED;
}

static void wfx_tx_fixup_rates(struct ieee80211_tx_rate *rates)
{
	int i;
	bool finished;

	// Firmware is not able to mix rates with different flags
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
			rates[i].flags |= IEEE80211_TX_RC_SHORT_GI;
		if (!(rates[0].flags & IEEE80211_TX_RC_SHORT_GI))
			rates[i].flags &= ~IEEE80211_TX_RC_SHORT_GI;
		if (!(rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS))
			rates[i].flags &= ~IEEE80211_TX_RC_USE_RTS_CTS;
	}

	// Sort rates and remove duplicates
	do {
		finished = true;
		for (i = 0; i < IEEE80211_TX_MAX_RATES - 1; i++) {
			if (rates[i + 1].idx == rates[i].idx &&
			    rates[i].idx != -1) {
				rates[i].count += rates[i + 1].count;
				if (rates[i].count > 15)
					rates[i].count = 15;
				rates[i + 1].idx = -1;
				rates[i + 1].count = 0;

				finished = false;
			}
			if (rates[i + 1].idx > rates[i].idx) {
				swap(rates[i + 1], rates[i]);
				finished = false;
			}
		}
	} while (!finished);
	// Ensure that MCS0 or 1Mbps is present at the end of the retry list
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (rates[i].idx == 0)
			break;
		if (rates[i].idx == -1) {
			rates[i].idx = 0;
			rates[i].count = 8; // == hw->max_rate_tries
			rates[i].flags = rates[i - 1].flags &
					 IEEE80211_TX_RC_MCS;
			break;
		}
	}
	// All retries use long GI
	for (i = 1; i < IEEE80211_TX_MAX_RATES; i++)
		rates[i].flags &= ~IEEE80211_TX_RC_SHORT_GI;
}

static u8 wfx_tx_get_rate_id(struct wfx_vif *wvif,
			     struct ieee80211_tx_info *tx_info)
{
	bool tx_policy_renew = false;
	u8 rate_id;

	rate_id = wfx_tx_policy_get(wvif,
				    tx_info->driver_rates, &tx_policy_renew);
	if (rate_id == HIF_TX_RETRY_POLICY_INVALID)
		dev_warn(wvif->wdev->dev, "unable to get a valid Tx policy");

	if (tx_policy_renew) {
		wfx_tx_lock(wvif->wdev);
		if (!schedule_work(&wvif->tx_policy_upload_work))
			wfx_tx_unlock(wvif->wdev);
	}
	return rate_id;
}

static int wfx_tx_get_frame_format(struct ieee80211_tx_info *tx_info)
{
	if (!(tx_info->driver_rates[0].flags & IEEE80211_TX_RC_MCS))
		return HIF_FRAME_FORMAT_NON_HT;
	else if (!(tx_info->driver_rates[0].flags & IEEE80211_TX_RC_GREEN_FIELD))
		return HIF_FRAME_FORMAT_MIXED_FORMAT_HT;
	else
		return HIF_FRAME_FORMAT_GF_HT_11N;
}

static int wfx_tx_get_icv_len(struct ieee80211_key_conf *hw_key)
{
	int mic_space;

	if (!hw_key)
		return 0;
	if (hw_key->cipher == WLAN_CIPHER_SUITE_AES_CMAC)
		return 0;
	mic_space = (hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) ? 8 : 0;
	return hw_key->icv_len + mic_space;
}

static int wfx_tx_inner(struct wfx_vif *wvif, struct ieee80211_sta *sta,
			struct sk_buff *skb)
{
	struct hif_msg *hif_msg;
	struct hif_req_tx *req;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *hw_key = tx_info->control.hw_key;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int queue_id = skb_get_queue_mapping(skb);
	size_t offset = (size_t)skb->data & 3;
	int wmsg_len = sizeof(struct hif_msg) +
			sizeof(struct hif_req_tx) + offset;

	WARN(queue_id >= IEEE80211_NUM_ACS, "unsupported queue_id");
	wfx_tx_fixup_rates(tx_info->driver_rates);

	// From now tx_info->control is unusable
	memset(tx_info->rate_driver_data, 0, sizeof(struct wfx_tx_priv));

	// Fill hif_msg
	WARN(skb_headroom(skb) < wmsg_len, "not enough space in skb");
	WARN(offset & 1, "attempt to transmit an unaligned frame");
	skb_put(skb, wfx_tx_get_icv_len(hw_key));
	skb_push(skb, wmsg_len);
	memset(skb->data, 0, wmsg_len);
	hif_msg = (struct hif_msg *)skb->data;
	hif_msg->len = cpu_to_le16(skb->len);
	hif_msg->id = HIF_REQ_ID_TX;
	hif_msg->interface = wvif->id;
	if (skb->len > wvif->wdev->hw_caps.size_inp_ch_buf) {
		dev_warn(wvif->wdev->dev,
			 "requested frame size (%d) is larger than maximum supported (%d)\n",
			 skb->len, wvif->wdev->hw_caps.size_inp_ch_buf);
		skb_pull(skb, wmsg_len);
		return -EIO;
	}

	// Fill tx request
	req = (struct hif_req_tx *)hif_msg->body;
	// packet_id just need to be unique on device. 32bits are more than
	// necessary for that task, so we tae advantage of it to add some extra
	// data for debug.
	req->packet_id = atomic_add_return(1, &wvif->wdev->packet_id) & 0xFFFF;
	req->packet_id |= IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl)) << 16;
	req->packet_id |= queue_id << 28;

	req->fc_offset = offset;
	if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		req->after_dtim = 1;
	req->peer_sta_id = wfx_tx_get_link_id(wvif, sta, hdr);
	// Queue index are inverted between firmware and Linux
	req->queue_id = 3 - queue_id;
	req->retry_policy_index = wfx_tx_get_rate_id(wvif, tx_info);
	req->frame_format = wfx_tx_get_frame_format(tx_info);
	if (tx_info->driver_rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
		req->short_gi = 1;

	// Auxiliary operations
	wfx_tx_queues_put(wvif, skb);
	if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		schedule_work(&wvif->update_tim_work);
	wfx_bh_request_tx(wvif->wdev);
	return 0;
}

void wfx_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	    struct sk_buff *skb)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif;
	struct ieee80211_sta *sta = control ? control->sta : NULL;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	size_t driver_data_room = sizeof_field(struct ieee80211_tx_info,
					       rate_driver_data);

	compiletime_assert(sizeof(struct wfx_tx_priv) <= driver_data_room,
			   "struct tx_priv is too large");
	WARN(skb->next || skb->prev, "skb is already member of a list");
	// control.vif can be NULL for injected frames
	if (tx_info->control.vif)
		wvif = (struct wfx_vif *)tx_info->control.vif->drv_priv;
	else
		wvif = wvif_iterate(wdev, NULL);
	if (WARN_ON(!wvif))
		goto drop;
	// Because of TX_AMPDU_SETUP_IN_HW, mac80211 does not try to send any
	// BlockAck session management frame. The check below exist just in case.
	if (ieee80211_is_action_back(hdr)) {
		dev_info(wdev->dev, "drop BA action\n");
		goto drop;
	}
	if (wfx_tx_inner(wvif, sta, skb))
		goto drop;

	return;

drop:
	ieee80211_tx_status_irqsafe(wdev->hw, skb);
}

static void wfx_skb_dtor(struct wfx_vif *wvif, struct sk_buff *skb)
{
	struct hif_msg *hif = (struct hif_msg *)skb->data;
	struct hif_req_tx *req = (struct hif_req_tx *)hif->body;
	unsigned int offset = sizeof(struct hif_msg) +
			      sizeof(struct hif_req_tx) +
			      req->fc_offset;

	if (!wvif) {
		pr_warn("%s: vif associated with the skb does not exist anymore\n", __func__);
		return;
	}
	wfx_tx_policy_put(wvif, req->retry_policy_index);
	skb_pull(skb, offset);
	ieee80211_tx_status_irqsafe(wvif->wdev->hw, skb);
}

static void wfx_tx_fill_rates(struct wfx_dev *wdev,
			      struct ieee80211_tx_info *tx_info,
			      const struct hif_cnf_tx *arg)
{
	struct ieee80211_tx_rate *rate;
	int tx_count;
	int i;

	tx_count = arg->ack_failures;
	if (!arg->status || arg->ack_failures)
		tx_count += 1; // Also report success
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		rate = &tx_info->status.rates[i];
		if (rate->idx < 0)
			break;
		if (tx_count < rate->count &&
		    arg->status == HIF_STATUS_TX_FAIL_RETRIES &&
		    arg->ack_failures)
			dev_dbg(wdev->dev, "all retries were not consumed: %d != %d\n",
				rate->count, tx_count);
		if (tx_count <= rate->count && tx_count &&
		    arg->txed_rate != wfx_get_hw_rate(wdev, rate))
			dev_dbg(wdev->dev, "inconsistent tx_info rates: %d != %d\n",
				arg->txed_rate, wfx_get_hw_rate(wdev, rate));
		if (tx_count > rate->count) {
			tx_count -= rate->count;
		} else if (!tx_count) {
			rate->count = 0;
			rate->idx = -1;
		} else {
			rate->count = tx_count;
			tx_count = 0;
		}
	}
	if (tx_count)
		dev_dbg(wdev->dev, "%d more retries than expected\n", tx_count);
}

void wfx_tx_confirm_cb(struct wfx_dev *wdev, const struct hif_cnf_tx *arg)
{
	struct ieee80211_tx_info *tx_info;
	struct wfx_vif *wvif;
	struct sk_buff *skb;

	skb = wfx_pending_get(wdev, arg->packet_id);
	if (!skb) {
		dev_warn(wdev->dev, "received unknown packet_id (%#.8x) from chip\n",
			 arg->packet_id);
		return;
	}
	tx_info = IEEE80211_SKB_CB(skb);
	wvif = wdev_to_wvif(wdev, ((struct hif_msg *)skb->data)->interface);
	WARN_ON(!wvif);
	if (!wvif)
		return;

	// Note that wfx_pending_get_pkt_us_delay() get data from tx_info
	_trace_tx_stats(arg, skb, wfx_pending_get_pkt_us_delay(wdev, skb));
	wfx_tx_fill_rates(wdev, tx_info, arg);
	// From now, you can touch to tx_info->status, but do not touch to
	// tx_priv anymore
	// FIXME: use ieee80211_tx_info_clear_status()
	memset(tx_info->rate_driver_data, 0, sizeof(tx_info->rate_driver_data));
	memset(tx_info->pad, 0, sizeof(tx_info->pad));

	if (!arg->status) {
		tx_info->status.tx_time =
			le32_to_cpu(arg->media_delay) -
			le32_to_cpu(arg->tx_queue_delay);
		if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK)
			tx_info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		else
			tx_info->flags |= IEEE80211_TX_STAT_ACK;
	} else if (arg->status == HIF_STATUS_TX_FAIL_REQUEUE) {
		WARN(!arg->requeue, "incoherent status and result_flags");
		if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
			wvif->after_dtim_tx_allowed = false; // DTIM period elapsed
			schedule_work(&wvif->update_tim_work);
		}
		tx_info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	}
	wfx_skb_dtor(wvif, skb);
}

static void wfx_flush_vif(struct wfx_vif *wvif, u32 queues,
			  struct sk_buff_head *dropped)
{
	struct wfx_queue *queue;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (!(BIT(i) & queues))
			continue;
		queue = &wvif->tx_queue[i];
		if (dropped)
			wfx_tx_queue_drop(wvif, queue, dropped);
	}
	if (wvif->wdev->chip_frozen)
		return;
	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (!(BIT(i) & queues))
			continue;
		queue = &wvif->tx_queue[i];
		if (wait_event_timeout(wvif->wdev->tx_dequeue,
				       wfx_tx_queue_empty(wvif, queue),
				       msecs_to_jiffies(1000)) <= 0)
			dev_warn(wvif->wdev->dev,
				 "frames queued while flushing tx queues?");
	}
}

void wfx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u32 queues, bool drop)
{
	struct wfx_dev *wdev = hw->priv;
	struct sk_buff_head dropped;
	struct wfx_vif *wvif;
	struct hif_msg *hif;
	struct sk_buff *skb;

	skb_queue_head_init(&dropped);
	if (vif) {
		wvif = (struct wfx_vif *)vif->drv_priv;
		wfx_flush_vif(wvif, queues, drop ? &dropped : NULL);
	} else {
		wvif = NULL;
		while ((wvif = wvif_iterate(wdev, wvif)) != NULL)
			wfx_flush_vif(wvif, queues, drop ? &dropped : NULL);
	}
	wfx_tx_flush(wdev);
	if (wdev->chip_frozen)
		wfx_pending_drop(wdev, &dropped);
	while ((skb = skb_dequeue(&dropped)) != NULL) {
		hif = (struct hif_msg *)skb->data;
		wvif = wdev_to_wvif(wdev, hif->interface);
		ieee80211_tx_info_clear_status(IEEE80211_SKB_CB(skb));
		wfx_skb_dtor(wvif, skb);
	}
}
