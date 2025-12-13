// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_execlist.h"

#include <drm/drm_managed.h>

#include "instructions/xe_mi_commands.h"
#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_lrc_layout.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_hw_fence.h"
#include "xe_irq.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_mocs.h"
#include "xe_ring_ops_types.h"
#include "xe_sched_job.h"

#define XE_EXECLIST_HANG_LIMIT 1

#define SW_CTX_ID_SHIFT 37
#define SW_CTX_ID_WIDTH 11
#define XEHP_SW_CTX_ID_SHIFT  39
#define XEHP_SW_CTX_ID_WIDTH  16

#define SW_CTX_ID \
	GENMASK_ULL(SW_CTX_ID_WIDTH + SW_CTX_ID_SHIFT - 1, \
		    SW_CTX_ID_SHIFT)

#define XEHP_SW_CTX_ID \
	GENMASK_ULL(XEHP_SW_CTX_ID_WIDTH + XEHP_SW_CTX_ID_SHIFT - 1, \
		    XEHP_SW_CTX_ID_SHIFT)


static void __start_lrc(struct xe_hw_engine *hwe, struct xe_lrc *lrc,
			u32 ctx_id)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_mmio *mmio = &gt->mmio;
	struct xe_device *xe = gt_to_xe(gt);
	u64 lrc_desc;
	u32 ring_mode = _MASKED_BIT_ENABLE(GFX_DISABLE_LEGACY_MODE);

	lrc_desc = xe_lrc_descriptor(lrc);

	if (GRAPHICS_VERx100(xe) >= 1250) {
		xe_gt_assert(hwe->gt, FIELD_FIT(XEHP_SW_CTX_ID, ctx_id));
		lrc_desc |= FIELD_PREP(XEHP_SW_CTX_ID, ctx_id);
	} else {
		xe_gt_assert(hwe->gt, FIELD_FIT(SW_CTX_ID, ctx_id));
		lrc_desc |= FIELD_PREP(SW_CTX_ID, ctx_id);
	}

	if (hwe->class == XE_ENGINE_CLASS_COMPUTE)
		xe_mmio_write32(mmio, RCU_MODE,
				_MASKED_BIT_ENABLE(RCU_MODE_CCS_ENABLE));

	xe_lrc_write_ctx_reg(lrc, CTX_RING_TAIL, lrc->ring.tail);
	lrc->ring.old_tail = lrc->ring.tail;

	/*
	 * Make sure the context image is complete before we submit it to HW.
	 *
	 * Ostensibly, writes (including the WCB) should be flushed prior to
	 * an uncached write such as our mmio register access, the empirical
	 * evidence (esp. on Braswell) suggests that the WC write into memory
	 * may not be visible to the HW prior to the completion of the UC
	 * register write and that we may begin execution from the context
	 * before its image is complete leading to invalid PD chasing.
	 */
	wmb();

	xe_mmio_write32(mmio, RING_HWS_PGA(hwe->mmio_base),
			xe_bo_ggtt_addr(hwe->hwsp));
	xe_mmio_read32(mmio, RING_HWS_PGA(hwe->mmio_base));

	if (xe_device_has_msix(gt_to_xe(hwe->gt)))
		ring_mode |= _MASKED_BIT_ENABLE(GFX_MSIX_INTERRUPT_ENABLE);
	xe_mmio_write32(mmio, RING_MODE(hwe->mmio_base), ring_mode);

	xe_mmio_write32(mmio, RING_EXECLIST_SQ_CONTENTS_LO(hwe->mmio_base),
			lower_32_bits(lrc_desc));
	xe_mmio_write32(mmio, RING_EXECLIST_SQ_CONTENTS_HI(hwe->mmio_base),
			upper_32_bits(lrc_desc));
	xe_mmio_write32(mmio, RING_EXECLIST_CONTROL(hwe->mmio_base),
			EL_CTRL_LOAD);
}

static void __xe_execlist_port_start(struct xe_execlist_port *port,
				     struct xe_execlist_exec_queue *exl)
{
	struct xe_device *xe = gt_to_xe(port->hwe->gt);
	int max_ctx = FIELD_MAX(SW_CTX_ID);

