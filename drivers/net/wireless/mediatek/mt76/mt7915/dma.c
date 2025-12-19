// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "../dma.h"
#include "mac.h"

static int
mt7915_init_tx_queues(struct mt7915_phy *phy, int idx, int n_desc, int ring_base)
{
	struct mt7915_dev *dev = phy->dev;
	struct mtk_wed_device *wed = NULL;

	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		if (is_mt798x(&dev->mt76))
			ring_base += MT_TXQ_ID(0) * MT_RING_SIZE;
		else
			ring_base = MT_WED_TX_RING_BASE;

		idx -= MT_TXQ_ID(0);
		wed = &dev->mt76.mmio.wed;
	}

	return mt76_connac_init_tx_queues(phy->mt76, idx, n_desc, ring_base,
					  wed, MT_WED_Q_TX(idx));
}

static int mt7915_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7915_dev *dev;

	dev = container_of(napi, struct mt7915_dev, mt76.tx_napi);

	mt76_connac_tx_cleanup(&dev->mt76);
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
		RXQ_CONFIG(MT_RXQ_MAIN, WFDMA0, MT_INT_RX_DONE_BAND0,
			   MT7915_RXQ_BAND0);
		RXQ_CONFIG(MT_RXQ_MCU, WFDMA1, MT_INT_RX_DONE_WM,
			   MT7915_RXQ_MCU_WM);
		RXQ_CONFIG(MT_RXQ_MCU_WA, WFDMA1, MT_INT_RX_DONE_WA,
			   MT7915_RXQ_MCU_WA);
		RXQ_CONFIG(MT_RXQ_BAND1, WFDMA0, MT_INT_RX_DONE_BAND1,
			   MT7915_RXQ_BAND1);
		RXQ_CONFIG(MT_RXQ_BAND1_WA, WFDMA1, MT_INT_RX_DONE_WA_EXT,
			   MT7915_RXQ_MCU_WA_EXT);
		RXQ_CONFIG(MT_RXQ_MAIN_WA, WFDMA1, MT_INT_RX_DONE_WA_MAIN,
			   MT7915_RXQ_MCU_WA);
		TXQ_CONFIG(0, WFDMA1, MT_INT_TX_DONE_BAND0, MT7915_TXQ_BAND0);
		TXQ_CONFIG(1, WFDMA1, MT_INT_TX_DONE_BAND1, MT7915_TXQ_BAND1);
		MCUQ_CONFIG(MT_MCUQ_WM, WFDMA1, MT_INT_TX_DONE_MCU_WM,
			    MT7915_TXQ_MCU_WM);
		MCUQ_CONFIG(MT_MCUQ_WA, WFDMA1, MT_INT_TX_DONE_MCU_WA,
			    MT7915_TXQ_MCU_WA);
		MCUQ_CONFIG(MT_MCUQ_FWDL, WFDMA1, MT_INT_TX_DONE_FWDL,
			    MT7915_TXQ_FWDL);
	} else {
		RXQ_CONFIG(MT_RXQ_MCU, WFDMA0, MT_INT_RX_DONE_WM,
			   MT7916_RXQ_MCU_WM);
		RXQ_CONFIG(MT_RXQ_BAND1_WA, WFDMA0, MT_INT_RX_DONE_WA_EXT_MT7916,
			   MT7916_RXQ_MCU_WA_EXT);
		MCUQ_CONFIG(MT_MCUQ_WM, WFDMA0, MT_INT_TX_DONE_MCU_WM,
			    MT7915_TXQ_MCU_WM);
		MCUQ_CONFIG(MT_MCUQ_WA, WFDMA0, MT_INT_TX_DONE_MCU_WA_MT7916,
			    MT7915_TXQ_MCU_WA);
		MCUQ_CONFIG(MT_MCUQ_FWDL, WFDMA0, MT_INT_TX_DONE_FWDL,
			    MT7915_TXQ_FWDL);

		if (is_mt7916(&dev->mt76) && mtk_wed_device_active(&dev->mt76.mmio.wed)) {
			RXQ_CONFIG(MT_RXQ_MAIN, WFDMA0, MT_INT_WED_RX_DONE_BAND0_MT7916,
				   MT7916_RXQ_BAND0);
			RXQ_CONFIG(MT_RXQ_MCU_WA, WFDMA0, MT_INT_WED_RX_DONE_WA_MT7916,
				   MT7916_RXQ_MCU_WA);
			if (dev->hif2)
				RXQ_CONFIG(MT_RXQ_BAND1, WFDMA0,
					   MT_INT_RX_DONE_BAND1_MT7916,
					   MT7916_RXQ_BAND1);
			else
				RXQ_CONFIG(MT_RXQ_BAND1, WFDMA0,
					   MT_INT_WED_RX_DONE_BAND1_MT7916,
					   MT7916_RXQ_BAND1);
			RXQ_CONFIG(MT_RXQ_MAIN_WA, WFDMA0, MT_INT_WED_RX_DONE_WA_MAIN_MT7916,
				   MT7916_RXQ_MCU_WA_MAIN);
			TXQ_CONFIG(0, WFDMA0, MT_INT_WED_TX_DONE_BAND0,
				   MT7915_TXQ_BAND0);
			TXQ_CONFIG(1, WFDMA0, MT_INT_WED_TX_DONE_BAND1,
				   MT7915_TXQ_BAND1);
		} else {
			RXQ_CONFIG(MT_RXQ_MAIN, WFDMA0, MT_INT_RX_DONE_BAND0_MT7916,
				   MT7916_RXQ_BAND0);
			RXQ_CONFIG(MT_RXQ_MCU_WA, WFDMA0, MT_INT_RX_DONE_WA,
				   MT7916_RXQ_MCU_WA);
			RXQ_CONFIG(MT_RXQ_BAND1, WFDMA0, MT_INT_RX_DONE_BAND1_MT7916,
				   MT7916_RXQ_BAND1);
			RXQ_CONFIG(MT_RXQ_MAIN_WA, WFDMA0, MT_INT_RX_DONE_WA_MAIN_MT7916,
				   MT7916_RXQ_MCU_WA_MAIN);
			TXQ_CONFIG(0, WFDMA0, MT_INT_TX_DONE_BAND0,
				   MT7915_TXQ_BAND0);
			TXQ_CONFIG(1, WFDMA0, MT_INT_TX_DONE_BAND1,
				   MT7915_TXQ_BAND1);
		}
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

	mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_MCU) + ofs,
		PREFETCH(0x140, 0x4));
	mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_MCU_WA) + ofs,
		PREFETCH(0x180, 0x4));
	if (!is_mt7915(&dev->mt76)) {
		mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_MAIN_WA) + ofs,
			PREFETCH(0x1c0, 0x4));
		base = 0x40;
	}
	mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_BAND1_WA) + ofs,
		PREFETCH(0x1c0 + base, 0x4));
	mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_MAIN) + ofs,
		PREFETCH(0x200 + base, 0x4));
	mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_BAND1) + ofs,
		PREFETCH(0x240 + base, 0x4));

	/* for mt7915, the ring which is next the last
	 * used ring must be initialized.
	 */
	if (is_mt7915(&dev->mt76)) {
		ofs += 0x4;
		mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_WA) + ofs,
			PREFETCH(0x140, 0x0));
		mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_BAND1_WA) + ofs,
			PREFETCH(0x200 + base, 0x0));
		mt76_wr(dev, MT_RXQ_BAND1_CTRL(MT_RXQ_BAND1) + ofs,
			PREFETCH(0x280 + base, 0x0));
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

