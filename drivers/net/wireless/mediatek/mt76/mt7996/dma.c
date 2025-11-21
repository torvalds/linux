// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include "mt7996.h"
#include "../dma.h"
#include "mac.h"

int mt7996_init_tx_queues(struct mt7996_phy *phy, int idx, int n_desc,
			  int ring_base, struct mtk_wed_device *wed)
{
	struct mt7996_dev *dev = phy->dev;
	u32 flags = 0;

	if (mtk_wed_device_active(wed)) {
		ring_base += MT_TXQ_ID(0) * MT_RING_SIZE;
		idx -= MT_TXQ_ID(0);

		if (wed == &dev->mt76.mmio.wed_hif2)
			flags = MT_WED_Q_TX(0);
		else
			flags = MT_WED_Q_TX(idx);
	}

	return mt76_connac_init_tx_queues(phy->mt76, idx, n_desc,
					  ring_base, wed, flags);
}

static int mt7996_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7996_dev *dev;

	dev = container_of(napi, struct mt7996_dev, mt76.tx_napi);

	mt76_connac_tx_cleanup(&dev->mt76);
	if (napi_complete_done(napi, 0))
		mt7996_irq_enable(dev, MT_INT_TX_DONE_MCU);

	return 0;
}

static void mt7996_dma_config(struct mt7996_dev *dev)
{
#define Q_CONFIG(q, wfdma, int, id) do {		\
	if (wfdma)					\
		dev->q_wfdma_mask |= (1 << (q));	\
	dev->q_int_mask[(q)] = int;			\
	dev->q_id[(q)] = id;				\
} while (0)

#define MCUQ_CONFIG(q, wfdma, int, id)	Q_CONFIG(q, (wfdma), (int), (id))
#define RXQ_CONFIG(q, wfdma, int, id)	Q_CONFIG(__RXQ(q), (wfdma), (int), (id))
#define TXQ_CONFIG(q, wfdma, int, id)	Q_CONFIG(__TXQ(q), (wfdma), (int), (id))

	/* rx queue */
	RXQ_CONFIG(MT_RXQ_MCU, WFDMA0, MT_INT_RX_DONE_WM, MT7996_RXQ_MCU_WM);
	/* for mt7990, RX ring 1 is for SDO instead */
	RXQ_CONFIG(MT_RXQ_MCU_WA, WFDMA0, MT_INT_RX_DONE_WA, MT7996_RXQ_MCU_WA);
	RXQ_CONFIG(MT_RXQ_MAIN, WFDMA0, MT_INT_RX_DONE_BAND0, MT7996_RXQ_BAND0);
	if (mt7996_has_wa(dev))
		RXQ_CONFIG(MT_RXQ_MAIN_WA, WFDMA0, MT_INT_RX_DONE_WA_MAIN,
			   MT7996_RXQ_MCU_WA_MAIN);

	switch (mt76_chip(&dev->mt76)) {
	case MT7992_DEVICE_ID:
		RXQ_CONFIG(MT_RXQ_BAND1_WA, WFDMA0, MT_INT_RX_DONE_WA_EXT, MT7996_RXQ_MCU_WA_EXT);
		RXQ_CONFIG(MT_RXQ_BAND1, WFDMA0, MT_INT_RX_DONE_BAND1, MT7996_RXQ_BAND1);
		break;
	case MT7990_DEVICE_ID:
		RXQ_CONFIG(MT_RXQ_BAND1, WFDMA0, MT_INT_RX_DONE_BAND1, MT7996_RXQ_BAND1);
		RXQ_CONFIG(MT_RXQ_TXFREE_BAND0, WFDMA0,
			   MT_INT_RX_TXFREE_BAND0_MT7990, MT7990_RXQ_TXFREE0);
		if (dev->hif2)
			RXQ_CONFIG(MT_RXQ_TXFREE_BAND1, WFDMA0,
				   MT_INT_RX_TXFREE_BAND1_MT7990, MT7990_RXQ_TXFREE1);
		break;
	case MT7996_DEVICE_ID:
	default:
		/* mt7996 band2 */
		RXQ_CONFIG(MT_RXQ_BAND2_WA, WFDMA0, MT_INT_RX_DONE_WA_TRI, MT7996_RXQ_MCU_WA_TRI);
		RXQ_CONFIG(MT_RXQ_BAND2, WFDMA0, MT_INT_RX_DONE_BAND2, MT7996_RXQ_BAND2);
		break;
	}

	if (mt7996_has_hwrro(dev)) {
		/* band0 */
		RXQ_CONFIG(MT_RXQ_RRO_BAND0, WFDMA0, MT_INT_RX_DONE_RRO_BAND0,
			   MT7996_RXQ_RRO_BAND0);
		if (dev->mt76.hwrro_mode == MT76_HWRRO_V3)
			RXQ_CONFIG(MT_RXQ_MSDU_PAGE_BAND0, WFDMA0,
				   MT_INT_RX_DONE_MSDU_PG_BAND0,
				   MT7996_RXQ_MSDU_PG_BAND0);
		if (is_mt7996(&dev->mt76)) {
			RXQ_CONFIG(MT_RXQ_TXFREE_BAND0, WFDMA0,
				   MT_INT_RX_TXFREE_MAIN, MT7996_RXQ_TXFREE0);
			/* band1 */
			RXQ_CONFIG(MT_RXQ_MSDU_PAGE_BAND1, WFDMA0,
				   MT_INT_RX_DONE_MSDU_PG_BAND1,
				   MT7996_RXQ_MSDU_PG_BAND1);
			/* band2 */
			RXQ_CONFIG(MT_RXQ_RRO_BAND2, WFDMA0,
				   MT_INT_RX_DONE_RRO_BAND2,
				   MT7996_RXQ_RRO_BAND2);
			RXQ_CONFIG(MT_RXQ_MSDU_PAGE_BAND2, WFDMA0,
				   MT_INT_RX_DONE_MSDU_PG_BAND2,
				   MT7996_RXQ_MSDU_PG_BAND2);
			RXQ_CONFIG(MT_RXQ_TXFREE_BAND2, WFDMA0,
				   MT_INT_RX_TXFREE_TRI, MT7996_RXQ_TXFREE2);
		} else {
			RXQ_CONFIG(MT_RXQ_RRO_BAND1, WFDMA0,
				   MT_INT_RX_DONE_RRO_BAND1,
				   MT7996_RXQ_RRO_BAND1);
		}

		if (dev->mt76.hwrro_mode == MT76_HWRRO_V3)
			RXQ_CONFIG(MT_RXQ_RRO_IND, WFDMA0,
				   MT_INT_RX_DONE_RRO_IND,
				   MT7996_RXQ_RRO_IND);
		else
			RXQ_CONFIG(MT_RXQ_RRO_RXDMAD_C, WFDMA0,
				   MT_INT_RX_DONE_RRO_RXDMAD_C,
				   MT7996_RXQ_RRO_RXDMAD_C);
	}

	/* data tx queue */
	if (is_mt7996(&dev->mt76)) {
		TXQ_CONFIG(0, WFDMA0, MT_INT_TX_DONE_BAND0, MT7996_TXQ_BAND0);
		if (dev->hif2) {
			/* default bn1:ring19 bn2:ring21 */
			TXQ_CONFIG(1, WFDMA0, MT_INT_TX_DONE_BAND1,
				   MT7996_TXQ_BAND1);
			TXQ_CONFIG(2, WFDMA0, MT_INT_TX_DONE_BAND2,
				   MT7996_TXQ_BAND2);
		} else {
			/* single pcie bn0/1:ring18 bn2:ring19 */
			TXQ_CONFIG(2, WFDMA0, MT_INT_TX_DONE_BAND1,
				   MT7996_TXQ_BAND1);
		}
	} else {
		if (dev->hif2) {
			/*  bn0:ring18 bn1:ring21 */
			TXQ_CONFIG(0, WFDMA0, MT_INT_TX_DONE_BAND0,
				   MT7996_TXQ_BAND0);
			TXQ_CONFIG(1, WFDMA0, MT_INT_TX_DONE_BAND2,
				   MT7996_TXQ_BAND2);
		} else {
			/* single pcie bn0:ring18 bn1:ring19 */
			TXQ_CONFIG(0, WFDMA0, MT_INT_TX_DONE_BAND0,
				   MT7996_TXQ_BAND0);
			TXQ_CONFIG(1, WFDMA0, MT_INT_TX_DONE_BAND1,
				   MT7996_TXQ_BAND1);
		}
	}

	/* mcu tx queue */
	MCUQ_CONFIG(MT_MCUQ_FWDL, WFDMA0, MT_INT_TX_DONE_FWDL, MT7996_TXQ_FWDL);
	MCUQ_CONFIG(MT_MCUQ_WM, WFDMA0, MT_INT_TX_DONE_MCU_WM, MT7996_TXQ_MCU_WM);
	if (mt7996_has_wa(dev))
		MCUQ_CONFIG(MT_MCUQ_WA, WFDMA0, MT_INT_TX_DONE_MCU_WA,
			    MT7996_TXQ_MCU_WA);
}

