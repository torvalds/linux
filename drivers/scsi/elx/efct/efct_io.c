// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"
#include "efct_hw.h"
#include "efct_io.h"

struct efct_io_pool {
	struct efct *efct;
	spinlock_t lock;	/* IO pool lock */
	u32 io_num_ios;		/* Total IOs allocated */
	struct efct_io *ios[EFCT_NUM_SCSI_IOS];
	struct list_head freelist;

};

struct efct_io_pool *
efct_io_pool_create(struct efct *efct, u32 num_sgl)
{
	u32 i = 0;
	struct efct_io_pool *io_pool;
	struct efct_io *io;

	/* Allocate the IO pool */
	io_pool = kzalloc(sizeof(*io_pool), GFP_KERNEL);
	if (!io_pool)
		return NULL;

	io_pool->efct = efct;
	INIT_LIST_HEAD(&io_pool->freelist);
	/* initialize IO pool lock */
	spin_lock_init(&io_pool->lock);

	for (i = 0; i < EFCT_NUM_SCSI_IOS; i++) {
		io = kzalloc(sizeof(*io), GFP_KERNEL);
		if (!io)
			break;

		io_pool->io_num_ios++;
		io_pool->ios[i] = io;
		io->tag = i;
		io->instance_index = i;

		/* Allocate a response buffer */
		io->rspbuf.size = SCSI_RSP_BUF_LENGTH;
		io->rspbuf.virt = dma_alloc_coherent(&efct->pci->dev,
						     io->rspbuf.size,
						     &io->rspbuf.phys, GFP_DMA);
		if (!io->rspbuf.virt) {
			efc_log_err(efct, "dma_alloc rspbuf failed\n");
			efct_io_pool_free(io_pool);
			return NULL;
		}

		/* Allocate SGL */
		io->sgl = kzalloc(sizeof(*io->sgl) * num_sgl, GFP_KERNEL);
		if (!io->sgl) {
			efct_io_pool_free(io_pool);
			return NULL;
		}

		memset(io->sgl, 0, sizeof(*io->sgl) * num_sgl);
		io->sgl_allocated = num_sgl;
		io->sgl_count = 0;

		INIT_LIST_HEAD(&io->list_entry);
		list_add_tail(&io->list_entry, &io_pool->freelist);
	}

	return io_pool;
}

int
efct_io_pool_free(struct efct_io_pool *io_pool)
{
	struct efct *efct;
	u32 i;
	struct efct_io *io;

	if (io_pool) {
		efct = io_pool->efct;

		for (i = 0; i < io_pool->io_num_ios; i++) {
			io = io_pool->ios[i];
			if (!io)
				continue;

			kfree(io->sgl);
			dma_free_coherent(&efct->pci->dev,
					  io->rspbuf.size, io->rspbuf.virt,
					  io->rspbuf.phys);
			memset(&io->rspbuf, 0, sizeof(struct efc_dma));
		}

		kfree(io_pool);
		efct->xport->io_pool = NULL;
	}

	return 0;
}

struct efct_io *
efct_io_pool_io_alloc(struct efct_io_pool *io_pool)
{
	struct efct_io *io = NULL;
	struct efct *efct;
	unsigned long flags = 0;

	efct = io_pool->efct;

	spin_lock_irqsave(&io_pool->lock, flags);

	if (!list_empty(&io_pool->freelist)) {
		io = list_first_entry(&io_pool->freelist, struct efct_io,
				      list_entry);
		list_del_init(&io->list_entry);
	}

	spin_unlock_irqrestore(&io_pool->lock, flags);

	if (!io)
		return NULL;

	io->io_type = EFCT_IO_TYPE_MAX;
	io->hio_type = EFCT_HW_IO_MAX;
	io->hio = NULL;
	io->transferred = 0;
	io->efct = efct;
	io->timeout = 0;
	io->sgl_count = 0;
	io->tgt_task_tag = 0;
	io->init_task_tag = 0;
	io->hw_tag = 0;
	io->display_name = "pending";
	io->seq_init = 0;
	io->io_free = 0;
	io->release = NULL;
	atomic_add_return(1, &efct->xport->io_active_count);
	atomic_add_return(1, &efct->xport->io_total_alloc);
	return io;
}

/* Free an object used to track an IO */
void
efct_io_pool_io_free(struct efct_io_pool *io_pool, struct efct_io *io)
{
	struct efct *efct;
	struct efct_hw_io *hio = NULL;
	unsigned long flags = 0;

	efct = io_pool->efct;

	spin_lock_irqsave(&io_pool->lock, flags);
	hio = io->hio;
	io->hio = NULL;
	io->io_free = 1;
	INIT_LIST_HEAD(&io->list_entry);
	list_add(&io->list_entry, &io_pool->freelist);
	spin_unlock_irqrestore(&io_pool->lock, flags);

	if (hio)
		efct_hw_io_free(&efct->hw, hio);

	atomic_sub_return(1, &efct->xport->io_active_count);
	atomic_add_return(1, &efct->xport->io_total_free);
}

/* Find an I/O given it's node and ox_id */
struct efct_io *
efct_io_find_tgt_io(struct efct *efct, struct efct_node *node,
		    u16 ox_id, u16 rx_id)
{
	struct efct_io	*io = NULL;
	unsigned long flags = 0;
	u8 found = false;

	spin_lock_irqsave(&node->active_ios_lock, flags);
	list_for_each_entry(io, &node->active_ios, list_entry) {
		if ((io->cmd_tgt && io->init_task_tag == ox_id) &&
		    (rx_id == 0xffff || io->tgt_task_tag == rx_id)) {
			if (kref_get_unless_zero(&io->ref))
				found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&node->active_ios_lock, flags);
	return found ? io : NULL;
}
