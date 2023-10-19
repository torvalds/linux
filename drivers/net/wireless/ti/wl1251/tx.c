// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "wl1251.h"
#include "reg.h"
#include "tx.h"
#include "ps.h"
#include "io.h"
#include "event.h"

static bool wl1251_tx_double_buffer_busy(struct wl1251 *wl, u32 data_out_count)
{
	int used, data_in_count;

	data_in_count = wl->data_in_count;

	if (data_in_count < data_out_count)
		/* data_in_count has wrapped */
		data_in_count += TX_STATUS_DATA_OUT_COUNT_MASK + 1;

	used = data_in_count - data_out_count;

	WARN_ON(used < 0);
	WARN_ON(used > DP_TX_PACKET_RING_CHUNK_NUM);

	if (used >= DP_TX_PACKET_RING_CHUNK_NUM)
		return true;
	else
		return false;
}

static int wl1251_tx_path_status(struct wl1251 *wl)
{
	u32 status, addr, data_out_count;
	bool busy;

	addr = wl->data_path->tx_control_addr;
	status = wl1251_mem_read32(wl, addr);
	data_out_count = status & TX_STATUS_DATA_OUT_COUNT_MASK;
	busy = wl1251_tx_double_buffer_busy(wl, data_out_count);

	if (busy)
		return -EBUSY;

	return 0;
}

static int wl1251_tx_id(struct wl1251 *wl, struct sk_buff *skb)
{
	int i;

	for (i = 0; i < FW_TX_CMPLT_BLOCK_SIZE; i++)
		if (wl->tx_frames[i] == NULL) {
			wl->tx_frames[i] = skb;
			return i;
		}

	return -EBUSY;
}

static void wl1251_tx_control(struct tx_double_buffer_desc *tx_hdr,
			      struct ieee80211_tx_info *control, u16 fc)
{
	*(u16 *)&tx_hdr->control = 0;

	tx_hdr->control.rate_policy = 0;

	/* 802.11 packets */
	tx_hdr->control.packet_type = 0;

	/* Also disable retry and ACK policy for injected packets */
	if ((control->flags & IEEE80211_TX_CTL_NO_ACK) ||
	    (control->flags & IEEE80211_TX_CTL_INJECTED)) {
		tx_hdr->control.rate_policy = 1;
		tx_hdr->control.ack_policy = 1;
	}

	tx_hdr->control.tx_complete = 1;

	if ((fc & IEEE80211_FTYPE_DATA) &&
	    ((fc & IEEE80211_STYPE_QOS_DATA) ||
	     (fc & IEEE80211_STYPE_QOS_NULLFUNC)))
		tx_hdr->control.qos = 1;
}

/* RSN + MIC = 8 + 8 = 16 bytes (worst case - AES). */
#define MAX_MSDU_SECURITY_LENGTH      16
#define MAX_MPDU_SECURITY_LENGTH      16
#define WLAN_QOS_HDR_LEN              26
#define MAX_MPDU_HEADER_AND_SECURITY  (MAX_MPDU_SECURITY_LENGTH + \
				       WLAN_QOS_HDR_LEN)
#define HW_BLOCK_SIZE                 252
static void wl1251_tx_frag_block_num(struct tx_double_buffer_desc *tx_hdr)
{
	u16 payload_len, frag_threshold, mem_blocks;
	u16 num_mpdus, mem_blocks_per_frag;

	frag_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	tx_hdr->frag_threshold = cpu_to_le16(frag_threshold);

	payload_len = le16_to_cpu(tx_hdr->length) + MAX_MSDU_SECURITY_LENGTH;

	if (payload_len > frag_threshold) {
		mem_blocks_per_frag =
			((frag_threshold + MAX_MPDU_HEADER_AND_SECURITY) /
			 HW_BLOCK_SIZE) + 1;
		num_mpdus = payload_len / frag_threshold;
		mem_blocks = num_mpdus * mem_blocks_per_frag;
		payload_len -= num_mpdus * frag_threshold;
		num_mpdus++;

	} else {
		mem_blocks_per_frag = 0;
		mem_blocks = 0;
		num_mpdus = 1;
	}

	mem_blocks += (payload_len / HW_BLOCK_SIZE) + 1;

	if (num_mpdus > 1)
		mem_blocks += min(num_mpdus, mem_blocks_per_frag);

	tx_hdr->num_mem_blocks = mem_blocks;
}

static int wl1251_tx_fill_hdr(struct wl1251 *wl, struct sk_buff *skb,
			      struct ieee80211_tx_info *control)
{
	struct tx_double_buffer_desc *tx_hdr;
	struct ieee80211_rate *rate;
	int id;
	u16 fc;

