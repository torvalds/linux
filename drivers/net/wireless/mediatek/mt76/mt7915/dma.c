// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "../dma.h"
#include "mac.h"

static int
mt7915_init_tx_queues(struct mt7915_dev *dev, int n_desc)
{
	struct mt76_sw_queue *q;
	struct mt76_queue *hwq;
	int err, i;

	hwq = devm_kzalloc(dev->mt76.dev, sizeof(*hwq), GFP_KERNEL);
	if (!hwq)
		return -ENOMEM;

	err = mt76_queue_alloc(dev, hwq, MT7915_TXQ_BAND0, n_desc, 0,
			       MT_TX_RING_BASE);
	if (err < 0)
		return err;

	for (i = 0; i < MT_TXQ_MCU; i++) {
		q = &dev->mt76.q_tx[i];
		INIT_LIST_HEAD(&q->swq);
		q->q = hwq;
	}

	return 0;
}

static int
mt7915_init_mcu_queue(struct mt7915_dev *dev, struct mt76_sw_queue *q,
		      int idx, int n_desc)
{
	struct mt76_queue *hwq;
	int err;

	hwq = devm_kzalloc(dev->mt76.dev, sizeof(*hwq), GFP_KERNEL);
	if (!hwq)
		return -ENOMEM;

	err = mt76_queue_alloc(dev, hwq, idx, n_desc, 0, MT_TX_RING_BASE);
	if (err < 0)
		return err;

	INIT_LIST_HEAD(&q->swq);
	q->q = hwq;

	return 0;
}

void mt7915_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	enum rx_pkt_type type;

	type = FIELD_GET(MT_RXD0_PKT_TYPE, le32_to_cpu(rxd[0]));

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7915_mac_tx_free(dev, skb);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7915_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_NORMAL:
		if (!mt7915_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		/* fall through */
	default:
		dev_kfree_skb(skb);
		break;
	}
}

static void
mt7915_tx_cleanup(struct mt7915_dev *dev)
{
	mt76_queue_tx_cleanup(dev, MT_TXQ_MCU, false);
	mt76_queue_tx_cleanup(dev, MT_TXQ_MCU_WA, false);
	mt76_queue_tx_cleanup(dev, MT_TXQ_PSD, false);
	mt76_queue_tx_cleanup(dev, MT_TXQ_BE, false);
}

static int mt7915_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7915_dev *dev;

	dev = container_of(napi, struct mt7915_dev, mt76.tx_napi);

	mt7915_tx_cleanup(dev);
	mt7915_mac_sta_poll(dev);

	tasklet_schedule(&dev->mt76.tx_tasklet);

	if (napi_complete_done(napi, 0))
		mt7915_irq_enable(dev, MT_INT_TX_DONE_ALL);

	return 0;
}

