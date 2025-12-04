// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "mt7603.h"

struct beacon_bc_data {
	struct mt7603_dev *dev;
	struct sk_buff_head q;
	struct sk_buff *tail[MT7603_MAX_INTERFACES];
	int count[MT7603_MAX_INTERFACES];
};

static void
mt7603_mac_stuck_beacon_recovery(struct mt7603_dev *dev)
{
	if (dev->beacon_check % 5 != 4)
		return;

	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_EN);
	mt76_set(dev, MT_SCH_4, MT_SCH_4_RESET);
	mt76_clear(dev, MT_SCH_4, MT_SCH_4_RESET);
	mt76_set(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_EN);

	mt76_set(dev, MT_WF_CFG_OFF_WOCCR, MT_WF_CFG_OFF_WOCCR_TMAC_GC_DIS);
	mt76_set(dev, MT_ARB_SCR, MT_ARB_SCR_TX_DISABLE);
	mt76_clear(dev, MT_ARB_SCR, MT_ARB_SCR_TX_DISABLE);
	mt76_clear(dev, MT_WF_CFG_OFF_WOCCR, MT_WF_CFG_OFF_WOCCR_TMAC_GC_DIS);
}

static void
mt7603_update_beacon_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7603_dev *dev = (struct mt7603_dev *)priv;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt7603_vif *mvif = (struct mt7603_vif *)vif->drv_priv;
	struct sk_buff *skb = NULL;
	u32 om_idx = mvif->idx;
	u32 val;

	if (!(mdev->beacon_mask & BIT(mvif->idx)))
		return;

	skb = ieee80211_beacon_get(mt76_hw(dev), vif, 0);
	if (!skb)
		return;

	if (om_idx)
		om_idx |= 0x10;
	val = MT_DMA_FQCR0_BUSY | MT_DMA_FQCR0_MODE |
		FIELD_PREP(MT_DMA_FQCR0_TARGET_BSS, om_idx) |
		FIELD_PREP(MT_DMA_FQCR0_DEST_PORT_ID, 3) |
		FIELD_PREP(MT_DMA_FQCR0_DEST_QUEUE_ID, 8);

	spin_lock_bh(&dev->ps_lock);

	mt76_wr(dev, MT_DMA_FQCR0, val |
		FIELD_PREP(MT_DMA_FQCR0_TARGET_QID, MT_TX_HW_QUEUE_BCN));
	if (!mt76_poll(dev, MT_DMA_FQCR0, MT_DMA_FQCR0_BUSY, 0, 5000)) {
		dev->beacon_check = MT7603_WATCHDOG_TIMEOUT;
		goto out;
	}

	mt76_wr(dev, MT_DMA_FQCR0, val |
		FIELD_PREP(MT_DMA_FQCR0_TARGET_QID, MT_TX_HW_QUEUE_BMC));
	if (!mt76_poll(dev, MT_DMA_FQCR0, MT_DMA_FQCR0_BUSY, 0, 5000)) {
		dev->beacon_check = MT7603_WATCHDOG_TIMEOUT;
		goto out;
	}

	mt76_tx_queue_skb(dev, dev->mphy.q_tx[MT_TXQ_BEACON],
			  MT_TXQ_BEACON, skb, &mvif->sta.wcid, NULL);

out:
	spin_unlock_bh(&dev->ps_lock);
}

static void
mt7603_add_buffered_bc(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct beacon_bc_data *data = priv;
	struct mt7603_dev *dev = data->dev;
	struct mt7603_vif *mvif = (struct mt7603_vif *)vif->drv_priv;
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
	data->count[mvif->idx]++;
}

