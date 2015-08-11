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
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef IOATDMA_V2_H
#define IOATDMA_V2_H

#include <linux/dmaengine.h>
#include <linux/circ_buf.h>
#include "dma.h"
#include "hw.h"


extern int ioat_pending_level;
extern int ioat_ring_alloc_order;

/*
 * workaround for IOAT ver.3.0 null descriptor issue
 * (channel returns error when size is 0)
 */
#define NULL_DESC_BUFFER_SIZE 1

#define IOAT_MAX_ORDER 16
#define ioat_get_alloc_order() \
	(min(ioat_ring_alloc_order, IOAT_MAX_ORDER))
#define ioat_get_max_alloc_order() \
	(min(ioat_ring_max_alloc_order, IOAT_MAX_ORDER))

static inline u32 ioat2_ring_size(struct ioatdma_chan *ioat_chan)
{
	return 1 << ioat_chan->alloc_order;
}

/* count of descriptors in flight with the engine */
static inline u16 ioat2_ring_active(struct ioatdma_chan *ioat_chan)
{
	return CIRC_CNT(ioat_chan->head, ioat_chan->tail,
			ioat2_ring_size(ioat_chan));
}

/* count of descriptors pending submission to hardware */
static inline u16 ioat2_ring_pending(struct ioatdma_chan *ioat_chan)
{
	return CIRC_CNT(ioat_chan->head, ioat_chan->issued,
			ioat2_ring_size(ioat_chan));
}

static inline u32 ioat2_ring_space(struct ioatdma_chan *ioat_chan)
{
	return ioat2_ring_size(ioat_chan) - ioat2_ring_active(ioat_chan);
}

static inline u16
ioat2_xferlen_to_descs(struct ioatdma_chan *ioat_chan, size_t len)
{
	u16 num_descs = len >> ioat_chan->xfercap_log;

	num_descs += !!(len & ((1 << ioat_chan->xfercap_log) - 1));
	return num_descs;
}

/**
 * struct ioat_ring_ent - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor (for memcpy)
 * @fill: hardware fill descriptor
 * @xor: hardware xor descriptor
 * @xor_ex: hardware xor extension descriptor
 * @pq: hardware pq descriptor
 * @pq_ex: hardware pq extension descriptor
 * @pqu: hardware pq update descriptor
 * @raw: hardware raw (un-typed) descriptor
 * @txd: the generic software descriptor for all engines
 * @len: total transaction length for unmap
 * @result: asynchronous result of validate operations
 * @id: identifier for debug
 */

struct ioat_ring_ent {
	union {
		struct ioat_dma_descriptor *hw;
		struct ioat_xor_descriptor *xor;
		struct ioat_xor_ext_descriptor *xor_ex;
		struct ioat_pq_descriptor *pq;
		struct ioat_pq_ext_descriptor *pq_ex;
		struct ioat_pq_update_descriptor *pqu;
		struct ioat_raw_descriptor *raw;
	};
	size_t len;
	struct dma_async_tx_descriptor txd;
	enum sum_check_flags *result;
	#ifdef DEBUG
	int id;
	#endif
	struct ioat_sed_ent *sed;
};

static inline struct ioat_ring_ent *
ioat2_get_ring_ent(struct ioatdma_chan *ioat_chan, u16 idx)
{
	return ioat_chan->ring[idx & (ioat2_ring_size(ioat_chan) - 1)];
}

static inline void
ioat2_set_chainaddr(struct ioatdma_chan *ioat_chan, u64 addr)
{
	writel(addr & 0x00000000FFFFFFFF,
	       ioat_chan->reg_base + IOAT2_CHAINADDR_OFFSET_LOW);
	writel(addr >> 32,
	       ioat_chan->reg_base + IOAT2_CHAINADDR_OFFSET_HIGH);
}

int ioat2_dma_probe(struct ioatdma_device *ioat_dma, int dca);
int ioat3_dma_probe(struct ioatdma_device *ioat_dma, int dca);
struct dca_provider *ioat3_dca_init(struct pci_dev *pdev, void __iomem *iobase);
int ioat2_check_space_lock(struct ioatdma_chan *ioat_chan, int num_descs);
int ioat2_enumerate_channels(struct ioatdma_device *ioat_dma);
struct dma_async_tx_descriptor *
ioat2_dma_prep_memcpy_lock(struct dma_chan *c, dma_addr_t dma_dest,
			   dma_addr_t dma_src, size_t len, unsigned long flags);
void ioat2_issue_pending(struct dma_chan *chan);
int ioat2_alloc_chan_resources(struct dma_chan *c);
void ioat2_free_chan_resources(struct dma_chan *c);
void __ioat2_restart_chan(struct ioatdma_chan *ioat_chan);
bool reshape_ring(struct ioatdma_chan *ioat, int order);
void __ioat2_issue_pending(struct ioatdma_chan *ioat_chan);
void ioat2_timer_event(unsigned long data);
int ioat2_quiesce(struct ioatdma_chan *ioat_chan, unsigned long tmo);
int ioat2_reset_sync(struct ioatdma_chan *ioat_chan, unsigned long tmo);
extern struct kobj_type ioat2_ktype;
extern struct kmem_cache *ioat2_cache;
#endif /* IOATDMA_V2_H */
