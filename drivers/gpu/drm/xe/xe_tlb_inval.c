// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_abi.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_stats.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_tlb_inval.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_tlb_inval.h"
#include "xe_trace.h"

/**
 * DOC: Xe TLB invalidation
 *
 * Xe TLB invalidation is implemented in two layers. The first is the frontend
 * API, which provides an interface for TLB invalidations to the driver code.
 * The frontend handles seqno assignment, synchronization (fences), and the
 * timeout mechanism. The frontend is implemented via an embedded structure
 * xe_tlb_inval that includes a set of ops hooking into the backend. The backend
 * interacts with the hardware (or firmware) to perform the actual invalidation.
 */

#define FENCE_STACK_BIT		DMA_FENCE_FLAG_USER_BITS

static void xe_tlb_inval_fence_fini(struct xe_tlb_inval_fence *fence)
{
	if (WARN_ON_ONCE(!fence->tlb_inval))
		return;

	xe_pm_runtime_put(fence->tlb_inval->xe);
	fence->tlb_inval = NULL; /* fini() should be called once */
}

static void
xe_tlb_inval_fence_signal(struct xe_tlb_inval_fence *fence)
{
	bool stack = test_bit(FENCE_STACK_BIT, &fence->base.flags);

	lockdep_assert_held(&fence->tlb_inval->pending_lock);

	list_del(&fence->link);
	trace_xe_tlb_inval_fence_signal(fence->tlb_inval->xe, fence);
	xe_tlb_inval_fence_fini(fence);
	dma_fence_signal(&fence->base);
	if (!stack)
		dma_fence_put(&fence->base);
}

static void
xe_tlb_inval_fence_signal_unlocked(struct xe_tlb_inval_fence *fence)
{
	struct xe_tlb_inval *tlb_inval = fence->tlb_inval;

	spin_lock_irq(&tlb_inval->pending_lock);
	xe_tlb_inval_fence_signal(fence);
	spin_unlock_irq(&tlb_inval->pending_lock);
}

static void xe_tlb_inval_fence_timeout(struct work_struct *work)
{
	struct xe_tlb_inval *tlb_inval = container_of(work, struct xe_tlb_inval,
						      fence_tdr.work);
	struct xe_device *xe = tlb_inval->xe;
	struct xe_tlb_inval_fence *fence, *next;
	long timeout_delay = tlb_inval->ops->timeout_delay(tlb_inval);

	tlb_inval->ops->flush(tlb_inval);

	spin_lock_irq(&tlb_inval->pending_lock);
	list_for_each_entry_safe(fence, next,
				 &tlb_inval->pending_fences, link) {
		s64 since_inval_ms = ktime_ms_delta(ktime_get(),
						    fence->inval_time);

		if (msecs_to_jiffies(since_inval_ms) < timeout_delay)
			break;

		trace_xe_tlb_inval_fence_timeout(xe, fence);
		drm_err(&xe->drm,
			"TLB invalidation fence timeout, seqno=%d recv=%d",
			fence->seqno, tlb_inval->seqno_recv);

		fence->base.error = -ETIME;
		xe_tlb_inval_fence_signal(fence);
	}
	if (!list_empty(&tlb_inval->pending_fences))
		queue_delayed_work(system_wq, &tlb_inval->fence_tdr,
				   timeout_delay);
	spin_unlock_irq(&tlb_inval->pending_lock);
}

/**
 * tlb_inval_fini - Clean up TLB invalidation state
 * @drm: @drm_device
 * @arg: pointer to struct @xe_tlb_inval
 *
 * Cancel pending fence workers and clean up any additional
 * TLB invalidation state.
 */
static void tlb_inval_fini(struct drm_device *drm, void *arg)
{
	struct xe_tlb_inval *tlb_inval = arg;

	xe_tlb_inval_reset(tlb_inval);
}

