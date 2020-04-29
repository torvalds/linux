// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <uapi/linux/idxd.h>
#include "idxd.h"
#include "registers.h"

struct idxd_desc *idxd_alloc_desc(struct idxd_wq *wq, enum idxd_op_type optype)
{
	struct idxd_desc *desc;
	int idx;
	struct idxd_device *idxd = wq->idxd;

	if (idxd->state != IDXD_DEV_ENABLED)
		return ERR_PTR(-EIO);

	if (optype == IDXD_OP_BLOCK)
		percpu_down_read(&wq->submit_lock);
	else if (!percpu_down_read_trylock(&wq->submit_lock))
		return ERR_PTR(-EBUSY);

	if (!atomic_add_unless(&wq->dq_count, 1, wq->size)) {
		int rc;

		if (optype == IDXD_OP_NONBLOCK) {
			percpu_up_read(&wq->submit_lock);
			return ERR_PTR(-EAGAIN);
		}

		percpu_up_read(&wq->submit_lock);
		percpu_down_write(&wq->submit_lock);
		rc = wait_event_interruptible(wq->submit_waitq,
					      atomic_add_unless(&wq->dq_count,
								1, wq->size) ||
					       idxd->state != IDXD_DEV_ENABLED);
		percpu_up_write(&wq->submit_lock);
		if (rc < 0)
			return ERR_PTR(-EINTR);
		if (idxd->state != IDXD_DEV_ENABLED)
			return ERR_PTR(-EIO);
	} else {
		percpu_up_read(&wq->submit_lock);
	}

	idx = sbitmap_get(&wq->sbmap, 0, false);
	if (idx < 0) {
		atomic_dec(&wq->dq_count);
		return ERR_PTR(-EAGAIN);
	}

	desc = wq->descs[idx];
	memset(desc->hw, 0, sizeof(struct dsa_hw_desc));
	memset(desc->completion, 0, sizeof(struct dsa_completion_record));
	return desc;
}

void idxd_free_desc(struct idxd_wq *wq, struct idxd_desc *desc)
{
	atomic_dec(&wq->dq_count);

	sbitmap_clear_bit(&wq->sbmap, desc->id);
	wake_up(&wq->submit_waitq);
}

int idxd_submit_desc(struct idxd_wq *wq, struct idxd_desc *desc)
{
	struct idxd_device *idxd = wq->idxd;
	int vec = desc->hw->int_handle;
	void __iomem *portal;

	if (idxd->state != IDXD_DEV_ENABLED)
		return -EIO;

	portal = wq->dportal + idxd_get_wq_portal_offset(IDXD_PORTAL_UNLIMITED);
	/*
	 * The wmb() flushes writes to coherent DMA data before possibly
	 * triggering a DMA read. The wmb() is necessary even on UP because
	 * the recipient is a device.
	 */
	wmb();
	iosubmit_cmds512(portal, desc->hw, 1);

	/*
	 * Pending the descriptor to the lockless list for the irq_entry
	 * that we designated the descriptor to.
	 */
	if (desc->hw->flags & IDXD_OP_FLAG_RCI)
		llist_add(&desc->llnode,
			  &idxd->irq_entries[vec].pending_llist);

	return 0;
}
