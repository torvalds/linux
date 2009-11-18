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

#include "wl1271.h"
#include "wl1271_spi.h"
#include "wl1271_reg.h"
#include "wl1271_ps.h"
#include "wl1271_tx.h"

static int wl1271_tx_id(struct wl1271 *wl, struct sk_buff *skb)
{
	int i;

	for (i = 0; i < FW_TX_CMPLT_BLOCK_SIZE; i++)
		if (wl->tx_frames[i] == NULL) {
			wl->tx_frames[i] = skb;
			return i;
		}

	return -EBUSY;
}

static int wl1271_tx_allocate(struct wl1271 *wl, struct sk_buff *skb, u32 extra)
{
	struct wl1271_tx_hw_descr *desc;
	u32 total_len = skb->len + sizeof(struct wl1271_tx_hw_descr) + extra;
	u32 total_blocks, excluded;
	int id, ret = -EBUSY;

	/* allocate free identifier for the packet */
	id = wl1271_tx_id(wl, skb);
	if (id < 0)
		return id;

	/* approximate the number of blocks required for this packet
	   in the firmware */
	/* FIXME: try to figure out what is done here and make it cleaner */
	total_blocks = (skb->len) >> TX_HW_BLOCK_SHIFT_DIV;
	excluded = (total_blocks << 2) + (skb->len & 0xff) + 34;
	total_blocks += (excluded > 252) ? 2 : 1;
	total_blocks += TX_HW_BLOCK_SPARE;

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
	} else
		wl->tx_frames[id] = NULL;

	return ret;
}

static int wl1271_tx_fill_hdr(struct wl1271 *wl, struct sk_buff *skb,
			      u32 extra, struct ieee80211_tx_info *control)
{
	struct wl1271_tx_hw_descr *desc;
	int pad;

	desc = (struct wl1271_tx_hw_descr *) skb->data;

	/* configure packet life time */
	desc->start_time = jiffies_to_usecs(jiffies) - wl->time_offset;
	desc->life_time = TX_HW_MGMT_PKT_LIFETIME_TU;

	/* configure the tx attributes */
	desc->tx_attr = wl->session_counter << TX_HW_ATTR_OFST_SESSION_COUNTER;
	/* FIXME: do we know the packet priority? can we identify mgmt
	   packets, and use max prio for them at least? */
	desc->tid = 0;
	desc->aid = TX_HW_DEFAULT_AID;
	desc->reserved = 0;

	/* align the length (and store in terms of words) */
	pad = WL1271_TX_ALIGN(skb->len);
	desc->length = pad >> 2;

	/* calculate number of padding bytes */
	pad = pad - skb->len;
	desc->tx_attr |= pad << TX_HW_ATTR_OFST_LAST_WORD_PAD;

	wl1271_debug(DEBUG_TX, "tx_fill_hdr: pad: %d", pad);
	return 0;
}

static int wl1271_tx_send_packet(struct wl1271 *wl, struct sk_buff *skb,
				 struct ieee80211_tx_info *control)
{

	struct wl1271_tx_hw_descr *desc;
	int len;

	/* FIXME: This is a workaround for getting non-aligned packets.
	   This happens at least with EAPOL packets from the user space.
	   Our DMA requires packets to be aligned on a 4-byte boundary.
	*/
	if (unlikely((long)skb->data & 0x03)) {
		int offset = (4 - (long)skb->data) & 0x03;
		wl1271_debug(DEBUG_TX, "skb offset %d", offset);

		/* check whether the current skb can be used */
		if (!skb_cloned(skb) && (skb_tailroom(skb) >= offset)) {
			unsigned char *src = skb->data;

			/* align the buffer on a 4-byte boundary */
			skb_reserve(skb, offset);
			memmove(skb->data, src, skb->len);
		} else {
			wl1271_info("No handler, fixme!");
			return -EINVAL;
		}
	}

	len = WL1271_TX_ALIGN(skb->len);

	/* perform a fixed address block write with the packet */
	wl1271_spi_reg_write(wl, WL1271_SLV_MEM_DATA, skb->data, len, true);

	/* write packet new counter into the write access register */
	wl->tx_packets_count++;
	wl1271_reg_write32(wl, WL1271_HOST_WR_ACCESS, wl->tx_packets_count);

	desc = (struct wl1271_tx_hw_descr *) skb->data;
	wl1271_debug(DEBUG_TX, "tx id %u skb 0x%p payload %u (%u words)",
		     desc->id, skb, len, desc->length);

	return 0;
}

/* caller must hold wl->mutex */
static int wl1271_tx_frame(struct wl1271 *wl, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info;
	u32 extra = 0;
	int ret = 0;
	u8 idx;

	if (!skb)
		return -EINVAL;

	info = IEEE80211_SKB_CB(skb);

	if (info->control.hw_key &&
	    info->control.hw_key->alg == ALG_TKIP)
		extra = WL1271_TKIP_IV_SPACE;

	if (info->control.hw_key) {
		idx = info->control.hw_key->hw_key_idx;

		/* FIXME: do we have to do this if we're not using WEP? */
		if (unlikely(wl->default_key != idx)) {
			ret = wl1271_cmd_set_default_wep_key(wl, idx);
			if (ret < 0)
				return ret;
		}
	}

	ret = wl1271_tx_allocate(wl, skb, extra);
	if (ret < 0)
		return ret;

	ret = wl1271_tx_fill_hdr(wl, skb, extra, info);
	if (ret < 0)
		return ret;

	ret = wl1271_tx_send_packet(wl, skb, info);
	if (ret < 0)
		return ret;

	return ret;
}