static u32 __mt7996_dma_prefetch_base(u16 *base, u8 depth)
{
	u32 ret = *base << 16 | depth;

	*base = *base + (depth << 4);

	return ret;
}

static void __mt7996_dma_prefetch(struct mt7996_dev *dev, u32 ofs)
{
	u16 base = 0;
	u8 queue, val;

#define PREFETCH(_depth)	(__mt7996_dma_prefetch_base(&base, (_depth)))
	/* prefetch SRAM wrapping boundary for tx/rx ring. */
	/* Tx Command Rings */
	val = is_mt7996(&dev->mt76) ? 2 : 4;
	mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_FWDL) + ofs, PREFETCH(val));
	mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_WM) + ofs, PREFETCH(val));
	if (mt7996_has_wa(dev))
		mt76_wr(dev, MT_MCUQ_EXT_CTRL(MT_MCUQ_WA) + ofs, PREFETCH(val));

	/* Tx Data Rings */
	mt76_wr(dev, MT_TXQ_EXT_CTRL(0) + ofs, PREFETCH(0x8));
	if (!is_mt7996(&dev->mt76) || dev->hif2)
		mt76_wr(dev, MT_TXQ_EXT_CTRL(1) + ofs, PREFETCH(0x8));
	if (is_mt7996(&dev->mt76))
		mt76_wr(dev, MT_TXQ_EXT_CTRL(2) + ofs, PREFETCH(0x8));

	/* Rx Event Rings */
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MCU) + ofs, PREFETCH(val));
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MCU_WA) + ofs, PREFETCH(val));

	/* Rx TxFreeDone From WA Rings */
	if (mt7996_has_wa(dev)) {
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MAIN_WA) + ofs, PREFETCH(val));
		queue = is_mt7996(&dev->mt76) ? MT_RXQ_BAND2_WA : MT_RXQ_BAND1_WA;
		mt76_wr(dev, MT_RXQ_EXT_CTRL(queue) + ofs, PREFETCH(val));
	}

	/* Rx TxFreeDone From MAC Rings */
	val = is_mt7996(&dev->mt76) ? 4 : 8;
	if ((is_mt7996(&dev->mt76) && mt7996_has_hwrro(dev)) ||
	    is_mt7990(&dev->mt76))
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_TXFREE_BAND0) + ofs, PREFETCH(val));
	if (is_mt7990(&dev->mt76) && dev->hif2)
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_TXFREE_BAND1) + ofs, PREFETCH(val));
	else if (is_mt7996(&dev->mt76) && mt7996_has_hwrro(dev))
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_TXFREE_BAND2) + ofs, PREFETCH(val));

	/* Rx Data Rings */
	mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MAIN) + ofs, PREFETCH(0x10));
	queue = is_mt7996(&dev->mt76) ? MT_RXQ_BAND2 : MT_RXQ_BAND1;
	mt76_wr(dev, MT_RXQ_EXT_CTRL(queue) + ofs, PREFETCH(0x10));

	/* Rx RRO Rings */
	if (mt7996_has_hwrro(dev)) {
		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_RRO_BAND0) + ofs, PREFETCH(0x10));
		queue = is_mt7996(&dev->mt76) ? MT_RXQ_RRO_BAND2 : MT_RXQ_RRO_BAND1;
		mt76_wr(dev, MT_RXQ_EXT_CTRL(queue) + ofs, PREFETCH(0x10));

		mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MSDU_PAGE_BAND0) + ofs, PREFETCH(val));
		if (is_mt7996(&dev->mt76)) {
			mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MSDU_PAGE_BAND1) + ofs,
				PREFETCH(val));
			mt76_wr(dev, MT_RXQ_EXT_CTRL(MT_RXQ_MSDU_PAGE_BAND2) + ofs,
				PREFETCH(val));
		}
	}
