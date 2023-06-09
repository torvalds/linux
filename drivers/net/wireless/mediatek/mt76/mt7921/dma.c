// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7921.h"
#include "../dma.h"
#include "mac.h"

static int mt7921_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7921_dev *dev;

	dev = container_of(napi, struct mt7921_dev, mt76.tx_napi);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		napi_complete(napi);
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return 0;
	}

	mt76_connac_tx_cleanup(&dev->mt76);
	if (napi_complete(napi))
		mt7921_irq_enable(dev, MT_INT_TX_DONE_ALL);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	return 0;
}

static int mt7921_poll_rx(struct napi_struct *napi, int budget)
{
	struct mt7921_dev *dev;
	int done;

	dev = container_of(napi->dev, struct mt7921_dev, mt76.napi_dev);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		napi_complete(napi);
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return 0;
	}
	done = mt76_dma_rx_poll(napi, budget);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	return done;
}

static void mt7921_dma_prefetch(struct mt7921_dev *dev)
{
#define PREFETCH(base, depth)	((base) << 16 | (depth))

	mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x40, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x80, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING4_EXT_CTRL, PREFETCH(0xc0, 0x4));
	mt76_wr(dev, MT_WFDMA0_RX_RING5_EXT_CTRL, PREFETCH(0x100, 0x4));

	mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x140, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING1_EXT_CTRL, PREFETCH(0x180, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING2_EXT_CTRL, PREFETCH(0x1c0, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING3_EXT_CTRL, PREFETCH(0x200, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING4_EXT_CTRL, PREFETCH(0x240, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING5_EXT_CTRL, PREFETCH(0x280, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING6_EXT_CTRL, PREFETCH(0x2c0, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x340, 0x4));
	mt76_wr(dev, MT_WFDMA0_TX_RING17_EXT_CTRL, PREFETCH(0x380, 0x4));
}

static int mt7921_dma_disable(struct mt7921_dev *dev, bool force)
{
	/* disable WFDMA0 */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	if (!mt76_poll_msec_tick(dev, MT_WFDMA0_GLO_CFG,
				 MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
				 MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 100, 1))
		return -ETIMEDOUT;

	/* disable dmashdl */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG_EXT0,
		   MT_WFDMA0_CSR_TX_DMASHDL_ENABLE);
	mt76_set(dev, MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);

	if (force) {
		/* reset */
		mt76_clear(dev, MT_WFDMA0_RST,
			   MT_WFDMA0_RST_DMASHDL_ALL_RST |
			   MT_WFDMA0_RST_LOGIC_RST);

		mt76_set(dev, MT_WFDMA0_RST,
			 MT_WFDMA0_RST_DMASHDL_ALL_RST |
			 MT_WFDMA0_RST_LOGIC_RST);
	}

	return 0;
}

static int mt7921_dma_enable(struct mt7921_dev *dev)
{
	/* configure perfetch settings */
	mt7921_dma_prefetch(dev);

	/* reset dma idx */
	mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);

	/* configure delay interrupt */
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
		 MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
		 MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

	/* enable interrupts for TX/RX rings */
	mt7921_irq_enable(dev,
			  MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
			  MT_INT_MCU_CMD);
	mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);

	return 0;
}

static int mt7921_dma_reset(struct mt7921_dev *dev, bool force)
{
	int i, err;

	err = mt7921_dma_disable(dev, force);
	if (err)
		return err;

	/* reset hw queues */
	for (i = 0; i < __MT_TXQ_MAX; i++)
		mt76_queue_reset(dev, dev->mphy.q_tx[i]);

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_reset(dev, dev->mt76.q_mcu[i]);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_reset(dev, &dev->mt76.q_rx[i]);

	mt76_tx_status_check(&dev->mt76, true);

	return mt7921_dma_enable(dev);
}

