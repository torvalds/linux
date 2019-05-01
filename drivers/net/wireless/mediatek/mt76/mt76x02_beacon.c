/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
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

static void mt76x02_set_beacon_offsets(struct mt76x02_dev *dev)
{
	u32 regs[4] = {};
	u16 val;
	int i;

	for (i = 0; i < dev->beacon_ops->nslots; i++) {
		val = i * dev->beacon_ops->slot_size;
		regs[i / 4] |= (val / 64) << (8 * (i % 4));
	}

	for (i = 0; i < 4; i++)
		mt76_wr(dev, MT_BCN_OFFSET(i), regs[i]);
}

static int
mt76x02_write_beacon(struct mt76x02_dev *dev, int offset, struct sk_buff *skb)
{
	int beacon_len = dev->beacon_ops->slot_size;
	struct mt76x02_txwi txwi;

	if (WARN_ON_ONCE(beacon_len < skb->len + sizeof(struct mt76x02_txwi)))
		return -ENOSPC;

	mt76x02_mac_write_txwi(dev, &txwi, skb, NULL, NULL, skb->len);

	mt76_wr_copy(dev, offset, &txwi, sizeof(txwi));
	offset += sizeof(txwi);

	mt76_wr_copy(dev, offset, skb->data, skb->len);
	return 0;
}

static int
__mt76x02_mac_set_beacon(struct mt76x02_dev *dev, u8 bcn_idx,
			 struct sk_buff *skb)
{
	int beacon_len = dev->beacon_ops->slot_size;
	int beacon_addr = MT_BEACON_BASE + (beacon_len * bcn_idx);
	int ret = 0;
	int i;

	/* Prevent corrupt transmissions during update */
	mt76_set(dev, MT_BCN_BYPASS_MASK, BIT(bcn_idx));

	if (skb) {
		ret = mt76x02_write_beacon(dev, beacon_addr, skb);
		if (!ret)
			dev->beacon_data_mask |= BIT(bcn_idx);
	} else {
		dev->beacon_data_mask &= ~BIT(bcn_idx);
		for (i = 0; i < beacon_len; i += 4)
			mt76_wr(dev, beacon_addr + i, 0);
	}

	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xff00 | ~dev->beacon_data_mask);

	return ret;
}

int mt76x02_mac_set_beacon(struct mt76x02_dev *dev, u8 vif_idx,
			   struct sk_buff *skb)
{
	bool force_update = false;
	int bcn_idx = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->beacons); i++) {
		if (vif_idx == i) {
			force_update = !!dev->beacons[i] ^ !!skb;

			if (dev->beacons[i])
				dev_kfree_skb(dev->beacons[i]);

			dev->beacons[i] = skb;
			__mt76x02_mac_set_beacon(dev, bcn_idx, skb);
		} else if (force_update && dev->beacons[i]) {
			__mt76x02_mac_set_beacon(dev, bcn_idx,
						 dev->beacons[i]);
		}

		bcn_idx += !!dev->beacons[i];
	}

	for (i = bcn_idx; i < ARRAY_SIZE(dev->beacons); i++) {
		if (!(dev->beacon_data_mask & BIT(i)))
			break;

		__mt76x02_mac_set_beacon(dev, i, NULL);
	}

	mt76_rmw_field(dev, MT_MAC_BSSID_DW1, MT_MAC_BSSID_DW1_MBEACON_N,
		       bcn_idx - 1);
	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_set_beacon);

static void
__mt76x02_mac_set_beacon_enable(struct mt76x02_dev *dev, u8 vif_idx,
				bool val, struct sk_buff *skb)
{
	u8 old_mask = dev->mt76.beacon_mask;
	bool en;
	u32 reg;

	if (val) {
		dev->mt76.beacon_mask |= BIT(vif_idx);
		if (skb)
			mt76x02_mac_set_beacon(dev, vif_idx, skb);
	} else {
		dev->mt76.beacon_mask &= ~BIT(vif_idx);
		mt76x02_mac_set_beacon(dev, vif_idx, NULL);
	}

	if (!!old_mask == !!dev->mt76.beacon_mask)
		return;

	en = dev->mt76.beacon_mask;

	reg = MT_BEACON_TIME_CFG_BEACON_TX |
	      MT_BEACON_TIME_CFG_TBTT_EN |
	      MT_BEACON_TIME_CFG_TIMER_EN;
	mt76_rmw(dev, MT_BEACON_TIME_CFG, reg, reg * en);

	dev->beacon_ops->beacon_enable(dev, en);
}

