// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/dmaengine.h>
#include <uapi/linux/idxd.h>
#include "../dmaengine.h"
#include "idxd.h"
#include "registers.h"

void idxd_device_wqs_clear_state(struct idxd_device *idxd)
{
	int i;

	lockdep_assert_held(&idxd->dev_lock);
	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

		wq->state = IDXD_WQ_DISABLED;
	}
}

static int idxd_restart(struct idxd_device *idxd)
{
	int i, rc;

	lockdep_assert_held(&idxd->dev_lock);

	rc = __idxd_device_reset(idxd);
	if (rc < 0)
		goto out;

	rc = idxd_device_config(idxd);
	if (rc < 0)
		goto out;

	rc = idxd_device_enable(idxd);
	if (rc < 0)
		goto out;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

		if (wq->state == IDXD_WQ_ENABLED) {
			rc = idxd_wq_enable(wq);
			if (rc < 0) {
				dev_warn(&idxd->pdev->dev,
					 "Unable to re-enable wq %s\n",
					 dev_name(&wq->conf_dev));
			}
		}
	}

	return 0;

 out:
	idxd_device_wqs_clear_state(idxd);
	idxd->state = IDXD_DEV_HALTED;
	return rc;
}

irqreturn_t idxd_irq_handler(int vec, void *data)
{
	struct idxd_irq_entry *irq_entry = data;
	struct idxd_device *idxd = irq_entry->idxd;

	idxd_mask_msix_vector(idxd, irq_entry->id);
	return IRQ_WAKE_THREAD;
}

irqreturn_t idxd_misc_thread(int vec, void *data)
{
	struct idxd_irq_entry *irq_entry = data;
	struct idxd_device *idxd = irq_entry->idxd;
	struct device *dev = &idxd->pdev->dev;
	union gensts_reg gensts;
	u32 cause, val = 0;
	int i, rc;
	bool err = false;

	cause = ioread32(idxd->reg_base + IDXD_INTCAUSE_OFFSET);

	if (cause & IDXD_INTC_ERR) {
		spin_lock_bh(&idxd->dev_lock);
		for (i = 0; i < 4; i++)
			idxd->sw_err.bits[i] = ioread64(idxd->reg_base +
					IDXD_SWERR_OFFSET + i * sizeof(u64));
		iowrite64(IDXD_SWERR_ACK, idxd->reg_base + IDXD_SWERR_OFFSET);

		if (idxd->sw_err.valid && idxd->sw_err.wq_idx_valid) {
			int id = idxd->sw_err.wq_idx;
			struct idxd_wq *wq = &idxd->wqs[id];

			if (wq->type == IDXD_WQT_USER)
				wake_up_interruptible(&wq->idxd_cdev.err_queue);
		} else {
			int i;

			for (i = 0; i < idxd->max_wqs; i++) {
				struct idxd_wq *wq = &idxd->wqs[i];

				if (wq->type == IDXD_WQT_USER)
					wake_up_interruptible(&wq->idxd_cdev.err_queue);
			}
		}

		spin_unlock_bh(&idxd->dev_lock);
		val |= IDXD_INTC_ERR;

		for (i = 0; i < 4; i++)
			dev_warn(dev, "err[%d]: %#16.16llx\n",
				 i, idxd->sw_err.bits[i]);
		err = true;
	}

	if (cause & IDXD_INTC_CMD) {
		/* Driver does use command interrupts */
		val |= IDXD_INTC_CMD;
	}

	if (cause & IDXD_INTC_OCCUPY) {
		/* Driver does not utilize occupancy interrupt */
		val |= IDXD_INTC_OCCUPY;
	}

	if (cause & IDXD_INTC_PERFMON_OVFL) {
		/*
		 * Driver does not utilize perfmon counter overflow interrupt
		 * yet.
		 */
		val |= IDXD_INTC_PERFMON_OVFL;
	}

	val ^= cause;
	if (val)
		dev_warn_once(dev, "Unexpected interrupt cause bits set: %#x\n",
			      val);

	iowrite32(cause, idxd->reg_base + IDXD_INTCAUSE_OFFSET);
	if (!err)
		return IRQ_HANDLED;

	gensts.bits = ioread32(idxd->reg_base + IDXD_GENSTATS_OFFSET);
	if (gensts.state == IDXD_DEVICE_STATE_HALT) {
		spin_lock_bh(&idxd->dev_lock);
		if (gensts.reset_type == IDXD_DEVICE_RESET_SOFTWARE) {
			rc = idxd_restart(idxd);
			if (rc < 0)
				dev_err(&idxd->pdev->dev,
					"idxd restart failed, device halt.");
		} else {
			idxd_device_wqs_clear_state(idxd);
			idxd->state = IDXD_DEV_HALTED;
			dev_err(&idxd->pdev->dev,
				"idxd halted, need %s.\n",
				gensts.reset_type == IDXD_DEVICE_RESET_FLR ?
				"FLR" : "system reset");
		}
		spin_unlock_bh(&idxd->dev_lock);
	}

	idxd_unmask_msix_vector(idxd, irq_entry->id);
	return IRQ_HANDLED;
}

static int irq_process_pending_llist(struct idxd_irq_entry *irq_entry,
				     int *processed)
{
	struct idxd_desc *desc, *t;
	struct llist_node *head;
	int queued = 0;

	head = llist_del_all(&irq_entry->pending_llist);
	if (!head)
		return 0;

	llist_for_each_entry_safe(desc, t, head, llnode) {
		if (desc->completion->status) {
			idxd_dma_complete_txd(desc, IDXD_COMPLETE_NORMAL);
			idxd_free_desc(desc->wq, desc);
			(*processed)++;
		} else {
			list_add_tail(&desc->list, &irq_entry->work_list);
			queued++;
		}
	}

	return queued;
}

static int irq_process_work_list(struct idxd_irq_entry *irq_entry,
				 int *processed)
{
	struct list_head *node, *next;
	int queued = 0;

	if (list_empty(&irq_entry->work_list))
		return 0;

	list_for_each_safe(node, next, &irq_entry->work_list) {
		struct idxd_desc *desc =
			container_of(node, struct idxd_desc, list);

		if (desc->completion->status) {
			list_del(&desc->list);
			/* process and callback */
			idxd_dma_complete_txd(desc, IDXD_COMPLETE_NORMAL);
			idxd_free_desc(desc->wq, desc);
			(*processed)++;
		} else {
			queued++;
		}
	}

	return queued;
}

irqreturn_t idxd_wq_thread(int irq, void *data)
{
	struct idxd_irq_entry *irq_entry = data;
	int rc, processed = 0, retry = 0;

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
	 * 5. Repeat until no more descriptors.
	 */
	do {
		rc = irq_process_work_list(irq_entry, &processed);
		if (rc != 0) {
			retry++;
			continue;
		}

		rc = irq_process_pending_llist(irq_entry, &processed);
	} while (rc != 0 && retry != 10);

	idxd_unmask_msix_vector(irq_entry->idxd, irq_entry->id);

	if (processed == 0)
		return IRQ_NONE;

	return IRQ_HANDLED;
}