	if (!skb)
		return -EINVAL;

	id = wl1251_tx_id(wl, skb);
	if (id < 0)
		return id;

	fc = *(u16 *)skb->data;
	tx_hdr = skb_push(skb, sizeof(*tx_hdr));

	tx_hdr->length = cpu_to_le16(skb->len - sizeof(*tx_hdr));
	rate = ieee80211_get_tx_rate(wl->hw, control);
	tx_hdr->rate = cpu_to_le16(rate->hw_value);
	tx_hdr->expiry_time = cpu_to_le32(1 << 16);
	tx_hdr->id = id;

	tx_hdr->xmit_queue = wl1251_tx_get_queue(skb_get_queue_mapping(skb));

	wl1251_tx_control(tx_hdr, control, fc);
	wl1251_tx_frag_block_num(tx_hdr);

	return 0;
}

/* We copy the packet to the target */
static int wl1251_tx_send_packet(struct wl1251 *wl, struct sk_buff *skb,
				 struct ieee80211_tx_info *control)
{
	struct tx_double_buffer_desc *tx_hdr;
	int len;
	u32 addr;

	if (!skb)
		return -EINVAL;

	tx_hdr = (struct tx_double_buffer_desc *) skb->data;

	if (control->control.hw_key &&
	    control->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		int hdrlen;
		__le16 fc;
		u16 length;
		u8 *pos;

		fc = *(__le16 *)(skb->data + sizeof(*tx_hdr));
		length = le16_to_cpu(tx_hdr->length) + WL1251_TKIP_IV_SPACE;
		tx_hdr->length = cpu_to_le16(length);

		hdrlen = ieee80211_hdrlen(fc);

		pos = skb_push(skb, WL1251_TKIP_IV_SPACE);
		memmove(pos, pos + WL1251_TKIP_IV_SPACE,
			sizeof(*tx_hdr) + hdrlen);
	}

	/* Revisit. This is a workaround for getting non-aligned packets.
	   This happens at least with EAPOL packets from the user space.
	   Our DMA requires packets to be aligned on a 4-byte boundary.
	*/
	if (unlikely((long)skb->data & 0x03)) {
		int offset = (4 - (long)skb->data) & 0x03;
		wl1251_debug(DEBUG_TX, "skb offset %d", offset);

		/* check whether the current skb can be used */
		if (skb_cloned(skb) || (skb_tailroom(skb) < offset)) {
			struct sk_buff *newskb = skb_copy_expand(skb, 0, 3,
								 GFP_KERNEL);

			if (unlikely(newskb == NULL))
				return -EINVAL;

			tx_hdr = (struct tx_double_buffer_desc *) newskb->data;

			dev_kfree_skb_any(skb);
			wl->tx_frames[tx_hdr->id] = skb = newskb;

			offset = (4 - (long)skb->data) & 0x03;
			wl1251_debug(DEBUG_TX, "new skb offset %d", offset);
		}

		/* align the buffer on a 4-byte boundary */
		if (offset) {
			unsigned char *src = skb->data;
			skb_reserve(skb, offset);
			memmove(skb->data, src, skb->len);
			tx_hdr = (struct tx_double_buffer_desc *) skb->data;
		}
	}

	/* Our skb->data at this point includes the HW header */
	len = WL1251_TX_ALIGN(skb->len);

	if (wl->data_in_count & 0x1)
		addr = wl->data_path->tx_packet_ring_addr +
			wl->data_path->tx_packet_ring_chunk_size;
	else
		addr = wl->data_path->tx_packet_ring_addr;

	wl1251_mem_write(wl, addr, skb->data, len);

	wl1251_debug(DEBUG_TX, "tx id %u skb 0x%p payload %u rate 0x%x "
		     "queue %d", tx_hdr->id, skb, tx_hdr->length,
		     tx_hdr->rate, tx_hdr->xmit_queue);

	return 0;
}

static void wl1251_tx_trigger(struct wl1251 *wl)
{
	u32 data, addr;

	if (wl->data_in_count & 0x1) {
		addr = ACX_REG_INTERRUPT_TRIG_H;
		data = INTR_TRIG_TX_PROC1;
	} else {
		addr = ACX_REG_INTERRUPT_TRIG;
		data = INTR_TRIG_TX_PROC0;
	}

	wl1251_reg_write32(wl, addr, data);

	/* Bumping data in */
	wl->data_in_count = (wl->data_in_count + 1) &
		TX_STATUS_DATA_OUT_COUNT_MASK;
}

