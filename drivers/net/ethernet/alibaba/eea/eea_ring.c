// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include "eea_pci.h"
#include "eea_ring.h"

void eea_ering_irq_active(struct eea_ring *ering, struct eea_ring *tx_ering)
{
	u64 value = 0, rx_idx, tx_idx;

	tx_idx = (u64)tx_ering->cq.hw_idx;
	rx_idx = (u64)ering->cq.hw_idx;

	value |= EEA_IRQ_UNMASK << EEA_DB_FLAGS_OFF;
	value |= tx_idx << EEA_DB_TX_CQ_HEAD_OFF;
	value |= rx_idx << EEA_DB_RX_CQ_HEAD_OFF;

	writeq(value, ering->db);
}

void *eea_ering_cq_get_desc(const struct eea_ring *ering)
{
	u8 phase;
	u8 *desc;

	desc = ering->cq.desc + (ering->cq.head << ering->cq.desc_size_shift);

	phase = READ_ONCE(*(u8 *)(desc + ering->cq.desc_size - 1));

	if ((phase & EEA_RING_DESC_F_CQ_PHASE) == ering->cq.phase) {
		dma_rmb();
		return desc;
	}

	return NULL;
}

/* sq api */
void *eea_ering_sq_alloc_desc(struct eea_ring *ering, u16 id, bool is_last,
			      u16 flags)
{
	struct eea_ring_sq *sq = &ering->sq;
	struct eea_common_desc *desc;

	if (!sq->shadow_num) {
		sq->shadow_idx = sq->head;
		sq->shadow_id = cpu_to_le16(id);
	}

	if (!is_last)
		flags |= EEA_RING_DESC_F_MORE;

	desc = sq->desc + (sq->shadow_idx << sq->desc_size_shift);

	desc->flags = cpu_to_le16(flags);
	desc->id = sq->shadow_id;

	if (unlikely(++sq->shadow_idx >= ering->num))
		sq->shadow_idx = 0;

	++sq->shadow_num;

	return desc;
}

/* This is an allocation API for admin Q. For each call to admin Q, only one
 * desc will be allocated.
 */
void *eea_ering_aq_alloc_desc(struct eea_ring *ering)
{
	struct eea_ring_sq *sq = &ering->sq;
	struct eea_common_desc *desc;

	if (!sq->shadow_num)
		sq->shadow_idx = sq->head;

	desc = sq->desc + (sq->shadow_idx << sq->desc_size_shift);

	if (unlikely(++sq->shadow_idx >= ering->num))
		sq->shadow_idx = 0;

	++sq->shadow_num;

	return desc;
}

void eea_ering_sq_commit_desc(struct eea_ring *ering)
{
	struct eea_ring_sq *sq = &ering->sq;
	int num;

	num = sq->shadow_num;

	ering->num_free -= num;

	sq->head       = sq->shadow_idx;
	sq->hw_idx     += num;
	sq->shadow_num = 0;
}

void eea_ering_sq_cancel(struct eea_ring *ering)
{
	ering->sq.shadow_num = 0;
}

/* cq api */
void eea_ering_cq_ack_desc(struct eea_ring *ering, u32 num)
{
	struct eea_ring_cq *cq = &ering->cq;

	cq->head += num;
	cq->hw_idx += num;

	if (unlikely(cq->head >= ering->num)) {
		cq->head -= ering->num;
		cq->phase ^= EEA_RING_DESC_F_CQ_PHASE;
	}

	ering->num_free += num;
}

/* notify */
void eea_ering_kick(struct eea_ring *ering)
{
	u64 value = 0, idx;

	idx = (u64)ering->sq.hw_idx;

	value |= EEA_IDX_PRESENT << EEA_DB_FLAGS_OFF;
	value |= idx << EEA_DB_IDX_OFF;

	writeq(value, ering->db);
}

/* ering alloc/free */
static void ering_free_queue(struct eea_device *edev, size_t size,
			     void *queue, dma_addr_t dma_handle)
{
	dma_free_coherent(edev->dma_dev, size, queue, dma_handle);
}

static void *ering_alloc_queue(struct eea_device *edev, size_t size,
			       dma_addr_t *dma_handle)
{
	gfp_t flags = GFP_KERNEL | __GFP_NOWARN;

	return dma_alloc_coherent(edev->dma_dev, size, dma_handle, flags);
}

static int ering_alloc_queues(struct eea_ring *ering, struct eea_device *edev,
			      size_t num, u8 sq_desc_size, u8 cq_desc_size)
{
	dma_addr_t addr;
	size_t size;
	void *ring;

	size = num * sq_desc_size;

	ring = ering_alloc_queue(edev, size, &addr);
	if (!ring)
		return -ENOMEM;

	ering->sq.desc     = ring;
	ering->sq.dma_addr = addr;
	ering->sq.dma_size = size;
	ering->sq.desc_size = sq_desc_size;
	ering->sq.desc_size_shift = fls(sq_desc_size) - 1;

	size = num * cq_desc_size;

	ring = ering_alloc_queue(edev, size, &addr);
	if (!ring)
		goto err_free_sq;

	ering->cq.desc     = ring;
	ering->cq.dma_addr = addr;
	ering->cq.dma_size = size;
	ering->cq.desc_size = cq_desc_size;
	ering->cq.desc_size_shift = fls(cq_desc_size) - 1;

	ering->num = num;

	return 0;

err_free_sq:
	ering_free_queue(ering->edev, ering->sq.dma_size,
			 ering->sq.desc, ering->sq.dma_addr);
	return -ENOMEM;
}

static void ering_init(struct eea_ring *ering)
{
	ering->cq.phase = EEA_RING_DESC_F_CQ_PHASE;
	ering->num_free = ering->num;
}

struct eea_ring *eea_ering_alloc(u32 index, u32 num, struct eea_device *edev,
				 u8 sq_desc_size, u8 cq_desc_size,
				 const char *name)
{
	struct eea_ring *ering;

	if (num > EEA_NET_IO_HW_RING_DEPTH_MAX ||
	    num < EEA_NET_IO_RING_DEPTH_MIN)
		return NULL;

	if (!is_power_of_2(num))
		return NULL;

	if (!sq_desc_size || !is_power_of_2(sq_desc_size))
		return NULL;

	if (!cq_desc_size || !is_power_of_2(cq_desc_size))
		return NULL;

	ering = kzalloc(sizeof(*ering), GFP_KERNEL);
	if (!ering)
		return NULL;

	ering->edev = edev;
	ering->name = name;
	ering->index = index;

	if (ering_alloc_queues(ering, edev, num, sq_desc_size, cq_desc_size))
		goto err_free;

	ering_init(ering);

	return ering;

err_free:
	kfree(ering);
	return NULL;
}

void eea_ering_free(struct eea_ring *ering)
{
	ering_free_queue(ering->edev, ering->cq.dma_size,
			 ering->cq.desc, ering->cq.dma_addr);

	ering_free_queue(ering->edev, ering->sq.dma_size,
			 ering->sq.desc, ering->sq.dma_addr);

	kfree(ering);
}
