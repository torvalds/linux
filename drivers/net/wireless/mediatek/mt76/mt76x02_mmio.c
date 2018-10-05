/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>

#include "mt76x02.h"

static int
mt76x02_init_tx_queue(struct mt76x02_dev *dev, struct mt76_queue *q,
		      int idx, int n_desc)
{
	int ret;

	q->regs = dev->mt76.mmio.regs + MT_TX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->hw_idx = idx;

	ret = mt76_queue_alloc(dev, q);
	if (ret)
		return ret;

	mt76x02_irq_enable(dev, MT_INT_TX_DONE(idx));

	return 0;
}

static int
mt76x02_init_rx_queue(struct mt76x02_dev *dev, struct mt76_queue *q,
		      int idx, int n_desc, int bufsize)
{
	int ret;

	q->regs = dev->mt76.mmio.regs + MT_RX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->buf_size = bufsize;

	ret = mt76_queue_alloc(dev, q);
	if (ret)
		return ret;

	mt76x02_irq_enable(dev, MT_INT_RX_DONE(idx));

	return 0;
}

static void mt76x02_process_tx_status_fifo(struct mt76x02_dev *dev)
{
	struct mt76x02_tx_status stat;
	u8 update = 1;

	while (kfifo_get(&dev->txstatus_fifo, &stat))
		mt76x02_send_tx_status(&dev->mt76, &stat, &update);
}

static void mt76x02_tx_tasklet(unsigned long data)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)data;
	int i;

	mt76x02_process_tx_status_fifo(dev);

	for (i = MT_TXQ_MCU; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, i, false);

	mt76x02_mac_poll_tx_status(dev, false);
	mt76x02_irq_enable(dev, MT_INT_TX_DONE_ALL);
}

int mt76x02_dma_init(struct mt76x02_dev *dev)
{
	struct mt76_txwi_cache __maybe_unused *t;
	int i, ret, fifo_size;
	struct mt76_queue *q;
	void *status_fifo;

	BUILD_BUG_ON(sizeof(t->txwi) < sizeof(struct mt76x02_txwi));
	BUILD_BUG_ON(sizeof(struct mt76x02_rxwi) > MT_RX_HEADROOM);

	fifo_size = roundup_pow_of_two(32 * sizeof(struct mt76x02_tx_status));
	status_fifo = devm_kzalloc(dev->mt76.dev, fifo_size, GFP_KERNEL);
	if (!status_fifo)
		return -ENOMEM;

	tasklet_init(&dev->tx_tasklet, mt76x02_tx_tasklet, (unsigned long) dev);
	kfifo_init(&dev->txstatus_fifo, status_fifo, fifo_size);

	mt76_dma_attach(&dev->mt76);

	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		ret = mt76x02_init_tx_queue(dev, &dev->mt76.q_tx[i],
					    mt76_ac_to_hwq(i),
					    MT_TX_RING_SIZE);
		if (ret)
			return ret;
	}

	ret = mt76x02_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_PSD],
				    MT_TX_HW_QUEUE_MGMT, MT_TX_RING_SIZE);
	if (ret)
		return ret;

	ret = mt76x02_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_MCU],
				    MT_TX_HW_QUEUE_MCU, MT_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt76x02_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MCU], 1,
				    MT_MCU_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	q = &dev->mt76.q_rx[MT_RXQ_MAIN];
	q->buf_offset = MT_RX_HEADROOM - sizeof(struct mt76x02_rxwi);
	ret = mt76x02_init_rx_queue(dev, q, 0, MT76X02_RX_RING_SIZE,
				    MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	return mt76_init_queues(dev);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_init);

void mt76x02_set_irq_mask(struct mt76x02_dev *dev, u32 clear, u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->mt76.mmio.irq_lock, flags);
	dev->mt76.mmio.irqmask &= ~clear;
	dev->mt76.mmio.irqmask |= set;
	mt76_wr(dev, MT_INT_MASK_CSR, dev->mt76.mmio.irqmask);
	spin_unlock_irqrestore(&dev->mt76.mmio.irq_lock, flags);
}
EXPORT_SYMBOL_GPL(mt76x02_set_irq_mask);

static void mt76x02_dma_enable(struct mt76x02_dev *dev)
{
	u32 val;

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	mt76x02_wait_for_wpdma(&dev->mt76, 1000);
	usleep_range(50, 100);

	val = FIELD_PREP(MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 3) |
	      MT_WPDMA_GLO_CFG_TX_DMA_EN |
	      MT_WPDMA_GLO_CFG_RX_DMA_EN;
	mt76_set(dev, MT_WPDMA_GLO_CFG, val);
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_enable);

void mt76x02_dma_cleanup(struct mt76x02_dev *dev)
{
	tasklet_kill(&dev->tx_tasklet);
	mt76_dma_cleanup(&dev->mt76);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_cleanup);

void mt76x02_dma_disable(struct mt76x02_dev *dev)
{
	u32 val = mt76_rr(dev, MT_WPDMA_GLO_CFG);

	val &= MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
	       MT_WPDMA_GLO_CFG_BIG_ENDIAN |
	       MT_WPDMA_GLO_CFG_HDR_SEG_LEN;
	val |= MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE;
	mt76_wr(dev, MT_WPDMA_GLO_CFG, val);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_disable);

void mt76x02_mac_start(struct mt76x02_dev *dev)
{
	mt76x02_dma_enable(dev);
	mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);
	mt76x02_irq_enable(dev,
			   MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
			   MT_INT_TX_STAT);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_start);
