// SPDX-License-Identifier: GPL-2.0-only
/*
 * Data transmitting implementation.
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

static int wfx_get_hw_rate(struct wfx_dev *wdev, const struct ieee80211_tx_rate *rate)
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
	/* The device only support 2GHz, else band information should be retrieved from
	 * ieee80211_tx_info
	 */
	band = wdev->hw->wiphy->bands[NL80211_BAND_2GHZ];
	if (rate->idx >= band->n_bitrates) {
		WARN(1, "wrong rate->idx value: %d", rate->idx);
		return -1;
	}
	return band->bitrates[rate->idx].hw_value;
}

/* TX policy cache implementation */

static void wfx_tx_policy_build(struct wfx_vif *wvif, struct wfx_tx_policy *policy,
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
		/* Pack two values in each byte of policy->rates */
		count = rates[i].count;
		if (rateid % 2)
			count <<= 4;
		policy->rates[rateid / 2] |= count;
	}
}

static bool wfx_tx_policy_is_equal(const struct wfx_tx_policy *a, const struct wfx_tx_policy *b)
{
	return !memcmp(a->rates, b->rates, sizeof(a->rates));
}

static int wfx_tx_policy_find(struct wfx_tx_policy_cache *cache, struct wfx_tx_policy *wanted)
{
	struct wfx_tx_policy *it;

	list_for_each_entry(it, &cache->used, link)
		if (wfx_tx_policy_is_equal(wanted, it))
			return it - cache->cache;
	list_for_each_entry(it, &cache->free, link)
		if (wfx_tx_policy_is_equal(wanted, it))
			return it - cache->cache;
	return -1;
}

static void wfx_tx_policy_use(struct wfx_tx_policy_cache *cache, struct wfx_tx_policy *entry)
{
	++entry->usage_count;
	list_move(&entry->link, &cache->used);
}

static int wfx_tx_policy_release(struct wfx_tx_policy_cache *cache, struct wfx_tx_policy *entry)
{
	int ret = --entry->usage_count;

	if (!ret)
		list_move(&entry->link, &cache->free);
	return ret;
}

static int wfx_tx_policy_get(struct wfx_vif *wvif, struct ieee80211_tx_rate *rates, bool *renew)
{
	int idx;
	struct wfx_tx_policy_cache *cache = &wvif->tx_policy_cache;
	struct wfx_tx_policy wanted;
	struct wfx_tx_policy *entry;

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
		/* If policy is not found create a new one using the oldest entry in "free" list */
		*renew = true;
		entry = list_entry(cache->free.prev, struct wfx_tx_policy, link);
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
	struct wfx_tx_policy_cache *cache = &wvif->tx_policy_cache;

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
	struct wfx_tx_policy *policies = wvif->tx_policy_cache.cache;
	u8 tmp_rates[12];
	int i, is_used;

	do {
		spin_lock_bh(&wvif->tx_policy_cache.lock);
		for (i = 0; i < ARRAY_SIZE(wvif->tx_policy_cache.cache); ++i) {
			is_used = memzcmp(policies[i].rates, sizeof(policies[i].rates));
			if (!policies[i].uploaded && is_used)
				break;
		}
		if (i < ARRAY_SIZE(wvif->tx_policy_cache.cache)) {
			policies[i].uploaded = true;
			memcpy(tmp_rates, policies[i].rates, sizeof(tmp_rates));
			spin_unlock_bh(&wvif->tx_policy_cache.lock);
			wfx_hif_set_tx_rate_retry_policy(wvif, i, tmp_rates);
		} else {
			spin_unlock_bh(&wvif->tx_policy_cache.lock);
		}
	} while (i < ARRAY_SIZE(wvif->tx_policy_cache.cache));
	return 0;
}

void wfx_tx_policy_upload_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, tx_policy_upload_work);

	wfx_tx_policy_upload(wvif);
	wfx_tx_unlock(wvif->wdev);
}

void wfx_tx_policy_init(struct wfx_vif *wvif)
{
	struct wfx_tx_policy_cache *cache = &wvif->tx_policy_cache;
	int i;

	memset(cache, 0, sizeof(*cache));

	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->used);
	INIT_LIST_HEAD(&cache->free);

	for (i = 0; i < ARRAY_SIZE(cache->cache); ++i)
		list_add(&cache->cache[i].link, &cache->free);
}

