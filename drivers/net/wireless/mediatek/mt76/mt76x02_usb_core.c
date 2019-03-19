/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mt76x02.h"

static void mt76x02u_remove_dma_hdr(struct sk_buff *skb)
{
	int hdr_len;

	skb_pull(skb, sizeof(struct mt76x02_txwi) + MT_DMA_HDR_LEN);
	hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	if (hdr_len % 4)
		mt76x02_remove_hdr_pad(skb, 2);
}

void mt76x02u_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			      struct mt76_queue_entry *e)
{
	mt76x02u_remove_dma_hdr(e->skb);
	mt76_tx_complete_skb(mdev, e->skb);
}
EXPORT_SYMBOL_GPL(mt76x02u_tx_complete_skb);

int mt76x02u_skb_dma_info(struct sk_buff *skb, int port, u32 flags)
{
	struct sk_buff *iter, *last = skb;
	u32 info, pad;

	/* Buffer layout:
	 *	|   4B   | xfer len |      pad       |  4B  |
	 *	| TXINFO | pkt/cmd  | zero pad to 4B | zero |
	 *
	 * length field of TXINFO should be set to 'xfer len'.
	 */
	info = FIELD_PREP(MT_TXD_INFO_LEN, round_up(skb->len, 4)) |
	       FIELD_PREP(MT_TXD_INFO_DPORT, port) | flags;
	put_unaligned_le32(info, skb_push(skb, sizeof(info)));

	/* Add zero pad of 4 - 7 bytes */
	pad = round_up(skb->len, 4) + 4 - skb->len;

	/* First packet of a A-MSDU burst keeps track of the whole burst
	 * length, need to update lenght of it and the last packet.
	 */
	skb_walk_frags(skb, iter) {
		last = iter;
		if (!iter->next) {
			skb->data_len += pad;
			skb->len += pad;
			break;
		}
	}

	if (skb_pad(last, pad))
		return -ENOMEM;
	__skb_put(last, pad);

	return 0;
}

int mt76x02u_tx_prepare_skb(struct mt76_dev *mdev, void *data,
			    struct sk_buff *skb, enum mt76_txq_id qid,
			    struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			    struct mt76_tx_info *tx_info)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	int pid, len = skb->len, ep = q2ep(mdev->q_tx[qid].q->hw_idx);
	struct mt76x02_txwi *txwi;
	enum mt76_qsel qsel;
	u32 flags;

	mt76_insert_hdr_pad(skb);

	txwi = (struct mt76x02_txwi *)(skb->data - sizeof(struct mt76x02_txwi));
	mt76x02_mac_write_txwi(dev, txwi, skb, wcid, sta, len);
	skb_push(skb, sizeof(struct mt76x02_txwi));

	pid = mt76_tx_status_skb_add(mdev, wcid, skb);
	txwi->pktid = pid;

	if (pid >= MT_PACKET_ID_FIRST || ep == MT_EP_OUT_HCCA)
		qsel = MT_QSEL_MGMT;
	else
		qsel = MT_QSEL_EDCA;

	flags = FIELD_PREP(MT_TXD_INFO_QSEL, qsel) |
		MT_TXD_INFO_80211;
	if (!wcid || wcid->hw_key_idx == 0xff || wcid->sw_iv)
		flags |= MT_TXD_INFO_WIV;

	return mt76x02u_skb_dma_info(skb, WLAN_PORT, flags);
}
EXPORT_SYMBOL_GPL(mt76x02u_tx_prepare_skb);

/* Trigger pre-TBTT event 8 ms before TBTT */
#define PRE_TBTT_USEC 8000
static void mt76x02u_start_pre_tbtt_timer(struct mt76x02_dev *dev)
{
	u64 time;
	u32 tbtt;

	/* Get remaining TBTT in usec */
	tbtt = mt76_get_field(dev, MT_TBTT_TIMER, MT_TBTT_TIMER_VAL);
	tbtt *= 32;

	if (tbtt <= PRE_TBTT_USEC) {
		queue_work(system_highpri_wq, &dev->pre_tbtt_work);
		return;
	}

	time = (tbtt - PRE_TBTT_USEC) * 1000ull;
	hrtimer_start(&dev->pre_tbtt_timer, time, HRTIMER_MODE_REL);
}

static void mt76x02u_restart_pre_tbtt_timer(struct mt76x02_dev *dev)
{
	u32 tbtt, dw0, dw1;
	u64 tsf, time;

	/* Get remaining TBTT in usec */
	tbtt = mt76_get_field(dev, MT_TBTT_TIMER, MT_TBTT_TIMER_VAL);
	tbtt *= 32;

	dw0 = mt76_rr(dev, MT_TSF_TIMER_DW0);
	dw1 = mt76_rr(dev, MT_TSF_TIMER_DW1);
	tsf = (u64)dw0 << 32 | dw1;
	dev_dbg(dev->mt76.dev, "TSF: %llu us TBTT %u us\n", tsf, tbtt);

	/* Convert beacon interval in TU (1024 usec) to nsec */
	time = ((1000000000ull * dev->beacon_int) >> 10);

	/* Adjust time to trigger hrtimer 8ms before TBTT */
	if (tbtt < PRE_TBTT_USEC)
		time -= (PRE_TBTT_USEC - tbtt) * 1000ull;
	else
		time += (tbtt - PRE_TBTT_USEC) * 1000ull;

	hrtimer_start(&dev->pre_tbtt_timer, time, HRTIMER_MODE_REL);
}

static void mt76x02u_pre_tbtt_work(struct work_struct *work)
{
	struct mt76x02_dev *dev =
		container_of(work, struct mt76x02_dev, pre_tbtt_work);

	if (!dev->beacon_mask)
		return;
	mt76x02u_restart_pre_tbtt_timer(dev);
}

static enum hrtimer_restart mt76x02u_pre_tbtt_interrupt(struct hrtimer *timer)
{
	struct mt76x02_dev *dev =
	    container_of(timer, struct mt76x02_dev, pre_tbtt_timer);

	queue_work(system_highpri_wq, &dev->pre_tbtt_work);

	return HRTIMER_NORESTART;
}

static void mt76x02u_pre_tbtt_enable(struct mt76x02_dev *dev, bool en)
{
}

static void mt76x02u_beacon_enable(struct mt76x02_dev *dev, bool en)
{
}

void mt76x02u_init_beacon_config(struct mt76x02_dev *dev)
{
	static const struct mt76x02_beacon_ops beacon_ops = {
		.pre_tbtt_enable = mt76x02u_pre_tbtt_enable,
		.beacon_enable = mt76x02u_beacon_enable,
	};
	dev->beacon_ops = &beacon_ops;

	hrtimer_init(&dev->pre_tbtt_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dev->pre_tbtt_timer.function = mt76x02u_pre_tbtt_interrupt;
	INIT_WORK(&dev->pre_tbtt_work, mt76x02u_pre_tbtt_work);

	mt76x02_init_beacon_config(dev);
}
EXPORT_SYMBOL_GPL(mt76x02u_init_beacon_config);