#undef PREFETCH

	mt76_set(dev, WF_WFDMA0_GLO_CFG_EXT1 + ofs, WF_WFDMA0_GLO_CFG_EXT1_CALC_MODE);
}

void mt7996_dma_prefetch(struct mt7996_dev *dev)
{
	__mt7996_dma_prefetch(dev, 0);
	if (dev->hif2)
		__mt7996_dma_prefetch(dev, MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0));
}

static void mt7996_dma_disable(struct mt7996_dev *dev, bool reset)
{
	u32 hif1_ofs = 0;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	if (reset) {
		mt76_clear(dev, MT_WFDMA0_RST,
			   MT_WFDMA0_RST_DMASHDL_ALL_RST |
			   MT_WFDMA0_RST_LOGIC_RST);

		mt76_set(dev, MT_WFDMA0_RST,
			 MT_WFDMA0_RST_DMASHDL_ALL_RST |
			 MT_WFDMA0_RST_LOGIC_RST);

		if (dev->hif2) {
			mt76_clear(dev, MT_WFDMA0_RST + hif1_ofs,
				   MT_WFDMA0_RST_DMASHDL_ALL_RST |
				   MT_WFDMA0_RST_LOGIC_RST);

			mt76_set(dev, MT_WFDMA0_RST + hif1_ofs,
				 MT_WFDMA0_RST_DMASHDL_ALL_RST |
				 MT_WFDMA0_RST_LOGIC_RST);
		}
	}

	/* disable */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	if (dev->hif2) {
		mt76_clear(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_RX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
			   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
			   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);
	}
}

void mt7996_dma_start(struct mt7996_dev *dev, bool reset, bool wed_reset)
{
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	u32 hif1_ofs = 0;
	u32 irq_mask;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* enable WFDMA Tx/Rx */
	if (!reset) {
		if (mtk_wed_device_active(wed) && mtk_wed_get_rx_capa(wed))
			mt76_set(dev, MT_WFDMA0_GLO_CFG,
				 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
				 MT_WFDMA0_GLO_CFG_EXT_EN);
		else
			mt76_set(dev, MT_WFDMA0_GLO_CFG,
				 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_RX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
				 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 |
				 MT_WFDMA0_GLO_CFG_EXT_EN);

		if (dev->hif2)
			mt76_set(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
				 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_RX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
				 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 |
				 MT_WFDMA0_GLO_CFG_EXT_EN);
	}

	/* enable interrupts for TX/RX rings */
	irq_mask = MT_INT_MCU_CMD | MT_INT_RX_DONE_MCU | MT_INT_TX_DONE_MCU;

	if (mt7996_band_valid(dev, MT_BAND0))
		irq_mask |= MT_INT_BAND0_RX_DONE;

	if (mt7996_band_valid(dev, MT_BAND1)) {
		irq_mask |= MT_INT_BAND1_RX_DONE;
		if (is_mt7992(&dev->mt76) && dev->hif2)
			irq_mask |= MT_INT_RX_TXFREE_BAND1_EXT;
	}

	if (mt7996_band_valid(dev, MT_BAND2))
		irq_mask |= MT_INT_BAND2_RX_DONE | MT_INT_TX_RX_DONE_EXT;

	if (mtk_wed_device_active(wed) && wed_reset) {
		u32 wed_irq_mask = irq_mask;

		wed_irq_mask |= MT_INT_TX_DONE_BAND0 | MT_INT_TX_DONE_BAND1;
		mt76_wr(dev, MT_INT_MASK_CSR, wed_irq_mask);
		mtk_wed_device_start(wed, wed_irq_mask);
	}

	if (!mt7996_has_wa(dev))
		irq_mask &= ~(MT_INT_RX(MT_RXQ_MAIN_WA) |
			      MT_INT_RX(MT_RXQ_BAND1_WA));
	irq_mask = reset ? MT_INT_MCU_CMD : irq_mask;

	mt7996_irq_enable(dev, irq_mask);
	mt7996_irq_disable(dev, 0);
}