/* Tx implementation */

static bool wfx_is_action_back(struct ieee80211_hdr *hdr)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)hdr;

	if (!ieee80211_is_action(mgmt->frame_control))
		return false;
	if (mgmt->u.action.category != WLAN_CATEGORY_BACK)
		return false;
	return true;
}

struct wfx_tx_priv *wfx_skb_tx_priv(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info;

	if (!skb)
		return NULL;
	tx_info = IEEE80211_SKB_CB(skb);
	return (struct wfx_tx_priv *)tx_info->rate_driver_data;
}

struct wfx_hif_req_tx *wfx_skb_txreq(struct sk_buff *skb)
{
	struct wfx_hif_msg *hif = (struct wfx_hif_msg *)skb->data;
	struct wfx_hif_req_tx *req = (struct wfx_hif_req_tx *)hif->body;

	return req;
}

struct wfx_vif *wfx_skb_wvif(struct wfx_dev *wdev, struct sk_buff *skb)
{
	struct wfx_tx_priv *tx_priv = wfx_skb_tx_priv(skb);
	struct wfx_hif_msg *hif = (struct wfx_hif_msg *)skb->data;

	if (tx_priv->vif_id != hif->interface && hif->interface != 2) {
		dev_err(wdev->dev, "corrupted skb");
		return wdev_to_wvif(wdev, hif->interface);
	}
	return wdev_to_wvif(wdev, tx_priv->vif_id);
}

static u8 wfx_tx_get_link_id(struct wfx_vif *wvif, struct ieee80211_sta *sta,
			     struct ieee80211_hdr *hdr)
{
	struct wfx_sta_priv *sta_priv = sta ? (struct wfx_sta_priv *)&sta->drv_priv : NULL;
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	const u8 *da = ieee80211_get_DA(hdr);

	if (sta_priv && sta_priv->link_id)
		return sta_priv->link_id;
	if (vif->type != NL80211_IFTYPE_AP)
		return 0;
	if (is_multicast_ether_addr(da))
		return 0;
	return HIF_LINK_ID_NOT_ASSOCIATED;
}

static void wfx_tx_fixup_rates(struct ieee80211_tx_rate *rates)
{
	bool has_rate0 = false;
	int i, j;

	for (i = 1, j = 1; j < IEEE80211_TX_MAX_RATES; j++) {
		if (rates[j].idx == -1)
			break;
		/* The device use the rates in descending order, whatever the request from minstrel.
		 * We have to trade off here. Most important is to respect the primary rate
		 * requested by minstrel. So, we drops the entries with rate higher than the
		 * previous.
		 */
		if (rates[j].idx >= rates[i - 1].idx) {
			rates[i - 1].count += rates[j].count;
			rates[i - 1].count = min_t(u16, 15, rates[i - 1].count);
		} else {
			memcpy(rates + i, rates + j, sizeof(rates[i]));
			if (rates[i].idx == 0)
				has_rate0 = true;
			/* The device apply Short GI only on the first rate */
			rates[i].flags &= ~IEEE80211_TX_RC_SHORT_GI;
			i++;
		}
	}
	/* Ensure that MCS0 or 1Mbps is present at the end of the retry list */
	if (!has_rate0 && i < IEEE80211_TX_MAX_RATES) {
		rates[i].idx = 0;
		rates[i].count = 8; /* == hw->max_rate_tries */
		rates[i].flags = rates[0].flags & IEEE80211_TX_RC_MCS;
		i++;
	}
	for (; i < IEEE80211_TX_MAX_RATES; i++) {
		memset(rates + i, 0, sizeof(rates[i]));
		rates[i].idx = -1;
	}
}

