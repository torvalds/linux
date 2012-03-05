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
#include <linux/etherdevice.h>

#include "wl12xx.h"
#include "debug.h"
#include "io.h"
#include "reg.h"
#include "ps.h"
#include "tx.h"
#include "event.h"

static int wl1271_set_default_wep_key(struct wl1271 *wl,
				      struct wl12xx_vif *wlvif, u8 id)
{
	int ret;
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);

	if (is_ap)
		ret = wl12xx_cmd_set_default_wep_key(wl, id,
						     wlvif->ap.bcast_hlid);
	else
		ret = wl12xx_cmd_set_default_wep_key(wl, id, wlvif->sta.hlid);

	if (ret < 0)
		return ret;

	wl1271_debug(DEBUG_CRYPT, "default wep key idx: %d", (int)id);
	return 0;
}

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
		if (unlikely(wl->tx_frames_cnt == ACX_TX_DESCRIPTORS))
			clear_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags);

		wl->tx_frames[id] = NULL;
		wl->tx_frames_cnt--;
	}
}

static void wl1271_tx_ap_update_inconnection_sta(struct wl1271 *wl,
						 struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	/*
	 * add the station to the known list before transmitting the
	 * authentication response. this way it won't get de-authed by FW
	 * when transmitting too soon.
	 */
	hdr = (struct ieee80211_hdr *)(skb->data +
				       sizeof(struct wl1271_tx_hw_descr));
	if (ieee80211_is_auth(hdr->frame_control))
		wl1271_acx_set_inconnection_sta(wl, hdr->addr1);
}

static void wl1271_tx_regulate_link(struct wl1271 *wl,
				    struct wl12xx_vif *wlvif,
				    u8 hlid)
{
	bool fw_ps, single_sta;
	u8 tx_pkts;

	if (WARN_ON(!test_bit(hlid, wlvif->links_map)))
		return;

	fw_ps = test_bit(hlid, (unsigned long *)&wl->ap_fw_ps_map);
	tx_pkts = wl->links[hlid].allocated_pkts;
	single_sta = (wl->active_sta_count == 1);

	/*
	 * if in FW PS and there is enough data in FW we can put the link
	 * into high-level PS and clean out its TX queues.
	 * Make an exception if this is the only connected station. In this
	 * case FW-memory congestion is not a problem.
	 */
	if (!single_sta && fw_ps && tx_pkts >= WL1271_PS_STA_MAX_PACKETS)
		wl12xx_ps_link_start(wl, wlvif, hlid, true);
}

bool wl12xx_is_dummy_packet(struct wl1271 *wl, struct sk_buff *skb)
{
	return wl->dummy_packet == skb;
}

u8 wl12xx_tx_get_hlid_ap(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			 struct sk_buff *skb)
{
	struct ieee80211_tx_info *control = IEEE80211_SKB_CB(skb);

	if (control->control.sta) {
		struct wl1271_station *wl_sta;

		wl_sta = (struct wl1271_station *)
				control->control.sta->drv_priv;
		return wl_sta->hlid;
	} else {
		struct ieee80211_hdr *hdr;

		if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags))
			return wl->system_hlid;

		hdr = (struct ieee80211_hdr *)skb->data;
		if (ieee80211_is_mgmt(hdr->frame_control))
			return wlvif->ap.global_hlid;
		else
			return wlvif->ap.bcast_hlid;
	}
}

u8 wl12xx_tx_get_hlid(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		      struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!wlvif || wl12xx_is_dummy_packet(wl, skb))
		return wl->system_hlid;

	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		return wl12xx_tx_get_hlid_ap(wl, wlvif, skb);

	if ((test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags) ||
	     test_bit(WLVIF_FLAG_IBSS_JOINED, &wlvif->flags)) &&
	    !ieee80211_is_auth(hdr->frame_control) &&
	    !ieee80211_is_assoc_req(hdr->frame_control))
		return wlvif->sta.hlid;
	else
		return wlvif->dev_hlid;
}

