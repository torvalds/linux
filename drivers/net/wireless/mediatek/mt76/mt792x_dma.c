// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/module.h>
#include <linux/firmware.h>

#include "mt792x.h"
#include "dma.h"
#include "trace.h"

irqreturn_t mt792x_irq_handler(int irq, void *dev_instance)
{
	struct mt792x_dev *dev = dev_instance;

	if (test_bit(MT76_REMOVED, &dev->mt76.phy.state))
		return IRQ_NONE;
	mt76_wr(dev, dev->irq_map->host_irq_enable, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->mt76.irq_tasklet);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(mt792x_irq_handler);

void mt792x_irq_tasklet(unsigned long data)
{
	struct mt792x_dev *dev = (struct mt792x_dev *)data;
	const struct mt792x_irq_map *irq_map = dev->irq_map;
	u32 intr, mask = 0;

	mt76_wr(dev, irq_map->host_irq_enable, 0);

	intr = mt76_rr(dev, MT_WFDMA0_HOST_INT_STA);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_WFDMA0_HOST_INT_STA, intr);

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask |= intr & (irq_map->rx.data_complete_mask |
			irq_map->rx.wm_complete_mask |
			irq_map->rx.wm2_complete_mask);
	if (intr & dev->irq_map->tx.mcu_complete_mask)
		mask |= dev->irq_map->tx.mcu_complete_mask;

	if (intr & MT_INT_MCU_CMD) {
		u32 intr_sw;

		intr_sw = mt76_rr(dev, MT_MCU_CMD);
		/* ack MCU2HOST_SW_INT_STA */
		mt76_wr(dev, MT_MCU_CMD, intr_sw);
		if (intr_sw & MT_MCU_CMD_WAKE_RX_PCIE) {
			mask |= irq_map->rx.data_complete_mask;
			intr |= irq_map->rx.data_complete_mask;
		}
	}

	mt76_set_irq_mask(&dev->mt76, irq_map->host_irq_enable, mask, 0);

	if (intr & dev->irq_map->tx.all_complete_mask)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & irq_map->rx.wm_complete_mask)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU]);

	if (intr & irq_map->rx.wm2_complete_mask)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU_WA]);

	if (intr & irq_map->rx.data_complete_mask)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MAIN]);
}
EXPORT_SYMBOL_GPL(mt792x_irq_tasklet);

void mt792x_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	const struct mt792x_irq_map *irq_map = dev->irq_map;

	if (q == MT_RXQ_MAIN)
		mt76_connac_irq_enable(mdev, irq_map->rx.data_complete_mask);
	else if (q == MT_RXQ_MCU_WA)
		mt76_connac_irq_enable(mdev, irq_map->rx.wm2_complete_mask);
	else
		mt76_connac_irq_enable(mdev, irq_map->rx.wm_complete_mask);
}
EXPORT_SYMBOL_GPL(mt792x_rx_poll_complete);

#define PREFETCH(base, depth)	((base) << 16 | (depth))
static void mt792x_dma_prefetch(struct mt792x_dev *dev)
{
	if (is_mt7925(&dev->mt76)) {
		/* rx ring */
		mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0000, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING1_EXT_CTRL, PREFETCH(0x0040, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x0080, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x00c0, 0x4));
		/* tx ring */
		mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x0100, 0x10));
		mt76_wr(dev, MT_WFDMA0_TX_RING1_EXT_CTRL, PREFETCH(0x0200, 0x10));
		mt76_wr(dev, MT_WFDMA0_TX_RING2_EXT_CTRL, PREFETCH(0x0300, 0x10));
		mt76_wr(dev, MT_WFDMA0_TX_RING3_EXT_CTRL, PREFETCH(0x0400, 0x10));
		mt76_wr(dev, MT_WFDMA0_TX_RING15_EXT_CTRL, PREFETCH(0x0500, 0x4));
		mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x0540, 0x4));
	} else {
		/* rx ring */
		mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x40, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x80, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING4_EXT_CTRL, PREFETCH(0xc0, 0x4));
		mt76_wr(dev, MT_WFDMA0_RX_RING5_EXT_CTRL, PREFETCH(0x100, 0x4));
		/* tx ring */
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
}

