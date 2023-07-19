// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_execlist.h"

#include <drm/drm_managed.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gpu_commands.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_lrc_layout.h"
#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_gt.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_mocs.h"
#include "xe_ring_ops_types.h"
#include "xe_sched_job.h"

#define XE_EXECLIST_HANG_LIMIT 1

#define GEN11_SW_CTX_ID_SHIFT 37
#define GEN11_SW_CTX_ID_WIDTH 11
#define XEHP_SW_CTX_ID_SHIFT  39
#define XEHP_SW_CTX_ID_WIDTH  16

#define GEN11_SW_CTX_ID \
	GENMASK_ULL(GEN11_SW_CTX_ID_WIDTH + GEN11_SW_CTX_ID_SHIFT - 1, \
		    GEN11_SW_CTX_ID_SHIFT)

#define XEHP_SW_CTX_ID \
	GENMASK_ULL(XEHP_SW_CTX_ID_WIDTH + XEHP_SW_CTX_ID_SHIFT - 1, \
		    XEHP_SW_CTX_ID_SHIFT)


static void __start_lrc(struct xe_hw_engine *hwe, struct xe_lrc *lrc,
			u32 ctx_id)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	u64 lrc_desc;

	lrc_desc = xe_lrc_descriptor(lrc);

	if (GRAPHICS_VERx100(xe) >= 1250) {
		XE_BUG_ON(!FIELD_FIT(XEHP_SW_CTX_ID, ctx_id));
		lrc_desc |= FIELD_PREP(XEHP_SW_CTX_ID, ctx_id);
	} else {
		XE_BUG_ON(!FIELD_FIT(GEN11_SW_CTX_ID, ctx_id));
		lrc_desc |= FIELD_PREP(GEN11_SW_CTX_ID, ctx_id);
	}

	if (hwe->class == XE_ENGINE_CLASS_COMPUTE)
		xe_mmio_write32(hwe->gt, RCU_MODE,
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

	xe_mmio_write32(gt, RING_HWS_PGA(hwe->mmio_base),
			xe_bo_ggtt_addr(hwe->hwsp));
	xe_mmio_read32(gt, RING_HWS_PGA(hwe->mmio_base));
	xe_mmio_write32(gt, RING_MODE(hwe->mmio_base),
			_MASKED_BIT_ENABLE(GFX_DISABLE_LEGACY_MODE));

	xe_mmio_write32(gt, RING_EXECLIST_SQ_CONTENTS_LO(hwe->mmio_base),
			lower_32_bits(lrc_desc));
	xe_mmio_write32(gt, RING_EXECLIST_SQ_CONTENTS_HI(hwe->mmio_base),
			upper_32_bits(lrc_desc));
	xe_mmio_write32(gt, RING_EXECLIST_CONTROL(hwe->mmio_base),
			EL_CTRL_LOAD);
}

static void __xe_execlist_port_start(struct xe_execlist_port *port,
				     struct xe_execlist_engine *exl)
{
	struct xe_device *xe = gt_to_xe(port->hwe->gt);
	int max_ctx = FIELD_MAX(GEN11_SW_CTX_ID);

	if (GRAPHICS_VERx100(xe) >= 1250)
		max_ctx = FIELD_MAX(XEHP_SW_CTX_ID);

	xe_execlist_port_assert_held(port);

	if (port->running_exl != exl || !exl->has_run) {
		port->last_ctx_id++;

		/* 0 is reserved for the kernel context */
		if (port->last_ctx_id > max_ctx)
			port->last_ctx_id = 1;
	}

	__start_lrc(port->hwe, exl->engine->lrc, port->last_ctx_id);
	port->running_exl = exl;
	exl->has_run = true;
}

static void __xe_execlist_port_idle(struct xe_execlist_port *port)
{
	u32 noop[2] = { MI_NOOP, MI_NOOP };

	xe_execlist_port_assert_held(port);

	if (!port->running_exl)
		return;

	xe_lrc_write_ring(&port->hwe->kernel_lrc, noop, sizeof(noop));
	__start_lrc(port->hwe, &port->hwe->kernel_lrc, 0);
	port->running_exl = NULL;
}