static void enable_tx_for_packet_injection(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_cmd_join(wl, BSS_TYPE_STA_BSS, wl->channel,
			      wl->beacon_int, wl->dtim_period);
	if (ret < 0) {
		wl1251_warning("join failed");
		return;
	}

	ret = wl1251_event_wait(wl, JOIN_EVENT_COMPLETE_ID, 100);
	if (ret < 0) {
		wl1251_warning("join timeout");
		return;
	}

	wl->joined = true;
}

/* caller must hold wl->mutex */
static int wl1251_tx_frame(struct wl1251 *wl, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info;
	int ret = 0;
	u8 idx;

	info = IEEE80211_SKB_CB(skb);

	if (info->control.hw_key) {
		if (unlikely(wl->monitor_present))
			return -EINVAL;

		idx = info->control.hw_key->hw_key_idx;
		if (unlikely(wl->default_key != idx)) {
			ret = wl1251_acx_default_key(wl, idx);
			if (ret < 0)
				return ret;
		}
	}

	/* Enable tx path in monitor mode for packet injection */
	if ((wl->vif == NULL) && !wl->joined)
		enable_tx_for_packet_injection(wl);

	ret = wl1251_tx_path_status(wl);
	if (ret < 0)
		return ret;

	ret = wl1251_tx_fill_hdr(wl, skb, info);
	if (ret < 0)
		return ret;

	ret = wl1251_tx_send_packet(wl, skb, info);
	if (ret < 0)
		return ret;

	wl1251_tx_trigger(wl);

	return ret;
}

void wl1251_tx_work(struct work_struct *work)
{
	struct wl1251 *wl = container_of(work, struct wl1251, tx_work);
	struct sk_buff *skb;
	bool woken_up = false;
	int ret;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1251_STATE_OFF))
		goto out;

	while ((skb = skb_dequeue(&wl->tx_queue))) {
		if (!woken_up) {
			ret = wl1251_ps_elp_wakeup(wl);
			if (ret < 0)
				goto out;
			woken_up = true;
		}

		ret = wl1251_tx_frame(wl, skb);
		if (ret == -EBUSY) {
			skb_queue_head(&wl->tx_queue, skb);
			goto out;
		} else if (ret < 0) {
			dev_kfree_skb(skb);
			goto out;
		}
	}

out:
	if (woken_up)
		wl1251_ps_elp_sleep(wl);

	mutex_unlock(&wl->mutex);
}

static const char *wl1251_tx_parse_status(u8 status)
{
	/* 8 bit status field, one character per bit plus null */
	static char buf[9];
	int i = 0;

	memset(buf, 0, sizeof(buf));

	if (status & TX_DMA_ERROR)
		buf[i++] = 'm';
	if (status & TX_DISABLED)
		buf[i++] = 'd';
	if (status & TX_RETRY_EXCEEDED)
		buf[i++] = 'r';
	if (status & TX_TIMEOUT)
		buf[i++] = 't';
	if (status & TX_KEY_NOT_FOUND)
		buf[i++] = 'k';
	if (status & TX_ENCRYPT_FAIL)
		buf[i++] = 'e';
	if (status & TX_UNAVAILABLE_PRIORITY)
		buf[i++] = 'p';

	/* bit 0 is unused apparently */

	return buf;
}

static void wl1251_tx_packet_cb(struct wl1251 *wl,
				struct tx_result *result)
{
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	int hdrlen;
	u8 *frame;

	skb = wl->tx_frames[result->id];
	if (skb == NULL) {
		wl1251_error("SKB for packet %d is NULL", result->id);
		return;
	}

	info = IEEE80211_SKB_CB(skb);

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) &&
	    !(info->flags & IEEE80211_TX_CTL_INJECTED) &&
	    (result->status == TX_SUCCESS))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.rates[0].count = result->ack_failures + 1;
	wl->stats.retry_count += result->ack_failures;

	/*
	 * We have to remove our private TX header before pushing
	 * the skb back to mac80211.
	 */
	frame = skb_pull(skb, sizeof(struct tx_double_buffer_desc));
	if (info->control.hw_key &&
	    info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		hdrlen = ieee80211_get_hdrlen_from_skb(skb);
		memmove(frame + WL1251_TKIP_IV_SPACE, frame, hdrlen);
		skb_pull(skb, WL1251_TKIP_IV_SPACE);
	}

	wl1251_debug(DEBUG_TX, "tx status id %u skb 0x%p failures %u rate 0x%x"
		     " status 0x%x (%s)",
		     result->id, skb, result->ack_failures, result->rate,
		     result->status, wl1251_tx_parse_status(result->status));


	ieee80211_tx_status(wl->hw, skb);

	wl->tx_frames[result->id] = NULL;
}