int mt792x_dma_enable(struct mt792x_dev *dev)
{
	/* configure perfetch settings */
	mt792x_dma_prefetch(dev);

	/* reset dma idx */
	mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
	if (is_mt7925(&dev->mt76))
		mt76_wr(dev, MT_WFDMA0_RST_DRX_PTR, ~0);

	/* configure delay interrupt */
	mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
		 MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
		 MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 FIELD_PREP(MT_WFDMA0_GLO_CFG_DMA_SIZE, 3) |
		 MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
		 MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
		 MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	if (is_mt7925(&dev->mt76)) {
		mt76_rmw(dev, MT_UWFDMA0_GLO_CFG_EXT1, BIT(28), BIT(28));
		mt76_set(dev, MT_WFDMA0_INT_RX_PRI, 0x0F00);
		mt76_set(dev, MT_WFDMA0_INT_TX_PRI, 0x7F00);
	}
	mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

	/* enable interrupts for TX/RX rings */
	mt76_connac_irq_enable(&dev->mt76,
			       dev->irq_map->tx.all_complete_mask |
			       dev->irq_map->rx.data_complete_mask |
			       dev->irq_map->rx.wm2_complete_mask |
			       dev->irq_map->rx.wm_complete_mask |
			       MT_INT_MCU_CMD);
	mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_dma_enable);

static int
mt792x_dma_reset(struct mt792x_dev *dev, bool force)
{
	int i, err;

	err = mt792x_dma_disable(dev, force);
	if (err)
		return err;

	/* reset hw queues */
	for (i = 0; i < __MT_TXQ_MAX; i++)
		mt76_queue_reset(dev, dev->mphy.q_tx[i], true);

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_reset(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_reset(dev, &dev->mt76.q_rx[i], true);

	mt76_tx_status_check(&dev->mt76, true);

	return mt792x_dma_enable(dev);
}

int mt792x_wpdma_reset(struct mt792x_dev *dev, bool force)
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
		err = mt792x_wfsys_reset(dev);
		if (err)
			return err;
	}
	err = mt792x_dma_reset(dev, force);
	if (err)
		return err;

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_reset(dev, i);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_wpdma_reset);

int mt792x_wpdma_reinit_cond(struct mt792x_dev *dev)
{
	struct mt76_connac_pm *pm = &dev->pm;
	int err;

	/* check if the wpdma must be reinitialized */
	if (mt792x_dma_need_reinit(dev)) {
		/* disable interrutpts */
		mt76_wr(dev, dev->irq_map->host_irq_enable, 0);
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);

		err = mt792x_wpdma_reset(dev, false);
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
EXPORT_SYMBOL_GPL(mt792x_wpdma_reinit_cond);

int mt792x_dma_disable(struct mt792x_dev *dev, bool force)
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
EXPORT_SYMBOL_GPL(mt792x_dma_disable);

void mt792x_dma_cleanup(struct mt792x_dev *dev)
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
EXPORT_SYMBOL_GPL(mt792x_dma_cleanup);

int mt792x_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt792x_dev *dev;

	dev = container_of(napi, struct mt792x_dev, mt76.tx_napi);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		napi_complete(napi);
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return 0;
	}

	mt76_connac_tx_cleanup(&dev->mt76);
	if (napi_complete(napi))
		mt76_connac_irq_enable(&dev->mt76,
				       dev->irq_map->tx.all_complete_mask);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_poll_tx);

int mt792x_poll_rx(struct napi_struct *napi, int budget)
{
	struct mt792x_dev *dev;
	int done;

	dev = mt76_priv(napi->dev);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		napi_complete(napi);
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return 0;
	}
	done = mt76_dma_rx_poll(napi, budget);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	return done;
}
EXPORT_SYMBOL_GPL(mt792x_poll_rx);

int mt792x_wfsys_reset(struct mt792x_dev *dev)
{
	u32 addr = is_mt7921(&dev->mt76) ? 0x18000140 : 0x7c000140;

	mt76_clear(dev, addr, WFSYS_SW_RST_B);
	msleep(50);
	mt76_set(dev, addr, WFSYS_SW_RST_B);

	if (!__mt76_poll_msec(&dev->mt76, addr, WFSYS_SW_INIT_DONE,
			      WFSYS_SW_INIT_DONE, 500))
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_wfsys_reset);

