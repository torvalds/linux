/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "wl12xx.h"
#include "io.h"
#include "reg.h"
#include "ps.h"
#include "tx.h"

static int wl1271_alloc_tx_id(struct wl1271 *wl, struct sk_buff *skb)
{
	int id;

	id = find_first_zero_bit(wl->tx_frames_map, ACX_TX_DESCRIPTORS);
	if (id >= ACX_TX_DESCRIPTORS)
		return -EBUSY;

	__set_bit(id, wl->tx_frames_map);
	wl->tx_frames[id] = skb;
	wl->tx_frames_cnt++;
	return id;
}

static void wl1271_free_tx_id(struct wl1271 *wl, int id)
{
	if (__test_and_clear_bit(id, wl->tx_frames_map)) {
		wl->tx_frames[id] = NULL;
		wl->tx_frames_cnt--;
	}
}

static int wl1271_tx_allocate(struct wl1271 *wl, struct sk_buff *skb, u32 extra,
				u32 buf_offset)
{
	struct wl1271_tx_hw_descr *desc;
	u32 total_len = skb->len + sizeof(struct wl1271_tx_hw_descr) + extra;
	u32 total_blocks;
	int id, ret = -EBUSY;

	if (buf_offset + total_len > WL1271_AGGR_BUFFER_SIZE)
		return -EAGAIN;

	/* allocate free identifier for the packet */
	id = wl1271_alloc_tx_id(wl, skb);
	if (id < 0)
		return id;

	/* approximate the number of blocks required for this packet
	   in the firmware */
	total_blocks = total_len + TX_HW_BLOCK_SIZE - 1;
	total_blocks = total_blocks / TX_HW_BLOCK_SIZE + TX_HW_BLOCK_SPARE;
	if (total_blocks <= wl->tx_blocks_available) {
		desc = (struct wl1271_tx_hw_descr *)skb_push(
			skb, total_len - skb->len);

		desc->extra_mem_blocks = TX_HW_BLOCK_SPARE;
		desc->total_mem_blocks = total_blocks;
		desc->id = id;

		wl->tx_blocks_available -= total_blocks;

		ret = 0;

		wl1271_debug(DEBUG_TX,
			     "tx_allocate: size: %d, blocks: %d, id: %d",
			     total_len, total_blocks, id);
	} else {
		wl1271_free_tx_id(wl, id);
	}

	return ret;
}

static void wl1271_tx_fill_hdr(struct wl1271 *wl, struct sk_buff *skb,
			      u32 extra, struct ieee80211_tx_info *control)
{
	struct timespec ts;
	struct wl1271_tx_hw_descr *desc;
	int pad, ac;
	s64 hosttime;
	u16 tx_attr;

	desc = (struct wl1271_tx_hw_descr *) skb->data;

	/* relocate space for security header */
	if (extra) {
		void *framestart = skb->data + sizeof(*desc);
		u16 fc = *(u16 *)(framestart + extra);
		int hdrlen = ieee80211_hdrlen(cpu_to_le16(fc));
		memmove(framestart, framestart + extra, hdrlen);
	}

	/* configure packet life time */
	getnstimeofday(&ts);
	hosttime = (timespec_to_ns(&ts) >> 10);
	desc->start_time = cpu_to_le32(hosttime - wl->time_offset);
	desc->life_time = cpu_to_le16(TX_HW_MGMT_PKT_LIFETIME_TU);

	/* configure the tx attributes */
	tx_attr = wl->session_counter << TX_HW_ATTR_OFST_SESSION_COUNTER;

	/* queue (we use same identifiers for tid's and ac's */
	ac = wl1271_tx_get_queue(skb_get_queue_mapping(skb));
	desc->tid = ac;
	desc->aid = TX_HW_DEFAULT_AID;
	desc->reserved = 0;

	/* align the length (and store in terms of words) */
	pad = WL1271_TX_ALIGN(skb->len);
	desc->length = cpu_to_le16(pad >> 2);

	/* calculate number of padding bytes */
	pad = pad - skb->len;
	tx_attr |= pad << TX_HW_ATTR_OFST_LAST_WORD_PAD;

	/* if the packets are destined for AP (have a STA entry) send them
	   with AP rate policies, otherwise use default basic rates */
	if (control->control.sta)
		tx_attr |= ACX_TX_AP_FULL_RATE << TX_HW_ATTR_OFST_RATE_POLICY;

	desc->tx_attr = cpu_to_le16(tx_attr);

	wl1271_debug(DEBUG_TX, "tx_fill_hdr: pad: %d", pad);
}