	if (GRAPHICS_VERx100(xe) >= 1250)
		max_ctx = FIELD_MAX(XEHP_SW_CTX_ID);

	xe_execlist_port_assert_held(port);

	if (port->running_exl != exl || !exl->has_run) {
		port->last_ctx_id++;

		/* 0 is reserved for the kernel context */
		if (port->last_ctx_id > max_ctx)
			port->last_ctx_id = 1;
	}

	__start_lrc(port->hwe, exl->q->lrc[0], port->last_ctx_id);
	port->running_exl = exl;
	exl->has_run = true;
}

static void __xe_execlist_port_idle(struct xe_execlist_port *port)
{
	u32 noop[2] = { MI_NOOP, MI_NOOP };

	xe_execlist_port_assert_held(port);

	if (!port->running_exl)
		return;

	xe_lrc_write_ring(port->lrc, noop, sizeof(noop));
	__start_lrc(port->hwe, port->lrc, 0);
	port->running_exl = NULL;
}

static bool xe_execlist_is_idle(struct xe_execlist_exec_queue *exl)
{
	struct xe_lrc *lrc = exl->q->lrc[0];

	return lrc->ring.tail == lrc->ring.old_tail;
}

static void __xe_execlist_port_start_next_active(struct xe_execlist_port *port)
{
	struct xe_execlist_exec_queue *exl = NULL;
	int i;

	xe_execlist_port_assert_held(port);

	for (i = ARRAY_SIZE(port->active) - 1; i >= 0; i--) {
		while (!list_empty(&port->active[i])) {
			exl = list_first_entry(&port->active[i],
					       struct xe_execlist_exec_queue,
					       active_link);
			list_del(&exl->active_link);

			if (xe_execlist_is_idle(exl)) {
				exl->active_priority = XE_EXEC_QUEUE_PRIORITY_UNSET;
				continue;
			}

			list_add_tail(&exl->active_link, &port->active[i]);
			__xe_execlist_port_start(port, exl);
			return;
		}
	}

	__xe_execlist_port_idle(port);
}

static u64 read_execlist_status(struct xe_hw_engine *hwe)
{
	struct xe_gt *gt = hwe->gt;
	u32 hi, lo;

	lo = xe_mmio_read32(&gt->mmio, RING_EXECLIST_STATUS_LO(hwe->mmio_base));
	hi = xe_mmio_read32(&gt->mmio, RING_EXECLIST_STATUS_HI(hwe->mmio_base));

	return lo | (u64)hi << 32;
}

static void xe_execlist_port_irq_handler_locked(struct xe_execlist_port *port)
{
	u64 status;

	xe_execlist_port_assert_held(port);

	status = read_execlist_status(port->hwe);
	if (status & BIT(7))
		return;

	__xe_execlist_port_start_next_active(port);
}

static void xe_execlist_port_irq_handler(struct xe_hw_engine *hwe,
					 u16 intr_vec)
{
	struct xe_execlist_port *port = hwe->exl_port;

	spin_lock(&port->lock);
	xe_execlist_port_irq_handler_locked(port);
	spin_unlock(&port->lock);
}

static void xe_execlist_port_wake_locked(struct xe_execlist_port *port,
					 enum xe_exec_queue_priority priority)
{
	xe_execlist_port_assert_held(port);

	if (port->running_exl && port->running_exl->active_priority >= priority)
		return;

	__xe_execlist_port_start_next_active(port);
}

static void xe_execlist_make_active(struct xe_execlist_exec_queue *exl)
{
	struct xe_execlist_port *port = exl->port;
	enum xe_exec_queue_priority priority = exl->q->sched_props.priority;

	XE_WARN_ON(priority == XE_EXEC_QUEUE_PRIORITY_UNSET);
	XE_WARN_ON(priority < 0);
	XE_WARN_ON(priority >= ARRAY_SIZE(exl->port->active));

	spin_lock_irq(&port->lock);

	if (exl->active_priority != priority &&
	    exl->active_priority != XE_EXEC_QUEUE_PRIORITY_UNSET) {
		/* Priority changed, move it to the right list */
		list_del(&exl->active_link);
		exl->active_priority = XE_EXEC_QUEUE_PRIORITY_UNSET;
	}

	if (exl->active_priority == XE_EXEC_QUEUE_PRIORITY_UNSET) {
		exl->active_priority = priority;
		list_add_tail(&exl->active_link, &port->active[priority]);
	}

	xe_execlist_port_wake_locked(exl->port, priority);

	spin_unlock_irq(&port->lock);
}