int mt7915_dma_start(struct mt7915_dev *dev, bool reset, bool wed_reset)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 hif1_ofs = 0;
	u32 irq_mask;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* enable wpdma tx/rx */
	if (!reset) {
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
	}

	/* enable interrupts for TX/RX rings */
	irq_mask = MT_INT_RX_DONE_MCU |
		   MT_INT_TX_DONE_MCU |
		   MT_INT_MCU_CMD;

	if (!dev->phy.mt76->band_idx)
		irq_mask |= MT_INT_BAND0_RX_DONE;

	if (dev->dbdc_support || dev->phy.mt76->band_idx)
		irq_mask |= MT_INT_BAND1_RX_DONE;

	if (mtk_wed_device_active(&dev->mt76.mmio.wed) && wed_reset) {
		u32 wed_irq_mask = irq_mask;
		int ret;

		wed_irq_mask |= MT_INT_TX_DONE_BAND0 | MT_INT_TX_DONE_BAND1;
		if (!is_mt798x(&dev->mt76))
			mt76_wr(dev, MT_INT_WED_MASK_CSR, wed_irq_mask);
		else
			mt76_wr(dev, MT_INT_MASK_CSR, wed_irq_mask);

		ret = mt7915_mcu_wed_enable_rx_stats(dev);
		if (ret)
			return ret;

		mtk_wed_device_start(&dev->mt76.mmio.wed, wed_irq_mask);
	}

	irq_mask = reset ? MT_INT_MCU_CMD : irq_mask;

	mt7915_irq_enable(dev, irq_mask);
	mt7915_irq_disable(dev, 0);

	return 0;
}