void mt7603_pre_tbtt_tasklet(struct tasklet_struct *t)
{
	struct mt7603_dev *dev = from_tasklet(dev, t, mt76.pre_tbtt_tasklet);
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_queue *q;
	struct beacon_bc_data data = {};
	struct sk_buff *skb;
	int i, nframes;

	if (dev->mphy.offchannel)
		return;

	data.dev = dev;
	__skb_queue_head_init(&data.q);

	/* Flush all previous CAB queue packets and beacons */
	mt76_wr(dev, MT_WF_ARB_CAB_FLUSH, GENMASK(30, 16) | BIT(0));

	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_CAB], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BEACON], false);

	if (dev->mphy.q_tx[MT_TXQ_BEACON]->queued > 0)
		dev->beacon_check++;
	else
		dev->beacon_check = 0;
	mt7603_mac_stuck_beacon_recovery(dev);

	q = dev->mphy.q_tx[MT_TXQ_BEACON];
	spin_lock(&q->lock);
	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7603_update_beacon_iter, dev);
	mt76_queue_kick(dev, q);
	spin_unlock(&q->lock);

	mt76_csa_check(mdev);
	if (mdev->csa_complete)
		return;

	q = dev->mphy.q_tx[MT_TXQ_CAB];
	do {
		nframes = skb_queue_len(&data.q);
		ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7603_add_buffered_bc, &data);
	} while (nframes != skb_queue_len(&data.q) &&
		 skb_queue_len(&data.q) < 8);

	if (skb_queue_empty(&data.q))
		return;

	for (i = 0; i < ARRAY_SIZE(data.tail); i++) {
		if (!data.tail[i])
			continue;

		mt76_skb_set_moredata(data.tail[i], false);
	}

	spin_lock(&q->lock);
	while ((skb = __skb_dequeue(&data.q)) != NULL) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_vif *vif = info->control.vif;
		struct mt7603_vif *mvif = (struct mt7603_vif *)vif->drv_priv;

		mt76_tx_queue_skb(dev, q, MT_TXQ_CAB, skb, &mvif->sta.wcid, NULL);
	}
	mt76_queue_kick(dev, q);
	spin_unlock(&q->lock);

	for (i = 0; i < ARRAY_SIZE(data.count); i++)
		mt76_wr(dev, MT_WF_ARB_CAB_COUNT_B0_REG(i),
			data.count[i] << MT_WF_ARB_CAB_COUNT_B0_SHIFT(i));

	mt76_wr(dev, MT_WF_ARB_CAB_START,
		MT_WF_ARB_CAB_START_BSSn(0) |
		(MT_WF_ARB_CAB_START_BSS0n(1) *
		 ((1 << (MT7603_MAX_INTERFACES - 1)) - 1)));
}

void mt7603_beacon_set_timer(struct mt7603_dev *dev, int idx, int intval)
{
	u32 pre_tbtt = MT7603_PRE_TBTT_TIME / 64;

	if (idx >= 0) {
		if (intval)
			dev->mt76.beacon_mask |= BIT(idx);
		else
			dev->mt76.beacon_mask &= ~BIT(idx);
	}

	if (!dev->mt76.beacon_mask || (!intval && idx < 0)) {
		mt7603_irq_disable(dev, MT_INT_MAC_IRQ3);
		mt76_clear(dev, MT_ARB_SCR, MT_ARB_SCR_BCNQ_OPMODE_MASK);
		mt76_wr(dev, MT_HW_INT_MASK(3), 0);
		return;
	}

	if (intval)
		dev->mt76.beacon_int = intval;
	mt76_wr(dev, MT_TBTT,
		FIELD_PREP(MT_TBTT_PERIOD, intval) | MT_TBTT_CAL_ENABLE);

	mt76_wr(dev, MT_TBTT_TIMER_CFG, 0x99); /* start timer */

	mt76_rmw_field(dev, MT_ARB_SCR, MT_ARB_SCR_BCNQ_OPMODE_MASK,
		       MT_BCNQ_OPMODE_AP);
	mt76_clear(dev, MT_ARB_SCR, MT_ARB_SCR_TBTT_BCN_PRIO);
	mt76_set(dev, MT_ARB_SCR, MT_ARB_SCR_TBTT_BCAST_PRIO);

	mt76_wr(dev, MT_PRE_TBTT, pre_tbtt);

	mt76_set(dev, MT_HW_INT_MASK(3),
		 MT_HW_INT3_PRE_TBTT0 | MT_HW_INT3_TBTT0);

	mt76_set(dev, MT_WF_ARB_BCN_START,
		 MT_WF_ARB_BCN_START_BSSn(0) |
		 ((dev->mt76.beacon_mask >> 1) *
		  MT_WF_ARB_BCN_START_BSS0n(1)));
	mt7603_irq_enable(dev, MT_INT_MAC_IRQ3);

	if (dev->mt76.beacon_mask & ~BIT(0))
		mt76_set(dev, MT_LPON_SBTOR(0), MT_LPON_SBTOR_SUB_BSS_EN);
	else
		mt76_clear(dev, MT_LPON_SBTOR(0), MT_LPON_SBTOR_SUB_BSS_EN);
}