static unsigned int wl12xx_calc_packet_alignment(struct wl1271 *wl,
						unsigned int packet_length)
{
	if (wl->quirks & WL12XX_QUIRK_NO_BLOCKSIZE_ALIGNMENT)
		return ALIGN(packet_length, WL1271_TX_ALIGN_TO);
	else
		return ALIGN(packet_length, WL12XX_BUS_BLOCK_SIZE);
}

static int wl1271_tx_allocate(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			      struct sk_buff *skb, u32 extra, u32 buf_offset,
			      u8 hlid)
{
	struct wl1271_tx_hw_descr *desc;
	u32 total_len = skb->len + sizeof(struct wl1271_tx_hw_descr) + extra;
	u32 len;
	u32 total_blocks;
	int id, ret = -EBUSY, ac;
	u32 spare_blocks = wl->tx_spare_blocks;
	bool is_dummy = false;

	if (buf_offset + total_len > WL1271_AGGR_BUFFER_SIZE)
		return -EAGAIN;

	/* allocate free identifier for the packet */
	id = wl1271_alloc_tx_id(wl, skb);
	if (id < 0)
		return id;

	/* approximate the number of blocks required for this packet
	   in the firmware */
	len = wl12xx_calc_packet_alignment(wl, total_len);

	/* in case of a dummy packet, use default amount of spare mem blocks */
	if (unlikely(wl12xx_is_dummy_packet(wl, skb))) {
		is_dummy = true;
		spare_blocks = TX_HW_BLOCK_SPARE_DEFAULT;
	}

	total_blocks = (len + TX_HW_BLOCK_SIZE - 1) / TX_HW_BLOCK_SIZE +
		spare_blocks;

	if (total_blocks <= wl->tx_blocks_available) {
		desc = (struct wl1271_tx_hw_descr *)skb_push(
			skb, total_len - skb->len);

		/* HW descriptor fields change between wl127x and wl128x */
		if (wl->chip.id == CHIP_ID_1283_PG20) {
			desc->wl128x_mem.total_mem_blocks = total_blocks;
		} else {
			desc->wl127x_mem.extra_blocks = spare_blocks;
			desc->wl127x_mem.total_mem_blocks = total_blocks;
		}

		desc->id = id;

		wl->tx_blocks_available -= total_blocks;
		wl->tx_allocated_blocks += total_blocks;

		ac = wl1271_tx_get_queue(skb_get_queue_mapping(skb));
		wl->tx_allocated_pkts[ac]++;

		if (!is_dummy && wlvif &&
		    wlvif->bss_type == BSS_TYPE_AP_BSS &&
		    test_bit(hlid, wlvif->ap.sta_hlid_map))
			wl->links[hlid].allocated_pkts++;

		ret = 0;

		wl1271_debug(DEBUG_TX,
			     "tx_allocate: size: %d, blocks: %d, id: %d",
			     total_len, total_blocks, id);
	} else {
		wl1271_free_tx_id(wl, id);
	}

	return ret;
}

static void wl1271_tx_fill_hdr(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			       struct sk_buff *skb, u32 extra,
			       struct ieee80211_tx_info *control, u8 hlid)
{
	struct timespec ts;
	struct wl1271_tx_hw_descr *desc;
	int aligned_len, ac, rate_idx;
	s64 hosttime;
	u16 tx_attr = 0;
	__le16 frame_control;
	struct ieee80211_hdr *hdr;
	u8 *frame_start;
	bool is_dummy;

	desc = (struct wl1271_tx_hw_descr *) skb->data;
	frame_start = (u8 *)(desc + 1);
	hdr = (struct ieee80211_hdr *)(frame_start + extra);
	frame_control = hdr->frame_control;

	/* relocate space for security header */
	if (extra) {
		int hdrlen = ieee80211_hdrlen(frame_control);
		memmove(frame_start, hdr, hdrlen);
	}

	/* configure packet life time */
	getnstimeofday(&ts);
	hosttime = (timespec_to_ns(&ts) >> 10);
	desc->start_time = cpu_to_le32(hosttime - wl->time_offset);

	is_dummy = wl12xx_is_dummy_packet(wl, skb);
	if (is_dummy || !wlvif || wlvif->bss_type != BSS_TYPE_AP_BSS)
		desc->life_time = cpu_to_le16(TX_HW_MGMT_PKT_LIFETIME_TU);
	else
		desc->life_time = cpu_to_le16(TX_HW_AP_MODE_PKT_LIFETIME_TU);