/* caller must hold wl->mutex */
static int wl1271_prepare_tx_frame(struct wl1271 *wl, struct sk_buff *skb,
							u32 buf_offset)
{
	struct ieee80211_tx_info *info;
	u32 extra = 0;
	int ret = 0;
	u8 idx;
	u32 total_len;

	if (!skb)
		return -EINVAL;

	info = IEEE80211_SKB_CB(skb);

	if (info->control.hw_key &&
	    info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP)
		extra = WL1271_TKIP_IV_SPACE;

	if (info->control.hw_key) {
		idx = info->control.hw_key->hw_key_idx;

		/* FIXME: do we have to do this if we're not using WEP? */
		if (unlikely(wl->default_key != idx)) {
			ret = wl1271_cmd_set_default_wep_key(wl, idx);
			if (ret < 0)
				return ret;
			wl->default_key = idx;
		}
	}

	ret = wl1271_tx_allocate(wl, skb, extra, buf_offset);
	if (ret < 0)
		return ret;

	wl1271_tx_fill_hdr(wl, skb, extra, info);

	/*
	 * The length of each packet is stored in terms of words. Thus, we must
	 * pad the skb data to make sure its length is aligned.
	 * The number of padding bytes is computed and set in wl1271_tx_fill_hdr
	 */
	total_len = WL1271_TX_ALIGN(skb->len);
	memcpy(wl->aggr_buf + buf_offset, skb->data, skb->len);
	memset(wl->aggr_buf + buf_offset + skb->len, 0, total_len - skb->len);

	return total_len;
}

u32 wl1271_tx_enabled_rates_get(struct wl1271 *wl, u32 rate_set)
{
	struct ieee80211_supported_band *band;
	u32 enabled_rates = 0;
	int bit;

	band = wl->hw->wiphy->bands[wl->band];
	for (bit = 0; bit < band->n_bitrates; bit++) {
		if (rate_set & 0x1)
			enabled_rates |= band->bitrates[bit].hw_value;
		rate_set >>= 1;
	}

#ifdef CONFIG_WL12XX_HT
	/* MCS rates indication are on bits 16 - 23 */
	rate_set >>= HW_HT_RATES_OFFSET - band->n_bitrates;

	for (bit = 0; bit < 8; bit++) {
		if (rate_set & 0x1)
			enabled_rates |= (CONF_HW_BIT_RATE_MCS_0 << bit);
		rate_set >>= 1;
	}
#endif

	return enabled_rates;
}

static void handle_tx_low_watermark(struct wl1271 *wl)
{
	unsigned long flags;

	if (test_bit(WL1271_FLAG_TX_QUEUE_STOPPED, &wl->flags) &&
	    wl->tx_queue_count <= WL1271_TX_QUEUE_LOW_WATERMARK) {
		/* firmware buffer has space, restart queues */
		spin_lock_irqsave(&wl->wl_lock, flags);
		ieee80211_wake_queues(wl->hw);
		clear_bit(WL1271_FLAG_TX_QUEUE_STOPPED, &wl->flags);
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}
}

static struct sk_buff *wl1271_skb_dequeue(struct wl1271 *wl)
{
	struct sk_buff *skb = NULL;
	unsigned long flags;

	skb = skb_dequeue(&wl->tx_queue[CONF_TX_AC_VO]);
	if (skb)
		goto out;
	skb = skb_dequeue(&wl->tx_queue[CONF_TX_AC_VI]);
	if (skb)
		goto out;
	skb = skb_dequeue(&wl->tx_queue[CONF_TX_AC_BE]);
	if (skb)
		goto out;
	skb = skb_dequeue(&wl->tx_queue[CONF_TX_AC_BK]);

out:
	if (skb) {
		spin_lock_irqsave(&wl->wl_lock, flags);
		wl->tx_queue_count--;
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}

	return skb;
}

static void wl1271_skb_queue_head(struct wl1271 *wl, struct sk_buff *skb)
{
	unsigned long flags;
	int q = wl1271_tx_get_queue(skb_get_queue_mapping(skb));

	skb_queue_head(&wl->tx_queue[q], skb);
	spin_lock_irqsave(&wl->wl_lock, flags);
	wl->tx_queue_count++;
	spin_unlock_irqrestore(&wl->wl_lock, flags);
}

