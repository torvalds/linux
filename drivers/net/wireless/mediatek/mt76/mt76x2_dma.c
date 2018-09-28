/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include "mt76x2.h"
#include "mt76x02_dma.h"
#include "mt76x02_util.h"

static int
mt76x2_init_tx_queue(struct mt76x2_dev *dev, struct mt76_queue *q,
		     int idx, int n_desc)
{
	int ret;

	q->regs = dev->mt76.mmio.regs + MT_TX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->hw_idx = idx;

	ret = mt76_queue_alloc(dev, q);
	if (ret)
		return ret;

	mt76x02_irq_enable(&dev->mt76, MT_INT_TX_DONE(idx));

	return 0;
}

static int
mt76x2_init_rx_queue(struct mt76x2_dev *dev, struct mt76_queue *q,
		     int idx, int n_desc, int bufsize)
{
	int ret;

	q->regs = dev->mt76.mmio.regs + MT_RX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->buf_size = bufsize;

	ret = mt76_queue_alloc(dev, q);
	if (ret)
		return ret;

	mt76x02_irq_enable(&dev->mt76, MT_INT_RX_DONE(idx));

	return 0;
}

static void
mt76x2_tx_tasklet(unsigned long data)
{
	struct mt76x2_dev *dev = (struct mt76x2_dev *) data;
	int i;

	mt76x2_mac_process_tx_status_fifo(dev);

	for (i = MT_TXQ_MCU; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, i, false);

	mt76x2_mac_poll_tx_status(dev, false);
	mt76x02_irq_enable(&dev->mt76, MT_INT_TX_DONE_ALL);
}

int mt76x2_dma_init(struct mt76x2_dev *dev)
{
	int ret;
	int i;
	struct mt76_txwi_cache __maybe_unused *t;
	struct mt76_queue *q;

	BUILD_BUG_ON(sizeof(t->txwi) < sizeof(struct mt76x02_txwi));
	BUILD_BUG_ON(sizeof(struct mt76x02_rxwi) > MT_RX_HEADROOM);

	mt76_dma_attach(&dev->mt76);

	tasklet_init(&dev->tx_tasklet, mt76x2_tx_tasklet, (unsigned long) dev);

	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		ret = mt76x2_init_tx_queue(dev, &dev->mt76.q_tx[i],
					   mt76_ac_to_hwq(i), MT_TX_RING_SIZE);
		if (ret)
			return ret;
	}

	ret = mt76x2_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_PSD],
				   MT_TX_HW_QUEUE_MGMT, MT_TX_RING_SIZE);
	if (ret)
		return ret;

	ret = mt76x2_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_MCU],
				   MT_TX_HW_QUEUE_MCU, MT_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt76x2_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MCU], 1,
				   MT_MCU_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	q = &dev->mt76.q_rx[MT_RXQ_MAIN];
	q->buf_offset = MT_RX_HEADROOM - sizeof(struct mt76x02_rxwi);
	ret = mt76x2_init_rx_queue(dev, q, 0, MT76x2_RX_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	return mt76_init_queues(dev);
}

void mt76x2_dma_cleanup(struct mt76x2_dev *dev)
{
	tasklet_kill(&dev->tx_tasklet);
	mt76_dma_cleanup(&dev->mt76);
}