	/* queue */
	ac = wl1271_tx_get_queue(skb_get_queue_mapping(skb));
	desc->tid = skb->priority;

	if (is_dummy) {
		/*
		 * FW expects the dummy packet to have an invalid session id -
		 * any session id that is different than the one set in the join
		 */
		tx_attr = (SESSION_COUNTER_INVALID <<
			   TX_HW_ATTR_OFST_SESSION_COUNTER) &
			   TX_HW_ATTR_SESSION_COUNTER;

		tx_attr |= TX_HW_ATTR_TX_DUMMY_REQ;
	} else if (wlvif) {
		/* configure the tx attributes */
		tx_attr = wlvif->session_counter <<
			  TX_HW_ATTR_OFST_SESSION_COUNTER;
	}

	desc->hlid = hlid;
	if (is_dummy || !wlvif)
		rate_idx = 0;
	else if (wlvif->bss_type != BSS_TYPE_AP_BSS) {
		/* if the packets are destined for AP (have a STA entry)
		   send them with AP rate policies, otherwise use default
		   basic rates */
		if (control->flags & IEEE80211_TX_CTL_NO_CCK_RATE)
			rate_idx = wlvif->sta.p2p_rate_idx;
		else if (control->control.sta)
			rate_idx = wlvif->sta.ap_rate_idx;
		else
			rate_idx = wlvif->sta.basic_rate_idx;
	} else {
		if (hlid == wlvif->ap.global_hlid)
			rate_idx = wlvif->ap.mgmt_rate_idx;
		else if (hlid == wlvif->ap.bcast_hlid)
			rate_idx = wlvif->ap.bcast_rate_idx;
		else
			rate_idx = wlvif->ap.ucast_rate_idx[ac];
	}

	tx_attr |= rate_idx << TX_HW_ATTR_OFST_RATE_POLICY;
	desc->reserved = 0;

	aligned_len = wl12xx_calc_packet_alignment(wl, skb->len);

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		desc->wl128x_mem.extra_bytes = aligned_len - skb->len;
		desc->length = cpu_to_le16(aligned_len >> 2);

		wl1271_debug(DEBUG_TX, "tx_fill_hdr: hlid: %d "
			     "tx_attr: 0x%x len: %d life: %d mem: %d",
			     desc->hlid, tx_attr,
			     le16_to_cpu(desc->length),
			     le16_to_cpu(desc->life_time),
			     desc->wl128x_mem.total_mem_blocks);
	} else {
		int pad;

		/* Store the aligned length in terms of words */
		desc->length = cpu_to_le16(aligned_len >> 2);

		/* calculate number of padding bytes */
		pad = aligned_len - skb->len;
		tx_attr |= pad << TX_HW_ATTR_OFST_LAST_WORD_PAD;

		wl1271_debug(DEBUG_TX, "tx_fill_hdr: pad: %d hlid: %d "
			     "tx_attr: 0x%x len: %d life: %d mem: %d", pad,
			     desc->hlid, tx_attr,
			     le16_to_cpu(desc->length),
			     le16_to_cpu(desc->life_time),
			     desc->wl127x_mem.total_mem_blocks);
	}

	/* for WEP shared auth - no fw encryption is needed */
	if (ieee80211_is_auth(frame_control) &&
	    ieee80211_has_protected(frame_control))
		tx_attr |= TX_HW_ATTR_HOST_ENCRYPT;

	desc->tx_attr = cpu_to_le16(tx_attr);
}