void mt76x02_mac_set_beacon_enable(struct mt76x02_dev *dev,
				   struct ieee80211_vif *vif, bool val)
{
	u8 vif_idx = ((struct mt76x02_vif *)vif->drv_priv)->idx;
	struct sk_buff *skb = NULL;

	dev->beacon_ops->pre_tbtt_enable(dev, false);

	if (mt76_is_usb(dev))
		skb = ieee80211_beacon_get(mt76_hw(dev), vif);

	if (!dev->mt76.beacon_mask)
		dev->tbtt_count = 0;

	__mt76x02_mac_set_beacon_enable(dev, vif_idx, val, skb);

	dev->beacon_ops->pre_tbtt_enable(dev, true);
}

void
mt76x02_resync_beacon_timer(struct mt76x02_dev *dev)
{
	u32 timer_val = dev->mt76.beacon_int << 4;

	dev->tbtt_count++;

	/*
	 * Beacon timer drifts by 1us every tick, the timer is configured
	 * in 1/16 TU (64us) units.
	 */
	if (dev->tbtt_count < 63)
		return;

	/*
	 * The updated beacon interval takes effect after two TBTT, because
	 * at this point the original interval has already been loaded into
	 * the next TBTT_TIMER value
	 */
	if (dev->tbtt_count == 63)
		timer_val -= 1;

	mt76_rmw_field(dev, MT_BEACON_TIME_CFG,
		       MT_BEACON_TIME_CFG_INTVAL, timer_val);

	if (dev->tbtt_count >= 64)
		dev->tbtt_count = 0;
}
EXPORT_SYMBOL_GPL(mt76x02_resync_beacon_timer);

void
mt76x02_update_beacon_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	struct sk_buff *skb = NULL;

	if (!(dev->mt76.beacon_mask & BIT(mvif->idx)))
		return;

	skb = ieee80211_beacon_get(mt76_hw(dev), vif);
	if (!skb)
		return;

	mt76x02_mac_set_beacon(dev, mvif->idx, skb);
}
EXPORT_SYMBOL_GPL(mt76x02_update_beacon_iter);

static void
mt76x02_add_buffered_bc(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct beacon_bc_data *data = priv;
	struct mt76x02_dev *dev = data->dev;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;

	if (!(dev->mt76.beacon_mask & BIT(mvif->idx)))
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

void
mt76x02_enqueue_buffered_bc(struct mt76x02_dev *dev, struct beacon_bc_data *data,
			    int max_nframes)
{
	int i, nframes;

	data->dev = dev;
	__skb_queue_head_init(&data->q);

	do {
		nframes = skb_queue_len(&data->q);
		ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt76x02_add_buffered_bc, data);
	} while (nframes != skb_queue_len(&data->q) &&
		 skb_queue_len(&data->q) < max_nframes);

	if (!skb_queue_len(&data->q))
		return;

	for (i = 0; i < ARRAY_SIZE(data->tail); i++) {
		if (!data->tail[i])
			continue;
		mt76_skb_set_moredata(data->tail[i], false);
	}
}
EXPORT_SYMBOL_GPL(mt76x02_enqueue_buffered_bc);

void mt76x02_init_beacon_config(struct mt76x02_dev *dev)
{
	int i;

	mt76_clear(dev, MT_BEACON_TIME_CFG, (MT_BEACON_TIME_CFG_TIMER_EN |
					     MT_BEACON_TIME_CFG_TBTT_EN |
					     MT_BEACON_TIME_CFG_BEACON_TX));
	mt76_set(dev, MT_BEACON_TIME_CFG, MT_BEACON_TIME_CFG_SYNC_MODE);
	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xffff);

	for (i = 0; i < 8; i++)
		mt76x02_mac_set_beacon(dev, i, NULL);

	mt76x02_set_beacon_offsets(dev);
}
EXPORT_SYMBOL_GPL(mt76x02_init_beacon_config);