/**
 * xe_gt_tlb_inval_init - Initialize TLB invalidation state
 * @gt: GT structure
 *
 * Initialize TLB invalidation state, purely software initialization, should
 * be called once during driver load.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_gt_tlb_inval_init_early(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tlb_inval *tlb_inval = &gt->tlb_inval;
	int err;

	tlb_inval->xe = xe;
	tlb_inval->seqno = 1;
	INIT_LIST_HEAD(&tlb_inval->pending_fences);
	spin_lock_init(&tlb_inval->pending_lock);
	spin_lock_init(&tlb_inval->lock);
	INIT_DELAYED_WORK(&tlb_inval->fence_tdr, xe_tlb_inval_fence_timeout);

	err = drmm_mutex_init(&xe->drm, &tlb_inval->seqno_lock);
	if (err)
		return err;

	tlb_inval->job_wq = drmm_alloc_ordered_workqueue(&xe->drm,
							 "gt-tbl-inval-job-wq",
							 WQ_MEM_RECLAIM);
	if (IS_ERR(tlb_inval->job_wq))
		return PTR_ERR(tlb_inval->job_wq);

	/* XXX: Blindly setting up backend to GuC */
	xe_guc_tlb_inval_init_early(&gt->uc.guc, tlb_inval);

	return drmm_add_action_or_reset(&xe->drm, tlb_inval_fini, tlb_inval);
}

/**
 * xe_tlb_inval_reset() - TLB invalidation reset
 * @tlb_inval: TLB invalidation client
 *
 * Signal any pending invalidation fences, should be called during a GT reset
 */
void xe_tlb_inval_reset(struct xe_tlb_inval *tlb_inval)
{
	struct xe_tlb_inval_fence *fence, *next;
	int pending_seqno;

	/*
	 * we can get here before the backends are even initialized if we're
	 * wedging very early, in which case there are not going to be any
	 * pendind fences so we can bail immediately.
	 */
	if (!tlb_inval->ops->initialized(tlb_inval))
		return;

	/*
	 * Backend is already disabled at this point. No new TLB requests can
	 * appear.
	 */

	mutex_lock(&tlb_inval->seqno_lock);
	spin_lock_irq(&tlb_inval->pending_lock);
	cancel_delayed_work(&tlb_inval->fence_tdr);
	/*
	 * We might have various kworkers waiting for TLB flushes to complete
	 * which are not tracked with an explicit TLB fence, however at this
	 * stage that will never happen since the backend is already disabled,
	 * so make sure we signal them here under the assumption that we have
	 * completed a full GT reset.
	 */
	if (tlb_inval->seqno == 1)
		pending_seqno = TLB_INVALIDATION_SEQNO_MAX - 1;
	else
		pending_seqno = tlb_inval->seqno - 1;
	WRITE_ONCE(tlb_inval->seqno_recv, pending_seqno);

	list_for_each_entry_safe(fence, next,
				 &tlb_inval->pending_fences, link)
		xe_tlb_inval_fence_signal(fence);
	spin_unlock_irq(&tlb_inval->pending_lock);
	mutex_unlock(&tlb_inval->seqno_lock);
}

static bool xe_tlb_inval_seqno_past(struct xe_tlb_inval *tlb_inval, int seqno)
{
	int seqno_recv = READ_ONCE(tlb_inval->seqno_recv);

	lockdep_assert_held(&tlb_inval->pending_lock);

	if (seqno - seqno_recv < -(TLB_INVALIDATION_SEQNO_MAX / 2))
		return false;

	if (seqno - seqno_recv > (TLB_INVALIDATION_SEQNO_MAX / 2))
		return true;

	return seqno_recv >= seqno;
}

static void xe_tlb_inval_fence_prep(struct xe_tlb_inval_fence *fence)
{
	struct xe_tlb_inval *tlb_inval = fence->tlb_inval;

	fence->seqno = tlb_inval->seqno;
	trace_xe_tlb_inval_fence_send(tlb_inval->xe, fence);

	spin_lock_irq(&tlb_inval->pending_lock);
	fence->inval_time = ktime_get();
	list_add_tail(&fence->link, &tlb_inval->pending_fences);

	if (list_is_singular(&tlb_inval->pending_fences))
		queue_delayed_work(system_wq, &tlb_inval->fence_tdr,
				   tlb_inval->ops->timeout_delay(tlb_inval));
	spin_unlock_irq(&tlb_inval->pending_lock);

	tlb_inval->seqno = (tlb_inval->seqno + 1) %
		TLB_INVALIDATION_SEQNO_MAX;
	if (!tlb_inval->seqno)
		tlb_inval->seqno = 1;
}

