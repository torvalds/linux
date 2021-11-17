// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/debugfs.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/kthread.h>

#include "dpu_core_irq.h"
#include "dpu_trace.h"

/**
 * dpu_core_irq_callback_handler - dispatch core interrupts
 * @arg:		private data of callback handler
 * @irq_idx:		interrupt index
 */
static void dpu_core_irq_callback_handler(void *arg, int irq_idx)
{
	struct dpu_kms *dpu_kms = arg;
	struct dpu_irq *irq_obj = &dpu_kms->irq_obj;
	struct dpu_irq_callback *cb;

	VERB("irq_idx=%d\n", irq_idx);

	if (list_empty(&irq_obj->irq_cb_tbl[irq_idx]))
		DRM_ERROR("no registered cb, idx:%d\n", irq_idx);

	atomic_inc(&irq_obj->irq_counts[irq_idx]);

	/*
	 * Perform registered function callback
	 */
	list_for_each_entry(cb, &irq_obj->irq_cb_tbl[irq_idx], list)
		if (cb->func)
			cb->func(cb->arg, irq_idx);
}

u32 dpu_core_irq_read(struct dpu_kms *dpu_kms, int irq_idx, bool clear)
{
	if (!dpu_kms->hw_intr ||
			!dpu_kms->hw_intr->ops.get_interrupt_status)
		return 0;

	if (irq_idx < 0) {
		DPU_ERROR("[%pS] invalid irq_idx=%d\n",
				__builtin_return_address(0), irq_idx);
		return 0;
	}

	return dpu_kms->hw_intr->ops.get_interrupt_status(dpu_kms->hw_intr,
			irq_idx, clear);
}

int dpu_core_irq_register_callback(struct dpu_kms *dpu_kms, int irq_idx,
		struct dpu_irq_callback *register_irq_cb)
{
	unsigned long irq_flags;

	if (!dpu_kms->irq_obj.irq_cb_tbl) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!register_irq_cb || !register_irq_cb->func) {
		DPU_ERROR("invalid irq_cb:%d func:%d\n",
				register_irq_cb != NULL,
				register_irq_cb ?
					register_irq_cb->func != NULL : -1);
		return -EINVAL;
	}

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->total_irqs) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	VERB("[%pS] irq_idx=%d\n", __builtin_return_address(0), irq_idx);

	irq_flags = dpu_kms->hw_intr->ops.lock(dpu_kms->hw_intr);
	trace_dpu_core_irq_register_callback(irq_idx, register_irq_cb);
	list_del_init(&register_irq_cb->list);
	list_add_tail(&register_irq_cb->list,
			&dpu_kms->irq_obj.irq_cb_tbl[irq_idx]);
	if (list_is_first(&register_irq_cb->list,
			&dpu_kms->irq_obj.irq_cb_tbl[irq_idx])) {
		int ret = dpu_kms->hw_intr->ops.enable_irq_locked(
				dpu_kms->hw_intr,
				irq_idx);
		if (ret)
			DPU_ERROR("Fail to enable IRQ for irq_idx:%d\n",
					irq_idx);
	}
	dpu_kms->hw_intr->ops.unlock(dpu_kms->hw_intr, irq_flags);

	return 0;
}

int dpu_core_irq_unregister_callback(struct dpu_kms *dpu_kms, int irq_idx,
		struct dpu_irq_callback *register_irq_cb)
{
	unsigned long irq_flags;

	if (!dpu_kms->irq_obj.irq_cb_tbl) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!register_irq_cb || !register_irq_cb->func) {
		DPU_ERROR("invalid irq_cb:%d func:%d\n",
				register_irq_cb != NULL,
				register_irq_cb ?
					register_irq_cb->func != NULL : -1);
		return -EINVAL;
	}

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->total_irqs) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	VERB("[%pS] irq_idx=%d\n", __builtin_return_address(0), irq_idx);

	irq_flags = dpu_kms->hw_intr->ops.lock(dpu_kms->hw_intr);
	trace_dpu_core_irq_unregister_callback(irq_idx, register_irq_cb);
	list_del_init(&register_irq_cb->list);
	/* empty callback list but interrupt is still enabled */
	if (list_empty(&dpu_kms->irq_obj.irq_cb_tbl[irq_idx])) {
		int ret = dpu_kms->hw_intr->ops.disable_irq_locked(
				dpu_kms->hw_intr,
				irq_idx);
		if (ret)
			DPU_ERROR("Fail to disable IRQ for irq_idx:%d\n",
					irq_idx);
		VERB("irq_idx=%d ret=%d\n", irq_idx, ret);
	}
	dpu_kms->hw_intr->ops.unlock(dpu_kms->hw_intr, irq_flags);

	return 0;
}