static u8 wfx_tx_get_retry_policy_id(struct wfx_vif *wvif, struct ieee80211_tx_info *tx_info)
{
	bool tx_policy_renew = false;
	u8 ret;

	ret = wfx_tx_policy_get(wvif, tx_info->driver_rates, &tx_policy_renew);
	if (ret == HIF_TX_RETRY_POLICY_INVALID)
		dev_warn(wvif->wdev->dev, "unable to get a valid Tx policy");

	if (tx_policy_renew) {
		wfx_tx_lock(wvif->wdev);
		if (!schedule_work(&wvif->tx_policy_upload_work))
			wfx_tx_unlock(wvif->wdev);
	}
	return ret;
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

static int wfx_tx_inner(struct wfx_vif *wvif, struct ieee80211_sta *sta, struct sk_buff *skb)
{
	struct wfx_hif_msg *hif_msg;
	struct wfx_hif_req_tx *req;
	struct wfx_tx_priv *tx_priv;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *hw_key = tx_info->control.hw_key;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int queue_id = skb_get_queue_mapping(skb);
	size_t offset = (size_t)skb->data & 3;
	int wmsg_len = sizeof(struct wfx_hif_msg) + sizeof(struct wfx_hif_req_tx) + offset;

	WARN(queue_id >= IEEE80211_NUM_ACS, "unsupported queue_id");
	wfx_tx_fixup_rates(tx_info->driver_rates);

	/* From now tx_info->control is unusable */
	memset(tx_info->rate_driver_data, 0, sizeof(struct wfx_tx_priv));
	/* Fill tx_priv */
	tx_priv = (struct wfx_tx_priv *)tx_info->rate_driver_data;
	tx_priv->icv_size = wfx_tx_get_icv_len(hw_key);
	tx_priv->vif_id = wvif->id;

	/* Fill hif_msg */
	WARN(skb_headroom(skb) < wmsg_len, "not enough space in skb");
	WARN(offset & 1, "attempt to transmit an unaligned frame");
	skb_put(skb, tx_priv->icv_size);
	skb_push(skb, wmsg_len);
	memset(skb->data, 0, wmsg_len);
	hif_msg = (struct wfx_hif_msg *)skb->data;
	hif_msg->len = cpu_to_le16(skb->len);
	hif_msg->id = HIF_REQ_ID_TX;
	if (tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)
		hif_msg->interface = 2;
	else
		hif_msg->interface = wvif->id;
	if (skb->len > le16_to_cpu(wvif->wdev->hw_caps.size_inp_ch_buf)) {
		dev_warn(wvif->wdev->dev,
			 "requested frame size (%d) is larger than maximum supported (%d)\n",
			 skb->len, le16_to_cpu(wvif->wdev->hw_caps.size_inp_ch_buf));
		skb_pull(skb, wmsg_len);
		return -EIO;
	}

	/* Fill tx request */
	req = (struct wfx_hif_req_tx *)hif_msg->body;
	/* packet_id just need to be unique on device. 32bits are more than necessary for that task,
	 * so we take advantage of it to add some extra data for debug.
	 */
	req->packet_id = atomic_add_return(1, &wvif->wdev->packet_id) & 0xFFFF;
	req->packet_id |= IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl)) << 16;
	req->packet_id |= queue_id << 28;

	req->fc_offset = offset;
	/* Queue index are inverted between firmware and Linux */
	req->queue_id = 3 - queue_id;
	if (tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) {
		req->peer_sta_id = HIF_LINK_ID_NOT_ASSOCIATED;
		req->retry_policy_index = HIF_TX_RETRY_POLICY_INVALID;
		req->frame_format = HIF_FRAME_FORMAT_NON_HT;
	} else {
		req->peer_sta_id = wfx_tx_get_link_id(wvif, sta, hdr);
		req->retry_policy_index = wfx_tx_get_retry_policy_id(wvif, tx_info);
		req->frame_format = wfx_tx_get_frame_format(tx_info);
	}
	if (tx_info->driver_rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
		req->short_gi = 1;
	if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		req->after_dtim = 1;

	/* Auxiliary operations */
	wfx_tx_queues_put(wvif, skb);
	if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		schedule_work(&wvif->update_tim_work);
	wfx_bh_request_tx(wvif->wdev);
	return 0;
}

