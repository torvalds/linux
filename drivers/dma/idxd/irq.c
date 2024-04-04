// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/sched/mm.h>
#include <uapi/linux/idxd.h>
#include "../dmaengine.h"
#include "idxd.h"
#include "registers.h"

enum irq_work_type {
	IRQ_WORK_NORMAL = 0,
	IRQ_WORK_PROCESS_FAULT,
};

struct idxd_resubmit {
	struct work_struct work;
	struct idxd_desc *desc;
};

struct idxd_int_handle_revoke {
	struct work_struct work;
	struct idxd_device *idxd;
};

static void idxd_device_reinit(struct work_struct *work)
{
	struct idxd_device *idxd = container_of(work, struct idxd_device, work);
	struct device *dev = &idxd->pdev->dev;
	int rc, i;

	idxd_device_reset(idxd);
	rc = idxd_device_config(idxd);
	if (rc < 0)
		goto out;

	rc = idxd_device_enable(idxd);
	if (rc < 0)
		goto out;

	for (i = 0; i < idxd->max_wqs; i++) {
		if (test_bit(i, idxd->wq_enable_map)) {
			struct idxd_wq *wq = idxd->wqs[i];

			rc = idxd_wq_enable(wq);
			if (rc < 0) {
				clear_bit(i, idxd->wq_enable_map);
				dev_warn(dev, "Unable to re-enable wq %s\n",
					 dev_name(wq_confdev(wq)));
			}
		}
	}

	return;

 out:
	idxd_device_clear_state(idxd);
}

/*
 * The function sends a drain descriptor for the interrupt handle. The drain ensures
 * all descriptors with this interrupt handle is flushed and the interrupt
 * will allow the cleanup of the outstanding descriptors.
 */
static void idxd_int_handle_revoke_drain(struct idxd_irq_entry *ie)
{
	struct idxd_wq *wq = ie_to_wq(ie);
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	struct dsa_hw_desc desc = {};
	void __iomem *portal;
	int rc;

	/* Issue a simple drain operation with interrupt but no completion record */
	desc.flags = IDXD_OP_FLAG_RCI;
	desc.opcode = DSA_OPCODE_DRAIN;
	desc.priv = 1;

	if (ie->pasid != IOMMU_PASID_INVALID)
		desc.pasid = ie->pasid;
	desc.int_handle = ie->int_handle;
	portal = idxd_wq_portal_addr(wq);

	/*
	 * The wmb() makes sure that the descriptor is all there before we
	 * issue.
	 */
	wmb();
	if (wq_dedicated(wq)) {
		iosubmit_cmds512(portal, &desc, 1);
	} else {
		rc = idxd_enqcmds(wq, portal, &desc);
		/* This should not fail unless hardware failed. */
		if (rc < 0)
			dev_warn(dev, "Failed to submit drain desc on wq %d\n", wq->id);
	}
}

static void idxd_abort_invalid_int_handle_descs(struct idxd_irq_entry *ie)
{
	LIST_HEAD(flist);
	struct idxd_desc *d, *t;
	struct llist_node *head;

	spin_lock(&ie->list_lock);
	head = llist_del_all(&ie->pending_llist);
	if (head) {
		llist_for_each_entry_safe(d, t, head, llnode)
			list_add_tail(&d->list, &ie->work_list);
	}

	list_for_each_entry_safe(d, t, &ie->work_list, list) {
		if (d->completion->status == DSA_COMP_INT_HANDLE_INVAL)
			list_move_tail(&d->list, &flist);
	}
	spin_unlock(&ie->list_lock);

	list_for_each_entry_safe(d, t, &flist, list) {
		list_del(&d->list);
		idxd_desc_complete(d, IDXD_COMPLETE_ABORT, true);
	}
}