void wl1271_tx_work_locked(struct wl1271 *wl)
{
	struct sk_buff *skb;
	bool woken_up = false;
	u32 sta_rates = 0;
	u32 buf_offset = 0;
	bool sent_packets = false;
	int ret;

	/* check if the rates supported by the AP have changed */
	if (unlikely(test_and_clear_bit(WL1271_FLAG_STA_RATES_CHANGED,
					&wl->flags))) {
		unsigned long flags;

		spin_lock_irqsave(&wl->wl_lock, flags);
		sta_rates = wl->sta_rate_set;
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	/* if rates have changed, re-configure the rate policy */
	if (unlikely(sta_rates)) {
		ret = wl1271_ps_elp_wakeup(wl, false);
		if (ret < 0)
			goto out;
		woken_up = true;

		wl->rate_set = wl1271_tx_enabled_rates_get(wl, sta_rates);
		wl1271_acx_rate_policies(wl);
	}

	while ((skb = wl1271_skb_dequeue(wl))) {
		if (!woken_up) {
			ret = wl1271_ps_elp_wakeup(wl, false);
			if (ret < 0)
				goto out_ack;
			woken_up = true;
		}

		ret = wl1271_prepare_tx_frame(wl, skb, buf_offset);
		if (ret == -EAGAIN) {
			/*
			 * Aggregation buffer is full.
			 * Flush buffer and try again.
			 */
			wl1271_skb_queue_head(wl, skb);
			wl1271_write(wl, WL1271_SLV_MEM_DATA, wl->aggr_buf,
				     buf_offset, true);
			sent_packets = true;
			buf_offset = 0;
			continue;
		} else if (ret == -EBUSY) {
			/*
			 * Firmware buffer is full.
			 * Queue back last skb, and stop aggregating.
			 */
			wl1271_skb_queue_head(wl, skb);
			/* No work left, avoid scheduling redundant tx work */
			set_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags);
			goto out_ack;
		} else if (ret < 0) {
			dev_kfree_skb(skb);
			goto out_ack;
		}
		buf_offset += ret;
		wl->tx_packets_count++;
	}

out_ack:
	if (buf_offset) {
		wl1271_write(wl, WL1271_SLV_MEM_DATA, wl->aggr_buf,
				buf_offset, true);
		sent_packets = true;
	}
	if (sent_packets) {
		/* interrupt the firmware with the new packets */
		wl1271_write32(wl, WL1271_HOST_WR_ACCESS, wl->tx_packets_count);
		handle_tx_low_watermark(wl);
	}

out:
	if (woken_up)
		wl1271_ps_elp_sleep(wl);
}

void wl1271_tx_work(struct work_struct *work)
{
	struct wl1271 *wl = container_of(work, struct wl1271, tx_work);

	mutex_lock(&wl->mutex);
	wl1271_tx_work_locked(wl);
	mutex_unlock(&wl->mutex);
}

static void wl1271_tx_complete_packet(struct wl1271 *wl,
				      struct wl1271_tx_hw_res_descr *result)
{
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	int id = result->id;
	int rate = -1;
	u8 retries = 0;

	/* check for id legality */
	if (unlikely(id >= ACX_TX_DESCRIPTORS || wl->tx_frames[id] == NULL)) {
		wl1271_warning("TX result illegal id: %d", id);
		return;
	}

	skb = wl->tx_frames[id];
	info = IEEE80211_SKB_CB(skb);

	/* update the TX status info */
	if (result->status == TX_SUCCESS) {
		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
			info->flags |= IEEE80211_TX_STAT_ACK;
		rate = wl1271_rate_to_idx(result->rate_class_index, wl->band);
		retries = result->ack_failures;
	} else if (result->status == TX_RETRY_EXCEEDED) {
		wl->stats.excessive_retries++;
		retries = result->ack_failures;
	}

	info->status.rates[0].idx = rate;
	info->status.rates[0].count = retries;
	info->status.rates[0].flags = 0;
	info->status.ack_signal = -1;

	wl->stats.retry_count += result->ack_failures;

