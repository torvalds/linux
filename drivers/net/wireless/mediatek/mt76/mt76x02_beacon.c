// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
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

	if (WARN_ON_ONCE(beacon_len < skb->len + sizeof(struct mt76x02_txwi)))
		return -ENOSPC;

	/* USB devices already reserve enough skb headroom for txwi's. This
	 * helps to save slow copies over USB.
	 */
	if (mt76_is_usb(&dev->mt76)) {
		struct mt76x02_txwi *txwi;

		txwi = (struct mt76x02_txwi *)(skb->data - sizeof(*txwi));
		mt76x02_mac_write_txwi(dev, txwi, skb, NULL, NULL, skb->len);
		skb_push(skb, sizeof(*txwi));
	} else {
		struct mt76x02_txwi txwi;

		mt76x02_mac_write_txwi(dev, &txwi, skb, NULL, NULL, skb->len);
		mt76_wr_copy(dev, offset, &txwi, sizeof(txwi));
		offset += sizeof(txwi);
	}

	mt76_wr_copy(dev, offset, skb->data, skb->len);
	return 0;
}

void mt76x02_mac_set_beacon(struct mt76x02_dev *dev,
			    struct sk_buff *skb)
{
	int bcn_len = dev->beacon_ops->slot_size;
	int bcn_addr = MT_BEACON_BASE + (bcn_len * dev->beacon_data_count);

	if (!mt76x02_write_beacon(dev, bcn_addr, skb))
		dev->beacon_data_count++;
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_set_beacon);

void mt76x02_mac_set_beacon_enable(struct mt76x02_dev *dev,
				   struct ieee80211_vif *vif, bool enable)
{
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	u8 old_mask = dev->mt76.beacon_mask;

	mt76x02_pre_tbtt_enable(dev, false);

	if (!dev->mt76.beacon_mask)
		dev->tbtt_count = 0;

	if (enable) {
		dev->mt76.beacon_mask |= BIT(mvif->idx);
	} else {
		dev->mt76.beacon_mask &= ~BIT(mvif->idx);
	}

	if (!!old_mask == !!dev->mt76.beacon_mask)
		goto out;

	if (dev->mt76.beacon_mask)
		mt76_set(dev, MT_BEACON_TIME_CFG,
			 MT_BEACON_TIME_CFG_BEACON_TX |
			 MT_BEACON_TIME_CFG_TBTT_EN |
			 MT_BEACON_TIME_CFG_TIMER_EN);
	else
		mt76_clear(dev, MT_BEACON_TIME_CFG,
			   MT_BEACON_TIME_CFG_BEACON_TX |
			   MT_BEACON_TIME_CFG_TBTT_EN |
			   MT_BEACON_TIME_CFG_TIMER_EN);
	mt76x02_beacon_enable(dev, !!dev->mt76.beacon_mask);

out:
	mt76x02_pre_tbtt_enable(dev, true);
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

	mt76x02_mac_set_beacon(dev, skb);
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
mt76x02_enqueue_buffered_bc(struct mt76x02_dev *dev,
			    struct beacon_bc_data *data,
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
	mt76_clear(dev, MT_BEACON_TIME_CFG, (MT_BEACON_TIME_CFG_TIMER_EN |
					     MT_BEACON_TIME_CFG_TBTT_EN |
					     MT_BEACON_TIME_CFG_BEACON_TX));
	mt76_set(dev, MT_BEACON_TIME_CFG, MT_BEACON_TIME_CFG_SYNC_MODE);
	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xffff);
	mt76x02_set_beacon_offsets(dev);
}
EXPORT_SYMBOL_GPL(mt76x02_init_beacon_config);

