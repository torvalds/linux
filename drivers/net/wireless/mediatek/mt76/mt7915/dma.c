// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "../dma.h"
#include "mac.h"

int mt7915_init_tx_queues(struct mt7915_phy *phy, int idx, int n_desc, int ring_base)
{
	int i, err;

	err = mt76_init_tx_queue(phy->mt76, 0, idx, n_desc, ring_base);
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

static void mt7915_dma_config(struct mt7915_dev *dev)
{
#define Q_CONFIG(q, wfdma, int, id) do {		\
		if (wfdma)				\
			dev->wfdma_mask |= (1 << (q));	\
		dev->q_int_mask[(q)] = int;		\
		dev->q_id[(q)] = id;			\
	} while (0)

#define MCUQ_CONFIG(q, wfdma, int, id)	Q_CONFIG(q, (wfdma), (int), (id))
#define RXQ_CONFIG(q, wfdma, int, id)	Q_CONFIG(__RXQ(q), (wfdma), (int), (id))
#define TXQ_CONFIG(q, wfdma, int, id)	Q_CONFIG(__TXQ(q), (wfdma), (int), (id))

	if (is_mt7915(&dev->mt76)) {
		RXQ_CONFIG(MT_RXQ_MAIN, WFDMA0, MT_INT_RX_DONE_BAND0, MT7915_RXQ_BAND0);
		RXQ_CONFIG(MT_RXQ_MCU, WFDMA1, MT_INT_RX_DONE_WM, MT7915_RXQ_MCU_WM);
		RXQ_CONFIG(MT_RXQ_MCU_WA, WFDMA1, MT_INT_RX_DONE_WA, MT7915_RXQ_MCU_WA);
		RXQ_CONFIG(MT_RXQ_EXT, WFDMA0, MT_INT_RX_DONE_BAND1, MT7915_RXQ_BAND1);
		RXQ_CONFIG(MT_RXQ_EXT_WA, WFDMA1, MT_INT_RX_DONE_WA_EXT, MT7915_RXQ_MCU_WA_EXT);
		RXQ_CONFIG(MT_RXQ_MAIN_WA, WFDMA1, MT_INT_RX_DONE_WA_MAIN, MT7915_RXQ_MCU_WA);
		TXQ_CONFIG(0, WFDMA1, MT_INT_TX_DONE_BAND0, MT7915_TXQ_BAND0);
		TXQ_CONFIG(1, WFDMA1, MT_INT_TX_DONE_BAND1, MT7915_TXQ_BAND1);
		MCUQ_CONFIG(MT_MCUQ_WM, WFDMA1, MT_INT_TX_DONE_MCU_WM, MT7915_TXQ_MCU_WM);
		MCUQ_CONFIG(MT_MCUQ_WA, WFDMA1, MT_INT_TX_DONE_MCU_WA, MT7915_TXQ_MCU_WA);
		MCUQ_CONFIG(MT_MCUQ_FWDL, WFDMA1, MT_INT_TX_DONE_FWDL, MT7915_TXQ_FWDL);
	} else {
		RXQ_CONFIG(MT_RXQ_MAIN, WFDMA0, MT_INT_RX_DONE_BAND0_MT7916, MT7916_RXQ_BAND0);
		RXQ_CONFIG(MT_RXQ_MCU, WFDMA0, MT_INT_RX_DONE_WM, MT7916_RXQ_MCU_WM);
		RXQ_CONFIG(MT_RXQ_MCU_WA, WFDMA0, MT_INT_RX_DONE_WA, MT7916_RXQ_MCU_WA);
		RXQ_CONFIG(MT_RXQ_EXT, WFDMA0, MT_INT_RX_DONE_BAND1_MT7916, MT7916_RXQ_BAND1);
		RXQ_CONFIG(MT_RXQ_EXT_WA, WFDMA0, MT_INT_RX_DONE_WA_EXT_MT7916, MT7916_RXQ_MCU_WA_EXT);
		RXQ_CONFIG(MT_RXQ_MAIN_WA, WFDMA0, MT_INT_RX_DONE_WA_MAIN_MT7916, MT7916_RXQ_MCU_WA_MAIN);
		TXQ_CONFIG(0, WFDMA0, MT_INT_TX_DONE_BAND0, MT7915_TXQ_BAND0);
		TXQ_CONFIG(1, WFDMA0, MT_INT_TX_DONE_BAND1, MT7915_TXQ_BAND1);
		MCUQ_CONFIG(MT_MCUQ_WM, WFDMA0, MT_INT_TX_DONE_MCU_WM, MT7915_TXQ_MCU_WM);
		MCUQ_CONFIG(MT_MCUQ_WA, WFDMA0, MT_INT_TX_DONE_MCU_WA_MT7916, MT7915_TXQ_MCU_WA);
		MCUQ_CONFIG(MT_MCUQ_FWDL, WFDMA0, MT_INT_TX_DONE_FWDL, MT7915_TXQ_FWDL);
	}
}