static int mt7915_dma_enable(struct mt7915_dev *dev, bool reset)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 hif1_ofs = 0;

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

	return mt7915_dma_start(dev, reset, true);
}

int mt7915_dma_init(struct mt7915_dev *dev, struct mt7915_phy *phy2)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 wa_rx_base, wa_rx_idx;
	u32 hif1_ofs = 0;
	int ret;

	mt7915_dma_config(dev);

	mt76_dma_attach(&dev->mt76);

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	mt7915_dma_disable(dev, true);

	if (mtk_wed_device_active(&mdev->mmio.wed)) {
		if (!is_mt798x(mdev)) {
			u8 wed_control_rx1 = is_mt7915(mdev) ? 1 : 2;

			mt76_set(dev, MT_WFDMA_HOST_CONFIG,
				 MT_WFDMA_HOST_CONFIG_WED);
			mt76_wr(dev, MT_WFDMA_WED_RING_CONTROL,
				FIELD_PREP(MT_WFDMA_WED_RING_CONTROL_TX0, 18) |
				FIELD_PREP(MT_WFDMA_WED_RING_CONTROL_TX1, 19) |
				FIELD_PREP(MT_WFDMA_WED_RING_CONTROL_RX1,
					   wed_control_rx1));
			if (is_mt7915(mdev))
				mt76_rmw(dev, MT_WFDMA0_EXT0_CFG, MT_WFDMA0_EXT0_RXWB_KEEP,
					 MT_WFDMA0_EXT0_RXWB_KEEP);
		}
	} else {
		mt76_clear(dev, MT_WFDMA_HOST_CONFIG, MT_WFDMA_HOST_CONFIG_WED);
	}

	/* init tx queue */
	ret = mt7915_init_tx_queues(&dev->phy,
				    MT_TXQ_ID(dev->phy.mt76->band_idx),
				    MT7915_TX_RING_SIZE,
				    MT_TXQ_RING_BASE(0));
	if (ret)
		return ret;

	if (phy2) {
		ret = mt7915_init_tx_queues(phy2,
					    MT_TXQ_ID(phy2->mt76->band_idx),
					    MT7915_TX_RING_SIZE,
					    MT_TXQ_RING_BASE(1));
		if (ret)
			return ret;
	}

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
	if (mtk_wed_device_active(&mdev->mmio.wed) && is_mt7915(mdev)) {
		wa_rx_base = MT_WED_RX_RING_BASE;
		wa_rx_idx = MT7915_RXQ_MCU_WA;
		mdev->q_rx[MT_RXQ_MCU_WA].flags = MT_WED_Q_TXFREE;
		mdev->q_rx[MT_RXQ_MCU_WA].wed = &mdev->mmio.wed;
	} else {
		wa_rx_base = MT_RXQ_RING_BASE(MT_RXQ_MCU_WA);
		wa_rx_idx = MT_RXQ_ID(MT_RXQ_MCU_WA);
	}
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       wa_rx_idx, MT7915_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE, wa_rx_base);
	if (ret)
		return ret;

	/* rx data queue for band0 */
	if (!dev->phy.mt76->band_idx) {
		if (mtk_wed_device_active(&mdev->mmio.wed) &&
		    mtk_wed_get_rx_capa(&mdev->mmio.wed)) {
			mdev->q_rx[MT_RXQ_MAIN].flags =
				MT_WED_Q_RX(MT7915_RXQ_BAND0);
			dev->mt76.rx_token_size += MT7915_RX_RING_SIZE;
			mdev->q_rx[MT_RXQ_MAIN].wed = &mdev->mmio.wed;
		}

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
		wa_rx_base = MT_RXQ_RING_BASE(MT_RXQ_MAIN_WA);
		wa_rx_idx = MT_RXQ_ID(MT_RXQ_MAIN_WA);

		if (mtk_wed_device_active(&mdev->mmio.wed)) {
			mdev->q_rx[MT_RXQ_MAIN_WA].flags = MT_WED_Q_TXFREE;
			mdev->q_rx[MT_RXQ_MAIN_WA].wed = &mdev->mmio.wed;
			if (is_mt7916(mdev)) {
				wa_rx_base =  MT_WED_RX_RING_BASE;
				wa_rx_idx = MT7915_RXQ_MCU_WA;
			}
		}

		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN_WA],
				       wa_rx_idx, MT7915_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE, wa_rx_base);
		if (ret)
			return ret;
	}

	if (dev->dbdc_support || dev->phy.mt76->band_idx) {
		if (mtk_wed_device_active(&mdev->mmio.wed) &&
		    mtk_wed_get_rx_capa(&mdev->mmio.wed)) {
			mdev->q_rx[MT_RXQ_BAND1].flags =
				MT_WED_Q_RX(MT7915_RXQ_BAND1);
			dev->mt76.rx_token_size += MT7915_RX_RING_SIZE;
			mdev->q_rx[MT_RXQ_BAND1].wed = &mdev->mmio.wed;
		}

		/* rx data queue for band1 */
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_BAND1],
				       MT_RXQ_ID(MT_RXQ_BAND1),
				       MT7915_RX_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_BAND1) + hif1_ofs);
		if (ret)
			return ret;

		/* tx free notify event from WA for band1 */
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_BAND1_WA],
				       MT_RXQ_ID(MT_RXQ_BAND1_WA),
				       MT7915_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_BAND1_WA) + hif1_ofs);
		if (ret)
			return ret;
	}

	ret = mt76_init_queues(dev, mt76_dma_rx_poll);
	if (ret < 0)
		return ret;

	netif_napi_add_tx(dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt7915_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	mt7915_dma_enable(dev, false);

	return 0;
}

