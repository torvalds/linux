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
	unsigned long irq_flags;

	pr_debug("irq_idx=%d\n", irq_idx);

	if (list_empty(&irq_obj->irq_cb_tbl[irq_idx])) {
		DRM_ERROR("no registered cb, idx:%d enable_count:%d\n", irq_idx,
			atomic_read(&dpu_kms->irq_obj.enable_counts[irq_idx]));
	}

	atomic_inc(&irq_obj->irq_counts[irq_idx]);

	/*
	 * Perform registered function callback
	 */
	spin_lock_irqsave(&dpu_kms->irq_obj.cb_lock, irq_flags);
	list_for_each_entry(cb, &irq_obj->irq_cb_tbl[irq_idx], list)
		if (cb->func)
			cb->func(cb->arg, irq_idx);
	spin_unlock_irqrestore(&dpu_kms->irq_obj.cb_lock, irq_flags);

	/*
	 * Clear pending interrupt status in HW.
	 * NOTE: dpu_core_irq_callback_handler is protected by top-level
	 *       spinlock, so it is safe to clear any interrupt status here.
	 */
	dpu_kms->hw_intr->ops.clear_intr_status_nolock(
			dpu_kms->hw_intr,
			irq_idx);
}

int dpu_core_irq_idx_lookup(struct dpu_kms *dpu_kms,
		enum dpu_intr_type intr_type, u32 instance_idx)
{
	if (!dpu_kms || !dpu_kms->hw_intr ||
			!dpu_kms->hw_intr->ops.irq_idx_lookup)
		return -EINVAL;

	return dpu_kms->hw_intr->ops.irq_idx_lookup(intr_type,
			instance_idx);
}

/**
 * _dpu_core_irq_enable - enable core interrupt given by the index
 * @dpu_kms:		Pointer to dpu kms context
 * @irq_idx:		interrupt index
 */
static int _dpu_core_irq_enable(struct dpu_kms *dpu_kms, int irq_idx)
{
	unsigned long irq_flags;
	int ret = 0, enable_count;

	if (!dpu_kms || !dpu_kms->hw_intr ||
			!dpu_kms->irq_obj.enable_counts ||
			!dpu_kms->irq_obj.irq_counts) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->irq_idx_tbl_size) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	enable_count = atomic_read(&dpu_kms->irq_obj.enable_counts[irq_idx]);
	DRM_DEBUG_KMS("irq_idx=%d enable_count=%d\n", irq_idx, enable_count);
	trace_dpu_core_irq_enable_idx(irq_idx, enable_count);

	if (atomic_inc_return(&dpu_kms->irq_obj.enable_counts[irq_idx]) == 1) {
		ret = dpu_kms->hw_intr->ops.enable_irq(
				dpu_kms->hw_intr,
				irq_idx);
		if (ret)
			DPU_ERROR("Fail to enable IRQ for irq_idx:%d\n",
					irq_idx);

		DPU_DEBUG("irq_idx=%d ret=%d\n", irq_idx, ret);

		spin_lock_irqsave(&dpu_kms->irq_obj.cb_lock, irq_flags);
		/* empty callback list but interrupt is enabled */
		if (list_empty(&dpu_kms->irq_obj.irq_cb_tbl[irq_idx]))
			DPU_ERROR("irq_idx=%d enabled with no callback\n",
					irq_idx);
		spin_unlock_irqrestore(&dpu_kms->irq_obj.cb_lock, irq_flags);
	}

	return ret;
}

int dpu_core_irq_enable(struct dpu_kms *dpu_kms, int *irq_idxs, u32 irq_count)
{
	int i, ret = 0, counts;

	if (!dpu_kms || !irq_idxs || !irq_count) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	counts = atomic_read(&dpu_kms->irq_obj.enable_counts[irq_idxs[0]]);
	if (counts)
		DRM_ERROR("irq_idx=%d enable_count=%d\n", irq_idxs[0], counts);

	for (i = 0; (i < irq_count) && !ret; i++)
		ret = _dpu_core_irq_enable(dpu_kms, irq_idxs[i]);

	return ret;
}

/**
 * _dpu_core_irq_disable - disable core interrupt given by the index
 * @dpu_kms:		Pointer to dpu kms context
 * @irq_idx:		interrupt index
 */
static int _dpu_core_irq_disable(struct dpu_kms *dpu_kms, int irq_idx)
{
	int ret = 0, enable_count;

	if (!dpu_kms || !dpu_kms->hw_intr || !dpu_kms->irq_obj.enable_counts) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->irq_idx_tbl_size) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	enable_count = atomic_read(&dpu_kms->irq_obj.enable_counts[irq_idx]);
	DRM_DEBUG_KMS("irq_idx=%d enable_count=%d\n", irq_idx, enable_count);
	trace_dpu_core_irq_disable_idx(irq_idx, enable_count);

	if (atomic_dec_return(&dpu_kms->irq_obj.enable_counts[irq_idx]) == 0) {
		ret = dpu_kms->hw_intr->ops.disable_irq(
				dpu_kms->hw_intr,
				irq_idx);
		if (ret)
			DPU_ERROR("Fail to disable IRQ for irq_idx:%d\n",
					irq_idx);
		DPU_DEBUG("irq_idx=%d ret=%d\n", irq_idx, ret);
	}

	return ret;
}