/* caller must hold wl->mutex */
static int wl1271_prepare_tx_frame(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				   struct sk_buff *skb, u32 buf_offset)
{
	struct ieee80211_tx_info *info;
	u32 extra = 0;
	int ret = 0;
	u32 total_len;
	u8 hlid;
	bool is_dummy;

	if (!skb)
		return -EINVAL;

	info = IEEE80211_SKB_CB(skb);

	/* TODO: handle dummy packets on multi-vifs */
	is_dummy = wl12xx_is_dummy_packet(wl, skb);

	if (info->control.hw_key &&
	    info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP)
		extra = WL1271_EXTRA_SPACE_TKIP;

	if (info->control.hw_key) {
		bool is_wep;
		u8 idx = info->control.hw_key->hw_key_idx;
		u32 cipher = info->control.hw_key->cipher;

		is_wep = (cipher == WLAN_CIPHER_SUITE_WEP40) ||
			 (cipher == WLAN_CIPHER_SUITE_WEP104);

		if (unlikely(is_wep && wlvif->default_key != idx)) {
			ret = wl1271_set_default_wep_key(wl, wlvif, idx);
			if (ret < 0)
				return ret;
			wlvif->default_key = idx;
		}
	}
	hlid = wl12xx_tx_get_hlid(wl, wlvif, skb);
	if (hlid == WL12XX_INVALID_LINK_ID) {
		wl1271_error("invalid hlid. dropping skb 0x%p", skb);
		return -EINVAL;
	}

	ret = wl1271_tx_allocate(wl, wlvif, skb, extra, buf_offset, hlid);
	if (ret < 0)
		return ret;

	wl1271_tx_fill_hdr(wl, wlvif, skb, extra, info, hlid);

	if (!is_dummy && wlvif && wlvif->bss_type == BSS_TYPE_AP_BSS) {
		wl1271_tx_ap_update_inconnection_sta(wl, skb);
		wl1271_tx_regulate_link(wl, wlvif, hlid);
	}

	/*
	 * The length of each packet is stored in terms of
	 * words. Thus, we must pad the skb data to make sure its
	 * length is aligned.  The number of padding bytes is computed
	 * and set in wl1271_tx_fill_hdr.
	 * In special cases, we want to align to a specific block size
	 * (eg. for wl128x with SDIO we align to 256).
	 */
	total_len = wl12xx_calc_packet_alignment(wl, skb->len);

	memcpy(wl->aggr_buf + buf_offset, skb->data, skb->len);
	memset(wl->aggr_buf + buf_offset + skb->len, 0, total_len - skb->len);

	/* Revert side effects in the dummy packet skb, so it can be reused */
	if (is_dummy)
		skb_pull(skb, sizeof(struct wl1271_tx_hw_descr));

	return total_len;
}

u32 wl1271_tx_enabled_rates_get(struct wl1271 *wl, u32 rate_set,
				enum ieee80211_band rate_band)
{
	struct ieee80211_supported_band *band;
	u32 enabled_rates = 0;
	int bit;

	band = wl->hw->wiphy->bands[rate_band];
	for (bit = 0; bit < band->n_bitrates; bit++) {
		if (rate_set & 0x1)
			enabled_rates |= band->bitrates[bit].hw_value;
		rate_set >>= 1;
	}

	/* MCS rates indication are on bits 16 - 23 */
	rate_set >>= HW_HT_RATES_OFFSET - band->n_bitrates;

	for (bit = 0; bit < 8; bit++) {
		if (rate_set & 0x1)
			enabled_rates |= (CONF_HW_BIT_RATE_MCS_0 << bit);
		rate_set >>= 1;
	}

	return enabled_rates;
}

void wl1271_handle_tx_low_watermark(struct wl1271 *wl)
{
	unsigned long flags;
	int i;

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		if (test_bit(i, &wl->stopped_queues_map) &&
		    wl->tx_queue_count[i] <= WL1271_TX_QUEUE_LOW_WATERMARK) {
			/* firmware buffer has space, restart queues */
			spin_lock_irqsave(&wl->wl_lock, flags);
			ieee80211_wake_queue(wl->hw,
					     wl1271_tx_get_mac80211_queue(i));
			clear_bit(i, &wl->stopped_queues_map);
			spin_unlock_irqrestore(&wl->wl_lock, flags);
		}
	}
}

static struct sk_buff_head *wl1271_select_queue(struct wl1271 *wl,
						struct sk_buff_head *queues)
{
	int i, q = -1, ac;
	u32 min_pkts = 0xffffffff;

