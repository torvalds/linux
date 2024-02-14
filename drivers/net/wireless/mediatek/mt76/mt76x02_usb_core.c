// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include "mt76x02_usb.h"

static void mt76x02u_remove_dma_hdr(struct sk_buff *skb)
{
	int hdr_len;

	skb_pull(skb, sizeof(struct mt76x02_txwi) + MT_DMA_HDR_LEN);
	hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	if (hdr_len % 4)
		mt76x02_remove_hdr_pad(skb, 2);
}

void mt76x02u_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e)
{
	mt76x02u_remove_dma_hdr(e->skb);
	mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}
EXPORT_SYMBOL_GPL(mt76x02u_tx_complete_skb);

int mt76x02u_mac_start(struct mt76x02_dev *dev)
{
	mt76x02_mac_reset_counters(dev);

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	if (!mt76x02_wait_for_wpdma(&dev->mt76, 200000))
		return -ETIMEDOUT;

	mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);

	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);

	if (!mt76x02_wait_for_wpdma(&dev->mt76, 50))
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02u_mac_start);

int mt76x02u_skb_dma_info(struct sk_buff *skb, int port, u32 flags)
{
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

	pad = round_up(skb->len, 4) + 4 - skb->len;
	return mt76_skb_adjust_pad(skb, pad);
}

int mt76x02u_tx_prepare_skb(struct mt76_dev *mdev, void *data,
			    enum mt76_txq_id qid, struct mt76_wcid *wcid,
			    struct ieee80211_sta *sta,
			    struct mt76_tx_info *tx_info)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	int pid, len = tx_info->skb->len, ep = q2ep(dev->mphy.q_tx[qid]->hw_idx);
	struct mt76x02_txwi *txwi;
	bool ampdu = IEEE80211_SKB_CB(tx_info->skb)->flags & IEEE80211_TX_CTL_AMPDU;
	enum mt76_qsel qsel;
	u32 flags;
	int err;

	mt76_insert_hdr_pad(tx_info->skb);

	txwi = (struct mt76x02_txwi *)(tx_info->skb->data - sizeof(*txwi));
	mt76x02_mac_write_txwi(dev, txwi, tx_info->skb, wcid, sta, len);
	skb_push(tx_info->skb, sizeof(*txwi));

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);

	/* encode packet rate for no-skb packet id to fix up status reporting */
	if (pid == MT_PACKET_ID_NO_SKB)
		pid = MT_PACKET_ID_HAS_RATE |
		      (le16_to_cpu(txwi->rate) & MT_PKTID_RATE) |
		      FIELD_PREP(MT_PKTID_AC,
				 skb_get_queue_mapping(tx_info->skb));

	txwi->pktid = pid;

	if ((mt76_is_skb_pktid(pid) && ampdu) || ep == MT_EP_OUT_HCCA)
		qsel = MT_QSEL_MGMT;
	else
		qsel = MT_QSEL_EDCA;

	flags = FIELD_PREP(MT_TXD_INFO_QSEL, qsel) |
		MT_TXD_INFO_80211;
	if (!wcid || wcid->hw_key_idx == 0xff || wcid->sw_iv)
		flags |= MT_TXD_INFO_WIV;

	if (sta) {
		struct mt76x02_sta *msta = (struct mt76x02_sta *)sta->drv_priv;

		ewma_pktlen_add(&msta->pktlen, tx_info->skb->len);
	}

	err = mt76x02u_skb_dma_info(tx_info->skb, WLAN_PORT, flags);
	if (err && wcid)
		/* Release pktid in case of error. */
		idr_remove(&wcid->pktid, pid);

	return err;
}
EXPORT_SYMBOL_GPL(mt76x02u_tx_prepare_skb);

/* Trigger pre-TBTT event 8 ms before TBTT */
#define PRE_TBTT_USEC 8000

