/* SPDX-License-Identifier: ISC */

#include "mt7603.h"
#include "mac.h"
#include "../dma.h"

static int
mt7603_init_tx_queue(struct mt7603_dev *dev, struct mt76_queue *q,
		     int idx, int n_desc)
{
	int ret;

	q->hw_idx = idx;
	q->regs = dev->mt76.mmio.regs + MT_TX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;

	ret = mt76_queue_alloc(dev, q);
	if (ret)
		return ret;

	mt7603_irq_enable(dev, MT_INT_TX_DONE(idx));

	return 0;
}

static void
mt7603_rx_loopback_skb(struct mt7603_dev *dev, struct sk_buff *skb)
{
	__le32 *txd = (__le32 *)skb->data;
	struct ieee80211_hdr *hdr;
	struct ieee80211_sta *sta;
	struct mt7603_sta *msta;
	struct mt76_wcid *wcid;
	void *priv;
	int idx;
	u32 val;
	u8 tid;

	if (skb->len < MT_TXD_SIZE + sizeof(struct ieee80211_hdr))
		goto free;

	val = le32_to_cpu(txd[1]);
	idx = FIELD_GET(MT_TXD1_WLAN_IDX, val);
	skb->priority = FIELD_GET(MT_TXD1_TID, val);

	if (idx >= MT7603_WTBL_STA - 1)
		goto free;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (!wcid)
		goto free;

	priv = msta = container_of(wcid, struct mt7603_sta, wcid);
	val = le32_to_cpu(txd[0]);
	skb_set_queue_mapping(skb, FIELD_GET(MT_TXD0_Q_IDX, val));

	val &= ~(MT_TXD0_P_IDX | MT_TXD0_Q_IDX);
	val |= FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_HW_QUEUE_MGMT);
	txd[0] = cpu_to_le32(val);

	sta = container_of(priv, struct ieee80211_sta, drv_priv);
	hdr = (struct ieee80211_hdr *) &skb->data[MT_TXD_SIZE];
	tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	ieee80211_sta_set_buffered(sta, tid, true);

	spin_lock_bh(&dev->ps_lock);
	__skb_queue_tail(&msta->psq, skb);
	if (skb_queue_len(&msta->psq) >= 64) {
		skb = __skb_dequeue(&msta->psq);
		dev_kfree_skb(skb);
	}
	spin_unlock_bh(&dev->ps_lock);
	return;

free:
	dev_kfree_skb(skb);
}

void mt7603_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;

	type = FIELD_GET(MT_RXD0_PKT_TYPE, le32_to_cpu(rxd[0]));

	if (q == MT_RXQ_MCU) {
		if (type == PKT_TYPE_RX_EVENT)
			mt76_mcu_rx_event(&dev->mt76, skb);
		else
			mt7603_rx_loopback_skb(dev, skb);
		return;
	}

	switch (type) {
	case PKT_TYPE_TXS:
		for (rxd++; rxd + 5 <= end; rxd += 5)
			mt7603_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_RX_EVENT:
		mt76_mcu_rx_event(&dev->mt76, skb);
		return;
	case PKT_TYPE_NORMAL:
		if (mt7603_mac_fill_rx(dev, skb) == 0) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		/* fall through */
	default:
		dev_kfree_skb(skb);
		break;
	}
}

static int
mt7603_init_rx_queue(struct mt7603_dev *dev, struct mt76_queue *q,
		     int idx, int n_desc, int bufsize)
{
	int ret;

	q->regs = dev->mt76.mmio.regs + MT_RX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->buf_size = bufsize;

	ret = mt76_queue_alloc(dev, q);
	if (ret)
		return ret;

	mt7603_irq_enable(dev, MT_INT_RX_DONE(idx));

	return 0;
}

static void
mt7603_tx_tasklet(unsigned long data)
{
	struct mt7603_dev *dev = (struct mt7603_dev *)data;
	int i;

	dev->tx_dma_check = 0;
	for (i = MT_TXQ_MCU; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, i, false);

	mt7603_irq_enable(dev, MT_INT_TX_DONE_ALL);
}

int mt7603_dma_init(struct mt7603_dev *dev)
{
	static const u8 wmm_queue_map[] = {
		[IEEE80211_AC_BK] = 0,
		[IEEE80211_AC_BE] = 1,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_VO] = 3,
	};
	int ret;
	int i;

	mt76_dma_attach(&dev->mt76);

	init_waitqueue_head(&dev->mt76.mmio.mcu.wait);
	skb_queue_head_init(&dev->mt76.mmio.mcu.res_q);

	tasklet_init(&dev->tx_tasklet, mt7603_tx_tasklet, (unsigned long)dev);

	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_RX_DMA_EN |
		   MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);
	mt7603_pse_client_reset(dev);

	for (i = 0; i < ARRAY_SIZE(wmm_queue_map); i++) {
		ret = mt7603_init_tx_queue(dev, &dev->mt76.q_tx[i],
					   wmm_queue_map[i],
					   MT_TX_RING_SIZE);
		if (ret)
			return ret;
	}

	ret = mt7603_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_PSD],
				   MT_TX_HW_QUEUE_MGMT, MT_TX_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7603_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_MCU],
				   MT_TX_HW_QUEUE_MCU, MT_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7603_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_BEACON],
				   MT_TX_HW_QUEUE_BCN, MT_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7603_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_CAB],
				   MT_TX_HW_QUEUE_BMC, MT_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7603_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MCU], 1,
				   MT_MCU_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	ret = mt7603_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MAIN], 0,
				   MT7603_RX_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	mt76_wr(dev, MT_DELAY_INT_CFG, 0);
	return mt76_init_queues(dev);
}

void mt7603_dma_cleanup(struct mt7603_dev *dev)
{
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_RX_DMA_EN |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	tasklet_kill(&dev->tx_tasklet);
	mt76_dma_cleanup(&dev->mt76);
}