static void mt7996_dma_enable(struct mt7996_dev *dev, bool reset)
{
	u32 hif1_ofs = 0;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* reset dma idx */
	mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	if (dev->hif2)
		mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR + hif1_ofs, ~0);

	/* configure delay interrupt off */
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG1, 0);
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG2, 0);

	if (dev->hif2) {
		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0 + hif1_ofs, 0);
		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG1 + hif1_ofs, 0);
		mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG2 + hif1_ofs, 0);
	}

	/* configure perfetch settings */
	mt7996_dma_prefetch(dev);

	/* hif wait WFDMA idle */
	mt76_set(dev, MT_WFDMA0_BUSY_ENA,
		 MT_WFDMA0_BUSY_ENA_TX_FIFO0 |
		 MT_WFDMA0_BUSY_ENA_TX_FIFO1 |
		 MT_WFDMA0_BUSY_ENA_RX_FIFO);

	if (dev->hif2)
		mt76_set(dev, MT_WFDMA0_BUSY_ENA + hif1_ofs,
			 MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO0 |
			 MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO1 |
			 MT_WFDMA0_PCIE1_BUSY_ENA_RX_FIFO);

	mt76_poll(dev, MT_WFDMA_EXT_CSR_HIF_MISC,
		  MT_WFDMA_EXT_CSR_HIF_MISC_BUSY, 0, 1000);

	/* GLO_CFG_EXT0 */
	mt76_set(dev, WF_WFDMA0_GLO_CFG_EXT0,
		 WF_WFDMA0_GLO_CFG_EXT0_RX_WB_RXD |
		 WF_WFDMA0_GLO_CFG_EXT0_WED_MERGE_MODE);

	/* GLO_CFG_EXT1 */
	mt76_set(dev, WF_WFDMA0_GLO_CFG_EXT1,
		 WF_WFDMA0_GLO_CFG_EXT1_TX_FCTRL_MODE);

	/* WFDMA rx threshold */
	mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_45_TH, 0xc000c);
	mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_67_TH, 0x10008);
	mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_89_TH, 0x10008);
	mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_RRO_TH, 0x20);

	if (dev->hif2) {
		/* GLO_CFG_EXT0 */
		mt76_set(dev, WF_WFDMA0_GLO_CFG_EXT0 + hif1_ofs,
			 WF_WFDMA0_GLO_CFG_EXT0_RX_WB_RXD |
			 WF_WFDMA0_GLO_CFG_EXT0_WED_MERGE_MODE);

		/* GLO_CFG_EXT1 */
		mt76_set(dev, WF_WFDMA0_GLO_CFG_EXT1 + hif1_ofs,
			 WF_WFDMA0_GLO_CFG_EXT1_TX_FCTRL_MODE);

		mt76_set(dev, MT_WFDMA_HOST_CONFIG,
			 MT_WFDMA_HOST_CONFIG_PDMA_BAND);

		mt76_clear(dev, MT_WFDMA_HOST_CONFIG,
			   MT_WFDMA_HOST_CONFIG_BAND0_PCIE1 |
			   MT_WFDMA_HOST_CONFIG_BAND1_PCIE1 |
			   MT_WFDMA_HOST_CONFIG_BAND2_PCIE1);

		if (is_mt7996(&dev->mt76))
			mt76_set(dev, MT_WFDMA_HOST_CONFIG,
				 MT_WFDMA_HOST_CONFIG_BAND2_PCIE1);
		else
			mt76_set(dev, MT_WFDMA_HOST_CONFIG,
				 MT_WFDMA_HOST_CONFIG_BAND1_PCIE1);

		/* AXI read outstanding number */
		mt76_rmw(dev, MT_WFDMA_AXI_R2A_CTRL,
			 MT_WFDMA_AXI_R2A_CTRL_OUTSTAND_MASK, 0x14);

		if (dev->hif2->speed < PCIE_SPEED_5_0GT ||
		    (dev->hif2->speed == PCIE_SPEED_5_0GT &&
		     dev->hif2->width < PCIE_LNK_X2)) {
			mt76_rmw(dev, WF_WFDMA0_GLO_CFG_EXT0 + hif1_ofs,
				 WF_WFDMA0_GLO_CFG_EXT0_OUTSTAND_MASK,
				 FIELD_PREP(WF_WFDMA0_GLO_CFG_EXT0_OUTSTAND_MASK,
					    0x1));
			mt76_rmw(dev, MT_WFDMA_AXI_R2A_CTRL2,
				 MT_WFDMA_AXI_R2A_CTRL2_OUTSTAND_MASK,
				 FIELD_PREP(MT_WFDMA_AXI_R2A_CTRL2_OUTSTAND_MASK,
					    0x1));
		} else if (dev->hif2->speed < PCIE_SPEED_8_0GT ||
			   (dev->hif2->speed == PCIE_SPEED_8_0GT &&
			    dev->hif2->width < PCIE_LNK_X2)) {
			mt76_rmw(dev, WF_WFDMA0_GLO_CFG_EXT0 + hif1_ofs,
				 WF_WFDMA0_GLO_CFG_EXT0_OUTSTAND_MASK,
				 FIELD_PREP(WF_WFDMA0_GLO_CFG_EXT0_OUTSTAND_MASK,
					    0x2));
			mt76_rmw(dev, MT_WFDMA_AXI_R2A_CTRL2,
				 MT_WFDMA_AXI_R2A_CTRL2_OUTSTAND_MASK,
				 FIELD_PREP(MT_WFDMA_AXI_R2A_CTRL2_OUTSTAND_MASK,
					    0x2));
		}

		/* WFDMA rx threshold */
		mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_45_TH + hif1_ofs, 0xc000c);
		mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_67_TH + hif1_ofs, 0x10008);
		mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_89_TH + hif1_ofs, 0x10008);
		mt76_wr(dev, MT_WFDMA0_PAUSE_RX_Q_RRO_TH + hif1_ofs, 0x20);
	}

	if (dev->hif2) {
		/* fix hardware limitation, pcie1's rx ring3 is not available
		 * so, redirect pcie0 rx ring3 interrupt to pcie1
		 */
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) &&
		    mt7996_has_hwrro(dev)) {
			u32 intr = is_mt7996(&dev->mt76) ?
				   MT_WFDMA0_RX_INT_SEL_RING6 :
				   MT_WFDMA0_RX_INT_SEL_RING9 |
				   MT_WFDMA0_RX_INT_SEL_RING5;

			mt76_set(dev, MT_WFDMA0_RX_INT_PCIE_SEL + hif1_ofs,
				 intr);
		} else {
			mt76_set(dev, MT_WFDMA0_RX_INT_PCIE_SEL,
				 MT_WFDMA0_RX_INT_SEL_RING3);
		}
	}

	mt7996_dma_start(dev, reset, true);
}