static void idxd_int_handle_revoke(struct work_struct *work)
{
	struct idxd_int_handle_revoke *revoke =
		container_of(work, struct idxd_int_handle_revoke, work);
	struct idxd_device *idxd = revoke->idxd;
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	int i, new_handle, rc;

	if (!idxd->request_int_handles) {
		kfree(revoke);
		dev_warn(dev, "Unexpected int handle refresh interrupt.\n");
		return;
	}

	/*
	 * The loop attempts to acquire new interrupt handle for all interrupt
	 * vectors that supports a handle. If a new interrupt handle is acquired and the
	 * wq is kernel type, the driver will kill the percpu_ref to pause all
	 * ongoing descriptor submissions. The interrupt handle is then changed.
	 * After change, the percpu_ref is revived and all the pending submissions
	 * are woken to try again. A drain is sent to for the interrupt handle
	 * at the end to make sure all invalid int handle descriptors are processed.
	 */
	for (i = 1; i < idxd->irq_cnt; i++) {
		struct idxd_irq_entry *ie = idxd_get_ie(idxd, i);
		struct idxd_wq *wq = ie_to_wq(ie);

		if (ie->int_handle == INVALID_INT_HANDLE)
			continue;

		rc = idxd_device_request_int_handle(idxd, i, &new_handle, IDXD_IRQ_MSIX);
		if (rc < 0) {
			dev_warn(dev, "get int handle %d failed: %d\n", i, rc);
			/*
			 * Failed to acquire new interrupt handle. Kill the WQ
			 * and release all the pending submitters. The submitters will
			 * get error return code and handle appropriately.
			 */
			ie->int_handle = INVALID_INT_HANDLE;
			idxd_wq_quiesce(wq);
			idxd_abort_invalid_int_handle_descs(ie);
			continue;
		}

		/* No change in interrupt handle, nothing needs to be done */
		if (ie->int_handle == new_handle)
			continue;

		if (wq->state != IDXD_WQ_ENABLED || wq->type != IDXD_WQT_KERNEL) {
			/*
			 * All the MSIX interrupts are allocated at once during probe.
			 * Therefore we need to update all interrupts even if the WQ
			 * isn't supporting interrupt operations.
			 */
			ie->int_handle = new_handle;
			continue;
		}

		mutex_lock(&wq->wq_lock);
		reinit_completion(&wq->wq_resurrect);

		/* Kill percpu_ref to pause additional descriptor submissions */
		percpu_ref_kill(&wq->wq_active);

		/* Wait for all submitters quiesce before we change interrupt handle */
		wait_for_completion(&wq->wq_dead);

		ie->int_handle = new_handle;

		/* Revive percpu ref and wake up all the waiting submitters */
		percpu_ref_reinit(&wq->wq_active);
		complete_all(&wq->wq_resurrect);
		mutex_unlock(&wq->wq_lock);

		/*
		 * The delay here is to wait for all possible MOVDIR64B that
		 * are issued before percpu_ref_kill() has happened to have
		 * reached the PCIe domain before the drain is issued. The driver
		 * needs to ensure that the drain descriptor issued does not pass
		 * all the other issued descriptors that contain the invalid
		 * interrupt handle in order to ensure that the drain descriptor
		 * interrupt will allow the cleanup of all the descriptors with
		 * invalid interrupt handle.
		 */
		if (wq_dedicated(wq))
			udelay(100);
		idxd_int_handle_revoke_drain(ie);
	}
	kfree(revoke);
}

static void idxd_evl_fault_work(struct work_struct *work)
{
	struct idxd_evl_fault *fault = container_of(work, struct idxd_evl_fault, work);
	struct idxd_wq *wq = fault->wq;
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	struct idxd_evl *evl = idxd->evl;
	struct __evl_entry *entry_head = fault->entry;
	void *cr = (void *)entry_head + idxd->data->evl_cr_off;
	int cr_size = idxd->data->compl_size;
	u8 *status = (u8 *)cr + idxd->data->cr_status_off;
	u8 *result = (u8 *)cr + idxd->data->cr_result_off;
	int copied, copy_size;
	bool *bf;

	switch (fault->status) {
	case DSA_COMP_CRA_XLAT:
		if (entry_head->batch && entry_head->first_err_in_batch)
			evl->batch_fail[entry_head->batch_id] = false;

		copy_size = cr_size;
		idxd_user_counter_increment(wq, entry_head->pasid, COUNTER_FAULTS);
		break;
	case DSA_COMP_BATCH_EVL_ERR:
		bf = &evl->batch_fail[entry_head->batch_id];

		copy_size = entry_head->rcr || *bf ? cr_size : 0;
		if (*bf) {
			if (*status == DSA_COMP_SUCCESS)
				*status = DSA_COMP_BATCH_FAIL;
			*result = 1;
			*bf = false;
		}
		idxd_user_counter_increment(wq, entry_head->pasid, COUNTER_FAULTS);
		break;
	case DSA_COMP_DRAIN_EVL:
		copy_size = cr_size;
		break;
	default:
		copy_size = 0;
		dev_dbg_ratelimited(dev, "Unrecognized error code: %#x\n", fault->status);
		break;
	}

	if (copy_size == 0)
		return;

	/*
	 * Copy completion record to fault_addr in user address space
	 * that is found by wq and PASID.
	 */
	copied = idxd_copy_cr(wq, entry_head->pasid, entry_head->fault_addr,
			      cr, copy_size);
	/*
	 * The task that triggered the page fault is unknown currently
	 * because multiple threads may share the user address
	 * space or the task exits already before this fault.
	 * So if the copy fails, SIGSEGV can not be sent to the task.
	 * Just print an error for the failure. The user application
	 * waiting for the completion record will time out on this
	 * failure.
	 */
	switch (fault->status) {
	case DSA_COMP_CRA_XLAT:
		if (copied != copy_size) {
			idxd_user_counter_increment(wq, entry_head->pasid, COUNTER_FAULT_FAILS);
			dev_dbg_ratelimited(dev, "Failed to write to completion record: (%d:%d)\n",
					    copy_size, copied);
			if (entry_head->batch)
				evl->batch_fail[entry_head->batch_id] = true;
		}
		break;
	case DSA_COMP_BATCH_EVL_ERR:
		if (copied != copy_size) {
			idxd_user_counter_increment(wq, entry_head->pasid, COUNTER_FAULT_FAILS);
			dev_dbg_ratelimited(dev, "Failed to write to batch completion record: (%d:%d)\n",
					    copy_size, copied);
		}
		break;
	case DSA_COMP_DRAIN_EVL:
		if (copied != copy_size)
			dev_dbg_ratelimited(dev, "Failed to write to drain completion record: (%d:%d)\n",
					    copy_size, copied);
		break;
	}

	kmem_cache_free(idxd->evl_cache, fault);
}