static void xe_execlist_port_irq_fail_timer(struct timer_list *timer)
{
	struct xe_execlist_port *port =
		container_of(timer, struct xe_execlist_port, irq_fail);

	spin_lock_irq(&port->lock);
	xe_execlist_port_irq_handler_locked(port);
	spin_unlock_irq(&port->lock);

	port->irq_fail.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&port->irq_fail);
}

struct xe_execlist_port *xe_execlist_port_create(struct xe_device *xe,
						 struct xe_hw_engine *hwe)
{
	struct drm_device *drm = &xe->drm;
	struct xe_execlist_port *port;
	int i, err;

	port = drmm_kzalloc(drm, sizeof(*port), GFP_KERNEL);
	if (!port) {
		err = -ENOMEM;
		goto err;
	}

	port->hwe = hwe;

	port->lrc = xe_lrc_create(hwe, NULL, SZ_16K, XE_IRQ_DEFAULT_MSIX, 0);
	if (IS_ERR(port->lrc)) {
		err = PTR_ERR(port->lrc);
		goto err;
	}

	spin_lock_init(&port->lock);
	for (i = 0; i < ARRAY_SIZE(port->active); i++)
		INIT_LIST_HEAD(&port->active[i]);

	port->last_ctx_id = 1;
	port->running_exl = NULL;

	hwe->irq_handler = xe_execlist_port_irq_handler;

	/* TODO: Fix the interrupt code so it doesn't race like mad */
	timer_setup(&port->irq_fail, xe_execlist_port_irq_fail_timer, 0);
	port->irq_fail.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&port->irq_fail);

	return port;

err:
	return ERR_PTR(err);
}

void xe_execlist_port_destroy(struct xe_execlist_port *port)
{
	timer_delete(&port->irq_fail);

	/* Prevent an interrupt while we're destroying */
	spin_lock_irq(&gt_to_xe(port->hwe->gt)->irq.lock);
	port->hwe->irq_handler = NULL;
	spin_unlock_irq(&gt_to_xe(port->hwe->gt)->irq.lock);

	xe_lrc_put(port->lrc);
}

static struct dma_fence *
execlist_run_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);
	struct xe_exec_queue *q = job->q;
	struct xe_execlist_exec_queue *exl = job->q->execlist;

	q->ring_ops->emit_job(job);
	xe_execlist_make_active(exl);

	return job->fence;
}

static void execlist_job_free(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);

	xe_exec_queue_update_run_ticks(job->q);
	xe_sched_job_put(job);
}

static const struct drm_sched_backend_ops drm_sched_ops = {
	.run_job = execlist_run_job,
	.free_job = execlist_job_free,
};

static int execlist_exec_queue_init(struct xe_exec_queue *q)
{
	struct drm_gpu_scheduler *sched;
	const struct drm_sched_init_args args = {
		.ops = &drm_sched_ops,
		.num_rqs = 1,
		.credit_limit = q->lrc[0]->ring.size / MAX_JOB_SIZE_BYTES,
		.hang_limit = XE_SCHED_HANG_LIMIT,
		.timeout = XE_SCHED_JOB_TIMEOUT,
		.name = q->hwe->name,
		.dev = gt_to_xe(q->gt)->drm.dev,
	};
	struct xe_execlist_exec_queue *exl;
	struct xe_device *xe = gt_to_xe(q->gt);
	int err;

	xe_assert(xe, !xe_device_uc_enabled(xe));

	drm_info(&xe->drm, "Enabling execlist submission (GuC submission disabled)\n");

	exl = kzalloc(sizeof(*exl), GFP_KERNEL);
	if (!exl)
		return -ENOMEM;

	exl->q = q;

	err = drm_sched_init(&exl->sched, &args);
	if (err)
		goto err_free;

	sched = &exl->sched;
	err = drm_sched_entity_init(&exl->entity, 0, &sched, 1, NULL);
	if (err)
		goto err_sched;

	exl->port = q->hwe->exl_port;
	exl->has_run = false;
	exl->active_priority = XE_EXEC_QUEUE_PRIORITY_UNSET;
	q->execlist = exl;
	q->entity = &exl->entity;

	xe_exec_queue_assign_name(q, ffs(q->logical_mask) - 1);

	return 0;

err_sched:
	drm_sched_fini(&exl->sched);
err_free:
	kfree(exl);
	return err;
}

