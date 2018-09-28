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

#include "mt76.h"
#include "mt76x02_dma.h"
#include "mt76x02_regs.h"

void mt76x02_set_irq_mask(struct mt76_dev *dev, u32 clear, u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->mmio.irq_lock, flags);
	dev->mmio.irqmask &= ~clear;
	dev->mmio.irqmask |= set;
	__mt76_wr(dev, MT_INT_MASK_CSR, dev->mmio.irqmask);
	spin_unlock_irqrestore(&dev->mmio.irq_lock, flags);
}
EXPORT_SYMBOL_GPL(mt76x02_set_irq_mask);

void mt76x02_dma_enable(struct mt76_dev *dev)
{
	u32 val;

	__mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	mt76x02_wait_for_wpdma(dev, 1000);
	usleep_range(50, 100);

	val = FIELD_PREP(MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 3) |
	      MT_WPDMA_GLO_CFG_TX_DMA_EN |
	      MT_WPDMA_GLO_CFG_RX_DMA_EN;
	__mt76_set(dev, MT_WPDMA_GLO_CFG, val);
	__mt76_clear(dev, MT_WPDMA_GLO_CFG,
		     MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_enable);

void mt76x02_dma_disable(struct mt76_dev *dev)
{
	u32 val = __mt76_rr(dev, MT_WPDMA_GLO_CFG);

	val &= MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
	       MT_WPDMA_GLO_CFG_BIG_ENDIAN |
	       MT_WPDMA_GLO_CFG_HDR_SEG_LEN;
	val |= MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE;
	__mt76_wr(dev, MT_WPDMA_GLO_CFG, val);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_disable);