#define xe_tlb_inval_issue(__tlb_inval, __fence, op, args...)	\
({								\
	int __ret;						\
								\
	xe_assert((__tlb_inval)->xe, (__tlb_inval)->ops);	\
	xe_assert((__tlb_inval)->xe, (__fence));		\
								\
	mutex_lock(&(__tlb_inval)->seqno_lock); 		\
	xe_tlb_inval_fence_prep((__fence));			\
	__ret = op((__tlb_inval), (__fence)->seqno, ##args);	\
	if (__ret < 0)						\
		xe_tlb_inval_fence_signal_unlocked((__fence));	\
	mutex_unlock(&(__tlb_inval)->seqno_lock);		\
								\
	__ret == -ECANCELED ? 0 : __ret;			\
})

/**
 * xe_tlb_inval_all() - Issue a TLB invalidation for all TLBs
 * @tlb_inval: TLB invalidation client
 * @fence: invalidation fence which will be signal on TLB invalidation
 * completion
 *
 * Issue a TLB invalidation for all TLBs. Completion of TLB is asynchronous and
 * caller can use the invalidation fence to wait for completion.
 *
 * Return: 0 on success, negative error code on error
 */
int xe_tlb_inval_all(struct xe_tlb_inval *tlb_inval,
		     struct xe_tlb_inval_fence *fence)
{
	return xe_tlb_inval_issue(tlb_inval, fence, tlb_inval->ops->all);
}

/**
 * xe_tlb_inval_ggtt() - Issue a TLB invalidation for the GGTT
 * @tlb_inval: TLB invalidation client
 *
 * Issue a TLB invalidation for the GGTT. Completion of TLB is asynchronous and
 * caller can use the invalidation fence to wait for completion.
 *
 * Return: 0 on success, negative error code on error
 */
int xe_tlb_inval_ggtt(struct xe_tlb_inval *tlb_inval)
{
	struct xe_tlb_inval_fence fence, *fence_ptr = &fence;
	int ret;

	xe_tlb_inval_fence_init(tlb_inval, fence_ptr, true);
	ret = xe_tlb_inval_issue(tlb_inval, fence_ptr, tlb_inval->ops->ggtt);
	xe_tlb_inval_fence_wait(fence_ptr);

	return ret;
}

/**
 * xe_tlb_inval_range() - Issue a TLB invalidation for an address range
 * @tlb_inval: TLB invalidation client
 * @fence: invalidation fence which will be signal on TLB invalidation
 * completion
 * @start: start address
 * @end: end address
 * @asid: address space id
 *
 * Issue a range based TLB invalidation if supported, if not fallback to a full
 * TLB invalidation. Completion of TLB is asynchronous and caller can use
 * the invalidation fence to wait for completion.
 *
 * Return: Negative error code on error, 0 on success
 */
int xe_tlb_inval_range(struct xe_tlb_inval *tlb_inval,
		       struct xe_tlb_inval_fence *fence, u64 start, u64 end,
		       u32 asid)
{
	return xe_tlb_inval_issue(tlb_inval, fence, tlb_inval->ops->ppgtt,
				  start, end, asid);
}

/**
 * xe_tlb_inval_vm() - Issue a TLB invalidation for a VM
 * @tlb_inval: TLB invalidation client
 * @vm: VM to invalidate
 *
 * Invalidate entire VM's address space
 */
void xe_tlb_inval_vm(struct xe_tlb_inval *tlb_inval, struct xe_vm *vm)
{
	struct xe_tlb_inval_fence fence;
	u64 range = 1ull << vm->xe->info.va_bits;

	xe_tlb_inval_fence_init(tlb_inval, &fence, true);
	xe_tlb_inval_range(tlb_inval, &fence, 0, range, vm->usm.asid);
	xe_tlb_inval_fence_wait(&fence);
}

/**
 * xe_tlb_inval_done_handler() - TLB invalidation done handler
 * @tlb_inval: TLB invalidation client
 * @seqno: seqno of invalidation that is done
 *
 * Update recv seqno, signal any TLB invalidation fences, and restart TDR
 */
void xe_tlb_inval_done_handler(struct xe_tlb_inval *tlb_inval, int seqno)
{
	struct xe_device *xe = tlb_inval->xe;
	struct xe_tlb_inval_fence *fence, *next;
	unsigned long flags;

	/*
	 * This can also be run both directly from the IRQ handler and also in
	 * process_g2h_msg(). Only one may process any individual CT message,
	 * however the order they are processed here could result in skipping a
	 * seqno. To handle that we just process all the seqnos from the last
	 * seqno_recv up to and including the one in msg[0]. The delta should be
	 * very small so there shouldn't be much of pending_fences we actually
	 * need to iterate over here.
	 *
	 * From GuC POV we expect the seqnos to always appear in-order, so if we
	 * see something later in the timeline we can be sure that anything
	 * appearing earlier has already signalled, just that we have yet to
	 * officially process the CT message like if racing against
	 * process_g2h_msg().
	 */
	spin_lock_irqsave(&tlb_inval->pending_lock, flags);
	if (xe_tlb_inval_seqno_past(tlb_inval, seqno)) {
		spin_unlock_irqrestore(&tlb_inval->pending_lock, flags);
		return;
	}

	WRITE_ONCE(tlb_inval->seqno_recv, seqno);

	list_for_each_entry_safe(fence, next,
				 &tlb_inval->pending_fences, link) {
		trace_xe_tlb_inval_fence_recv(xe, fence);

		if (!xe_tlb_inval_seqno_past(tlb_inval, fence->seqno))
			break;

		xe_tlb_inval_fence_signal(fence);
	}

	if (!list_empty(&tlb_inval->pending_fences))
		mod_delayed_work(system_wq,
				 &tlb_inval->fence_tdr,
				 tlb_inval->ops->timeout_delay(tlb_inval));
	else
		cancel_delayed_work(&tlb_inval->fence_tdr);

	spin_unlock_irqrestore(&tlb_inval->pending_lock, flags);
}

static const char *
xe_inval_fence_get_driver_name(struct dma_fence *dma_fence)
{
	return "xe";
}

static const char *
xe_inval_fence_get_timeline_name(struct dma_fence *dma_fence)
{
	return "tlb_inval_fence";
}

static const struct dma_fence_ops inval_fence_ops = {
	.get_driver_name = xe_inval_fence_get_driver_name,
	.get_timeline_name = xe_inval_fence_get_timeline_name,
};

/**
 * xe_tlb_inval_fence_init() - Initialize TLB invalidation fence
 * @tlb_inval: TLB invalidation client
 * @fence: TLB invalidation fence to initialize
 * @stack: fence is stack variable
 *
 * Initialize TLB invalidation fence for use. xe_tlb_inval_fence_fini
 * will be automatically called when fence is signalled (all fences must signal),
 * even on error.
 */
void xe_tlb_inval_fence_init(struct xe_tlb_inval *tlb_inval,
			     struct xe_tlb_inval_fence *fence,
			     bool stack)
{
	xe_pm_runtime_get_noresume(tlb_inval->xe);

	spin_lock_irq(&tlb_inval->lock);
	dma_fence_init(&fence->base, &inval_fence_ops, &tlb_inval->lock,
		       dma_fence_context_alloc(1), 1);
	spin_unlock_irq(&tlb_inval->lock);
	INIT_LIST_HEAD(&fence->link);
	if (stack)
		set_bit(FENCE_STACK_BIT, &fence->base.flags);
	else
		dma_fence_get(&fence->base);
	fence->tlb_inval = tlb_inval;
}