static void __mt7915_dma_prefetch(struct mt7915_dev *dev, u32 ofs)
{
#define PREFETCH(_base, _depth)	((_base) << 16 | (_depth))
	u32 base = 0;

	/* prefetch SRAM wrapping boundary for tx/rx ring. */
	mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_FWDL) + ofs, PREFETCH(0x0, 0x4));
	mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_WM) + ofs, PREFETCH(0x40, 0x4));
	mt76_wr(dev, MT_TXQ_EXT_CTRL(0) + ofs, PREFETCH(0x80, 0x4));
	mt76_wr(dev, MT_TXQ_EXT_CTRL(1) + ofs, PREFETCH(0xc0, 0x4));
	mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_WA) + ofs, PREFETCH(0x100, 0x4));

	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MCU) + ofs, PREFETCH(0x140, 0x4));
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MCU_WA) + ofs, PREFETCH(0x180, 0x4));
	if (!is_mt7915(&dev->mt76)) {
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MAIN_WA) + ofs, PREFETCH(0x1c0, 0x4));
		base = 0x40;
	}
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_EXT_WA) + ofs, PREFETCH(0x1c0 + base, 0x4));
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MAIN) + ofs, PREFETCH(0x200 + base, 0x4));
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_EXT) + ofs, PREFETCH(0x240 + base, 0x4));

	/* for mt7915, the ring which is next the last
	 * used ring must be initialized.
	 */
	if (is_mt7915(&dev->mt76)) {
		ofs += 0x4;
		mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_WA) + ofs, PREFETCH(0x140, 0x0));
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_EXT_WA) + ofs, PREFETCH(0x200 + base, 0x0));
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_EXT) + ofs, PREFETCH(0x280 + base, 0x0));
	}
}

void mt7915_dma_prefetch(struct mt7915_dev *dev)
{
	__mt7915_dma_prefetch(dev, 0);
	if (dev->hif2)
		__mt7915_dma_prefetch(dev, MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0));
}

static void mt7915_dma_disable(struct mt7915_dev *dev, bool rst)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 hif1_ofs = 0;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* reset */
	if (rst) {
		mt76_clear(dev, MT_WFDMA0_RST,
			   MT_WFDMA0_RST_DMASHDL_ALL_RST |
			   MT_WFDMA0_RST_LOGIC_RST);

		mt76_set(dev, MT_WFDMA0_RST,
			 MT_WFDMA0_RST_DMASHDL_ALL_RST |
			 MT_WFDMA0_RST_LOGIC_RST);

		if (is_mt7915(mdev)) {
			mt76_clear(dev, MT_WFDMA1_RST,
				   MT_WFDMA1_RST_DMASHDL_ALL_RST |
				   MT_WFDMA1_RST_LOGIC_RST);

			mt76_set(dev, MT_WFDMA1_RST,
				 MT_WFDMA1_RST_DMASHDL_ALL_RST |
				 MT_WFDMA1_RST_LOGIC_RST);
		}

		if (dev->hif2) {
			mt76_clear(dev, MT_WFDMA0_RST + hif1_ofs,
				   MT_WFDMA0_RST_DMASHDL_ALL_RST |
				   MT_WFDMA0_RST_LOGIC_RST);

			mt76_set(dev, MT_WFDMA0_RST + hif1_ofs,
				 MT_WFDMA0_RST_DMASHDL_ALL_RST |
				 MT_WFDMA0_RST_LOGIC_RST);

			if (is_mt7915(mdev)) {
				mt76_clear(dev, MT_WFDMA1_RST + hif1_ofs,
					   MT_WFDMA1_RST_DMASHDL_ALL_RST |
					   MT_WFDMA1_RST_LOGIC_RST);

				mt76_set(dev, MT_WFDMA1_RST + hif1_ofs,
					 MT_WFDMA1_RST_DMASHDL_ALL_RST |
					 MT_WFDMA1_RST_LOGIC_RST);
			}
		}
	}

	/* disable */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	if (is_mt7915(mdev))
		mt76_clear(dev, MT_WFDMA1_GLO_CFG,
			   MT_WFDMA1_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA1_GLO_CFG_RX_DMA_EN |
			   MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
			   MT_WFDMA1_GLO_CFG_OMIT_RX_INFO |
			   MT_WFDMA1_GLO_CFG_OMIT_RX_INFO_PFET2);

	if (dev->hif2) {
		mt76_clear(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_RX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
			   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
			   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

		if (is_mt7915(mdev))
			mt76_clear(dev, MT_WFDMA1_GLO_CFG + hif1_ofs,
				   MT_WFDMA1_GLO_CFG_TX_DMA_EN |
				   MT_WFDMA1_GLO_CFG_RX_DMA_EN |
				   MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
				   MT_WFDMA1_GLO_CFG_OMIT_RX_INFO |
				   MT_WFDMA1_GLO_CFG_OMIT_RX_INFO_PFET2);
	}
}