int mt7996_dma_rro_init(struct mt7996_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u32 irq_mask;
	int ret;

	if (dev->mt76.hwrro_mode == MT76_HWRRO_V3_1) {
		/* rxdmad_c */
		mdev->q_rx[MT_RXQ_RRO_RXDMAD_C].flags = MT_WED_RRO_Q_RXDMAD_C;
		if (mtk_wed_device_active(&mdev->mmio.wed))
			mdev->q_rx[MT_RXQ_RRO_RXDMAD_C].wed = &mdev->mmio.wed;
		else
			mdev->q_rx[MT_RXQ_RRO_RXDMAD_C].flags |= MT_QFLAG_EMI_EN;
		ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_RRO_RXDMAD_C],
				       MT_RXQ_ID(MT_RXQ_RRO_RXDMAD_C),
				       MT7996_RX_RING_SIZE,
				       MT7996_RX_BUF_SIZE,
				       MT_RXQ_RRO_AP_RING_BASE);
		if (ret)
			return ret;

		/* We need to set cpu idx pointer before resetting the EMI
		 * queues.
		 */
		mdev->q_rx[MT_RXQ_RRO_RXDMAD_C].emi_cpu_idx =
			&dev->wed_rro.emi_rings_cpu.ptr->ring[0].idx;
		mt76_queue_reset(dev, &mdev->q_rx[MT_RXQ_RRO_RXDMAD_C], true);
		goto start_hw_rro;
	}

	/* ind cmd */
	mdev->q_rx[MT_RXQ_RRO_IND].flags = MT_WED_RRO_Q_IND;
	if (mtk_wed_device_active(&mdev->mmio.wed) &&
	    mtk_wed_get_rx_capa(&mdev->mmio.wed))
		mdev->q_rx[MT_RXQ_RRO_IND].wed = &mdev->mmio.wed;
	ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_RRO_IND],
			       MT_RXQ_ID(MT_RXQ_RRO_IND),
			       MT7996_RX_RING_SIZE,
			       0, MT_RXQ_RRO_IND_RING_BASE);
	if (ret)
		return ret;

	/* rx msdu page queue for band0 */
	mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND0].flags =
		MT_WED_RRO_Q_MSDU_PG(0) | MT_QFLAG_WED_RRO_EN;
	if (mtk_wed_device_active(&mdev->mmio.wed) &&
	    mtk_wed_get_rx_capa(&mdev->mmio.wed))
		mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND0].wed = &mdev->mmio.wed;
	ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND0],
			       MT_RXQ_ID(MT_RXQ_MSDU_PAGE_BAND0),
			       MT7996_RX_RING_SIZE,
			       MT7996_RX_MSDU_PAGE_SIZE,
			       MT_RXQ_RING_BASE(MT_RXQ_MSDU_PAGE_BAND0));
	if (ret)
		return ret;

	if (mt7996_band_valid(dev, MT_BAND1) && is_mt7996(&dev->mt76)) {
		/* rx msdu page queue for band1 */
		mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND1].flags =
			MT_WED_RRO_Q_MSDU_PG(1) | MT_QFLAG_WED_RRO_EN;
		if (mtk_wed_device_active(&mdev->mmio.wed) &&
		    mtk_wed_get_rx_capa(&mdev->mmio.wed))
			mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND1].wed = &mdev->mmio.wed;
		ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND1],
				       MT_RXQ_ID(MT_RXQ_MSDU_PAGE_BAND1),
				       MT7996_RX_RING_SIZE,
				       MT7996_RX_MSDU_PAGE_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_MSDU_PAGE_BAND1));
		if (ret)
			return ret;
	}

	if (mt7996_band_valid(dev, MT_BAND2)) {
		/* rx msdu page queue for band2 */
		mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND2].flags =
			MT_WED_RRO_Q_MSDU_PG(2) | MT_QFLAG_WED_RRO_EN;
		if (mtk_wed_device_active(&mdev->mmio.wed) &&
		    mtk_wed_get_rx_capa(&mdev->mmio.wed))
			mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND2].wed = &mdev->mmio.wed;
		ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_MSDU_PAGE_BAND2],
				       MT_RXQ_ID(MT_RXQ_MSDU_PAGE_BAND2),
				       MT7996_RX_RING_SIZE,
				       MT7996_RX_MSDU_PAGE_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_MSDU_PAGE_BAND2));
		if (ret)
			return ret;
	}