	/* update security sequence number */
	wl->tx_security_seq += (result->lsb_security_sequence_number -
				wl->tx_security_last_seq);
	wl->tx_security_last_seq = result->lsb_security_sequence_number;

	/* remove private header from packet */
	skb_pull(skb, sizeof(struct wl1271_tx_hw_descr));

	/* remove TKIP header space if present */
	if (info->control.hw_key &&
	    info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		int hdrlen = ieee80211_get_hdrlen_from_skb(skb);
		memmove(skb->data + WL1271_TKIP_IV_SPACE, skb->data, hdrlen);
		skb_pull(skb, WL1271_TKIP_IV_SPACE);
	}

	wl1271_debug(DEBUG_TX, "tx status id %u skb 0x%p failures %u rate 0x%x"
		     " status 0x%x",
		     result->id, skb, result->ack_failures,
		     result->rate_class_index, result->status);

	/* return the packet to the stack */
	ieee80211_tx_status(wl->hw, skb);
	wl1271_free_tx_id(wl, result->id);
}

/* Called upon reception of a TX complete interrupt */
void wl1271_tx_complete(struct wl1271 *wl)
{
	struct wl1271_acx_mem_map *memmap =
		(struct wl1271_acx_mem_map *)wl->target_mem_map;
	u32 count, fw_counter;
	u32 i;

	/* read the tx results from the chipset */
	wl1271_read(wl, le32_to_cpu(memmap->tx_result),
		    wl->tx_res_if, sizeof(*wl->tx_res_if), false);
	fw_counter = le32_to_cpu(wl->tx_res_if->tx_result_fw_counter);

	/* write host counter to chipset (to ack) */
	wl1271_write32(wl, le32_to_cpu(memmap->tx_result) +
		       offsetof(struct wl1271_tx_hw_res_if,
				tx_result_host_counter), fw_counter);

	count = fw_counter - wl->tx_results_count;
	wl1271_debug(DEBUG_TX, "tx_complete received, packets: %d", count);

	/* verify that the result buffer is not getting overrun */
	if (unlikely(count > TX_HW_RESULT_QUEUE_LEN))
		wl1271_warning("TX result overflow from chipset: %d", count);

	/* process the results */
	for (i = 0; i < count; i++) {
		struct wl1271_tx_hw_res_descr *result;
		u8 offset = wl->tx_results_count & TX_HW_RESULT_QUEUE_LEN_MASK;

		/* process the packet */
		result =  &(wl->tx_res_if->tx_results_queue[offset]);
		wl1271_tx_complete_packet(wl, result);

		wl->tx_results_count++;
	}
}

/* caller must hold wl->mutex */
void wl1271_tx_reset(struct wl1271 *wl)
{
	int i;
	struct sk_buff *skb;

	/* TX failure */
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		while ((skb = skb_dequeue(&wl->tx_queue[i]))) {
			wl1271_debug(DEBUG_TX, "freeing skb 0x%p", skb);
			ieee80211_tx_status(wl->hw, skb);
		}
	}
	wl->tx_queue_count = 0;

	/*
	 * Make sure the driver is at a consistent state, in case this
	 * function is called from a context other than interface removal.
	 */
	handle_tx_low_watermark(wl);

	for (i = 0; i < ACX_TX_DESCRIPTORS; i++)
		if (wl->tx_frames[i] != NULL) {
			skb = wl->tx_frames[i];
			wl1271_free_tx_id(wl, i);
			wl1271_debug(DEBUG_TX, "freeing skb 0x%p", skb);
			ieee80211_tx_status(wl->hw, skb);
		}
}

#define WL1271_TX_FLUSH_TIMEOUT 500000

/* caller must *NOT* hold wl->mutex */
void wl1271_tx_flush(struct wl1271 *wl)
{
	unsigned long timeout;
	timeout = jiffies + usecs_to_jiffies(WL1271_TX_FLUSH_TIMEOUT);

	while (!time_after(jiffies, timeout)) {
		mutex_lock(&wl->mutex);
		wl1271_debug(DEBUG_TX, "flushing tx buffer: %d",
			     wl->tx_frames_cnt);
		if ((wl->tx_frames_cnt == 0) && (wl->tx_queue_count == 0)) {
			mutex_unlock(&wl->mutex);
			return;
		}
		mutex_unlock(&wl->mutex);
		msleep(1);
	}

	wl1271_warning("Unable to flush all TX buffers, timed out.");
}