static void process_evl_entry(struct idxd_device *idxd,
			      struct __evl_entry *entry_head, unsigned int index)
{
	struct device *dev = &idxd->pdev->dev;
	struct idxd_evl *evl = idxd->evl;
	u8 status;

	if (test_bit(index, evl->bmap)) {
		clear_bit(index, evl->bmap);
	} else {
		status = DSA_COMP_STATUS(entry_head->error);

		if (status == DSA_COMP_CRA_XLAT || status == DSA_COMP_DRAIN_EVL ||
		    status == DSA_COMP_BATCH_EVL_ERR) {
			struct idxd_evl_fault *fault;
			int ent_size = evl_ent_size(idxd);

			if (entry_head->rci)
				dev_dbg(dev, "Completion Int Req set, ignoring!\n");

			if (!entry_head->rcr && status == DSA_COMP_DRAIN_EVL)
				return;

			fault = kmem_cache_alloc(idxd->evl_cache, GFP_ATOMIC);
			if (fault) {
				struct idxd_wq *wq = idxd->wqs[entry_head->wq_idx];

				fault->wq = wq;
				fault->status = status;
				memcpy(&fault->entry, entry_head, ent_size);
				INIT_WORK(&fault->work, idxd_evl_fault_work);
				queue_work(wq->wq, &fault->work);
			} else {
				dev_warn(dev, "Failed to service fault work.\n");
			}
		} else {
			dev_warn_ratelimited(dev, "Device error %#x operation: %#x fault addr: %#llx\n",
					     status, entry_head->operation,
					     entry_head->fault_addr);
		}
	}
}

static void process_evl_entries(struct idxd_device *idxd)
{
	union evl_status_reg evl_status;
	unsigned int h, t;
	struct idxd_evl *evl = idxd->evl;
	struct __evl_entry *entry_head;
	unsigned int ent_size = evl_ent_size(idxd);
	u32 size;

	evl_status.bits = 0;
	evl_status.int_pending = 1;

	mutex_lock(&evl->lock);
	/* Clear interrupt pending bit */
	iowrite32(evl_status.bits_upper32,
		  idxd->reg_base + IDXD_EVLSTATUS_OFFSET + sizeof(u32));
	evl_status.bits = ioread64(idxd->reg_base + IDXD_EVLSTATUS_OFFSET);
	t = evl_status.tail;
	h = evl_status.head;
	size = idxd->evl->size;

	while (h != t) {
		entry_head = (struct __evl_entry *)(evl->log + (h * ent_size));
		process_evl_entry(idxd, entry_head, h);
		h = (h + 1) % size;
	}

	evl_status.head = h;
	iowrite32(evl_status.bits_lower32, idxd->reg_base + IDXD_EVLSTATUS_OFFSET);
	mutex_unlock(&evl->lock);
}