int dpu_core_irq_disable(struct dpu_kms *dpu_kms, int *irq_idxs, u32 irq_count)
{
	int i, ret = 0, counts;

	if (!dpu_kms || !irq_idxs || !irq_count) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	counts = atomic_read(&dpu_kms->irq_obj.enable_counts[irq_idxs[0]]);
	if (counts == 2)
		DRM_ERROR("irq_idx=%d enable_count=%d\n", irq_idxs[0], counts);

	for (i = 0; (i < irq_count) && !ret; i++)
		ret = _dpu_core_irq_disable(dpu_kms, irq_idxs[i]);

	return ret;
}

u32 dpu_core_irq_read(struct dpu_kms *dpu_kms, int irq_idx, bool clear)
{
	if (!dpu_kms || !dpu_kms->hw_intr ||
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

	if (!dpu_kms || !dpu_kms->irq_obj.irq_cb_tbl) {
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

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->irq_idx_tbl_size) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	DPU_DEBUG("[%pS] irq_idx=%d\n", __builtin_return_address(0), irq_idx);

	spin_lock_irqsave(&dpu_kms->irq_obj.cb_lock, irq_flags);
	trace_dpu_core_irq_register_callback(irq_idx, register_irq_cb);
	list_del_init(&register_irq_cb->list);
	list_add_tail(&register_irq_cb->list,
			&dpu_kms->irq_obj.irq_cb_tbl[irq_idx]);
	spin_unlock_irqrestore(&dpu_kms->irq_obj.cb_lock, irq_flags);

	return 0;
}

int dpu_core_irq_unregister_callback(struct dpu_kms *dpu_kms, int irq_idx,
		struct dpu_irq_callback *register_irq_cb)
{
	unsigned long irq_flags;

	if (!dpu_kms || !dpu_kms->irq_obj.irq_cb_tbl) {
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

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->irq_idx_tbl_size) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	DPU_DEBUG("[%pS] irq_idx=%d\n", __builtin_return_address(0), irq_idx);

	spin_lock_irqsave(&dpu_kms->irq_obj.cb_lock, irq_flags);
	trace_dpu_core_irq_unregister_callback(irq_idx, register_irq_cb);
	list_del_init(&register_irq_cb->list);
	/* empty callback list but interrupt is still enabled */
	if (list_empty(&dpu_kms->irq_obj.irq_cb_tbl[irq_idx]) &&
			atomic_read(&dpu_kms->irq_obj.enable_counts[irq_idx]))
		DPU_ERROR("irq_idx=%d enabled with no callback\n", irq_idx);
	spin_unlock_irqrestore(&dpu_kms->irq_obj.cb_lock, irq_flags);

	return 0;
}

static void dpu_clear_all_irqs(struct dpu_kms *dpu_kms)
{
	if (!dpu_kms || !dpu_kms->hw_intr ||
			!dpu_kms->hw_intr->ops.clear_all_irqs)
		return;

	dpu_kms->hw_intr->ops.clear_all_irqs(dpu_kms->hw_intr);
}

static void dpu_disable_all_irqs(struct dpu_kms *dpu_kms)
{
	if (!dpu_kms || !dpu_kms->hw_intr ||
			!dpu_kms->hw_intr->ops.disable_all_irqs)
		return;

	dpu_kms->hw_intr->ops.disable_all_irqs(dpu_kms->hw_intr);
}

#ifdef CONFIG_DEBUG_FS
#define DEFINE_DPU_DEBUGFS_SEQ_FOPS(__prefix)				\
static int __prefix ## _open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, __prefix ## _show, inode->i_private);	\
}									\
static const struct file_operations __prefix ## _fops = {		\
	.owner = THIS_MODULE,						\
	.open = __prefix ## _open,					\
	.release = single_release,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
}