static bool xe_execlist_is_idle(struct xe_execlist_engine *exl)
{
	struct xe_lrc *lrc = exl->engine->lrc;

	return lrc->ring.tail == lrc->ring.old_tail;
}

static void __xe_execlist_port_start_next_active(struct xe_execlist_port *port)
{
	struct xe_execlist_engine *exl = NULL;
	int i;

	xe_execlist_port_assert_held(port);

	for (i = ARRAY_SIZE(port->active) - 1; i >= 0; i--) {
		while (!list_empty(&port->active[i])) {
			exl = list_first_entry(&port->active[i],
					       struct xe_execlist_engine,
					       active_link);
			list_del(&exl->active_link);

			if (xe_execlist_is_idle(exl)) {
				exl->active_priority = XE_ENGINE_PRIORITY_UNSET;
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

	lo = xe_mmio_read32(gt, RING_EXECLIST_STATUS_LO(hwe->mmio_base));
	hi = xe_mmio_read32(gt, RING_EXECLIST_STATUS_HI(hwe->mmio_base));

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
					 enum xe_engine_priority priority)
{
	xe_execlist_port_assert_held(port);

	if (port->running_exl && port->running_exl->active_priority >= priority)
		return;

	__xe_execlist_port_start_next_active(port);
}

static void xe_execlist_make_active(struct xe_execlist_engine *exl)
{
	struct xe_execlist_port *port = exl->port;
	enum xe_engine_priority priority = exl->active_priority;

	XE_BUG_ON(priority == XE_ENGINE_PRIORITY_UNSET);
	XE_BUG_ON(priority < 0);
	XE_BUG_ON(priority >= ARRAY_SIZE(exl->port->active));

	spin_lock_irq(&port->lock);

	if (exl->active_priority != priority &&
	    exl->active_priority != XE_ENGINE_PRIORITY_UNSET) {
		/* Priority changed, move it to the right list */
		list_del(&exl->active_link);
		exl->active_priority = XE_ENGINE_PRIORITY_UNSET;
	}

	if (exl->active_priority == XE_ENGINE_PRIORITY_UNSET) {
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
	int i;

	port = drmm_kzalloc(drm, sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->hwe = hwe;

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
}

void xe_execlist_port_destroy(struct xe_execlist_port *port)
{
	del_timer(&port->irq_fail);

	/* Prevent an interrupt while we're destroying */
	spin_lock_irq(&gt_to_xe(port->hwe->gt)->irq.lock);
	port->hwe->irq_handler = NULL;
	spin_unlock_irq(&gt_to_xe(port->hwe->gt)->irq.lock);
}

static struct dma_fence *
execlist_run_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);
	struct xe_engine *e = job->engine;
	struct xe_execlist_engine *exl = job->engine->execlist;

	e->ring_ops->emit_job(job);
	xe_execlist_make_active(exl);

	return dma_fence_get(job->fence);
}

static void execlist_job_free(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);

	xe_sched_job_put(job);
}

static const struct drm_sched_backend_ops drm_sched_ops = {
	.run_job = execlist_run_job,
	.free_job = execlist_job_free,
};

static int execlist_engine_init(struct xe_engine *e)
{
	struct drm_gpu_scheduler *sched;
	struct xe_execlist_engine *exl;
	int err;

	XE_BUG_ON(xe_device_guc_submission_enabled(gt_to_xe(e->gt)));

	exl = kzalloc(sizeof(*exl), GFP_KERNEL);
	if (!exl)
		return -ENOMEM;

	exl->engine = e;

	err = drm_sched_init(&exl->sched, &drm_sched_ops, NULL, 1,
			     e->lrc[0].ring.size / MAX_JOB_SIZE_BYTES,
			     XE_SCHED_HANG_LIMIT, XE_SCHED_JOB_TIMEOUT,
			     NULL, NULL, e->hwe->name,
			     gt_to_xe(e->gt)->drm.dev);
	if (err)
		goto err_free;

	sched = &exl->sched;
	err = drm_sched_entity_init(&exl->entity, 0, &sched, 1, NULL);
	if (err)
		goto err_sched;

	exl->port = e->hwe->exl_port;
	exl->has_run = false;
	exl->active_priority = XE_ENGINE_PRIORITY_UNSET;
	e->execlist = exl;
	e->entity = &exl->entity;

	switch (e->class) {
	case XE_ENGINE_CLASS_RENDER:
		sprintf(e->name, "rcs%d", ffs(e->logical_mask) - 1);
		break;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		sprintf(e->name, "vcs%d", ffs(e->logical_mask) - 1);
		break;
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		sprintf(e->name, "vecs%d", ffs(e->logical_mask) - 1);
		break;
	case XE_ENGINE_CLASS_COPY:
		sprintf(e->name, "bcs%d", ffs(e->logical_mask) - 1);
		break;
	case XE_ENGINE_CLASS_COMPUTE:
		sprintf(e->name, "ccs%d", ffs(e->logical_mask) - 1);
		break;
	default:
		XE_WARN_ON(e->class);
	}

	return 0;

err_sched:
	drm_sched_fini(&exl->sched);
err_free:
	kfree(exl);
	return err;
}

static void execlist_engine_fini_async(struct work_struct *w)
{
	struct xe_execlist_engine *ee =
		container_of(w, struct xe_execlist_engine, fini_async);
	struct xe_engine *e = ee->engine;
	struct xe_execlist_engine *exl = e->execlist;
	unsigned long flags;

	XE_BUG_ON(xe_device_guc_submission_enabled(gt_to_xe(e->gt)));

	spin_lock_irqsave(&exl->port->lock, flags);
	if (WARN_ON(exl->active_priority != XE_ENGINE_PRIORITY_UNSET))
		list_del(&exl->active_link);
	spin_unlock_irqrestore(&exl->port->lock, flags);

	if (e->flags & ENGINE_FLAG_PERSISTENT)
		xe_device_remove_persistent_engines(gt_to_xe(e->gt), e);
	drm_sched_entity_fini(&exl->entity);
	drm_sched_fini(&exl->sched);
	kfree(exl);

	xe_engine_fini(e);
}

static void execlist_engine_kill(struct xe_engine *e)
{
	/* NIY */
}

static void execlist_engine_fini(struct xe_engine *e)
{
	INIT_WORK(&e->execlist->fini_async, execlist_engine_fini_async);
	queue_work(system_unbound_wq, &e->execlist->fini_async);
}

static int execlist_engine_set_priority(struct xe_engine *e,
					enum xe_engine_priority priority)
{
	/* NIY */
	return 0;
}

static int execlist_engine_set_timeslice(struct xe_engine *e, u32 timeslice_us)
{
	/* NIY */
	return 0;
}

static int execlist_engine_set_preempt_timeout(struct xe_engine *e,
					       u32 preempt_timeout_us)
{
	/* NIY */
	return 0;
}

static int execlist_engine_set_job_timeout(struct xe_engine *e,
					   u32 job_timeout_ms)
{
	/* NIY */
	return 0;
}

static int execlist_engine_suspend(struct xe_engine *e)
{
	/* NIY */
	return 0;
}

static void execlist_engine_suspend_wait(struct xe_engine *e)

{
	/* NIY */
}

static void execlist_engine_resume(struct xe_engine *e)
{
	/* NIY */
}

static const struct xe_engine_ops execlist_engine_ops = {
	.init = execlist_engine_init,
	.kill = execlist_engine_kill,
	.fini = execlist_engine_fini,
	.set_priority = execlist_engine_set_priority,
	.set_timeslice = execlist_engine_set_timeslice,
	.set_preempt_timeout = execlist_engine_set_preempt_timeout,
	.set_job_timeout = execlist_engine_set_job_timeout,
	.suspend = execlist_engine_suspend,
	.suspend_wait = execlist_engine_suspend_wait,
	.resume = execlist_engine_resume,
};

int xe_execlist_init(struct xe_gt *gt)
{
	/* GuC submission enabled, nothing to do */
	if (xe_device_guc_submission_enabled(gt_to_xe(gt)))
		return 0;

	gt->engine_ops = &execlist_engine_ops;

	return 0;
}
