/*
 * Copyright(c) 2004 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef IOATDMA_V2_H
#define IOATDMA_V2_H

#include <linux/dmaengine.h>
#include "dma.h"
#include "hw.h"


extern int ioat_pending_level;

/*
 * workaround for IOAT ver.3.0 null descriptor issue
 * (channel returns error when size is 0)
 */
#define NULL_DESC_BUFFER_SIZE 1

#define IOAT_MAX_ORDER 16
#define ioat_get_alloc_order() \
	(min(ioat_ring_alloc_order, IOAT_MAX_ORDER))

/* struct ioat2_dma_chan - ioat v2 / v3 channel attributes
 * @base: common ioat channel parameters
 * @xfercap_log; log2 of channel max transfer length (for fast division)
 * @head: allocated index
 * @issued: hardware notification point
 * @tail: cleanup index
 * @pending: lock free indicator for issued != head
 * @dmacount: identical to 'head' except for occasionally resetting to zero
 * @alloc_order: log2 of the number of allocated descriptors
 * @ring: software ring buffer implementation of hardware ring
 * @ring_lock: protects ring attributes
 */
struct ioat2_dma_chan {
	struct ioat_chan_common base;
	size_t xfercap_log;
	u16 head;
	u16 issued;
	u16 tail;
	u16 dmacount;
	u16 alloc_order;
	int pending;
	struct ioat_ring_ent **ring;
	spinlock_t ring_lock;
};

static inline struct ioat2_dma_chan *to_ioat2_chan(struct dma_chan *c)
{
	struct ioat_chan_common *chan = to_chan_common(c);

	return container_of(chan, struct ioat2_dma_chan, base);
}

static inline u16 ioat2_ring_mask(struct ioat2_dma_chan *ioat)
{
	return (1 << ioat->alloc_order) - 1;
}

/* count of descriptors in flight with the engine */
static inline u16 ioat2_ring_active(struct ioat2_dma_chan *ioat)
{
	return (ioat->head - ioat->tail) & ioat2_ring_mask(ioat);
}

/* count of descriptors pending submission to hardware */
static inline u16 ioat2_ring_pending(struct ioat2_dma_chan *ioat)
{
	return (ioat->head - ioat->issued) & ioat2_ring_mask(ioat);
}

static inline u16 ioat2_ring_space(struct ioat2_dma_chan *ioat)
{
	u16 num_descs = ioat2_ring_mask(ioat) + 1;
	u16 active = ioat2_ring_active(ioat);

	BUG_ON(active > num_descs);

	return num_descs - active;
}

/* assumes caller already checked space */
static inline u16 ioat2_desc_alloc(struct ioat2_dma_chan *ioat, u16 len)
{
	ioat->head += len;
	return ioat->head - len;
}

static inline u16 ioat2_xferlen_to_descs(struct ioat2_dma_chan *ioat, size_t len)
{
	u16 num_descs = len >> ioat->xfercap_log;

	num_descs += !!(len & ((1 << ioat->xfercap_log) - 1));
	return num_descs;
}

struct ioat_ring_ent {
	struct ioat_dma_descriptor *hw;
	struct dma_async_tx_descriptor txd;
	size_t len;
	#ifdef DEBUG
	int id;
	#endif
};

static inline struct ioat_ring_ent *
ioat2_get_ring_ent(struct ioat2_dma_chan *ioat, u16 idx)
{
	return ioat->ring[idx & ioat2_ring_mask(ioat)];
}

int __devinit ioat2_dma_probe(struct ioatdma_device *dev, int dca);
int __devinit ioat3_dma_probe(struct ioatdma_device *dev, int dca);
struct dca_provider * __devinit ioat2_dca_init(struct pci_dev *pdev, void __iomem *iobase);
struct dca_provider * __devinit ioat3_dca_init(struct pci_dev *pdev, void __iomem *iobase);
#endif /* IOATDMA_V2_H */