int mt7915_dma_reset(struct mt7915_dev *dev, bool force)
{
	struct mt76_phy *mphy_ext = dev->mt76.phys[MT_BAND1];
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	int i;

	/* clean up hw queues */
	for (i = 0; i < ARRAY_SIZE(dev->mt76.phy.q_tx); i++) {
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);
		if (mphy_ext)
			mt76_queue_tx_cleanup(dev, mphy_ext->q_tx[i], true);
	}

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_mcu); i++)
		mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_cleanup(dev, &dev->mt76.q_rx[i]);

	/* reset wfsys */
	if (force)
		mt7915_wfsys_reset(dev);

	if (mtk_wed_device_active(wed))
		mtk_wed_device_dma_reset(wed);

	mt7915_dma_disable(dev, force);
	mt76_wed_dma_reset(&dev->mt76);

	/* reset hw queues */
	for (i = 0; i < __MT_TXQ_MAX; i++) {
		mt76_dma_reset_tx_queue(&dev->mt76, dev->mphy.q_tx[i]);
		if (mphy_ext)
			mt76_dma_reset_tx_queue(&dev->mt76, mphy_ext->q_tx[i]);
	}

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_reset(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i) {
		if (mt76_queue_is_wed_tx_free(&dev->mt76.q_rx[i]))
			continue;

		mt76_queue_reset(dev, &dev->mt76.q_rx[i], true);
	}

	mt76_tx_status_check(&dev->mt76, true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_reset(dev, i);

	if (mtk_wed_device_active(wed) && is_mt7915(&dev->mt76))
		mt76_rmw(dev, MT_WFDMA0_EXT0_CFG, MT_WFDMA0_EXT0_RXWB_KEEP,
			 MT_WFDMA0_EXT0_RXWB_KEEP);

	mt7915_dma_enable(dev, !force);

	return 0;
}

void mt7915_dma_cleanup(struct mt7915_dev *dev)
{
	mt7915_dma_disable(dev, true);

	mt76_dma_cleanup(&dev->mt76);
}
