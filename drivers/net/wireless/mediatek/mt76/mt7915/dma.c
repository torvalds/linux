// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "../dma.h"
#include "mac.h"

int mt7915_init_tx_queues(struct mt7915_phy *phy, int idx, int n_desc)
{
	int i, err;

	err = mt76_init_tx_queue(phy->mt76, 0, idx, n_desc, MT_TX_RING_BASE);
	if (err < 0)
		return err;

	for (i = 0; i <= MT_TXQ_PSD; i++)
		phy->mt76->q_tx[i] = phy->mt76->q_tx[0];

	return 0;
}

static void
mt7915_tx_cleanup(struct mt7915_dev *dev)
{
	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_WM], false);
	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_WA], false);
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

static void __mt7915_dma_prefetch(struct mt7915_dev *dev, u32 ofs)
{
#define PREFETCH(base, depth)	((base) << 16 | (depth))

	mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL + ofs, PREFETCH(0x0, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING1_EXT_CTRL + ofs, PREFETCH(0x40, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL + ofs, PREFETCH(0x80, 0x0));

	mt76_wr(dev, MT_WFDMA1_TX_RING0_EXT_CTRL + ofs, PREFETCH(0x80, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING1_EXT_CTRL + ofs, PREFETCH(0xc0, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING2_EXT_CTRL + ofs, PREFETCH(0x100, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING3_EXT_CTRL + ofs, PREFETCH(0x140, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING4_EXT_CTRL + ofs, PREFETCH(0x180, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING5_EXT_CTRL + ofs, PREFETCH(0x1c0, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING6_EXT_CTRL + ofs, PREFETCH(0x200, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING7_EXT_CTRL + ofs, PREFETCH(0x240, 0x4));

	mt76_wr(dev, MT_WFDMA1_TX_RING16_EXT_CTRL + ofs, PREFETCH(0x280, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING17_EXT_CTRL + ofs, PREFETCH(0x2c0, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING18_EXT_CTRL + ofs, PREFETCH(0x300, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING19_EXT_CTRL + ofs, PREFETCH(0x340, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING20_EXT_CTRL + ofs, PREFETCH(0x380, 0x4));
	mt76_wr(dev, MT_WFDMA1_TX_RING21_EXT_CTRL + ofs, PREFETCH(0x3c0, 0x0));

	mt76_wr(dev, MT_WFDMA1_RX_RING0_EXT_CTRL + ofs, PREFETCH(0x3c0, 0x4));
	mt76_wr(dev, MT_WFDMA1_RX_RING1_EXT_CTRL + ofs, PREFETCH(0x400, 0x4));
	mt76_wr(dev, MT_WFDMA1_RX_RING2_EXT_CTRL + ofs, PREFETCH(0x440, 0x4));
	mt76_wr(dev, MT_WFDMA1_RX_RING3_EXT_CTRL + ofs, PREFETCH(0x480, 0x0));
}

void mt7915_dma_prefetch(struct mt7915_dev *dev)
{
	__mt7915_dma_prefetch(dev, 0);
	if (dev->hif2)
		__mt7915_dma_prefetch(dev, MT_WFDMA1_PCIE1_BASE - MT_WFDMA1_BASE);
}

int mt7915_dma_init(struct mt7915_dev *dev)
{
	u32 hif1_ofs = 0;
	int ret;

	mt76_dma_attach(&dev->mt76);

	if (dev->hif2)
		hif1_ofs = MT_WFDMA1_PCIE1_BASE - MT_WFDMA1_BASE;

	/* configure global setting */
	mt76_set(dev, MT_WFDMA1_GLO_CFG,
		 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);

	/* reset dma idx */
	mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	mt76_wr(dev, MT_WFDMA1_RST_DTX_PTR, ~0);

	/* configure delay interrupt */
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);
	mt76_wr(dev, MT_WFDMA1_PRI_DLY_INT_CFG0, 0);

	if (dev->hif2) {
		mt76_set(dev, MT_WFDMA1_GLO_CFG + hif1_ofs,
			 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
			 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);

		mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR + hif1_ofs, ~0);
		mt76_wr(dev, MT_WFDMA1_RST_DTX_PTR + hif1_ofs, ~0);

		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0 + hif1_ofs, 0);
		mt76_wr(dev, MT_WFDMA1_PRI_DLY_INT_CFG0 + hif1_ofs, 0);
	}

	/* configure perfetch settings */
	mt7915_dma_prefetch(dev);

	/* init tx queue */
	ret = mt7915_init_tx_queues(&dev->phy, MT7915_TXQ_BAND0,
				    MT7915_TX_RING_SIZE);
	if (ret)
		return ret;

	/* command to WM */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WM, MT7915_TXQ_MCU_WM,
				  MT7915_TX_MCU_RING_SIZE, MT_TX_RING_BASE);
	if (ret)
		return ret;

	/* command to WA */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WA, MT7915_TXQ_MCU_WA,
				  MT7915_TX_MCU_RING_SIZE, MT_TX_RING_BASE);
	if (ret)
		return ret;

	/* firmware download */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_FWDL, MT7915_TXQ_FWDL,
				  MT7915_TX_FWDL_RING_SIZE, MT_TX_RING_BASE);
	if (ret)
		return ret;

	/* event from WM */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU],
			       MT7915_RXQ_MCU_WM, MT7915_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE, MT_RX_EVENT_RING_BASE);
	if (ret)
		return ret;

	/* event from WA */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       MT7915_RXQ_MCU_WA, MT7915_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE, MT_RX_EVENT_RING_BASE);
	if (ret)
		return ret;

	/* rx data queue */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN],
			       MT7915_RXQ_BAND0, MT7915_RX_RING_SIZE,
			       MT_RX_BUF_SIZE, MT_RX_DATA_RING_BASE);
	if (ret)
		return ret;

	if (dev->dbdc_support) {
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_EXT],
				       MT7915_RXQ_BAND1, MT7915_RX_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RX_DATA_RING_BASE + hif1_ofs);
		if (ret)
			return ret;

		/* event from WA */
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_EXT_WA],
				       MT7915_RXQ_MCU_WA_EXT,
				       MT7915_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RX_EVENT_RING_BASE + hif1_ofs);
		if (ret)
			return ret;
	}

	ret = mt76_init_queues(dev, mt76_dma_rx_poll);
	if (ret < 0)
		return ret;

	netif_tx_napi_add(&dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
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

	if (dev->hif2) {
		mt76_set(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			 (MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			  MT_WFDMA0_GLO_CFG_RX_DMA_EN));
		mt76_set(dev, MT_WFDMA1_GLO_CFG + hif1_ofs,
			 (MT_WFDMA1_GLO_CFG_TX_DMA_EN |
			  MT_WFDMA1_GLO_CFG_RX_DMA_EN));
		mt76_set(dev, MT_WFDMA_HOST_CONFIG,
			 MT_WFDMA_HOST_CONFIG_PDMA_BAND);
	}

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

	mt76_dma_cleanup(&dev->mt76);
}