irqreturn_t idxd_misc_thread(int vec, void *data)
{
	struct idxd_irq_entry *irq_entry = data;
	struct idxd_device *idxd = ie_to_idxd(irq_entry);
	struct device *dev = &idxd->pdev->dev;
	union gensts_reg gensts;
	u32 val = 0;
	int i;
	bool err = false;
	u32 cause;

	cause = ioread32(idxd->reg_base + IDXD_INTCAUSE_OFFSET);
	if (!cause)
		return IRQ_NONE;

	iowrite32(cause, idxd->reg_base + IDXD_INTCAUSE_OFFSET);

	if (cause & IDXD_INTC_HALT_STATE)
		goto halt;

	if (cause & IDXD_INTC_ERR) {
		spin_lock(&idxd->dev_lock);
		for (i = 0; i < 4; i++)
			idxd->sw_err.bits[i] = ioread64(idxd->reg_base +
					IDXD_SWERR_OFFSET + i * sizeof(u64));

		iowrite64(idxd->sw_err.bits[0] & IDXD_SWERR_ACK,
			  idxd->reg_base + IDXD_SWERR_OFFSET);

		if (idxd->sw_err.valid && idxd->sw_err.wq_idx_valid) {
			int id = idxd->sw_err.wq_idx;
			struct idxd_wq *wq = idxd->wqs[id];

			if (wq->type == IDXD_WQT_USER)
				wake_up_interruptible(&wq->err_queue);
		} else {
			int i;

			for (i = 0; i < idxd->max_wqs; i++) {
				struct idxd_wq *wq = idxd->wqs[i];

				if (wq->type == IDXD_WQT_USER)
					wake_up_interruptible(&wq->err_queue);
			}
		}

		spin_unlock(&idxd->dev_lock);
		val |= IDXD_INTC_ERR;

		for (i = 0; i < 4; i++)
			dev_warn_ratelimited(dev, "err[%d]: %#16.16llx\n",
					     i, idxd->sw_err.bits[i]);
		err = true;
	}

	if (cause & IDXD_INTC_INT_HANDLE_REVOKED) {
		struct idxd_int_handle_revoke *revoke;

		val |= IDXD_INTC_INT_HANDLE_REVOKED;

		revoke = kzalloc(sizeof(*revoke), GFP_ATOMIC);
		if (revoke) {
			revoke->idxd = idxd;
			INIT_WORK(&revoke->work, idxd_int_handle_revoke);
			queue_work(idxd->wq, &revoke->work);

		} else {
			dev_err(dev, "Failed to allocate work for int handle revoke\n");
			idxd_wqs_quiesce(idxd);
		}
	}

	if (cause & IDXD_INTC_CMD) {
		val |= IDXD_INTC_CMD;
		complete(idxd->cmd_done);
	}

	if (cause & IDXD_INTC_OCCUPY) {
		/* Driver does not utilize occupancy interrupt */
		val |= IDXD_INTC_OCCUPY;
	}

	if (cause & IDXD_INTC_PERFMON_OVFL) {
		val |= IDXD_INTC_PERFMON_OVFL;
		perfmon_counter_overflow(idxd);
	}

	if (cause & IDXD_INTC_EVL) {
		val |= IDXD_INTC_EVL;
		process_evl_entries(idxd);
	}

	val ^= cause;
	if (val)
		dev_warn_once(dev, "Unexpected interrupt cause bits set: %#x\n",
			      val);

	if (!err)
		goto out;

halt:
	gensts.bits = ioread32(idxd->reg_base + IDXD_GENSTATS_OFFSET);
	if (gensts.state == IDXD_DEVICE_STATE_HALT) {
		idxd->state = IDXD_DEV_HALTED;
		if (gensts.reset_type == IDXD_DEVICE_RESET_SOFTWARE) {
			/*
			 * If we need a software reset, we will throw the work
			 * on a system workqueue in order to allow interrupts
			 * for the device command completions.
			 */
			INIT_WORK(&idxd->work, idxd_device_reinit);
			queue_work(idxd->wq, &idxd->work);
		} else {
			idxd->state = IDXD_DEV_HALTED;
			idxd_wqs_quiesce(idxd);
			idxd_wqs_unmap_portal(idxd);
			idxd_device_clear_state(idxd);
			dev_err(&idxd->pdev->dev,
				"idxd halted, need %s.\n",
				gensts.reset_type == IDXD_DEVICE_RESET_FLR ?
				"FLR" : "system reset");
		}
	}

out:
	return IRQ_HANDLED;
}