	/*
	 * Find a non-empty ac where:
	 * 1. There are packets to transmit
	 * 2. The FW has the least allocated blocks
	 *
	 * We prioritize the ACs according to VO>VI>BE>BK
	 */
	for (i = 0; i < NUM_TX_QUEUES; i++) {
		ac = wl1271_tx_get_queue(i);
		if (!skb_queue_empty(&queues[ac]) &&
		    (wl->tx_allocated_pkts[ac] < min_pkts)) {
			q = ac;
			min_pkts = wl->tx_allocated_pkts[q];
		}
	}

	if (q == -1)
		return NULL;

	return &queues[q];
}

static struct sk_buff *wl12xx_lnk_skb_dequeue(struct wl1271 *wl,
					      struct wl1271_link *lnk)
{
	struct sk_buff *skb;
	unsigned long flags;
	struct sk_buff_head *queue;

	queue = wl1271_select_queue(wl, lnk->tx_queue);
	if (!queue)
		return NULL;

	skb = skb_dequeue(queue);
	if (skb) {
		int q = wl1271_tx_get_queue(skb_get_queue_mapping(skb));
		spin_lock_irqsave(&wl->wl_lock, flags);
		wl->tx_queue_count[q]--;
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}

	return skb;
}

static struct sk_buff *wl12xx_vif_skb_dequeue(struct wl1271 *wl,
					      struct wl12xx_vif *wlvif)
{
	struct sk_buff *skb = NULL;
	int i, h, start_hlid;

	/* start from the link after the last one */
	start_hlid = (wlvif->last_tx_hlid + 1) % WL12XX_MAX_LINKS;

	/* dequeue according to AC, round robin on each link */
	for (i = 0; i < WL12XX_MAX_LINKS; i++) {
		h = (start_hlid + i) % WL12XX_MAX_LINKS;

		/* only consider connected stations */
		if (!test_bit(h, wlvif->links_map))
			continue;

		skb = wl12xx_lnk_skb_dequeue(wl, &wl->links[h]);
		if (!skb)
			continue;

		wlvif->last_tx_hlid = h;
		break;
	}

	if (!skb)
		wlvif->last_tx_hlid = 0;

	return skb;
}

static struct sk_buff *wl1271_skb_dequeue(struct wl1271 *wl)
{
	unsigned long flags;
	struct wl12xx_vif *wlvif = wl->last_wlvif;
	struct sk_buff *skb = NULL;

	if (wlvif) {
		wl12xx_for_each_wlvif_continue(wl, wlvif) {
			skb = wl12xx_vif_skb_dequeue(wl, wlvif);
			if (skb) {
				wl->last_wlvif = wlvif;
				break;
			}
		}
	}

	/* do another pass */
	if (!skb) {
		wl12xx_for_each_wlvif(wl, wlvif) {
			skb = wl12xx_vif_skb_dequeue(wl, wlvif);
			if (skb) {
				wl->last_wlvif = wlvif;
				break;
			}
		}
	}

	if (!skb)
		skb = wl12xx_lnk_skb_dequeue(wl, &wl->links[wl->system_hlid]);

	if (!skb &&
	    test_and_clear_bit(WL1271_FLAG_DUMMY_PACKET_PENDING, &wl->flags)) {
		int q;

		skb = wl->dummy_packet;
		q = wl1271_tx_get_queue(skb_get_queue_mapping(skb));
		spin_lock_irqsave(&wl->wl_lock, flags);
		wl->tx_queue_count[q]--;
		spin_unlock_irqrestore(&wl->wl_lock, flags);
	}

	return skb;
}

static void wl1271_skb_queue_head(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				  struct sk_buff *skb)
{
	unsigned long flags;
	int q = wl1271_tx_get_queue(skb_get_queue_mapping(skb));

	if (wl12xx_is_dummy_packet(wl, skb)) {
		set_bit(WL1271_FLAG_DUMMY_PACKET_PENDING, &wl->flags);
	} else {
		u8 hlid = wl12xx_tx_get_hlid(wl, wlvif, skb);
		skb_queue_head(&wl->links[hlid].tx_queue[q], skb);

		/* make sure we dequeue the same packet next time */
		wlvif->last_tx_hlid = (hlid + WL12XX_MAX_LINKS - 1) %
				      WL12XX_MAX_LINKS;
	}

	spin_lock_irqsave(&wl->wl_lock, flags);
	wl->tx_queue_count[q]++;
	spin_unlock_irqrestore(&wl->wl_lock, flags);
}

