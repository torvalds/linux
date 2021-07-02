// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * LIBEFC LOCKING
 *
 * The critical sections protected by the efc's spinlock are quite broad and
 * may be improved upon in the future. The libefc code and its locking doesn't
 * influence the I/O path, so excessive locking doesn't impact I/O performance.
 *
 * The strategy is to lock whenever processing a request from user driver. This
 * means that the entry points into the libefc library are protected by efc
 * lock. So all the state machine transitions are protected.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "efc.h"

int efcport_init(struct efc *efc)
{
	u32 rc = 0;

	spin_lock_init(&efc->lock);
	INIT_LIST_HEAD(&efc->vport_list);
	efc->hold_frames = false;
	spin_lock_init(&efc->pend_frames_lock);
	INIT_LIST_HEAD(&efc->pend_frames);

	/* Create Node pool */
	efc->node_pool = mempool_create_kmalloc_pool(EFC_MAX_REMOTE_NODES,
						     sizeof(struct efc_node));
	if (!efc->node_pool) {
		efc_log_err(efc, "Can't allocate node pool\n");
		return -ENOMEM;
	}

	efc->node_dma_pool = dma_pool_create("node_dma_pool", &efc->pci->dev,
					     NODE_SPARAMS_SIZE, 0, 0);
	if (!efc->node_dma_pool) {
		efc_log_err(efc, "Can't allocate node dma pool\n");
		mempool_destroy(efc->node_pool);
		return -ENOMEM;
	}

	efc->els_io_pool = mempool_create_kmalloc_pool(EFC_ELS_IO_POOL_SZ,
						sizeof(struct efc_els_io_req));
	if (!efc->els_io_pool) {
		efc_log_err(efc, "Can't allocate els io pool\n");
		return -ENOMEM;
	}

	return rc;
}

static void
efc_purge_pending(struct efc *efc)
{
	struct efc_hw_sequence *frame, *next;
	unsigned long flags = 0;

	spin_lock_irqsave(&efc->pend_frames_lock, flags);

	list_for_each_entry_safe(frame, next, &efc->pend_frames, list_entry) {
		list_del(&frame->list_entry);
		efc->tt.hw_seq_free(efc, frame);
	}

	spin_unlock_irqrestore(&efc->pend_frames_lock, flags);
}

void efcport_destroy(struct efc *efc)
{
	efc_purge_pending(efc);
	mempool_destroy(efc->els_io_pool);
	mempool_destroy(efc->node_pool);
	dma_pool_destroy(efc->node_dma_pool);
}