static void dpu_clear_all_irqs(struct dpu_kms *dpu_kms)
{
	if (!dpu_kms->hw_intr || !dpu_kms->hw_intr->ops.clear_all_irqs)
		return;

	dpu_kms->hw_intr->ops.clear_all_irqs(dpu_kms->hw_intr);
}

static void dpu_disable_all_irqs(struct dpu_kms *dpu_kms)
{
	if (!dpu_kms->hw_intr || !dpu_kms->hw_intr->ops.disable_all_irqs)
		return;

	dpu_kms->hw_intr->ops.disable_all_irqs(dpu_kms->hw_intr);
}

#ifdef CONFIG_DEBUG_FS
static int dpu_debugfs_core_irq_show(struct seq_file *s, void *v)
{
	struct dpu_kms *dpu_kms = s->private;
	struct dpu_irq *irq_obj = &dpu_kms->irq_obj;
	struct dpu_irq_callback *cb;
	unsigned long irq_flags;
	int i, irq_count, cb_count;

	if (WARN_ON(!irq_obj->irq_cb_tbl))
		return 0;

	for (i = 0; i < irq_obj->total_irqs; i++) {
		irq_flags = dpu_kms->hw_intr->ops.lock(dpu_kms->hw_intr);
		cb_count = 0;
		irq_count = atomic_read(&irq_obj->irq_counts[i]);
		list_for_each_entry(cb, &irq_obj->irq_cb_tbl[i], list)
			cb_count++;
		dpu_kms->hw_intr->ops.unlock(dpu_kms->hw_intr, irq_flags);

		if (irq_count || cb_count)
			seq_printf(s, "idx:%d irq:%d cb:%d\n",
					i, irq_count, cb_count);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dpu_debugfs_core_irq);

void dpu_debugfs_core_irq_init(struct dpu_kms *dpu_kms,
		struct dentry *parent)
{
	debugfs_create_file("core_irq", 0600, parent, dpu_kms,
		&dpu_debugfs_core_irq_fops);
}
#endif

void dpu_core_irq_preinstall(struct dpu_kms *dpu_kms)
{
	int i;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	dpu_clear_all_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	/* Create irq callbacks for all possible irq_idx */
	dpu_kms->irq_obj.total_irqs = dpu_kms->hw_intr->total_irqs;
	dpu_kms->irq_obj.irq_cb_tbl = kcalloc(dpu_kms->irq_obj.total_irqs,
			sizeof(struct list_head), GFP_KERNEL);
	dpu_kms->irq_obj.irq_counts = kcalloc(dpu_kms->irq_obj.total_irqs,
			sizeof(atomic_t), GFP_KERNEL);
	for (i = 0; i < dpu_kms->irq_obj.total_irqs; i++) {
		INIT_LIST_HEAD(&dpu_kms->irq_obj.irq_cb_tbl[i]);
		atomic_set(&dpu_kms->irq_obj.irq_counts[i], 0);
	}
}

void dpu_core_irq_uninstall(struct dpu_kms *dpu_kms)
{
	int i;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	for (i = 0; i < dpu_kms->irq_obj.total_irqs; i++)
		if (!list_empty(&dpu_kms->irq_obj.irq_cb_tbl[i]))
			DPU_ERROR("irq_idx=%d still enabled/registered\n", i);

	dpu_clear_all_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	kfree(dpu_kms->irq_obj.irq_cb_tbl);
	kfree(dpu_kms->irq_obj.irq_counts);
	dpu_kms->irq_obj.irq_cb_tbl = NULL;
	dpu_kms->irq_obj.irq_counts = NULL;
	dpu_kms->irq_obj.total_irqs = 0;
}

irqreturn_t dpu_core_irq(struct dpu_kms *dpu_kms)
{
	/*
	 * Dispatch to HW driver to handle interrupt lookup that is being
	 * fired. When matching interrupt is located, HW driver will call to
	 * dpu_core_irq_callback_handler with the irq_idx from the lookup table.
	 * dpu_core_irq_callback_handler will perform the registered function
	 * callback, and do the interrupt status clearing once the registered
	 * callback is finished.
	 * Function will also clear the interrupt status after reading.
	 */
	dpu_kms->hw_intr->ops.dispatch_irqs(
			dpu_kms->hw_intr,
			dpu_core_irq_callback_handler,
			dpu_kms);

	return IRQ_HANDLED;
}