static void execlist_exec_queue_fini(struct xe_exec_queue *q)
{
	struct xe_execlist_exec_queue *exl = q->execlist;

	drm_sched_entity_fini(&exl->entity);
	drm_sched_fini(&exl->sched);

	kfree(exl);
}

static void execlist_exec_queue_destroy_async(struct work_struct *w)
{
	struct xe_execlist_exec_queue *ee =
		container_of(w, struct xe_execlist_exec_queue, destroy_async);
	struct xe_exec_queue *q = ee->q;
	struct xe_execlist_exec_queue *exl = q->execlist;
	struct xe_device *xe = gt_to_xe(q->gt);
	unsigned long flags;

	xe_assert(xe, !xe_device_uc_enabled(xe));

	spin_lock_irqsave(&exl->port->lock, flags);
	if (WARN_ON(exl->active_priority != XE_EXEC_QUEUE_PRIORITY_UNSET))
		list_del(&exl->active_link);
	spin_unlock_irqrestore(&exl->port->lock, flags);

	xe_exec_queue_fini(q);
}

static void execlist_exec_queue_kill(struct xe_exec_queue *q)
{
	/* NIY */
}

static void execlist_exec_queue_destroy(struct xe_exec_queue *q)
{
	INIT_WORK(&q->execlist->destroy_async, execlist_exec_queue_destroy_async);
	queue_work(system_unbound_wq, &q->execlist->destroy_async);
}

static int execlist_exec_queue_set_priority(struct xe_exec_queue *q,
					    enum xe_exec_queue_priority priority)
{
	/* NIY */
	return 0;
}

static int execlist_exec_queue_set_timeslice(struct xe_exec_queue *q, u32 timeslice_us)
{
	/* NIY */
	return 0;
}

static int execlist_exec_queue_set_preempt_timeout(struct xe_exec_queue *q,
						   u32 preempt_timeout_us)
{
	/* NIY */
	return 0;
}

static int execlist_exec_queue_suspend(struct xe_exec_queue *q)
{
	/* NIY */
	return 0;
}

static int execlist_exec_queue_suspend_wait(struct xe_exec_queue *q)

{
	/* NIY */
	return 0;
}

static void execlist_exec_queue_resume(struct xe_exec_queue *q)
{
	/* NIY */
}

static bool execlist_exec_queue_reset_status(struct xe_exec_queue *q)
{
	/* NIY */
	return false;
}

static const struct xe_exec_queue_ops execlist_exec_queue_ops = {
	.init = execlist_exec_queue_init,
	.kill = execlist_exec_queue_kill,
	.fini = execlist_exec_queue_fini,
	.destroy = execlist_exec_queue_destroy,
	.set_priority = execlist_exec_queue_set_priority,
	.set_timeslice = execlist_exec_queue_set_timeslice,
	.set_preempt_timeout = execlist_exec_queue_set_preempt_timeout,
	.suspend = execlist_exec_queue_suspend,
	.suspend_wait = execlist_exec_queue_suspend_wait,
	.resume = execlist_exec_queue_resume,
	.reset_status = execlist_exec_queue_reset_status,
};

int xe_execlist_init(struct xe_gt *gt)
{
	/* GuC submission enabled, nothing to do */
	if (xe_device_uc_enabled(gt_to_xe(gt)))
		return 0;

	gt->exec_queue_ops = &execlist_exec_queue_ops;

	return 0;
}
