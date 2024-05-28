// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf_controlq.h"

/**
 * idpf_ctlq_alloc_desc_ring - Allocate Control Queue (CQ) rings
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 */
static int idpf_ctlq_alloc_desc_ring(struct idpf_hw *hw,
				     struct idpf_ctlq_info *cq)
{
	size_t size = cq->ring_size * sizeof(struct idpf_ctlq_desc);

	cq->desc_ring.va = idpf_alloc_dma_mem(hw, &cq->desc_ring, size);
	if (!cq->desc_ring.va)
		return -ENOMEM;

	return 0;
}

/**
 * idpf_ctlq_alloc_bufs - Allocate Control Queue (CQ) buffers
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 *
 * Allocate the buffer head for all control queues, and if it's a receive
 * queue, allocate DMA buffers
 */
static int idpf_ctlq_alloc_bufs(struct idpf_hw *hw,
				struct idpf_ctlq_info *cq)
{
	int i;

	/* Do not allocate DMA buffers for transmit queues */
	if (cq->cq_type == IDPF_CTLQ_TYPE_MAILBOX_TX)
		return 0;

	/* We'll be allocating the buffer info memory first, then we can
	 * allocate the mapped buffers for the event processing
	 */
	cq->bi.rx_buff = kcalloc(cq->ring_size, sizeof(struct idpf_dma_mem *),
				 GFP_KERNEL);
	if (!cq->bi.rx_buff)
		return -ENOMEM;

	/* allocate the mapped buffers (except for the last one) */
	for (i = 0; i < cq->ring_size - 1; i++) {
		struct idpf_dma_mem *bi;
		int num = 1; /* number of idpf_dma_mem to be allocated */

		cq->bi.rx_buff[i] = kcalloc(num, sizeof(struct idpf_dma_mem),
					    GFP_KERNEL);
		if (!cq->bi.rx_buff[i])
			goto unwind_alloc_cq_bufs;

		bi = cq->bi.rx_buff[i];

		bi->va = idpf_alloc_dma_mem(hw, bi, cq->buf_size);
		if (!bi->va) {
			/* unwind will not free the failed entry */
			kfree(cq->bi.rx_buff[i]);
			goto unwind_alloc_cq_bufs;
		}
	}

	return 0;

unwind_alloc_cq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--) {
		idpf_free_dma_mem(hw, cq->bi.rx_buff[i]);
		kfree(cq->bi.rx_buff[i]);
	}
	kfree(cq->bi.rx_buff);

	return -ENOMEM;
}

/**
 * idpf_ctlq_free_desc_ring - Free Control Queue (CQ) rings
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 *
 * This assumes the posted send buffers have already been cleaned
 * and de-allocated
 */
static void idpf_ctlq_free_desc_ring(struct idpf_hw *hw,
				     struct idpf_ctlq_info *cq)
{
	idpf_free_dma_mem(hw, &cq->desc_ring);
}

/**
 * idpf_ctlq_free_bufs - Free CQ buffer info elements
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 *
 * Free the DMA buffers for RX queues, and DMA buffer header for both RX and TX
 * queues.  The upper layers are expected to manage freeing of TX DMA buffers
 */
static void idpf_ctlq_free_bufs(struct idpf_hw *hw, struct idpf_ctlq_info *cq)
{
	void *bi;

	if (cq->cq_type == IDPF_CTLQ_TYPE_MAILBOX_RX) {
		int i;

		/* free DMA buffers for rx queues*/
		for (i = 0; i < cq->ring_size; i++) {
			if (cq->bi.rx_buff[i]) {
				idpf_free_dma_mem(hw, cq->bi.rx_buff[i]);
				kfree(cq->bi.rx_buff[i]);
			}
		}

		bi = (void *)cq->bi.rx_buff;
	} else {
		bi = (void *)cq->bi.tx_msg;
	}

	/* free the buffer header */
	kfree(bi);
}

/**
 * idpf_ctlq_dealloc_ring_res - Free memory allocated for control queue
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 *
 * Free the memory used by the ring, buffers and other related structures
 */
void idpf_ctlq_dealloc_ring_res(struct idpf_hw *hw, struct idpf_ctlq_info *cq)
{
	/* free ring buffers and the ring itself */
	idpf_ctlq_free_bufs(hw, cq);
	idpf_ctlq_free_desc_ring(hw, cq);
}

/**
 * idpf_ctlq_alloc_ring_res - allocate memory for descriptor ring and bufs
 * @hw: pointer to hw struct
 * @cq: pointer to control queue struct
 *
 * Do *NOT* hold cq_lock when calling this as the memory allocation routines
 * called are not going to be atomic context safe
 */
int idpf_ctlq_alloc_ring_res(struct idpf_hw *hw, struct idpf_ctlq_info *cq)
{
	int err;

	/* allocate the ring memory */
	err = idpf_ctlq_alloc_desc_ring(hw, cq);
	if (err)
		return err;

	/* allocate buffers in the rings */
	err = idpf_ctlq_alloc_bufs(hw, cq);
	if (err)
		goto idpf_init_cq_free_ring;

	/* success! */
	return 0;

idpf_init_cq_free_ring:
	idpf_free_dma_mem(hw, &cq->desc_ring);

	return err;
}