void mt7915_dma_prefetch(struct mt7915_dev *dev)
{
#define PREFETCH(base, depth)	((base) << 16 | (depth))

	mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING1_EXT_CTRL, PREFETCH(0x40, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x80, 0x0));

	mt76_wr(dev, MT_WFDMA1_TX_RING0_EXT_CTRL, PREFETCH(0x80, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING1_EXT_CTRL, PREFETCH(0xc0, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING2_EXT_CTRL, PREFETCH(0x100, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING3_EXT_CTRL, PREFETCH(0x140, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING4_EXT_CTRL, PREFETCH(0x180, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING5_EXT_CTRL, PREFETCH(0x1c0, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING6_EXT_CTRL, PREFETCH(0x200, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING7_EXT_CTRL, PREFETCH(0x240, 0x4));

	mt76_wr(dev, MT_WFDMA1_TX_RING16_EXT_CTRL, PREFETCH(0x280, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING17_EXT_CTRL, PREFETCH(0x2c0, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING18_EXT_CTRL, PREFETCH(0x300, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING19_EXT_CTRL, PREFETCH(0x340, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING20_EXT_CTRL, PREFETCH(0x380, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING21_EXT_CTRL, PREFETCH(0x3c0, 0x0));

	mt76_wr(dev, MT_WFDMA1_RX_RING0_EXT_CTRL, PREFETCH(0x3c0, 0x4));
	mt76_wr(dev, MT_WFDMA1_RX_RING1_EXT_CTRL, PREFETCH(0x400, 0x4));
	mt76_wr(dev, MT_WFDMA1_RX_RING2_EXT_CTRL, PREFETCH(0x440, 0x4));
	mt76_wr(dev, MT_WFDMA1_RX_RING3_EXT_CTRL, PREFETCH(0x480, 0x0));
}

int mt7915_dma_init(struct mt7915_dev *dev)
{
	/* Increase buffer size to receive large VHT/HE MPDUs */
	int rx_buf_size = MT_RX_BUF_SIZE * 2;
	int ret;

	mt76_dma_attach(&dev->mt76);

	/* configure global setting */
	mt76_set(dev, MT_WFDMA1_GLO_CFG,
		 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);

	/* configure perfetch settings */
	mt7915_dma_prefetch(dev);

	/* reset dma idx */
	mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	mt76_wr(dev, MT_WFDMA1_RST_DTX_PTR, ~0);

	/* configure delay interrupt */
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);
	mt76_wr(dev, MT_WFDMA1_PRI_DLY_INT_CFG0, 0);

	/* init tx queue */
	ret = mt7915_init_tx_queues(dev, MT7915_TX_RING_SIZE);
	if (ret)
		return ret;

	/* command to WM */
	ret = mt7915_init_mcu_queue(dev, &dev->mt76.q_tx[MT_TXQ_MCU],
				    MT7915_TXQ_MCU_WM,
				    MT7915_TX_MCU_RING_SIZE);
	if (ret)
		return ret;

	/* command to WA */
	ret = mt7915_init_mcu_queue(dev, &dev->mt76.q_tx[MT_TXQ_MCU_WA],
				    MT7915_TXQ_MCU_WA,
				    MT7915_TX_MCU_RING_SIZE);
	if (ret)
		return ret;

	/* firmware download */
	ret = mt7915_init_mcu_queue(dev, &dev->mt76.q_tx[MT_TXQ_FWDL],
				    MT7915_TXQ_FWDL,
				    MT7915_TX_FWDL_RING_SIZE);
	if (ret)
		return ret;

	/* event from WM */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU],
			       MT7915_RXQ_MCU_WM, MT7915_RX_MCU_RING_SIZE,
			       rx_buf_size, MT_RX_EVENT_RING_BASE);
	if (ret)
		return ret;

	/* event from WA */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       MT7915_RXQ_MCU_WA, MT7915_RX_MCU_RING_SIZE,
			       rx_buf_size, MT_RX_EVENT_RING_BASE);
	if (ret)
		return ret;

	/* rx data */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN], 0,
			       MT7915_RX_RING_SIZE, rx_buf_size,
			       MT_RX_DATA_RING_BASE);
	if (ret)
		return ret;

	ret = mt76_init_queues(dev);
	if (ret < 0)
		return ret;

	netif_tx_napi_add(&dev->mt76.napi_dev, &dev->mt76.tx_napi,
			  mt7915_poll_tx, NAPI_POLL_WEIGHT);
	napi_enable(&dev->mt76.tx_napi);

	/* hif wait WFDMA idle */
	mt76_set(dev, MT_WFDMA0_BUSY_ENA,
		 MT_WFDMA0_BUSY_ENA_TX_FIFO0 |
		 MT_WFDMA0_BUSY_ENA_TX_FIFO1 |
		 MT_WFDMA0_BUSY_ENA_RX_FIFO);

	mt76_set(dev, MT_WFDMA1_BUSY_ENA,
		 MT_WFDMA1_BUSY_ENA_TX_FIFO0 |
		 MT_WFDMA1_BUSY_ENA_TX_FIFO1 |
		 MT_WFDMA1_BUSY_ENA_RX_FIFO);

	mt76_set(dev, MT_WFDMA0_PCIE1_BUSY_ENA,
		 MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO0 |
		 MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO1 |
		 MT_WFDMA0_PCIE1_BUSY_ENA_RX_FIFO);

	mt76_set(dev, MT_WFDMA1_PCIE1_BUSY_ENA,
		 MT_WFDMA1_PCIE1_BUSY_ENA_TX_FIFO0 |
		 MT_WFDMA1_PCIE1_BUSY_ENA_TX_FIFO1 |
		 MT_WFDMA1_PCIE1_BUSY_ENA_RX_FIFO);

	mt76_poll(dev, MT_WFDMA_EXT_CSR_HIF_MISC,
		  MT_WFDMA_EXT_CSR_HIF_MISC_BUSY, 0, 1000);

	/* set WFDMA Tx/Rx */
	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	mt76_set(dev, MT_WFDMA1_GLO_CFG,
		 MT_WFDMA1_GLO_CFG_TX_DMA_EN | MT_WFDMA1_GLO_CFG_RX_DMA_EN);

	/* enable interrupts for TX/RX rings */
	mt7915_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
			  MT_INT_MCU_CMD);

	return 0;
}

void mt7915_dma_cleanup(struct mt7915_dev *dev)
{
	/* disable */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	mt76_clear(dev, MT_WFDMA1_GLO_CFG,
		   MT_WFDMA1_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA1_GLO_CFG_RX_DMA_EN);

	/* reset */
	mt76_clear(dev, MT_WFDMA1_RST,
		   MT_WFDMA1_RST_DMASHDL_ALL_RST |
		   MT_WFDMA1_RST_LOGIC_RST);

	mt76_set(dev, MT_WFDMA1_RST,
		 MT_WFDMA1_RST_DMASHDL_ALL_RST |
		 MT_WFDMA1_RST_LOGIC_RST);

	mt76_clear(dev, MT_WFDMA0_RST,
		   MT_WFDMA0_RST_DMASHDL_ALL_RST |
		   MT_WFDMA0_RST_LOGIC_RST);

	mt76_set(dev, MT_WFDMA0_RST,
		 MT_WFDMA0_RST_DMASHDL_ALL_RST |
		 MT_WFDMA0_RST_LOGIC_RST);

	tasklet_kill(&dev->mt76.tx_tasklet);
	mt76_dma_cleanup(&dev->mt76);
}