start_hw_rro:
	if (mtk_wed_device_active(&mdev->mmio.wed)) {
		irq_mask = mdev->mmio.irqmask |
			   MT_INT_TX_DONE_BAND2;

		mt76_wr(dev, MT_INT_MASK_CSR, irq_mask);
		mtk_wed_device_start_hw_rro(&mdev->mmio.wed, irq_mask, false);
		mt7996_irq_enable(dev, irq_mask);
	} else {
		if (is_mt7996(&dev->mt76)) {
			mt76_queue_rx_init(dev, MT_RXQ_MSDU_PAGE_BAND1,
					   mt76_dma_rx_poll);
			mt76_queue_rx_init(dev, MT_RXQ_MSDU_PAGE_BAND2,
					   mt76_dma_rx_poll);
			mt76_queue_rx_init(dev, MT_RXQ_RRO_BAND2,
					   mt76_dma_rx_poll);
		} else {
			mt76_queue_rx_init(dev, MT_RXQ_RRO_BAND1,
					   mt76_dma_rx_poll);
		}

		mt76_queue_rx_init(dev, MT_RXQ_RRO_BAND0, mt76_dma_rx_poll);
		if (dev->mt76.hwrro_mode == MT76_HWRRO_V3_1) {
			mt76_queue_rx_init(dev, MT_RXQ_RRO_RXDMAD_C,
					   mt76_dma_rx_poll);
		} else {
			mt76_queue_rx_init(dev, MT_RXQ_RRO_IND,
					   mt76_dma_rx_poll);
			mt76_queue_rx_init(dev, MT_RXQ_MSDU_PAGE_BAND0,
					   mt76_dma_rx_poll);
		}
		mt7996_irq_enable(dev, MT_INT_RRO_RX_DONE);
	}

	return 0;
}