static int mt7915_dma_enable(struct mt7915_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 hif1_ofs = 0;
	u32 irq_mask;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* reset dma idx */
	mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	if (is_mt7915(mdev))
		mt76_wr(dev, MT_WFDMA1_RST_DTX_PTR, ~0);
	if (dev->hif2) {
		mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR + hif1_ofs, ~0);
		if (is_mt7915(mdev))
			mt76_wr(dev, MT_WFDMA1_RST_DTX_PTR + hif1_ofs, ~0);
	}

	/* configure delay interrupt off */
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);
	if (is_mt7915(mdev)) {
		mt76_wr(dev, MT_WFDMA1_PRI_DLY_INT_CFG0, 0);
	} else {
		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG1, 0);
		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG2, 0);
	}

	if (dev->hif2) {
		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0 + hif1_ofs, 0);
		if (is_mt7915(mdev)) {
			mt76_wr(dev, MT_WFDMA1_PRI_DLY_INT_CFG0 +
				hif1_ofs, 0);
		} else {
			mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG1 +
				hif1_ofs, 0);
			mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG2 +
				hif1_ofs, 0);
		}
	}

	/* configure perfetch settings */
	mt7915_dma_prefetch(dev);

	/* hif wait WFDMA idle */
	mt76_set(dev, MT_WFDMA0_BUSY_ENA,
		 MT_WFDMA0_BUSY_ENA_TX_FIFO0 |
		 MT_WFDMA0_BUSY_ENA_TX_FIFO1 |
		 MT_WFDMA0_BUSY_ENA_RX_FIFO);

	if (is_mt7915(mdev))
		mt76_set(dev, MT_WFDMA1_BUSY_ENA,
			 MT_WFDMA1_BUSY_ENA_TX_FIFO0 |
			 MT_WFDMA1_BUSY_ENA_TX_FIFO1 |
			 MT_WFDMA1_BUSY_ENA_RX_FIFO);

	if (dev->hif2) {
		mt76_set(dev, MT_WFDMA0_BUSY_ENA + hif1_ofs,
			 MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO0 |
			 MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO1 |
			 MT_WFDMA0_PCIE1_BUSY_ENA_RX_FIFO);

		if (is_mt7915(mdev))
			mt76_set(dev, MT_WFDMA1_BUSY_ENA + hif1_ofs,
				 MT_WFDMA1_PCIE1_BUSY_ENA_TX_FIFO0 |
				 MT_WFDMA1_PCIE1_BUSY_ENA_TX_FIFO1 |
				 MT_WFDMA1_PCIE1_BUSY_ENA_RX_FIFO);
	}

	mt76_poll(dev, MT_WFDMA_EXT_CSR_HIF_MISC,
		  MT_WFDMA_EXT_CSR_HIF_MISC_BUSY, 0, 1000);

	/* set WFDMA Tx/Rx */
	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		 MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	if (is_mt7915(mdev))
		mt76_set(dev, MT_WFDMA1_GLO_CFG,
			 MT_WFDMA1_GLO_CFG_TX_DMA_EN |
			 MT_WFDMA1_GLO_CFG_RX_DMA_EN |
			 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
			 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);

	if (dev->hif2) {
		mt76_set(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			 MT_WFDMA0_GLO_CFG_RX_DMA_EN |
			 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
			 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

		if (is_mt7915(mdev))
			mt76_set(dev, MT_WFDMA1_GLO_CFG + hif1_ofs,
				 MT_WFDMA1_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA1_GLO_CFG_RX_DMA_EN |
				 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
				 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);

		mt76_set(dev, MT_WFDMA_HOST_CONFIG,
			 MT_WFDMA_HOST_CONFIG_PDMA_BAND);
	}

	/* enable interrupts for TX/RX rings */
	irq_mask = MT_INT_RX_DONE_MCU |
		   MT_INT_TX_DONE_MCU |
		   MT_INT_MCU_CMD;

	if (!dev->phy.band_idx)
		irq_mask |= MT_INT_BAND0_RX_DONE;

	if (dev->dbdc_support || dev->phy.band_idx)
		irq_mask |= MT_INT_BAND1_RX_DONE;

	mt7915_irq_enable(dev, irq_mask);

	return 0;
}