int mt7921_wfsys_reset(struct mt7921_dev *dev)
{
	mt76_clear(dev, MT_WFSYS_SW_RST_B, WFSYS_SW_RST_B);
	msleep(50);
	mt76_set(dev, MT_WFSYS_SW_RST_B, WFSYS_SW_RST_B);

	if (!__mt76_poll_msec(&dev->mt76, MT_WFSYS_SW_RST_B,
			      WFSYS_SW_INIT_DONE, WFSYS_SW_INIT_DONE, 500))
		return -ETIMEDOUT;

	return 0;
}

int mt7921_wpdma_reset(struct mt7921_dev *dev, bool force)
{
	int i, err;

	/* clean up hw queues */
	for (i = 0; i < ARRAY_SIZE(dev->mt76.phy.q_tx); i++)
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_mcu); i++)
		mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_cleanup(dev, &dev->mt76.q_rx[i]);

	if (force) {
		err = mt7921_wfsys_reset(dev);
		if (err)
			return err;
	}
	err = mt7921_dma_reset(dev, force);
	if (err)
		return err;

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_reset(dev, i);

	return 0;
}

int mt7921_wpdma_reinit_cond(struct mt7921_dev *dev)
{
	struct mt76_connac_pm *pm = &dev->pm;
	int err;

	/* check if the wpdma must be reinitialized */
	if (mt7921_dma_need_reinit(dev)) {
		/* disable interrutpts */
		mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);

		err = mt7921_wpdma_reset(dev, false);
		if (err) {
			dev_err(dev->mt76.dev, "wpdma reset failed\n");
			return err;
		}

		/* enable interrutpts */
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
		pm->stats.lp_wake++;
	}

	return 0;
}

int mt7921_dma_init(struct mt7921_dev *dev)
{
	int ret;

	mt76_dma_attach(&dev->mt76);

	ret = mt7921_dma_disable(dev, true);
	if (ret)
		return ret;

	ret = mt7921_wfsys_reset(dev);
	if (ret)
		return ret;

	/* init tx queue */
	ret = mt76_connac_init_tx_queues(dev->phy.mt76, MT7921_TXQ_BAND0,
					 MT7921_TX_RING_SIZE,
					 MT_TX_RING_BASE, 0);
	if (ret)
		return ret;

	mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, 0x4);

	/* command to WM */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_WM, MT7921_TXQ_MCU_WM,
				  MT7921_TX_MCU_RING_SIZE, MT_TX_RING_BASE);
	if (ret)
		return ret;

	/* firmware download */
	ret = mt76_init_mcu_queue(&dev->mt76, MT_MCUQ_FWDL, MT7921_TXQ_FWDL,
				  MT7921_TX_FWDL_RING_SIZE, MT_TX_RING_BASE);
	if (ret)
		return ret;

	/* event from WM before firmware download */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU],
			       MT7921_RXQ_MCU_WM,
			       MT7921_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE, MT_RX_EVENT_RING_BASE);
	if (ret)
		return ret;

	/* Change mcu queue after firmware download */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       MT7921_RXQ_MCU_WM,
			       MT7921_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE, MT_WFDMA0(0x540));
	if (ret)
		return ret;

	/* rx data */
	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MAIN],
			       MT7921_RXQ_BAND0, MT7921_RX_RING_SIZE,
			       MT_RX_BUF_SIZE, MT_RX_DATA_RING_BASE);
	if (ret)
		return ret;

	ret = mt76_init_queues(dev, mt7921_poll_rx);
	if (ret < 0)
		return ret;

	netif_napi_add_tx(&dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt7921_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	return mt7921_dma_enable(dev);
}

void mt7921_dma_cleanup(struct mt7921_dev *dev)
{
	/* disable */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		   MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
		   MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	mt76_poll_msec_tick(dev, MT_WFDMA0_GLO_CFG,
			    MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
			    MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 100, 1);

	/* reset */
	mt76_clear(dev, MT_WFDMA0_RST,
		   MT_WFDMA0_RST_DMASHDL_ALL_RST |
		   MT_WFDMA0_RST_LOGIC_RST);

	mt76_set(dev, MT_WFDMA0_RST,
		 MT_WFDMA0_RST_DMASHDL_ALL_RST |
		 MT_WFDMA0_RST_LOGIC_RST);

	mt76_dma_cleanup(&dev->mt76);
}