void wfx_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control, struct sk_buff *skb)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif;
	struct ieee80211_sta *sta = control ? control->sta : NULL;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	size_t driver_data_room = sizeof_field(struct ieee80211_tx_info, rate_driver_data);

	BUILD_BUG_ON_MSG(sizeof(struct wfx_tx_priv) > driver_data_room,
			 "struct tx_priv is too large");
	WARN(skb->next || skb->prev, "skb is already member of a list");
	/* control.vif can be NULL for injected frames */
	if (tx_info->control.vif)
		wvif = (struct wfx_vif *)tx_info->control.vif->drv_priv;
	else
		wvif = wvif_iterate(wdev, NULL);
	if (WARN_ON(!wvif))
		goto drop;
	/* Because of TX_AMPDU_SETUP_IN_HW, mac80211 does not try to send any BlockAck session
	 * management frame. The check below exist just in case.
	 */
	if (wfx_is_action_back(hdr)) {
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
	struct wfx_hif_msg *hif = (struct wfx_hif_msg *)skb->data;
	struct wfx_hif_req_tx *req = (struct wfx_hif_req_tx *)hif->body;
	unsigned int offset = sizeof(struct wfx_hif_msg) + sizeof(struct wfx_hif_req_tx) +
			      req->fc_offset;

	if (!wvif) {
		pr_warn("vif associated with the skb does not exist anymore\n");
		return;
	}
	wfx_tx_policy_put(wvif, req->retry_policy_index);
	skb_pull(skb, offset);
	ieee80211_tx_status_irqsafe(wvif->wdev->hw, skb);
}

static void wfx_tx_fill_rates(struct wfx_dev *wdev, struct ieee80211_tx_info *tx_info,
			      const struct wfx_hif_cnf_tx *arg)
{
	struct ieee80211_tx_rate *rate;
	int tx_count;
	int i;

	tx_count = arg->ack_failures;
	if (!arg->status || arg->ack_failures)
		tx_count += 1; /* Also report success */
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		rate = &tx_info->status.rates[i];
		if (rate->idx < 0)
			break;
		if (tx_count < rate->count && arg->status == HIF_STATUS_TX_FAIL_RETRIES &&
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

void wfx_tx_confirm_cb(struct wfx_dev *wdev, const struct wfx_hif_cnf_tx *arg)
{
	const struct wfx_tx_priv *tx_priv;
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
	tx_priv = wfx_skb_tx_priv(skb);
	wvif = wfx_skb_wvif(wdev, skb);
	WARN_ON(!wvif);
	if (!wvif)
		return;

	/* Note that wfx_pending_get_pkt_us_delay() get data from tx_info */
	_trace_tx_stats(arg, skb, wfx_pending_get_pkt_us_delay(wdev, skb));
	wfx_tx_fill_rates(wdev, tx_info, arg);
	skb_trim(skb, skb->len - tx_priv->icv_size);

	/* From now, you can touch to tx_info->status, but do not touch to tx_priv anymore */
	/* FIXME: use ieee80211_tx_info_clear_status() */
	memset(tx_info->rate_driver_data, 0, sizeof(tx_info->rate_driver_data));
	memset(tx_info->pad, 0, sizeof(tx_info->pad));

	if (!arg->status) {
		tx_info->status.tx_time = le32_to_cpu(arg->media_delay) -
					  le32_to_cpu(arg->tx_queue_delay);
		if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK)
			tx_info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		else
			tx_info->flags |= IEEE80211_TX_STAT_ACK;
	} else if (arg->status == HIF_STATUS_TX_FAIL_REQUEUE) {
		WARN(!arg->requeue, "incoherent status and result_flags");
		if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
			wvif->after_dtim_tx_allowed = false; /* DTIM period elapsed */
			schedule_work(&wvif->update_tim_work);
		}
		tx_info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	}
	wfx_skb_dtor(wvif, skb);
}

static void wfx_flush_vif(struct wfx_vif *wvif, u32 queues, struct sk_buff_head *dropped)
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
		if (wait_event_timeout(wvif->wdev->tx_dequeue, wfx_tx_queue_empty(wvif, queue),
				       msecs_to_jiffies(1000)) <= 0)
			dev_warn(wvif->wdev->dev, "frames queued while flushing tx queues?");
	}
}

void wfx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u32 queues, bool drop)
{
	struct wfx_dev *wdev = hw->priv;
	struct sk_buff_head dropped;
	struct wfx_vif *wvif;
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
		wvif = wfx_skb_wvif(wdev, skb);
		ieee80211_tx_info_clear_status(IEEE80211_SKB_CB(skb));
		wfx_skb_dtor(wvif, skb);
	}
}
