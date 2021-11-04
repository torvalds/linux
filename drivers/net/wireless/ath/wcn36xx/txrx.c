/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "txrx.h"

static inline int get_rssi0(struct wcn36xx_rx_bd *bd)
{
	return 100 - ((bd->phy_stat0 >> 24) & 0xff);
}

struct wcn36xx_rate {
	u16 bitrate;
	u16 mcs_or_legacy_index;
	enum mac80211_rx_encoding encoding;
	enum mac80211_rx_encoding_flags encoding_flags;
	enum rate_info_bw bw;
};

/* Buffer descriptor rx_ch field is limited to 5-bit (4+1), a mapping is used
 * for 11A Channels.
 */
static const u8 ab_rx_ch_map[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104,
				   108, 112, 116, 120, 124, 128, 132, 136, 140,
				   149, 153, 157, 161, 165, 144 };

static const struct wcn36xx_rate wcn36xx_rate_table[] = {
	/* 11b rates */
	{  10, 0, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{  20, 1, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{  55, 2, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 110, 3, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },

	/* 11b SP (short preamble) */
	{  10, 0, RX_ENC_LEGACY, RX_ENC_FLAG_SHORTPRE, RATE_INFO_BW_20 },
	{  20, 1, RX_ENC_LEGACY, RX_ENC_FLAG_SHORTPRE, RATE_INFO_BW_20 },
	{  55, 2, RX_ENC_LEGACY, RX_ENC_FLAG_SHORTPRE, RATE_INFO_BW_20 },
	{ 110, 3, RX_ENC_LEGACY, RX_ENC_FLAG_SHORTPRE, RATE_INFO_BW_20 },

	/* 11ag */
	{  60, 4, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{  90, 5, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 120, 6, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 180, 7, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 240, 8, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 360, 9, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 480, 10, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },
	{ 540, 11, RX_ENC_LEGACY, 0, RATE_INFO_BW_20 },

	/* 11n */
	{  65, 0, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 130, 1, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 195, 2, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 260, 3, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 390, 4, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 520, 5, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 585, 6, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{ 650, 7, RX_ENC_HT, 0, RATE_INFO_BW_20 },

	/* 11n SGI */
	{  72, 0, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 144, 1, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 217, 2, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 289, 3, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 434, 4, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 578, 5, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 650, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{ 722, 7, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },

	/* 11n GF (greenfield) */
	{  65, 0, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 130, 1, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 195, 2, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 260, 3, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 390, 4, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 520, 5, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 585, 6, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },
	{ 650, 7, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_20 },

	/* 11n CB (channel bonding) */
	{ 135, 0, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 270, 1, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 405, 2, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 540, 3, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 810, 4, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1080, 5, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1215, 6, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1350, 7, RX_ENC_HT, 0, RATE_INFO_BW_40 },

	/* 11n CB + SGI */
	{ 150, 0, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 300, 1, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 450, 2, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 600, 3, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 900, 4, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1200, 5, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1500, 7, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11n GF + CB */
	{ 135, 0, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 270, 1, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 405, 2, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 540, 3, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 810, 4, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 1080, 5, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 1215, 6, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },
	{ 1350, 7, RX_ENC_HT, RX_ENC_FLAG_HT_GF, RATE_INFO_BW_40 },

	/* 11ac reserved indices */
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 20 MHz 800ns GI MCS 0-8 */
	{   65, 0, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  130, 1, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  195, 2, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  260, 3, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  390, 4, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  520, 5, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  585, 6, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  650, 7, RX_ENC_HT, 0, RATE_INFO_BW_20 },
	{  780, 8, RX_ENC_HT, 0, RATE_INFO_BW_20 },

	/* 11ac reserved indices */
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 20 MHz 400ns SGI MCS 6-8 */
	{  655, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{  722, 7, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },
	{  866, 8, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_20 },

	/* 11ac reserved indices */
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 40 MHz 800ns GI MCS 0-9 */
	{  135, 0, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{  270, 1, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{  405, 2, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{  540, 3, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{  810, 4, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1080, 5, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1215, 6, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1350, 7, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1350, 7, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1620, 8, RX_ENC_HT, 0, RATE_INFO_BW_40 },
	{ 1800, 9, RX_ENC_HT, 0, RATE_INFO_BW_40 },

	/* 11ac reserved indices */
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 40 MHz 400ns SGI MCS 5-7 */
	{ 1200, 5, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1500, 7, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac reserved index */
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 40 MHz 400ns SGI MCS 5-7 */
	{ 1800, 8, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 2000, 9, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac reserved index */
	{ 1350, 6, RX_ENC_HT,  RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 80 MHz 800ns GI MCS 0-7 */
	{  292, 0, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{  585, 1, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{  877, 2, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{ 1170, 3, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{ 1755, 4, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{ 2340, 5, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{ 2632, 6, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{ 2925, 7, RX_ENC_HT, 0, RATE_INFO_BW_80},

	/* 11 ac reserved index */
	{ 1350, 6, RX_ENC_HT,  RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 80 MHz 800 ns GI MCS 8-9 */
	{ 3510, 8, RX_ENC_HT, 0, RATE_INFO_BW_80},
	{ 3900, 9, RX_ENC_HT, 0, RATE_INFO_BW_80},

	/* 11 ac reserved indices */
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },
	{ 1350, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 80 MHz 400 ns SGI MCS 6-7 */
	{ 2925, 6, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_80 },
	{ 3250, 7, RX_ENC_HT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_80 },

	/* 11ac reserved index */
	{ 1350, 6, RX_ENC_HT,  RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_40 },

	/* 11ac 80 MHz 400ns SGI MCS 8-9 */
	{ 3900, 8, RX_ENC_VHT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_80 },
	{ 4333, 9, RX_ENC_VHT, RX_ENC_FLAG_SHORT_GI, RATE_INFO_BW_80 },
};

static struct sk_buff *wcn36xx_unchain_msdu(struct sk_buff_head *amsdu)
{
	struct sk_buff *skb, *first;
	int total_len = 0;
	int space;

	first = __skb_dequeue(amsdu);

	skb_queue_walk(amsdu, skb)
		total_len += skb->len;

	space = total_len - skb_tailroom(first);
	if (space > 0 && pskb_expand_head(first, 0, space, GFP_ATOMIC) < 0) {
		__skb_queue_head(amsdu, first);
		return NULL;
	}

	/* Walk list again, copying contents into msdu_head */
	while ((skb = __skb_dequeue(amsdu))) {
		skb_copy_from_linear_data(skb, skb_put(first, skb->len),
					  skb->len);
		dev_kfree_skb_irq(skb);
	}

	return first;
}

static void __skb_queue_purge_irq(struct sk_buff_head *list)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(list)) != NULL)
		dev_kfree_skb_irq(skb);
}

int wcn36xx_rx_skb(struct wcn36xx *wcn, struct sk_buff *skb)
{
	struct ieee80211_rx_status status;
	const struct wcn36xx_rate *rate;
	struct ieee80211_hdr *hdr;
	struct wcn36xx_rx_bd *bd;
	u16 fc, sn;

	/*
	 * All fields must be 0, otherwise it can lead to
	 * unexpected consequences.
	 */
	memset(&status, 0, sizeof(status));

	bd = (struct wcn36xx_rx_bd *)skb->data;
	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));
	wcn36xx_dbg_dump(WCN36XX_DBG_RX_DUMP,
			 "BD   <<< ", (char *)bd,
			 sizeof(struct wcn36xx_rx_bd));

	if (bd->pdu.mpdu_data_off <= bd->pdu.mpdu_header_off ||
	    bd->pdu.mpdu_len < bd->pdu.mpdu_header_len)
		goto drop;

	if (bd->asf && !bd->esf) { /* chained A-MSDU chunks */
		/* Sanity check */
		if (bd->pdu.mpdu_data_off + bd->pdu.mpdu_len > WCN36XX_PKT_SIZE)
			goto drop;

		skb_put(skb, bd->pdu.mpdu_data_off + bd->pdu.mpdu_len);
		skb_pull(skb, bd->pdu.mpdu_data_off);

		/* Only set status for first chained BD (with mac header) */
		goto done;
	}

	if (bd->pdu.mpdu_header_off < sizeof(*bd) ||
	    bd->pdu.mpdu_header_off + bd->pdu.mpdu_len > WCN36XX_PKT_SIZE)
		goto drop;

	skb_put(skb, bd->pdu.mpdu_header_off + bd->pdu.mpdu_len);
	skb_pull(skb, bd->pdu.mpdu_header_off);

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = __le16_to_cpu(hdr->frame_control);
	sn = IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl));

	status.mactime = 10;
	status.signal = -get_rssi0(bd);
	status.antenna = 1;
	status.flag = 0;
	status.rx_flags = 0;
	status.flag |= RX_FLAG_IV_STRIPPED |
		       RX_FLAG_MMIC_STRIPPED |
		       RX_FLAG_DECRYPTED;

	wcn36xx_dbg(WCN36XX_DBG_RX, "status.flags=%x\n", status.flag);

	if (bd->scan_learn) {
		/* If packet originate from hardware scanning, extract the
		 * band/channel from bd descriptor.
		 */
		u8 hwch = (bd->reserved0 << 4) + bd->rx_ch;

		if (bd->rf_band != 1 && hwch <= sizeof(ab_rx_ch_map) && hwch >= 1) {
			status.band = NL80211_BAND_5GHZ;
			status.freq = ieee80211_channel_to_frequency(ab_rx_ch_map[hwch - 1],
								     status.band);
		} else {
			status.band = NL80211_BAND_2GHZ;
			status.freq = ieee80211_channel_to_frequency(hwch, status.band);
		}
	} else {
		status.band = WCN36XX_BAND(wcn);
		status.freq = WCN36XX_CENTER_FREQ(wcn);
	}

	if (bd->rate_id < ARRAY_SIZE(wcn36xx_rate_table)) {
		rate = &wcn36xx_rate_table[bd->rate_id];
		status.encoding = rate->encoding;
		status.enc_flags = rate->encoding_flags;
		status.bw = rate->bw;
		status.rate_idx = rate->mcs_or_legacy_index;
		status.nss = 1;

		if (status.band == NL80211_BAND_5GHZ &&
		    status.encoding == RX_ENC_LEGACY &&
		    status.rate_idx >= 4) {
			/* no dsss rates in 5Ghz rates table */
			status.rate_idx -= 4;
		}
	} else {
		status.encoding = 0;
		status.bw = 0;
		status.enc_flags = 0;
		status.rate_idx = 0;
	}

	if (ieee80211_is_beacon(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control))
		status.boottime_ns = ktime_get_boottime_ns();

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));

	if (ieee80211_is_beacon(hdr->frame_control)) {
		wcn36xx_dbg(WCN36XX_DBG_BEACON, "beacon skb %p len %d fc %04x sn %d\n",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_BEACON_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	} else {
		wcn36xx_dbg(WCN36XX_DBG_RX, "rx skb %p len %d fc %04x sn %d\n",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_RX_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	}

done:
	/*  Chained AMSDU ? slow path */
	if (unlikely(bd->asf && !(bd->lsf && bd->esf))) {
		if (bd->esf && !skb_queue_empty(&wcn->amsdu)) {
			wcn36xx_err("Discarding non complete chain");
			__skb_queue_purge_irq(&wcn->amsdu);
		}

		__skb_queue_tail(&wcn->amsdu, skb);

		if (!bd->lsf)
			return 0; /* Not the last AMSDU, wait for more */

		skb = wcn36xx_unchain_msdu(&wcn->amsdu);
		if (!skb)
			goto drop;
	}

	ieee80211_rx_irqsafe(wcn->hw, skb);

	return 0;

drop: /* drop everything */
	wcn36xx_err("Drop frame! skb:%p len:%u hoff:%u doff:%u asf=%u esf=%u lsf=%u\n",
		    skb, bd->pdu.mpdu_len, bd->pdu.mpdu_header_off,
		    bd->pdu.mpdu_data_off, bd->asf, bd->esf, bd->lsf);

	dev_kfree_skb_irq(skb);
	__skb_queue_purge_irq(&wcn->amsdu);

	return -EINVAL;
}

static void wcn36xx_set_tx_pdu(struct wcn36xx_tx_bd *bd,
			       u32 mpdu_header_len,
			       u32 len,
			       u16 tid)
{
	bd->pdu.mpdu_header_len = mpdu_header_len;
	bd->pdu.mpdu_header_off = sizeof(*bd);
	bd->pdu.mpdu_data_off = bd->pdu.mpdu_header_len +
		bd->pdu.mpdu_header_off;
	bd->pdu.mpdu_len = len;
	bd->pdu.tid = tid;
}

static inline struct wcn36xx_vif *get_vif_by_addr(struct wcn36xx *wcn,
						  u8 *addr)
{
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_vif *vif = NULL;
	list_for_each_entry(vif_priv, &wcn->vif_list, list) {
			vif = wcn36xx_priv_to_vif(vif_priv);
			if (memcmp(vif->addr, addr, ETH_ALEN) == 0)
				return vif_priv;
	}
	wcn36xx_warn("vif %pM not found\n", addr);
	return NULL;
}

static void wcn36xx_tx_start_ampdu(struct wcn36xx *wcn,
				   struct wcn36xx_sta *sta_priv,
				   struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_sta *sta;
	u8 *qc, tid;

	if (!conf_is_ht(&wcn->hw->conf))
		return;

	sta = wcn36xx_priv_to_sta(sta_priv);

	if (WARN_ON(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	qc = ieee80211_get_qos_ctl(hdr);
	tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;

	spin_lock(&sta_priv->ampdu_lock);
	if (sta_priv->ampdu_state[tid] != WCN36XX_AMPDU_NONE)
		goto out_unlock;

	if (sta_priv->non_agg_frame_ct++ >= WCN36XX_AMPDU_START_THRESH) {
		sta_priv->ampdu_state[tid] = WCN36XX_AMPDU_START;
		sta_priv->non_agg_frame_ct = 0;
		ieee80211_start_tx_ba_session(sta, tid, 0);
	}
out_unlock:
	spin_unlock(&sta_priv->ampdu_lock);
}

static void wcn36xx_set_tx_data(struct wcn36xx_tx_bd *bd,
				struct wcn36xx *wcn,
				struct wcn36xx_vif **vif_priv,
				struct wcn36xx_sta *sta_priv,
				struct sk_buff *skb,
				bool bcast)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = NULL;
	struct wcn36xx_vif *__vif_priv = NULL;
	bool is_data_qos = ieee80211_is_data_qos(hdr->frame_control);
	u16 tid = 0;

	bd->bd_rate = WCN36XX_BD_RATE_DATA;

	/*
	 * For not unicast frames mac80211 will not set sta pointer so use
	 * self_sta_index instead.
	 */
	if (sta_priv) {
		__vif_priv = sta_priv->vif;
		vif = wcn36xx_priv_to_vif(__vif_priv);

		bd->dpu_sign = sta_priv->ucast_dpu_sign;
		if (vif->type == NL80211_IFTYPE_STATION) {
			bd->sta_index = sta_priv->bss_sta_index;
			bd->dpu_desc_idx = sta_priv->bss_dpu_desc_index;
		} else if (vif->type == NL80211_IFTYPE_AP ||
			   vif->type == NL80211_IFTYPE_ADHOC ||
			   vif->type == NL80211_IFTYPE_MESH_POINT) {
			bd->sta_index = sta_priv->sta_index;
			bd->dpu_desc_idx = sta_priv->dpu_desc_index;
		}
	} else {
		__vif_priv = get_vif_by_addr(wcn, hdr->addr2);
		bd->sta_index = __vif_priv->self_sta_index;
		bd->dpu_desc_idx = __vif_priv->self_dpu_desc_index;
		bd->dpu_sign = __vif_priv->self_ucast_dpu_sign;
	}

	if (is_data_qos) {
		tid = ieee80211_get_tid(hdr);
		/* TID->QID is one-to-one mapping */
		bd->queue_id = tid;
		bd->pdu.bd_ssn = WCN36XX_TXBD_SSN_FILL_DPU_QOS;
	} else {
		bd->pdu.bd_ssn = WCN36XX_TXBD_SSN_FILL_DPU_NON_QOS;
	}

	if (info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT ||
	    (sta_priv && !sta_priv->is_data_encrypted)) {
		bd->dpu_ne = 1;
	}

	if (ieee80211_is_any_nullfunc(hdr->frame_control)) {
		/* Don't use a regular queue for null packet (no ampdu) */
		bd->queue_id = WCN36XX_TX_U_WQ_ID;
		bd->bd_rate = WCN36XX_BD_RATE_CTRL;
		if (ieee80211_is_qos_nullfunc(hdr->frame_control))
			bd->pdu.bd_ssn = WCN36XX_TXBD_SSN_FILL_HOST;
	}

	if (bcast) {
		bd->ub = 1;
		bd->ack_policy = 1;
	}
	*vif_priv = __vif_priv;

	wcn36xx_set_tx_pdu(bd,
			   is_data_qos ?
			   sizeof(struct ieee80211_qos_hdr) :
			   sizeof(struct ieee80211_hdr_3addr),
			   skb->len, tid);

	if (sta_priv && is_data_qos)
		wcn36xx_tx_start_ampdu(wcn, sta_priv, skb);
}

static void wcn36xx_set_tx_mgmt(struct wcn36xx_tx_bd *bd,
				struct wcn36xx *wcn,
				struct wcn36xx_vif **vif_priv,
				struct sk_buff *skb,
				bool bcast)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct wcn36xx_vif *__vif_priv =
		get_vif_by_addr(wcn, hdr->addr2);
	bd->sta_index = __vif_priv->self_sta_index;
	bd->dpu_desc_idx = __vif_priv->self_dpu_desc_index;
	bd->dpu_ne = 1;

	/* default rate for unicast */
	if (ieee80211_is_mgmt(hdr->frame_control))
		bd->bd_rate = (WCN36XX_BAND(wcn) == NL80211_BAND_5GHZ) ?
			WCN36XX_BD_RATE_CTRL :
			WCN36XX_BD_RATE_MGMT;
	else if (ieee80211_is_ctl(hdr->frame_control))
		bd->bd_rate = WCN36XX_BD_RATE_CTRL;
	else
		wcn36xx_warn("frame control type unknown\n");

	/*
	 * In joining state trick hardware that probe is sent as
	 * unicast even if address is broadcast.
	 */
	if (__vif_priv->is_joining &&
	    ieee80211_is_probe_req(hdr->frame_control))
		bcast = false;

	if (bcast) {
		/* broadcast */
		bd->ub = 1;
		/* No ack needed not unicast */
		bd->ack_policy = 1;
		bd->queue_id = WCN36XX_TX_B_WQ_ID;
	} else
		bd->queue_id = WCN36XX_TX_U_WQ_ID;
	*vif_priv = __vif_priv;

	bd->pdu.bd_ssn = WCN36XX_TXBD_SSN_FILL_DPU_NON_QOS;

	wcn36xx_set_tx_pdu(bd,
			   ieee80211_is_data_qos(hdr->frame_control) ?
			   sizeof(struct ieee80211_qos_hdr) :
			   sizeof(struct ieee80211_hdr_3addr),
			   skb->len, WCN36XX_TID);
}

int wcn36xx_start_tx(struct wcn36xx *wcn,
		     struct wcn36xx_sta *sta_priv,
		     struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool is_low = ieee80211_is_data(hdr->frame_control);
	bool bcast = is_broadcast_ether_addr(hdr->addr1) ||
		is_multicast_ether_addr(hdr->addr1);
	bool ack_ind = (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) &&
					!(info->flags & IEEE80211_TX_CTL_NO_ACK);
	struct wcn36xx_tx_bd bd;
	int ret;

	memset(&bd, 0, sizeof(bd));

	wcn36xx_dbg(WCN36XX_DBG_TX,
		    "tx skb %p len %d fc %04x sn %d %s %s\n",
		    skb, skb->len, __le16_to_cpu(hdr->frame_control),
		    IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl)),
		    is_low ? "low" : "high", bcast ? "bcast" : "ucast");

	wcn36xx_dbg_dump(WCN36XX_DBG_TX_DUMP, "", skb->data, skb->len);

	bd.dpu_rf = WCN36XX_BMU_WQ_TX;

	if (unlikely(ack_ind)) {
		wcn36xx_dbg(WCN36XX_DBG_DXE, "TX_ACK status requested\n");

		/* Only one at a time is supported by fw. Stop the TX queues
		 * until the ack status gets back.
		 */
		ieee80211_stop_queues(wcn->hw);

		/* Request ack indication from the firmware */
		bd.tx_comp = 1;
	}

	/* Data frames served first*/
	if (is_low)
		wcn36xx_set_tx_data(&bd, wcn, &vif_priv, sta_priv, skb, bcast);
	else
		/* MGMT and CTRL frames are handeld here*/
		wcn36xx_set_tx_mgmt(&bd, wcn, &vif_priv, skb, bcast);

	buff_to_be((u32 *)&bd, sizeof(bd)/sizeof(u32));
	bd.tx_bd_sign = 0xbdbdbdbd;

	ret = wcn36xx_dxe_tx_frame(wcn, vif_priv, &bd, skb, is_low);
	if (unlikely(ret && ack_ind)) {
		/* If the skb has not been transmitted, resume TX queue */
		ieee80211_wake_queues(wcn->hw);
	}

	return ret;
}