/* Beacon SRAM memory is limited to 8kB. We need to send PS buffered frames
 * (which can be 1500 bytes big) via beacon memory. That make limit of number
 * of slots to 5. TODO: dynamically calculate offsets in beacon SRAM.
 */
#define N_BCN_SLOTS 5

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
	time = ((1000000000ull * dev->mt76.beacon_int) >> 10);

	/* Adjust time to trigger hrtimer 8ms before TBTT */
	if (tbtt < PRE_TBTT_USEC)
		time -= (PRE_TBTT_USEC - tbtt) * 1000ull;
	else
		time += (tbtt - PRE_TBTT_USEC) * 1000ull;

	hrtimer_start(&dev->pre_tbtt_timer, time, HRTIMER_MODE_REL);
}

static void mt76x02u_stop_pre_tbtt_timer(struct mt76x02_dev *dev)
{
	do {
		hrtimer_cancel(&dev->pre_tbtt_timer);
		cancel_work_sync(&dev->pre_tbtt_work);
		/* Timer can be rearmed by work. */
	} while (hrtimer_active(&dev->pre_tbtt_timer));
}

static void mt76x02u_pre_tbtt_work(struct work_struct *work)
{
	struct mt76x02_dev *dev =
		container_of(work, struct mt76x02_dev, pre_tbtt_work);
	struct beacon_bc_data data = {};
	struct sk_buff *skb;
	int nbeacons;

	if (!dev->mt76.beacon_mask)
		return;

	if (mt76_hw(dev)->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		return;

	mt76x02_resync_beacon_timer(dev);

	/* Prevent corrupt transmissions during update */
	mt76_set(dev, MT_BCN_BYPASS_MASK, 0xffff);
	dev->beacon_data_count = 0;

	ieee80211_iterate_active_interfaces(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt76x02_update_beacon_iter, dev);

	mt76_csa_check(&dev->mt76);

	if (dev->mt76.csa_complete) {
		mt76_csa_finish(&dev->mt76);
		goto out;
	}

	nbeacons = hweight8(dev->mt76.beacon_mask);
	mt76x02_enqueue_buffered_bc(dev, &data, N_BCN_SLOTS - nbeacons);

	while ((skb = __skb_dequeue(&data.q)) != NULL)
		mt76x02_mac_set_beacon(dev, skb);

out:
	mt76_wr(dev, MT_BCN_BYPASS_MASK,
		0xff00 | ~(0xff00 >> dev->beacon_data_count));

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
	if (en && dev->mt76.beacon_mask &&
	    !hrtimer_active(&dev->pre_tbtt_timer))
		mt76x02u_start_pre_tbtt_timer(dev);
	if (!en)
		mt76x02u_stop_pre_tbtt_timer(dev);
}

static void mt76x02u_beacon_enable(struct mt76x02_dev *dev, bool en)
{
	if (WARN_ON_ONCE(!dev->mt76.beacon_int))
		return;

	if (en)
		mt76x02u_start_pre_tbtt_timer(dev);
}

void mt76x02u_init_beacon_config(struct mt76x02_dev *dev)
{
	static const struct mt76x02_beacon_ops beacon_ops = {
		.nslots = N_BCN_SLOTS,
		.slot_size = (8192 / N_BCN_SLOTS) & ~63,
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

void mt76x02u_exit_beacon_config(struct mt76x02_dev *dev)
{
	if (!test_bit(MT76_REMOVED, &dev->mphy.state))
		mt76_clear(dev, MT_BEACON_TIME_CFG,
			   MT_BEACON_TIME_CFG_TIMER_EN |
			   MT_BEACON_TIME_CFG_SYNC_MODE |
			   MT_BEACON_TIME_CFG_TBTT_EN |
			   MT_BEACON_TIME_CFG_BEACON_TX);

	mt76x02u_stop_pre_tbtt_timer(dev);
}
EXPORT_SYMBOL_GPL(mt76x02u_exit_beacon_config);
