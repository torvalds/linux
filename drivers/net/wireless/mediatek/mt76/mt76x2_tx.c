/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include "mt76x2.h"
#include "mt76x2_dma.h"

struct beacon_bc_data {
	struct mt76x2_dev *dev;
	struct sk_buff_head q;
	struct sk_buff *tail[8];
};

int mt76x2_tx_prepare_skb(struct mt76_dev *mdev, void *txwi,
			  struct sk_buff *skb, struct mt76_queue *q,
			  struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			  u32 *tx_info)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int qsel = MT_QSEL_EDCA;
	int ret;

	if (q == &dev->mt76.q_tx[MT_TXQ_PSD] && wcid && wcid->idx < 128)
		mt76x2_mac_wcid_set_drop(dev, wcid->idx, false);

	mt76x2_mac_write_txwi(dev, txwi, skb, wcid, sta, skb->len);

	ret = mt76x2_insert_hdr_pad(skb);
	if (ret < 0)
		return ret;

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		qsel = MT_QSEL_MGMT;

	*tx_info = FIELD_PREP(MT_TXD_INFO_QSEL, qsel) |
		   MT_TXD_INFO_80211;

	if (!wcid || wcid->hw_key_idx == 0xff || wcid->sw_iv)
		*tx_info |= MT_TXD_INFO_WIV;

	return 0;
}

static void
mt76x2_update_beacon_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76x2_dev *dev = (struct mt76x2_dev *) priv;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	struct sk_buff *skb = NULL;

	if (!(dev->beacon_mask & BIT(mvif->idx)))
		return;

	skb = ieee80211_beacon_get(mt76_hw(dev), vif);
	if (!skb)
		return;

	mt76x2_mac_set_beacon(dev, mvif->idx, skb);
}

static void
mt76x2_add_buffered_bc(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct beacon_bc_data *data = priv;
	struct mt76x2_dev *dev = data->dev;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;

	if (!(dev->beacon_mask & BIT(mvif->idx)))
		return;

	skb = ieee80211_get_buffered_bc(mt76_hw(dev), vif);
	if (!skb)
		return;

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;
	info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
	mt76_skb_set_moredata(skb, true);
	__skb_queue_tail(&data->q, skb);
	data->tail[mvif->idx] = skb;
}

static void
mt76x2_resync_beacon_timer(struct mt76x2_dev *dev)
{
	u32 timer_val = dev->beacon_int << 4;

	dev->tbtt_count++;

	/*
	 * Beacon timer drifts by 1us every tick, the timer is configured
	 * in 1/16 TU (64us) units.
	 */
	if (dev->tbtt_count < 62)
		return;

	if (dev->tbtt_count >= 64) {
		dev->tbtt_count = 0;
		return;
	}

	/*
	 * The updated beacon interval takes effect after two TBTT, because
	 * at this point the original interval has already been loaded into
	 * the next TBTT_TIMER value
	 */
	if (dev->tbtt_count == 62)
		timer_val -= 1;

	mt76_rmw_field(dev, MT_BEACON_TIME_CFG,
		       MT_BEACON_TIME_CFG_INTVAL, timer_val);
}

void mt76x2_pre_tbtt_tasklet(unsigned long arg)
{
	struct mt76x2_dev *dev = (struct mt76x2_dev *) arg;
	struct mt76_queue *q = &dev->mt76.q_tx[MT_TXQ_PSD];
	struct beacon_bc_data data = {};
	struct sk_buff *skb;
	int i, nframes;

	mt76x2_resync_beacon_timer(dev);

	data.dev = dev;
	__skb_queue_head_init(&data.q);

	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt76x2_update_beacon_iter, dev);

	do {
		nframes = skb_queue_len(&data.q);
		ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt76x2_add_buffered_bc, &data);
	} while (nframes != skb_queue_len(&data.q));

	if (!nframes)
		return;

	for (i = 0; i < ARRAY_SIZE(data.tail); i++) {
		if (!data.tail[i])
			continue;

		mt76_skb_set_moredata(data.tail[i], false);
	}

	spin_lock_bh(&q->lock);
	while ((skb = __skb_dequeue(&data.q)) != NULL) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_vif *vif = info->control.vif;
		struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;

		mt76_dma_tx_queue_skb(&dev->mt76, q, skb, &mvif->group_wcid,
				      NULL);
	}
	spin_unlock_bh(&q->lock);
}

