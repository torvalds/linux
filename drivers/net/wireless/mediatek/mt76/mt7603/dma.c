// SPDX-License-Identifier: ISC

#include "mt7603.h"
#include "mac.h"
#include "../dma.h"

static const u8 wmm_queue_map[] = {
	[IEEE80211_AC_BK] = 0,
	[IEEE80211_AC_BE] = 1,
	[IEEE80211_AC_VI] = 2,
	[IEEE80211_AC_VO] = 3,
};

static void
mt7603_rx_loopback_skb(struct mt7603_dev *dev, struct sk_buff *skb)
{
	static const u8 tid_to_ac[8] = {
		IEEE80211_AC_BE,
		IEEE80211_AC_BK,
		IEEE80211_AC_BK,
		IEEE80211_AC_BE,
		IEEE80211_AC_VI,
		IEEE80211_AC_VI,
		IEEE80211_AC_VO,
		IEEE80211_AC_VO
	};
	__le32 *txd = (__le32 *)skb->data;
	struct ieee80211_hdr *hdr;
	struct ieee80211_sta *sta;
	struct mt7603_sta *msta;
	struct mt76_wcid *wcid;
	u8 tid = 0, hwq = 0;
	void *priv;
	int idx;
	u32 val;

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

	sta = container_of(priv, struct ieee80211_sta, drv_priv);
	hdr = (struct ieee80211_hdr *)&skb->data[MT_TXD_SIZE];

	hwq = wmm_queue_map[IEEE80211_AC_BE];
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		tid = *ieee80211_get_qos_ctl(hdr) &
			 IEEE80211_QOS_CTL_TAG1D_MASK;
		u8 qid = tid_to_ac[tid];
		hwq = wmm_queue_map[qid];
		skb_set_queue_mapping(skb, qid);
	} else if (ieee80211_is_data(hdr->frame_control)) {
		skb_set_queue_mapping(skb, IEEE80211_AC_BE);
		hwq = wmm_queue_map[IEEE80211_AC_BE];
	} else {
		skb_pull(skb, MT_TXD_SIZE);
		if (!ieee80211_is_bufferable_mmpdu(skb))
			goto free;
		skb_push(skb, MT_TXD_SIZE);
		skb_set_queue_mapping(skb, MT_TXQ_PSD);
		hwq = MT_TX_HW_QUEUE_MGMT;
	}

	ieee80211_sta_set_buffered(sta, tid, true);

	val = le32_to_cpu(txd[0]);
	val &= ~(MT_TXD0_P_IDX | MT_TXD0_Q_IDX);
	val |= FIELD_PREP(MT_TXD0_Q_IDX, hwq);
	txd[0] = cpu_to_le32(val);

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
			 struct sk_buff *skb, u32 *info)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

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
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}

static int
mt7603_init_rx_queue(struct mt7603_dev *dev, struct mt76_queue *q,
		     int idx, int n_desc, int bufsize)
{
	int err;

	err = mt76_queue_alloc(dev, q, idx, n_desc, bufsize,
			       MT_RX_RING_BASE);
	if (err < 0)
		return err;

	mt7603_irq_enable(dev, MT_INT_RX_DONE(idx));

	return 0;
}

static int mt7603_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7603_dev *dev;
	int i;

	dev = container_of(napi, struct mt7603_dev, mt76.tx_napi);
	dev->tx_dma_check = 0;

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_WM], false);
	for (i = MT_TXQ_PSD; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], false);

	if (napi_complete_done(napi, 0))
		mt7603_irq_enable(dev, MT_INT_TX_DONE_ALL);

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_WM], false);
	for (i = MT_TXQ_PSD; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], false);

	mt7603_mac_sta_poll(dev);

	mt76_worker_schedule(&dev->mt76.tx_worker);

	return 0;
}

int mt7603_dma_init(struct mt7603_dev *dev)
{
	int ret;
	int i;

	mt76_dma_attach(&dev->mt76);

	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_RX_DMA_EN |
		   MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);
	mt7603_pse_client_reset(dev);

	for (i = 0; i < ARRAY_SIZE(wmm_queue_map); i++) {
		ret = mt76_init_tx_queue(&dev->mphy, i, wmm_queue_map[i],
					 MT7603_TX_RING_SIZE, MT_TX_RING_BASE,
					 NULL, 0);
		if (ret)
			return ret;
	}

	ret = mt76_init_tx_queue(&dev->mphy, MT_TXQ_PSD, MT_TX_HW_QUEUE_MGMT,
				 MT7603_PSD_RING_SIZE, MT_TX_RING_BASE, NULL, 0);
	if (ret)
		return ret;

	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WM, MT_TX_HW_QUEUE_MCU,
				  MT_MCU_RING_SIZE, MT_TX_RING_BASE);
	if (ret)
		return ret;

	ret = mt76_init_tx_queue(&dev->mphy, MT_TXQ_BEACON, MT_TX_HW_QUEUE_BCN,
				 MT_MCU_RING_SIZE, MT_TX_RING_BASE, NULL, 0);
	if (ret)
		return ret;

	ret = mt76_init_tx_queue(&dev->mphy, MT_TXQ_CAB, MT_TX_HW_QUEUE_BMC,
				 MT_MCU_RING_SIZE, MT_TX_RING_BASE, NULL, 0);
	if (ret)
		return ret;

	mt7603_irq_enable(dev,
			  MT_INT_TX_DONE(IEEE80211_AC_VO) |
			  MT_INT_TX_DONE(IEEE80211_AC_VI) |
			  MT_INT_TX_DONE(IEEE80211_AC_BE) |
			  MT_INT_TX_DONE(IEEE80211_AC_BK) |
			  MT_INT_TX_DONE(MT_TX_HW_QUEUE_MGMT) |
			  MT_INT_TX_DONE(MT_TX_HW_QUEUE_MCU) |
			  MT_INT_TX_DONE(MT_TX_HW_QUEUE_BCN) |
			  MT_INT_TX_DONE(MT_TX_HW_QUEUE_BMC));

	ret = mt7603_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MCU], 1,
				   MT7603_MCU_RX_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	ret = mt7603_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MAIN], 0,
				   MT7603_RX_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	mt76_wr(dev, MT_DELAY_INT_CFG, 0);
	ret = mt76_init_queues(dev, mt76_dma_rx_poll);
	if (ret)
		return ret;

	netif_napi_add_tx(&dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt7603_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	return 0;
}

void mt7603_dma_cleanup(struct mt7603_dev *dev)
{
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_RX_DMA_EN |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	mt76_dma_cleanup(&dev->mt76);
}