/* Called upon reception of a TX complete interrupt */
void wl1251_tx_complete(struct wl1251 *wl)
{
	int i, result_index, num_complete = 0, queue_len;
	struct tx_result *result, *result_ptr;
	unsigned long flags;

	if (unlikely(wl->state != WL1251_STATE_ON))
		return;

	result = kmalloc_array(FW_TX_CMPLT_BLOCK_SIZE, sizeof(*result), GFP_KERNEL);
	if (!result) {
		wl1251_error("can not allocate result buffer");
		return;
	}

	/* First we read the result */
	wl1251_mem_read(wl, wl->data_path->tx_complete_addr, result,
			FW_TX_CMPLT_BLOCK_SIZE * sizeof(*result));

	result_index = wl->next_tx_complete;

	for (i = 0; i < FW_TX_CMPLT_BLOCK_SIZE; i++) {
		result_ptr = &result[result_index];

		if (result_ptr->done_1 == 1 &&
		    result_ptr->done_2 == 1) {
			wl1251_tx_packet_cb(wl, result_ptr);

			result_ptr->done_1 = 0;
			result_ptr->done_2 = 0;

			result_index = (result_index + 1) &
				(FW_TX_CMPLT_BLOCK_SIZE - 1);
			num_complete++;
		} else {
			break;
		}
	}

	queue_len = skb_queue_len(&wl->tx_queue);

	if ((num_complete > 0) && (queue_len > 0)) {
		/* firmware buffer has space, reschedule tx_work */
		wl1251_debug(DEBUG_TX, "tx_complete: reschedule tx_work");
		ieee80211_queue_work(wl->hw, &wl->tx_work);
	}

	if (wl->tx_queue_stopped &&
	    queue_len <= WL1251_TX_QUEUE_LOW_WATERMARK) {
		/* tx_queue has space, restart queues */
		wl1251_debug(DEBUG_TX, "tx_complete: waking queues");
		spin_lock_irqsave(&wl->wl_lock, flags);
		ieee80211_wake_queues(wl->hw);
		wl->tx_queue_stopped = false;
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}

	/* Every completed frame needs to be acknowledged */
	if (num_complete) {
		/*
		 * If we've wrapped, we have to clear
		 * the results in 2 steps.
		 */
		if (result_index > wl->next_tx_complete) {
			/* Only 1 write is needed */
			wl1251_mem_write(wl,
					 wl->data_path->tx_complete_addr +
					 (wl->next_tx_complete *
					  sizeof(struct tx_result)),
					 &result[wl->next_tx_complete],
					 num_complete *
					 sizeof(struct tx_result));


		} else if (result_index < wl->next_tx_complete) {
			/* 2 writes are needed */
			wl1251_mem_write(wl,
					 wl->data_path->tx_complete_addr +
					 (wl->next_tx_complete *
					  sizeof(struct tx_result)),
					 &result[wl->next_tx_complete],
					 (FW_TX_CMPLT_BLOCK_SIZE -
					  wl->next_tx_complete) *
					 sizeof(struct tx_result));

			wl1251_mem_write(wl,
					 wl->data_path->tx_complete_addr,
					 result,
					 (num_complete -
					  FW_TX_CMPLT_BLOCK_SIZE +
					  wl->next_tx_complete) *
					 sizeof(struct tx_result));

		} else {
			/* We have to write the whole array */
			wl1251_mem_write(wl,
					 wl->data_path->tx_complete_addr,
					 result,
					 FW_TX_CMPLT_BLOCK_SIZE *
					 sizeof(struct tx_result));
		}

	}

	kfree(result);
	wl->next_tx_complete = result_index;
}

/* caller must hold wl->mutex */
void wl1251_tx_flush(struct wl1251 *wl)
{
	int i;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;

	/* TX failure */
/* 	control->flags = 0; FIXME */

	while ((skb = skb_dequeue(&wl->tx_queue))) {
		info = IEEE80211_SKB_CB(skb);

		wl1251_debug(DEBUG_TX, "flushing skb 0x%p", skb);

		if (!(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS))
				continue;

		ieee80211_tx_status(wl->hw, skb);
	}

	for (i = 0; i < FW_TX_CMPLT_BLOCK_SIZE; i++)
		if (wl->tx_frames[i] != NULL) {
			skb = wl->tx_frames[i];
			info = IEEE80211_SKB_CB(skb);

			if (!(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS))
				continue;

			ieee80211_tx_status(wl->hw, skb);
			wl->tx_frames[i] = NULL;
		}
}
