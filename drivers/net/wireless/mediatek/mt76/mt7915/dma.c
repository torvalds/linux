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
}

static int mt7915_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7915_dev *dev;

	dev = container_of(napi, struct mt7915_dev, mt76.tx_napi);

	mt7915_tx_cleanup(dev);

	if (napi_complete_done(napi, 0))
		mt7915_irq_enable(dev, MT_INT_TX_DONE_MCU);

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

static u32 __mt7915_reg_addr(struct mt7915_dev *dev, u32 addr)
{
	static const struct {
		u32 phys;
		u32 mapped;
		u32 size;
	} fixed_map[] = {
		{ 0x54000000, 0x02000, 0x1000 }, /* WFDMA PCIE0 MCU DMA0 */
		{ 0x55000000, 0x03000, 0x1000 }, /* WFDMA PCIE0 MCU DMA1 */
		{ 0x58000000, 0x06000, 0x1000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
		{ 0x59000000, 0x07000, 0x1000 }, /* WFDMA PCIE1 MCU DMA1 */
		{ 0x7c000000, 0xf0000, 0x10000 }, /* CONN_INFRA */
		{ 0x7c020000, 0xd0000, 0x10000 }, /* CONN_INFRA, WFDMA */
		{ 0x80020000, 0xb0000, 0x10000 }, /* WF_TOP_MISC_OFF */
		{ 0x81020000, 0xc0000, 0x10000 }, /* WF_TOP_MISC_ON */
		{ 0x820c0000, 0x08000, 0x4000 }, /* WF_UMAC_TOP (PLE) */
		{ 0x820c8000, 0x0c000, 0x2000 }, /* WF_UMAC_TOP (PSE) */
		{ 0x820cc000, 0x0e000, 0x2000 }, /* WF_UMAC_TOP (PP) */
		{ 0x820ce000, 0x21c00, 0x0200 }, /* WF_LMAC_TOP (WF_SEC) */
		{ 0x820cf000, 0x22000, 0x1000 }, /* WF_LMAC_TOP (WF_PF) */
		{ 0x820d0000, 0x30000, 0x10000 }, /* WF_LMAC_TOP (WF_WTBLON) */
		{ 0x820e0000, 0x20000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
		{ 0x820e1000, 0x20400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
		{ 0x820e2000, 0x20800, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
		{ 0x820e3000, 0x20c00, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
		{ 0x820e4000, 0x21000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
		{ 0x820e5000, 0x21400, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
		{ 0x820e7000, 0x21e00, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
		{ 0x820e9000, 0x23400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
		{ 0x820ea000, 0x24000, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
		{ 0x820eb000, 0x24200, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
		{ 0x820ec000, 0x24600, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
		{ 0x820ed000, 0x24800, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
		{ 0x820f0000, 0xa0000, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_CFG) */
		{ 0x820f1000, 0xa0600, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_TRB) */
		{ 0x820f2000, 0xa0800, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_AGG) */
		{ 0x820f3000, 0xa0c00, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_ARB) */
		{ 0x820f4000, 0xa1000, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_TMAC) */
		{ 0x820f5000, 0xa1400, 0x0800 }, /* WF_LMAC_TOP BN1 (WF_RMAC) */
		{ 0x820f7000, 0xa1e00, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_DMA) */
		{ 0x820f9000, 0xa3400, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_WTBLOFF) */
		{ 0x820fa000, 0xa4000, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_ETBF) */
		{ 0x820fb000, 0xa4200, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_LPON) */
		{ 0x820fc000, 0xa4600, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_INT) */
		{ 0x820fd000, 0xa4800, 0x0800 }, /* WF_LMAC_TOP BN1 (WF_MIB) */
	};
	int i;

	if (addr < 0x100000)
		return addr;

	for (i = 0; i < ARRAY_SIZE(fixed_map); i++) {
		u32 ofs;

		if (addr < fixed_map[i].phys)
			continue;

		ofs = addr - fixed_map[i].phys;
		if (ofs > fixed_map[i].size)
			continue;

		return fixed_map[i].mapped + ofs;
	}

	if ((addr >= 0x18000000 && addr < 0x18c00000) ||
	    (addr >= 0x70000000 && addr < 0x78000000) ||
	    (addr >= 0x7c000000 && addr < 0x7c400000))
		return mt7915_reg_map_l1(dev, addr);

	return mt7915_reg_map_l2(dev, addr);
}

static u32 mt7915_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	u32 addr = __mt7915_reg_addr(dev, offset);

	return dev->bus_ops->rr(mdev, addr);
}

static void mt7915_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	u32 addr = __mt7915_reg_addr(dev, offset);

	dev->bus_ops->wr(mdev, addr, val);
}

static u32 mt7915_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	u32 addr = __mt7915_reg_addr(dev, offset);

	return dev->bus_ops->rmw(mdev, addr, mask, val);
}

int mt7915_dma_init(struct mt7915_dev *dev)
{
	/* Increase buffer size to receive large VHT/HE MPDUs */
	struct mt76_bus_ops *bus_ops;
	int rx_buf_size = MT_RX_BUF_SIZE * 2;
	int ret;

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->rr = mt7915_rr;
	bus_ops->wr = mt7915_wr;
	bus_ops->rmw = mt7915_rmw;
	dev->mt76.bus = bus_ops;

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
	mt7915_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_MCU |
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