void wl1271_tx_work(struct work_struct *work)
{
	struct wl1271 *wl = container_of(work, struct wl1271, tx_work);
	struct sk_buff *skb;
	bool woken_up = false;
	int ret;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state == WL1271_STATE_OFF))
		goto out;

	while ((skb = skb_dequeue(&wl->tx_queue))) {
		if (!woken_up) {
			ret = wl1271_ps_elp_wakeup(wl, false);
			if (ret < 0)
				goto out;
			woken_up = true;
		}

		ret = wl1271_tx_frame(wl, skb);
		if (ret == -EBUSY) {
			/* firmware buffer is full, stop queues */
			wl1271_debug(DEBUG_TX, "tx_work: fw buffer full, "
				     "stop queues");
			ieee80211_stop_queues(wl->hw);
			wl->tx_queue_stopped = true;
			skb_queue_head(&wl->tx_queue, skb);
			goto out;
		} else if (ret < 0) {
			dev_kfree_skb(skb);
			goto out;
		} else if (wl->tx_queue_stopped) {
			/* firmware buffer has space, restart queues */
			wl1271_debug(DEBUG_TX,
				     "complete_packet: waking queues");
			ieee80211_wake_queues(wl->hw);
			wl->tx_queue_stopped = false;
		}
	}

out:
	if (woken_up)
		wl1271_ps_elp_sleep(wl);

	mutex_unlock(&wl->mutex);
}

static void wl1271_tx_complete_packet(struct wl1271 *wl,
				      struct wl1271_tx_hw_res_descr *result)
{

	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	u32 header_len;
	int id = result->id;

	/* check for id legality */
	if (id >= TX_HW_RESULT_QUEUE_LEN || wl->tx_frames[id] == NULL) {
		wl1271_warning("TX result illegal id: %d", id);
		return;
	}

	skb = wl->tx_frames[id];
	info = IEEE80211_SKB_CB(skb);

	/* update packet status */
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		if (result->status == TX_SUCCESS)
			info->flags |= IEEE80211_TX_STAT_ACK;
		if (result->status & TX_RETRY_EXCEEDED) {
			/* FIXME */
			/* info->status.excessive_retries = 1; */
			wl->stats.excessive_retries++;
		}
	}

	/* FIXME */
	/* info->status.retry_count = result->ack_failures; */
	wl->stats.retry_count += result->ack_failures;

	/* get header len */
	if (info->control.hw_key &&
	    info->control.hw_key->alg == ALG_TKIP)
		header_len = WL1271_TKIP_IV_SPACE +
			sizeof(struct wl1271_tx_hw_descr);
	else
		header_len = sizeof(struct wl1271_tx_hw_descr);

	wl1271_debug(DEBUG_TX, "tx status id %u skb 0x%p failures %u rate 0x%x"
		     " status 0x%x",
		     result->id, skb, result->ack_failures,
		     result->rate_class_index, result->status);

	/* remove private header from packet */
	skb_pull(skb, header_len);

	/* return the packet to the stack */
	ieee80211_tx_status(wl->hw, skb);
	wl->tx_frames[result->id] = NULL;
}

/* Called upon reception of a TX complete interrupt */
void wl1271_tx_complete(struct wl1271 *wl, u32 count)
{
	struct wl1271_acx_mem_map *memmap =
		(struct wl1271_acx_mem_map *)wl->target_mem_map;
	u32 i;

	wl1271_debug(DEBUG_TX, "tx_complete received, packets: %d", count);

	/* read the tx results from the chipset */
	wl1271_spi_mem_read(wl, memmap->tx_result,
			    wl->tx_res_if, sizeof(*wl->tx_res_if));

	/* verify that the result buffer is not getting overrun */
	if (count > TX_HW_RESULT_QUEUE_LEN) {
		wl1271_warning("TX result overflow from chipset: %d", count);
		count = TX_HW_RESULT_QUEUE_LEN;
	}

	/* process the results */
	for (i = 0; i < count; i++) {
		struct wl1271_tx_hw_res_descr *result;
		u8 offset = wl->tx_results_count & TX_HW_RESULT_QUEUE_LEN_MASK;

		/* process the packet */
		result =  &(wl->tx_res_if->tx_results_queue[offset]);
		wl1271_tx_complete_packet(wl, result);

		wl->tx_results_count++;
	}

	/* write host counter to chipset (to ack) */
	wl1271_mem_write32(wl, memmap->tx_result +
			   offsetof(struct wl1271_tx_hw_res_if,
				    tx_result_host_counter),
			   wl->tx_res_if->tx_result_fw_counter);
}

/* caller must hold wl->mutex */
void wl1271_tx_flush(struct wl1271 *wl)
{
	int i;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;

	/* TX failure */
/* 	control->flags = 0; FIXME */

	while ((skb = skb_dequeue(&wl->tx_queue))) {
		info = IEEE80211_SKB_CB(skb);

		wl1271_debug(DEBUG_TX, "flushing skb 0x%p", skb);

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