static void idxd_int_handle_resubmit_work(struct work_struct *work)
{
	struct idxd_resubmit *irw = container_of(work, struct idxd_resubmit, work);
	struct idxd_desc *desc = irw->desc;
	struct idxd_wq *wq = desc->wq;
	int rc;

	desc->completion->status = 0;
	rc = idxd_submit_desc(wq, desc);
	if (rc < 0) {
		dev_dbg(&wq->idxd->pdev->dev, "Failed to resubmit desc %d to wq %d.\n",
			desc->id, wq->id);
		/*
		 * If the error is not -EAGAIN, it means the submission failed due to wq
		 * has been killed instead of ENQCMDS failure. Here the driver needs to
		 * notify the submitter of the failure by reporting abort status.
		 *
		 * -EAGAIN comes from ENQCMDS failure. idxd_submit_desc() will handle the
		 * abort.
		 */
		if (rc != -EAGAIN) {
			desc->completion->status = IDXD_COMP_DESC_ABORT;
			idxd_desc_complete(desc, IDXD_COMPLETE_ABORT, false);
		}
		idxd_free_desc(wq, desc);
	}
	kfree(irw);
}

bool idxd_queue_int_handle_resubmit(struct idxd_desc *desc)
{
	struct idxd_wq *wq = desc->wq;
	struct idxd_device *idxd = wq->idxd;
	struct idxd_resubmit *irw;

	irw = kzalloc(sizeof(*irw), GFP_KERNEL);
	if (!irw)
		return false;

	irw->desc = desc;
	INIT_WORK(&irw->work, idxd_int_handle_resubmit_work);
	queue_work(idxd->wq, &irw->work);
	return true;
}

static void irq_process_pending_llist(struct idxd_irq_entry *irq_entry)
{
	struct idxd_desc *desc, *t;
	struct llist_node *head;

	head = llist_del_all(&irq_entry->pending_llist);
	if (!head)
		return;

	llist_for_each_entry_safe(desc, t, head, llnode) {
		u8 status = desc->completion->status & DSA_COMP_STATUS_MASK;

		if (status) {
			/*
			 * Check against the original status as ABORT is software defined
			 * and 0xff, which DSA_COMP_STATUS_MASK can mask out.
			 */
			if (unlikely(desc->completion->status == IDXD_COMP_DESC_ABORT)) {
				idxd_desc_complete(desc, IDXD_COMPLETE_ABORT, true);
				continue;
			}

			idxd_desc_complete(desc, IDXD_COMPLETE_NORMAL, true);
		} else {
			spin_lock(&irq_entry->list_lock);
			list_add_tail(&desc->list,
				      &irq_entry->work_list);
			spin_unlock(&irq_entry->list_lock);
		}
	}
}

static void irq_process_work_list(struct idxd_irq_entry *irq_entry)
{
	LIST_HEAD(flist);
	struct idxd_desc *desc, *n;

	/*
	 * This lock protects list corruption from access of list outside of the irq handler
	 * thread.
	 */
	spin_lock(&irq_entry->list_lock);
	if (list_empty(&irq_entry->work_list)) {
		spin_unlock(&irq_entry->list_lock);
		return;
	}

	list_for_each_entry_safe(desc, n, &irq_entry->work_list, list) {
		if (desc->completion->status) {
			list_move_tail(&desc->list, &flist);
		}
	}

	spin_unlock(&irq_entry->list_lock);

	list_for_each_entry(desc, &flist, list) {
		/*
		 * Check against the original status as ABORT is software defined
		 * and 0xff, which DSA_COMP_STATUS_MASK can mask out.
		 */
		if (unlikely(desc->completion->status == IDXD_COMP_DESC_ABORT)) {
			idxd_desc_complete(desc, IDXD_COMPLETE_ABORT, true);
			continue;
		}

		idxd_desc_complete(desc, IDXD_COMPLETE_NORMAL, true);
	}
}

irqreturn_t idxd_wq_thread(int irq, void *data)
{
	struct idxd_irq_entry *irq_entry = data;

	/*
	 * There are two lists we are processing. The pending_llist is where
	 * submmiter adds all the submitted descriptor after sending it to
	 * the workqueue. It's a lockless singly linked list. The work_list
	 * is the common linux double linked list. We are in a scenario of
	 * multiple producers and a single consumer. The producers are all
	 * the kernel submitters of descriptors, and the consumer is the
	 * kernel irq handler thread for the msix vector when using threaded
	 * irq. To work with the restrictions of llist to remain lockless,
	 * we are doing the following steps:
	 * 1. Iterate through the work_list and process any completed
	 *    descriptor. Delete the completed entries during iteration.
	 * 2. llist_del_all() from the pending list.
	 * 3. Iterate through the llist that was deleted from the pending list
	 *    and process the completed entries.
	 * 4. If the entry is still waiting on hardware, list_add_tail() to
	 *    the work_list.
	 */
	irq_process_work_list(irq_entry);
	irq_process_pending_llist(irq_entry);

	return IRQ_HANDLED;
}
