// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <uapi/linux/idxd.h>
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

irqreturn_t idxd_wq_thread(int irq, void *data)
{
	struct idxd_irq_entry *irq_entry = data;

	idxd_unmask_msix_vector(irq_entry->idxd, irq_entry->id);

	return IRQ_HANDLED;
}