static bool wl1271_tx_is_data_present(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);

	return ieee80211_is_data_present(hdr->frame_control);
}

void wl12xx_rearm_rx_streaming(struct wl1271 *wl, unsigned long *active_hlids)
{
	struct wl12xx_vif *wlvif;
	u32 timeout;
	u8 hlid;

	if (!wl->conf.rx_streaming.interval)
		return;

	if (!wl->conf.rx_streaming.always &&
	    !test_bit(WL1271_FLAG_SOFT_GEMINI, &wl->flags))
		return;

	timeout = wl->conf.rx_streaming.duration;
	wl12xx_for_each_wlvif_sta(wl, wlvif) {
		bool found = false;
		for_each_set_bit(hlid, active_hlids, WL12XX_MAX_LINKS) {
			if (test_bit(hlid, wlvif->links_map)) {
				found  = true;
				break;
			}
		}

		if (!found)
			continue;

		/* enable rx streaming */
		if (!test_bit(WLVIF_FLAG_RX_STREAMING_STARTED, &wlvif->flags))
			ieee80211_queue_work(wl->hw,
					     &wlvif->rx_streaming_enable_work);

		mod_timer(&wlvif->rx_streaming_timer,
			  jiffies + msecs_to_jiffies(timeout));
	}
}

void wl1271_tx_work_locked(struct wl1271 *wl)
{
	struct wl12xx_vif *wlvif;
	struct sk_buff *skb;
	struct wl1271_tx_hw_descr *desc;
	u32 buf_offset = 0;
	bool sent_packets = false;
	unsigned long active_hlids[BITS_TO_LONGS(WL12XX_MAX_LINKS)] = {0};
	int ret;

	if (unlikely(wl->state == WL1271_STATE_OFF))
		return;

	while ((skb = wl1271_skb_dequeue(wl))) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		bool has_data = false;

		wlvif = NULL;
		if (!wl12xx_is_dummy_packet(wl, skb) && info->control.vif)
			wlvif = wl12xx_vif_to_data(info->control.vif);

		has_data = wlvif && wl1271_tx_is_data_present(skb);
		ret = wl1271_prepare_tx_frame(wl, wlvif, skb, buf_offset);
		if (ret == -EAGAIN) {
			/*
			 * Aggregation buffer is full.
			 * Flush buffer and try again.
			 */
			wl1271_skb_queue_head(wl, wlvif, skb);
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
			wl1271_skb_queue_head(wl, wlvif, skb);
			/* No work left, avoid scheduling redundant tx work */
			set_bit(WL1271_FLAG_FW_TX_BUSY, &wl->flags);
			goto out_ack;
		} else if (ret < 0) {
			if (wl12xx_is_dummy_packet(wl, skb))
				/*
				 * fw still expects dummy packet,
				 * so re-enqueue it
				 */
				wl1271_skb_queue_head(wl, wlvif, skb);
			else
				ieee80211_free_txskb(wl->hw, skb);
			goto out_ack;
		}
		buf_offset += ret;
		wl->tx_packets_count++;
		if (has_data) {
			desc = (struct wl1271_tx_hw_descr *) skb->data;
			__set_bit(desc->hlid, active_hlids);
		}
	}

out_ack:
	if (buf_offset) {
		wl1271_write(wl, WL1271_SLV_MEM_DATA, wl->aggr_buf,
				buf_offset, true);
		sent_packets = true;
	}
	if (sent_packets) {
		/*
		 * Interrupt the firmware with the new packets. This is only
		 * required for older hardware revisions
		 */
		if (wl->quirks & WL12XX_QUIRK_END_OF_TRANSACTION)
			wl1271_write32(wl, WL1271_HOST_WR_ACCESS,
				       wl->tx_packets_count);

		wl1271_handle_tx_low_watermark(wl);
	}
	wl12xx_rearm_rx_streaming(wl, active_hlids);
}

