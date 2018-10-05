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

#include <linux/delay.h>
#include "mt76x2.h"
#include "trace.h"

void mt76x2_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	mt76x02_irq_enable(mdev, MT_INT_RX_DONE(q));
}

irqreturn_t mt76x2_irq_handler(int irq, void *dev_instance)
{
	struct mt76x02_dev *dev = dev_instance;
	u32 intr;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mt76.state))
		return IRQ_NONE;

	trace_dev_irq(dev, intr, dev->mt76.mmio.irqmask);

	intr &= dev->mt76.mmio.irqmask;

	if (intr & MT_INT_TX_DONE_ALL) {
		mt76x02_irq_disable(&dev->mt76, MT_INT_TX_DONE_ALL);
		tasklet_schedule(&dev->tx_tasklet);
	}

	if (intr & MT_INT_RX_DONE(0)) {
		mt76x02_irq_disable(&dev->mt76, MT_INT_RX_DONE(0));
		napi_schedule(&dev->mt76.napi[0]);
	}

	if (intr & MT_INT_RX_DONE(1)) {
		mt76x02_irq_disable(&dev->mt76, MT_INT_RX_DONE(1));
		napi_schedule(&dev->mt76.napi[1]);
	}

	if (intr & MT_INT_PRE_TBTT)
		tasklet_schedule(&dev->pre_tbtt_tasklet);

	/* send buffered multicast frames now */
	if (intr & MT_INT_TBTT)
		mt76_queue_kick(dev, &dev->mt76.q_tx[MT_TXQ_PSD]);

	if (intr & MT_INT_TX_STAT) {
		mt76x02_mac_poll_tx_status(dev, true);
		tasklet_schedule(&dev->tx_tasklet);
	}

	if (intr & MT_INT_GPTIMER) {
		mt76x02_irq_disable(&dev->mt76, MT_INT_GPTIMER);
		tasklet_schedule(&dev->dfs_pd.dfs_tasklet);
	}

	return IRQ_HANDLED;
}