int mt7996_dma_init(struct mt7996_dev *dev)
{
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mtk_wed_device *wed_hif2 = &dev->mt76.mmio.wed_hif2;
	u32 rx_base;
	u32 hif1_ofs = 0;
	int ret;

	mt7996_dma_config(dev);

	mt76_dma_attach(&dev->mt76);

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	mt7996_dma_disable(dev, true);

	/* init tx queue */
	ret = mt7996_init_tx_queues(&dev->phy,
				    MT_TXQ_ID(dev->mphy.band_idx),
				    MT7996_TX_RING_SIZE,
				    MT_TXQ_RING_BASE(0),
				    wed);
	if (ret)
		return ret;

	/* command to WM */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WM,
				  MT_MCUQ_ID(MT_MCUQ_WM),
				  MT7996_TX_MCU_RING_SIZE,
				  MT_MCUQ_RING_BASE(MT_MCUQ_WM));
	if (ret)
		return ret;

	/* command to WA */
	if (mt7996_has_wa(dev)) {
		ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WA,
					  MT_MCUQ_ID(MT_MCUQ_WA),
					  MT7996_TX_MCU_RING_SIZE,
					  MT_MCUQ_RING_BASE(MT_MCUQ_WA));
		if (ret)
			return ret;
	}

	/* firmware download */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_FWDL,
				  MT_MCUQ_ID(MT_MCUQ_FWDL),
				  MT7996_TX_FWDL_RING_SIZE,
				  MT_MCUQ_RING_BASE(MT_MCUQ_FWDL));
	if (ret)
		return ret;

	/* event from WM */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU],
			       MT_RXQ_ID(MT_RXQ_MCU),
			       MT7996_RX_MCU_RING_SIZE,
			       MT7996_RX_MCU_BUF_SIZE,
			       MT_RXQ_RING_BASE(MT_RXQ_MCU));
	if (ret)
		return ret;

	/* event from WA, or SDO event for mt7990 */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       MT_RXQ_ID(MT_RXQ_MCU_WA),
			       MT7996_RX_MCU_RING_SIZE_WA,
			       MT7996_RX_MCU_BUF_SIZE,
			       MT_RXQ_RING_BASE(MT_RXQ_MCU_WA));
	if (ret)
		return ret;

	/* rx data queue for band0 and mt7996 band1 */
	if (mtk_wed_device_active(wed) && mtk_wed_get_rx_capa(wed)) {
		dev->mt76.q_rx[MT_RXQ_MAIN].flags = MT_WED_Q_RX(0);
		dev->mt76.q_rx[MT_RXQ_MAIN].wed = wed;
	}

	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN],
			       MT_RXQ_ID(MT_RXQ_MAIN),
			       MT7996_RX_RING_SIZE,
			       MT_RX_BUF_SIZE,
			       MT_RXQ_RING_BASE(MT_RXQ_MAIN));
	if (ret)
		return ret;

	/* tx free notify event from WA for band0 */
	if (mtk_wed_device_active(wed) &&
	    ((is_mt7996(&dev->mt76) && !mt7996_has_hwrro(dev)) ||
	     (is_mt7992(&dev->mt76)))) {
		dev->mt76.q_rx[MT_RXQ_MAIN_WA].flags = MT_WED_Q_TXFREE;
		dev->mt76.q_rx[MT_RXQ_MAIN_WA].wed = wed;
	}

	if (mt7996_has_wa(dev)) {
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN_WA],
				       MT_RXQ_ID(MT_RXQ_MAIN_WA),
				       MT7996_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_MAIN_WA));
		if (ret)
			return ret;
	} else {
		if (mtk_wed_device_active(wed)) {
			dev->mt76.q_rx[MT_RXQ_TXFREE_BAND0].flags = MT_WED_Q_TXFREE;
			dev->mt76.q_rx[MT_RXQ_TXFREE_BAND0].wed = wed;
		}
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_TXFREE_BAND0],
				       MT_RXQ_ID(MT_RXQ_TXFREE_BAND0),
				       MT7996_RX_MCU_RING_SIZE,
				       MT7996_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_TXFREE_BAND0));
		if (ret)
			return ret;
	}

	if (!mt7996_has_wa(dev) && dev->hif2) {
		if (mtk_wed_device_active(wed)) {
			dev->mt76.q_rx[MT_RXQ_TXFREE_BAND1].flags = MT_WED_Q_TXFREE;
			dev->mt76.q_rx[MT_RXQ_TXFREE_BAND1].wed = wed;
		}
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_TXFREE_BAND1],
				       MT_RXQ_ID(MT_RXQ_TXFREE_BAND1),
				       MT7996_RX_MCU_RING_SIZE,
				       MT7996_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_TXFREE_BAND1));
		if (ret)
			return ret;
	}

	if (mt7996_band_valid(dev, MT_BAND2)) {
		/* rx data queue for mt7996 band2 */
		rx_base = MT_RXQ_RING_BASE(MT_RXQ_BAND2) + hif1_ofs;
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_BAND2],
				       MT_RXQ_ID(MT_RXQ_BAND2),
				       MT7996_RX_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       rx_base);
		if (ret)
			return ret;

		/* tx free notify event from WA for mt7996 band2
		 * use pcie0's rx ring3, but, redirect pcie0 rx ring3 interrupt to pcie1
		 */
		if (mtk_wed_device_active(wed_hif2) && !mt7996_has_hwrro(dev)) {
			dev->mt76.q_rx[MT_RXQ_BAND2_WA].flags = MT_WED_Q_TXFREE;
			dev->mt76.q_rx[MT_RXQ_BAND2_WA].wed = wed_hif2;
		}

		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_BAND2_WA],
				       MT_RXQ_ID(MT_RXQ_BAND2_WA),
				       MT7996_RX_MCU_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_BAND2_WA));
		if (ret)
			return ret;
	} else if (mt7996_band_valid(dev, MT_BAND1)) {
		/* rx data queue for mt7992 band1 */
		rx_base = MT_RXQ_RING_BASE(MT_RXQ_BAND1) + hif1_ofs;
		if (mtk_wed_device_active(wed) && mtk_wed_get_rx_capa(wed)) {
			dev->mt76.q_rx[MT_RXQ_BAND1].flags = MT_WED_Q_RX(1);
			dev->mt76.q_rx[MT_RXQ_BAND1].wed = wed;
		}

		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_BAND1],
				       MT_RXQ_ID(MT_RXQ_BAND1),
				       MT7996_RX_RING_SIZE,
				       MT_RX_BUF_SIZE,
				       rx_base);
		if (ret)
			return ret;

		/* tx free notify event from WA for mt7992 band1 */
		if (mt7996_has_wa(dev)) {
			rx_base = MT_RXQ_RING_BASE(MT_RXQ_BAND1_WA) + hif1_ofs;
			if (mtk_wed_device_active(wed_hif2)) {
				dev->mt76.q_rx[MT_RXQ_BAND1_WA].flags =
					MT_WED_Q_TXFREE;
				dev->mt76.q_rx[MT_RXQ_BAND1_WA].wed = wed_hif2;
			}

			ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_BAND1_WA],
					       MT_RXQ_ID(MT_RXQ_BAND1_WA),
					       MT7996_RX_MCU_RING_SIZE,
					       MT_RX_BUF_SIZE,
					       rx_base);
			if (ret)
				return ret;
		}
	}

	if (mt7996_has_hwrro(dev)) {
		/* rx rro data queue for band0 */
		dev->mt76.q_rx[MT_RXQ_RRO_BAND0].flags =
			MT_WED_RRO_Q_DATA(0) | MT_QFLAG_WED_RRO_EN;
		if (mtk_wed_device_active(wed) && mtk_wed_get_rx_capa(wed))
			dev->mt76.q_rx[MT_RXQ_RRO_BAND0].wed = wed;
		ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_RRO_BAND0],
				       MT_RXQ_ID(MT_RXQ_RRO_BAND0),
				       MT7996_RX_RING_SIZE,
				       MT7996_RX_BUF_SIZE,
				       MT_RXQ_RING_BASE(MT_RXQ_RRO_BAND0));
		if (ret)
			return ret;

		if (is_mt7992(&dev->mt76)) {
			dev->mt76.q_rx[MT_RXQ_RRO_BAND1].flags =
				MT_WED_RRO_Q_DATA(1) | MT_QFLAG_WED_RRO_EN;
			if (mtk_wed_device_active(wed) &&
			    mtk_wed_get_rx_capa(wed))
				dev->mt76.q_rx[MT_RXQ_RRO_BAND1].wed = wed;
			ret = mt76_queue_alloc(dev,
					       &dev->mt76.q_rx[MT_RXQ_RRO_BAND1],
					       MT_RXQ_ID(MT_RXQ_RRO_BAND1),
					       MT7996_RX_RING_SIZE,
					       MT7996_RX_BUF_SIZE,
					       MT_RXQ_RING_BASE(MT_RXQ_RRO_BAND1) + hif1_ofs);
			if (ret)
				return ret;
		} else {
			if (mtk_wed_device_active(wed)) {
				/* tx free notify event from WA for band0 */
				dev->mt76.q_rx[MT_RXQ_TXFREE_BAND0].flags = MT_WED_Q_TXFREE;
				dev->mt76.q_rx[MT_RXQ_TXFREE_BAND0].wed = wed;
			}

			ret = mt76_queue_alloc(dev,
					       &dev->mt76.q_rx[MT_RXQ_TXFREE_BAND0],
					       MT_RXQ_ID(MT_RXQ_TXFREE_BAND0),
					       MT7996_RX_MCU_RING_SIZE,
					       MT7996_RX_BUF_SIZE,
					       MT_RXQ_RING_BASE(MT_RXQ_TXFREE_BAND0));
			if (ret)
				return ret;
		}

		if (mt7996_band_valid(dev, MT_BAND2)) {
			/* rx rro data queue for band2 */
			dev->mt76.q_rx[MT_RXQ_RRO_BAND2].flags =
				MT_WED_RRO_Q_DATA(1) | MT_QFLAG_WED_RRO_EN;
			if (mtk_wed_device_active(wed) &&
			    mtk_wed_get_rx_capa(wed))
				dev->mt76.q_rx[MT_RXQ_RRO_BAND2].wed = wed;
			ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_RRO_BAND2],
					       MT_RXQ_ID(MT_RXQ_RRO_BAND2),
					       MT7996_RX_RING_SIZE,
					       MT7996_RX_BUF_SIZE,
					       MT_RXQ_RING_BASE(MT_RXQ_RRO_BAND2) + hif1_ofs);
			if (ret)
				return ret;

			/* tx free notify event from MAC for band2 */
			if (mtk_wed_device_active(wed_hif2)) {
				dev->mt76.q_rx[MT_RXQ_TXFREE_BAND2].flags = MT_WED_Q_TXFREE;
				dev->mt76.q_rx[MT_RXQ_TXFREE_BAND2].wed = wed_hif2;
			}
			ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_TXFREE_BAND2],
					       MT_RXQ_ID(MT_RXQ_TXFREE_BAND2),
					       MT7996_RX_MCU_RING_SIZE,
					       MT7996_RX_BUF_SIZE,
					       MT_RXQ_RING_BASE(MT_RXQ_TXFREE_BAND2) + hif1_ofs);
			if (ret)
				return ret;
		}
	}

	ret = mt76_init_queues(dev, mt76_dma_rx_poll);
	if (ret < 0)
		return ret;

	netif_napi_add_tx(dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt7996_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	mt7996_dma_enable(dev, false);

	return 0;
}