int mt7915_dma_init(struct mt7915_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 hif1_ofs = 0;
	int ret;

	mt7915_dma_config(dev);

	mt76_dma_attach(&dev->mt76);

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	mt7915_dma_disable(dev, true);

	/* init tx queue */
	ret = mt7915_init_tx_queues(&dev->phy,
				    MT_TXQ_ID(dev->phy.band_idx),
				    MT7915_TX_RING_SIZE,
				    MT_TXQ_RING_BASE(0));
	if (ret)
		return ret;

	/* command to WM */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WM,
				  MT_MCUQ_ID(MT_MCUQ_WM),
				  MT7915_TX_MCU_RING_SIZE,
				  MT_MCUQ_RING_BASE(MT_MCUQ_WM));
	if (ret)
		return ret;

	/* command to WA */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WA,
				  MT_MCUQ_ID(MT_MCUQ_WA),
				  MT7915_TX_MCU_RING_SIZE,
				  MT_MCUQ_RING_BASE(MT_MCUQ_WA));
	if (ret)
		return ret;

	/* firmware download */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_FWDL,
				  MT_MCUQ_ID(MT_MCUQ_FWDL),
				  MT7915_TX_FWDL_RING_SIZE,
				  MT_MCUQ_RING_BASE(MT_MCUQ_FWDL));
	if (ret)
		return ret;

	/* event from WM */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU],
			       MT_RXQ_ID(MT_RXQ_MCU),
			       MT7915_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE,
			       MT_RXQ_RING_BASE(MT_RXQ_MCU));
	if (ret)
		return ret;

	/* event from WA */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       MT_RXQ_ID(MT_RXQ_MCU_WA),
			       MT7915_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE,
			       MT_RXQ_RING_BASE(MT_RXQ_MCU_WA));
	if (ret)
		return ret;

	/* rx data queue for band0 */
	if (!dev->phy.band_idx) {
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN],
				       MT_RXQ_ID(MT_RXQ_MAIN),
				       MT7915_RX_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_MAIN));
		if (ret)
			return ret;
	}

	/* tx free notify event from WA for band0 */
	if (!is_mt7915(mdev)) {
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN_WA],
				       MT_RXQ_ID(MT_RXQ_MAIN_WA),
				       MT7915_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_MAIN_WA));
		if (ret)
			return ret;
	}

	if (dev->dbdc_support || dev->phy.band_idx) {
		/* rx data queue for band1 */
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_EXT],
				       MT_RXQ_ID(MT_RXQ_EXT),
				       MT7915_RX_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_EXT) + hif1_ofs);
		if (ret)
			return ret;

		/* tx free notify event from WA for band1 */
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_EXT_WA],
				       MT_RXQ_ID(MT_RXQ_EXT_WA),
				       MT7915_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_EXT_WA) + hif1_ofs);
		if (ret)
			return ret;
	}

	ret = mt76_init_queues(dev, mt76_dma_rx_poll);
	if (ret < 0)
		return ret;

	netif_tx_napi_add(&dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt7915_poll_tx, NAPI_POLL_WEIGHT);
	napi_enable(&dev->mt76.tx_napi);

	mt7915_dma_enable(dev);

	return 0;
}

void mt7915_dma_cleanup(struct mt7915_dev *dev)
{
	mt7915_dma_disable(dev, true);

	mt76_dma_cleanup(&dev->mt76);
}