void wl1271_tx_work(struct work_struct *work)
{
	struct wl1271 *wl = container_of(work, struct wl1271, tx_work);
	int ret;

	mutex_lock(&wl->mutex);
	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1271_tx_work_locked(wl);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
}

static u8 wl1271_tx_get_rate_flags(u8 rate_class_index)
{
	u8 flags = 0;

	if (rate_class_index >= CONF_HW_RXTX_RATE_MCS_MIN &&
	    rate_class_index <= CONF_HW_RXTX_RATE_MCS_MAX)
		flags |= IEEE80211_TX_RC_MCS;
	if (rate_class_index == CONF_HW_RXTX_RATE_MCS7_SGI)
		flags |= IEEE80211_TX_RC_SHORT_GI;
	return flags;
}

static void wl1271_tx_complete_packet(struct wl1271 *wl,
				      struct wl1271_tx_hw_res_descr *result)
{
	struct ieee80211_tx_info *info;
	struct ieee80211_vif *vif;
	struct wl12xx_vif *wlvif;
	struct sk_buff *skb;
	int id = result->id;
	int rate = -1;
	u8 rate_flags = 0;
	u8 retries = 0;

	/* check for id legality */
	if (unlikely(id >= ACX_TX_DESCRIPTORS || wl->tx_frames[id] == NULL)) {
		wl1271_warning("TX result illegal id: %d", id);
		return;
	}

	skb = wl->tx_frames[id];
	info = IEEE80211_SKB_CB(skb);

	if (wl12xx_is_dummy_packet(wl, skb)) {
		wl1271_free_tx_id(wl, id);
		return;
	}

	/* info->control is valid as long as we don't update info->status */
	vif = info->control.vif;
	wlvif = wl12xx_vif_to_data(vif);

	/* update the TX status info */
	if (result->status == TX_SUCCESS) {
		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
			info->flags |= IEEE80211_TX_STAT_ACK;
		rate = wl1271_rate_to_idx(result->rate_class_index,
					  wlvif->band);
		rate_flags = wl1271_tx_get_rate_flags(result->rate_class_index);
		retries = result->ack_failures;
	} else if (result->status == TX_RETRY_EXCEEDED) {
		wl->stats.excessive_retries++;
		retries = result->ack_failures;
	}

	info->status.rates[0].idx = rate;
	info->status.rates[0].count = retries;
	info->status.rates[0].flags = rate_flags;
	info->status.ack_signal = -1;

	wl->stats.retry_count += result->ack_failures;

	/*
	 * update sequence number only when relevant, i.e. only in
	 * sessions of TKIP, AES and GEM (not in open or WEP sessions)
	 */
	if (info->control.hw_key &&
	    (info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     info->control.hw_key->cipher == WLAN_CIPHER_SUITE_CCMP ||
	     info->control.hw_key->cipher == WL1271_CIPHER_SUITE_GEM)) {
		u8 fw_lsb = result->tx_security_sequence_number_lsb;
		u8 cur_lsb = wlvif->tx_security_last_seq_lsb;

		/*
		 * update security sequence number, taking care of potential
		 * wrap-around
		 */
		wlvif->tx_security_seq += (fw_lsb - cur_lsb) & 0xff;
		wlvif->tx_security_last_seq_lsb = fw_lsb;
	}

	/* remove private header from packet */
	skb_pull(skb, sizeof(struct wl1271_tx_hw_descr));

	/* remove TKIP header space if present */
	if (info->control.hw_key &&
	    info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		int hdrlen = ieee80211_get_hdrlen_from_skb(skb);
		memmove(skb->data + WL1271_EXTRA_SPACE_TKIP, skb->data,
			hdrlen);
		skb_pull(skb, WL1271_EXTRA_SPACE_TKIP);
	}

	wl1271_debug(DEBUG_TX, "tx status id %u skb 0x%p failures %u rate 0x%x"
		     " status 0x%x",
		     result->id, skb, result->ack_failures,
		     result->rate_class_index, result->status);