void mt7996_dma_reset(struct mt7996_dev *dev, bool force)
{
	struct mt76_phy *phy2 = dev->mt76.phys[MT_BAND1];
	struct mt76_phy *phy3 = dev->mt76.phys[MT_BAND2];
	u32 hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);
	int i;

	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	if (dev->hif2)
		mt76_clear(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	usleep_range(1000, 2000);

	for (i = 0; i < __MT_TXQ_MAX; i++) {
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);
		if (phy2)
			mt76_queue_tx_cleanup(dev, phy2->q_tx[i], true);
		if (phy3)
			mt76_queue_tx_cleanup(dev, phy3->q_tx[i], true);
	}

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_cleanup(dev, &dev->mt76.q_rx[i]);

	mt76_tx_status_check(&dev->mt76, true);

	if (mt7996_has_hwrro(dev) &&
	    !mtk_wed_device_active(&dev->mt76.mmio.wed))
		mt7996_rro_msdu_page_map_free(dev);

	/* reset wfsys */
	if (force)
		mt7996_wfsys_reset(dev);

	if (dev->hif2 && mtk_wed_device_active(&dev->mt76.mmio.wed_hif2))
		mtk_wed_device_dma_reset(&dev->mt76.mmio.wed_hif2);

	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mtk_wed_device_dma_reset(&dev->mt76.mmio.wed);

	mt7996_dma_disable(dev, force);
	mt76_wed_dma_reset(&dev->mt76);

	/* reset hw queues */
	for (i = 0; i < __MT_TXQ_MAX; i++) {
		mt76_dma_reset_tx_queue(&dev->mt76, dev->mphy.q_tx[i]);
		if (phy2)
			mt76_dma_reset_tx_queue(&dev->mt76, phy2->q_tx[i]);
		if (phy3)
			mt76_dma_reset_tx_queue(&dev->mt76, phy3->q_tx[i]);
	}

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_reset(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i) {
		struct mt76_queue *q = &dev->mt76.q_rx[i];

		if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
			if (mt76_queue_is_wed_rro(q) ||
			    mt76_queue_is_wed_tx_free(q)) {
				if (force && mt76_queue_is_wed_rro_data(q))
					mt76_queue_reset(dev, q, false);
				continue;
			}
		}
		mt76_queue_reset(dev, q, true);
	}

	mt76_tx_status_check(&dev->mt76, true);

	mt76_for_each_q_rx(&dev->mt76, i) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) && force &&
		    (mt76_queue_is_wed_rro_ind(&dev->mt76.q_rx[i]) ||
		     mt76_queue_is_wed_rro_msdu_pg(&dev->mt76.q_rx[i])))
			continue;

		mt76_queue_rx_reset(dev, i);
	}

	mt7996_dma_enable(dev, !force);
}

void mt7996_dma_cleanup(struct mt7996_dev *dev)
{
	mt7996_dma_disable(dev, true);

	mt76_dma_cleanup(&dev->mt76);
}