static int dpu_debugfs_core_irq_show(struct seq_file *s, void *v)
{
	struct dpu_irq *irq_obj = s->private;
	struct dpu_irq_callback *cb;
	unsigned long irq_flags;
	int i, irq_count, enable_count, cb_count;

	if (WARN_ON(!irq_obj->enable_counts || !irq_obj->irq_cb_tbl))
		return 0;

	for (i = 0; i < irq_obj->total_irqs; i++) {
		spin_lock_irqsave(&irq_obj->cb_lock, irq_flags);
		cb_count = 0;
		irq_count = atomic_read(&irq_obj->irq_counts[i]);
		enable_count = atomic_read(&irq_obj->enable_counts[i]);
		list_for_each_entry(cb, &irq_obj->irq_cb_tbl[i], list)
			cb_count++;
		spin_unlock_irqrestore(&irq_obj->cb_lock, irq_flags);

		if (irq_count || enable_count || cb_count)
			seq_printf(s, "idx:%d irq:%d enable:%d cb:%d\n",
					i, irq_count, enable_count, cb_count);
	}

	return 0;
}

DEFINE_DPU_DEBUGFS_SEQ_FOPS(dpu_debugfs_core_irq);

void dpu_debugfs_core_irq_init(struct dpu_kms *dpu_kms,
		struct dentry *parent)
{
	debugfs_create_file("core_irq", 0600, parent, &dpu_kms->irq_obj,
		&dpu_debugfs_core_irq_fops);
}
#endif

void dpu_core_irq_preinstall(struct dpu_kms *dpu_kms)
{
	struct msm_drm_private *priv;
	int i;

	if (!dpu_kms->dev) {
		DPU_ERROR("invalid drm device\n");
		return;
	} else if (!dpu_kms->dev->dev_private) {
		DPU_ERROR("invalid device private\n");
		return;
	}
	priv = dpu_kms->dev->dev_private;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	dpu_clear_all_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	spin_lock_init(&dpu_kms->irq_obj.cb_lock);

	/* Create irq callbacks for all possible irq_idx */
	dpu_kms->irq_obj.total_irqs = dpu_kms->hw_intr->irq_idx_tbl_size;
	dpu_kms->irq_obj.irq_cb_tbl = kcalloc(dpu_kms->irq_obj.total_irqs,
			sizeof(struct list_head), GFP_KERNEL);
	dpu_kms->irq_obj.enable_counts = kcalloc(dpu_kms->irq_obj.total_irqs,
			sizeof(atomic_t), GFP_KERNEL);
	dpu_kms->irq_obj.irq_counts = kcalloc(dpu_kms->irq_obj.total_irqs,
			sizeof(atomic_t), GFP_KERNEL);
	for (i = 0; i < dpu_kms->irq_obj.total_irqs; i++) {
		INIT_LIST_HEAD(&dpu_kms->irq_obj.irq_cb_tbl[i]);
		atomic_set(&dpu_kms->irq_obj.enable_counts[i], 0);
		atomic_set(&dpu_kms->irq_obj.irq_counts[i], 0);
	}
}

void dpu_core_irq_uninstall(struct dpu_kms *dpu_kms)
{
	struct msm_drm_private *priv;
	int i;

	if (!dpu_kms->dev) {
		DPU_ERROR("invalid drm device\n");
		return;
	} else if (!dpu_kms->dev->dev_private) {
		DPU_ERROR("invalid device private\n");
		return;
	}
	priv = dpu_kms->dev->dev_private;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	for (i = 0; i < dpu_kms->irq_obj.total_irqs; i++)
		if (atomic_read(&dpu_kms->irq_obj.enable_counts[i]) ||
				!list_empty(&dpu_kms->irq_obj.irq_cb_tbl[i]))
			DPU_ERROR("irq_idx=%d still enabled/registered\n", i);

	dpu_clear_all_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	kfree(dpu_kms->irq_obj.irq_cb_tbl);
	kfree(dpu_kms->irq_obj.enable_counts);
	kfree(dpu_kms->irq_obj.irq_counts);
	dpu_kms->irq_obj.irq_cb_tbl = NULL;
	dpu_kms->irq_obj.enable_counts = NULL;
	dpu_kms->irq_obj.irq_counts = NULL;
	dpu_kms->irq_obj.total_irqs = 0;
}

irqreturn_t dpu_core_irq(struct dpu_kms *dpu_kms)
{
	/*
	 * Read interrupt status from all sources. Interrupt status are
	 * stored within hw_intr.
	 * Function will also clear the interrupt status after reading.
	 * Individual interrupt status bit will only get stored if it
	 * is enabled.
	 */
	dpu_kms->hw_intr->ops.get_interrupt_statuses(dpu_kms->hw_intr);

	/*
	 * Dispatch to HW driver to handle interrupt lookup that is being
	 * fired. When matching interrupt is located, HW driver will call to
	 * dpu_core_irq_callback_handler with the irq_idx from the lookup table.
	 * dpu_core_irq_callback_handler will perform the registered function
	 * callback, and do the interrupt status clearing once the registered
	 * callback is finished.
	 */
	dpu_kms->hw_intr->ops.dispatch_irqs(
			dpu_kms->hw_intr,
			dpu_core_irq_callback_handler,
			dpu_kms);

	return IRQ_HANDLED;
}