	/* return the packet to the stack */
	skb_queue_tail(&wl->deferred_tx_queue, skb);
	queue_work(wl->freezable_wq, &wl->netstack_work);
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

void wl1271_tx_reset_link_queues(struct wl1271 *wl, u8 hlid)
{
	struct sk_buff *skb;
	int i;
	unsigned long flags;
	struct ieee80211_tx_info *info;
	int total[NUM_TX_QUEUES];

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		total[i] = 0;
		while ((skb = skb_dequeue(&wl->links[hlid].tx_queue[i]))) {
			wl1271_debug(DEBUG_TX, "link freeing skb 0x%p", skb);

			if (!wl12xx_is_dummy_packet(wl, skb)) {
				info = IEEE80211_SKB_CB(skb);
				info->status.rates[0].idx = -1;
				info->status.rates[0].count = 0;
				ieee80211_tx_status_ni(wl->hw, skb);
			}

			total[i]++;
		}
	}

	spin_lock_irqsave(&wl->wl_lock, flags);
	for (i = 0; i < NUM_TX_QUEUES; i++)
		wl->tx_queue_count[i] -= total[i];
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	wl1271_handle_tx_low_watermark(wl);
}

/* caller must hold wl->mutex and TX must be stopped */
void wl12xx_tx_reset_wlvif(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	int i;

	/* TX failure */
	for_each_set_bit(i, wlvif->links_map, WL12XX_MAX_LINKS) {
		if (wlvif->bss_type == BSS_TYPE_AP_BSS)
			wl1271_free_sta(wl, wlvif, i);
		else
			wlvif->sta.ba_rx_bitmap = 0;

		wl1271_tx_reset_link_queues(wl, i);
		wl->links[i].allocated_pkts = 0;
		wl->links[i].prev_freed_pkts = 0;
	}
	wlvif->last_tx_hlid = 0;

}
/* caller must hold wl->mutex and TX must be stopped */
void wl12xx_tx_reset(struct wl1271 *wl, bool reset_tx_queues)
{
	int i;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;

	for (i = 0; i < NUM_TX_QUEUES; i++)
		wl->tx_queue_count[i] = 0;

	wl->stopped_queues_map = 0;

	/*
	 * Make sure the driver is at a consistent state, in case this
	 * function is called from a context other than interface removal.
	 * This call will always wake the TX queues.
	 */
	if (reset_tx_queues)
		wl1271_handle_tx_low_watermark(wl);

	for (i = 0; i < ACX_TX_DESCRIPTORS; i++) {
		if (wl->tx_frames[i] == NULL)
			continue;

		skb = wl->tx_frames[i];
		wl1271_free_tx_id(wl, i);
		wl1271_debug(DEBUG_TX, "freeing skb 0x%p", skb);

		if (!wl12xx_is_dummy_packet(wl, skb)) {
			/*
			 * Remove private headers before passing the skb to
			 * mac80211
			 */
			info = IEEE80211_SKB_CB(skb);
			skb_pull(skb, sizeof(struct wl1271_tx_hw_descr));
			if (info->control.hw_key &&
			    info->control.hw_key->cipher ==
			    WLAN_CIPHER_SUITE_TKIP) {
				int hdrlen = ieee80211_get_hdrlen_from_skb(skb);
				memmove(skb->data + WL1271_EXTRA_SPACE_TKIP,
					skb->data, hdrlen);
				skb_pull(skb, WL1271_EXTRA_SPACE_TKIP);
			}

			info->status.rates[0].idx = -1;
			info->status.rates[0].count = 0;

			ieee80211_tx_status_ni(wl->hw, skb);
		}
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
		wl1271_debug(DEBUG_TX, "flushing tx buffer: %d %d",
			     wl->tx_frames_cnt,
			     wl1271_tx_total_queue_count(wl));
		if ((wl->tx_frames_cnt == 0) &&
		    (wl1271_tx_total_queue_count(wl) == 0)) {
			mutex_unlock(&wl->mutex);
			return;
		}
		mutex_unlock(&wl->mutex);
		msleep(1);
	}

	wl1271_warning("Unable to flush all TX buffers, timed out.");
}

u32 wl1271_tx_min_rate_get(struct wl1271 *wl, u32 rate_set)
{
	if (WARN_ON(!rate_set))
		return 0;

	return BIT(__ffs(rate_set));
}
