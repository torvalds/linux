/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#include <linux/skbuff.h>
#include <linux/gfp.h>
#include <net/mac80211.h>

#include "wl1251.h"
#include "wl1251_reg.h"
#include "wl1251_io.h"
#include "wl1251_rx.h"
#include "wl1251_cmd.h"
#include "wl1251_acx.h"

static void wl1251_rx_header(struct wl1251 *wl,
			     struct wl1251_rx_descriptor *desc)
{
	u32 rx_packet_ring_addr;

	rx_packet_ring_addr = wl->data_path->rx_packet_ring_addr;
	if (wl->rx_current_buffer)
		rx_packet_ring_addr += wl->data_path->rx_packet_ring_chunk_size;

	wl1251_mem_read(wl, rx_packet_ring_addr, desc, sizeof(*desc));
}

static void wl1251_rx_status(struct wl1251 *wl,
			     struct wl1251_rx_descriptor *desc,
			     struct ieee80211_rx_status *status,
			     u8 beacon)
{
	u64 mactime;
	int ret;

	memset(status, 0, sizeof(struct ieee80211_rx_status));

	status->band = IEEE80211_BAND_2GHZ;
	status->mactime = desc->timestamp;

	/*
	 * The rx status timestamp is a 32 bits value while the TSF is a
	 * 64 bits one.
	 * For IBSS merging, TSF is mandatory, so we have to get it
	 * somehow, so we ask for ACX_TSF_INFO.
	 * That could be moved to the get_tsf() hook, but unfortunately,
	 * this one must be atomic, while our SPI routines can sleep.
	 */
	if ((wl->bss_type == BSS_TYPE_IBSS) && beacon) {
		ret = wl1251_acx_tsf_info(wl, &mactime);
		if (ret == 0)
			status->mactime = mactime;
	}

	status->signal = desc->rssi;

	/*
	 * FIXME: guessing that snr needs to be divided by two, otherwise
	 * the values don't make any sense
	 */
	status->noise = desc->rssi - desc->snr / 2;

	status->freq = ieee80211_channel_to_frequency(desc->channel);

	status->flag |= RX_FLAG_TSFT;

	if (desc->flags & RX_DESC_ENCRYPTION_MASK) {
		status->flag |= RX_FLAG_IV_STRIPPED | RX_FLAG_MMIC_STRIPPED;

		if (likely(!(desc->flags & RX_DESC_DECRYPT_FAIL)))
			status->flag |= RX_FLAG_DECRYPTED;

		if (unlikely(desc->flags & RX_DESC_MIC_FAIL))
			status->flag |= RX_FLAG_MMIC_ERROR;
	}

	if (unlikely(!(desc->flags & RX_DESC_VALID_FCS)))
		status->flag |= RX_FLAG_FAILED_FCS_CRC;


	/* FIXME: set status->rate_idx */
}

static void wl1251_rx_body(struct wl1251 *wl,
			   struct wl1251_rx_descriptor *desc)
{
	struct sk_buff *skb;
	struct ieee80211_rx_status status;
	u8 *rx_buffer, beacon = 0;
	u16 length, *fc;
	u32 curr_id, last_id_inc, rx_packet_ring_addr;

	length = WL1251_RX_ALIGN(desc->length  - PLCP_HEADER_LENGTH);
	curr_id = (desc->flags & RX_DESC_SEQNUM_MASK) >> RX_DESC_PACKETID_SHIFT;
	last_id_inc = (wl->rx_last_id + 1) % (RX_MAX_PACKET_ID + 1);

	if (last_id_inc != curr_id) {
		wl1251_warning("curr ID:%d, last ID inc:%d",
			       curr_id, last_id_inc);
		wl->rx_last_id = curr_id;
	} else {
		wl->rx_last_id = last_id_inc;
	}

	rx_packet_ring_addr = wl->data_path->rx_packet_ring_addr +
		sizeof(struct wl1251_rx_descriptor) + 20;
	if (wl->rx_current_buffer)
		rx_packet_ring_addr += wl->data_path->rx_packet_ring_chunk_size;

	skb = __dev_alloc_skb(length, GFP_KERNEL);
	if (!skb) {
		wl1251_error("Couldn't allocate RX frame");
		return;
	}

	rx_buffer = skb_put(skb, length);
	wl1251_mem_read(wl, rx_packet_ring_addr, rx_buffer, length);

	/* The actual lenght doesn't include the target's alignment */
	skb->len = desc->length  - PLCP_HEADER_LENGTH;

	fc = (u16 *)skb->data;

	if ((*fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BEACON)
		beacon = 1;

	wl1251_rx_status(wl, desc, &status, beacon);

	wl1251_debug(DEBUG_RX, "rx skb 0x%p: %d B %s", skb, skb->len,
		     beacon ? "beacon" : "");

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
	ieee80211_rx_ni(wl->hw, skb);
}

static void wl1251_rx_ack(struct wl1251 *wl)
{
	u32 data, addr;

	if (wl->rx_current_buffer) {
		addr = ACX_REG_INTERRUPT_TRIG_H;
		data = INTR_TRIG_RX_PROC1;
	} else {
		addr = ACX_REG_INTERRUPT_TRIG;
		data = INTR_TRIG_RX_PROC0;
	}

	wl1251_reg_write32(wl, addr, data);

	/* Toggle buffer ring */
	wl->rx_current_buffer = !wl->rx_current_buffer;
}


void wl1251_rx(struct wl1251 *wl)
{
	struct wl1251_rx_descriptor *rx_desc;

	if (wl->state != WL1251_STATE_ON)
		return;

	rx_desc = wl->rx_descriptor;

	/* We first read the frame's header */
	wl1251_rx_header(wl, rx_desc);

	/* Now we can read the body */
	wl1251_rx_body(wl, rx_desc);

	/* Finally, we need to ACK the RX */
	wl1251_rx_ack(wl);

	return;
}
