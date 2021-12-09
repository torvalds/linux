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

enum irq_work_type {
	IRQ_WORK_NORMAL = 0,
	IRQ_WORK_PROCESS_FAULT,
};

struct idxd_fault {
	struct work_struct work;
	u64 addr;
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
		struct idxd_wq *wq = idxd->wqs[i];

		if (wq->state == IDXD_WQ_ENABLED) {
			rc = idxd_wq_enable(wq);
			if (rc < 0) {
				dev_warn(dev, "Unable to re-enable wq %s\n",
					 dev_name(wq_confdev(wq)));
			}
		}
	}

	return;

 out:
	idxd_device_clear_state(idxd);
}

static int process_misc_interrupts(struct idxd_device *idxd, u32 cause)
{
	struct device *dev = &idxd->pdev->dev;
	union gensts_reg gensts;
	u32 val = 0;
	int i;
	bool err = false;

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
			dev_warn(dev, "err[%d]: %#16.16llx\n",
				 i, idxd->sw_err.bits[i]);
		err = true;
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

	val ^= cause;
	if (val)
		dev_warn_once(dev, "Unexpected interrupt cause bits set: %#x\n",
			      val);

	if (!err)
		return 0;

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
			spin_lock(&idxd->dev_lock);
			idxd->state = IDXD_DEV_HALTED;
			idxd_wqs_quiesce(idxd);
			idxd_wqs_unmap_portal(idxd);
			idxd_device_clear_state(idxd);
			dev_err(&idxd->pdev->dev,
				"idxd halted, need %s.\n",
				gensts.reset_type == IDXD_DEVICE_RESET_FLR ?
				"FLR" : "system reset");
			spin_unlock(&idxd->dev_lock);
			return -ENXIO;
		}
	}

	return 0;
}

irqreturn_t idxd_misc_thread(int vec, void *data)
{
	struct idxd_irq_entry *irq_entry = data;
	struct idxd_device *idxd = irq_entry->idxd;
	int rc;
	u32 cause;

	cause = ioread32(idxd->reg_base + IDXD_INTCAUSE_OFFSET);
	if (cause)
		iowrite32(cause, idxd->reg_base + IDXD_INTCAUSE_OFFSET);

	while (cause) {
		rc = process_misc_interrupts(idxd, cause);
		if (rc < 0)
			break;
		cause = ioread32(idxd->reg_base + IDXD_INTCAUSE_OFFSET);
		if (cause)
			iowrite32(cause, idxd->reg_base + IDXD_INTCAUSE_OFFSET);
	}

	return IRQ_HANDLED;
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
				complete_desc(desc, IDXD_COMPLETE_ABORT);
				continue;
			}

			complete_desc(desc, IDXD_COMPLETE_NORMAL);
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
			complete_desc(desc, IDXD_COMPLETE_ABORT);
			continue;
		}

		complete_desc(desc, IDXD_COMPLETE_NORMAL);
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
