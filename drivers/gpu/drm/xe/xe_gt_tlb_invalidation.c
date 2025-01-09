// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gt_tlb_invalidation.h"

#include "abi/guc_actions_abi.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_gt_stats.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_trace.h"
#include "regs/xe_guc_regs.h"

#define FENCE_STACK_BIT		DMA_FENCE_FLAG_USER_BITS

/*
 * TLB inval depends on pending commands in the CT queue and then the real
 * invalidation time. Double up the time to process full CT queue
 * just to be on the safe side.
 */
static long tlb_timeout_jiffies(struct xe_gt *gt)
{
	/* this reflects what HW/GuC needs to process TLB inv request */
	const long hw_tlb_timeout = HZ / 4;

	/* this estimates actual delay caused by the CTB transport */
	long delay = xe_guc_ct_queue_proc_time_jiffies(&gt->uc.guc.ct);

	return hw_tlb_timeout + 2 * delay;
}

static void xe_gt_tlb_invalidation_fence_fini(struct xe_gt_tlb_invalidation_fence *fence)
{
	if (WARN_ON_ONCE(!fence->gt))
		return;

	xe_pm_runtime_put(gt_to_xe(fence->gt));
	fence->gt = NULL; /* fini() should be called once */
}

static void
__invalidation_fence_signal(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence)
{
	bool stack = test_bit(FENCE_STACK_BIT, &fence->base.flags);

	trace_xe_gt_tlb_invalidation_fence_signal(xe, fence);
	xe_gt_tlb_invalidation_fence_fini(fence);
	dma_fence_signal(&fence->base);
	if (!stack)
		dma_fence_put(&fence->base);
}

static void
invalidation_fence_signal(struct xe_device *xe, struct xe_gt_tlb_invalidation_fence *fence)
{
	list_del(&fence->link);
	__invalidation_fence_signal(xe, fence);
}

void xe_gt_tlb_invalidation_fence_signal(struct xe_gt_tlb_invalidation_fence *fence)
{
	if (WARN_ON_ONCE(!fence->gt))
		return;

	__invalidation_fence_signal(gt_to_xe(fence->gt), fence);
}

static void xe_gt_tlb_fence_timeout(struct work_struct *work)
{
	struct xe_gt *gt = container_of(work, struct xe_gt,
					tlb_invalidation.fence_tdr.work);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_gt_tlb_invalidation_fence *fence, *next;

	LNL_FLUSH_WORK(&gt->uc.guc.ct.g2h_worker);

	spin_lock_irq(&gt->tlb_invalidation.pending_lock);
	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link) {
		s64 since_inval_ms = ktime_ms_delta(ktime_get(),
						    fence->invalidation_time);

		if (msecs_to_jiffies(since_inval_ms) < tlb_timeout_jiffies(gt))
			break;

		trace_xe_gt_tlb_invalidation_fence_timeout(xe, fence);
		xe_gt_err(gt, "TLB invalidation fence timeout, seqno=%d recv=%d",
			  fence->seqno, gt->tlb_invalidation.seqno_recv);

		fence->base.error = -ETIME;
		invalidation_fence_signal(xe, fence);
	}
	if (!list_empty(&gt->tlb_invalidation.pending_fences))
		queue_delayed_work(system_wq,
				   &gt->tlb_invalidation.fence_tdr,
				   tlb_timeout_jiffies(gt));
	spin_unlock_irq(&gt->tlb_invalidation.pending_lock);
}

/**
 * xe_gt_tlb_invalidation_init_early - Initialize GT TLB invalidation state
 * @gt: GT structure
 *
 * Initialize GT TLB invalidation state, purely software initialization, should
 * be called once during driver load.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_gt_tlb_invalidation_init_early(struct xe_gt *gt)
{
	gt->tlb_invalidation.seqno = 1;
	INIT_LIST_HEAD(&gt->tlb_invalidation.pending_fences);
	spin_lock_init(&gt->tlb_invalidation.pending_lock);
	spin_lock_init(&gt->tlb_invalidation.lock);
	INIT_DELAYED_WORK(&gt->tlb_invalidation.fence_tdr,
			  xe_gt_tlb_fence_timeout);

	return 0;
}

/**
 * xe_gt_tlb_invalidation_reset - Initialize GT TLB invalidation reset
 * @gt: GT structure
 *
 * Signal any pending invalidation fences, should be called during a GT reset
 */
void xe_gt_tlb_invalidation_reset(struct xe_gt *gt)
{
	struct xe_gt_tlb_invalidation_fence *fence, *next;
	int pending_seqno;

	/*
	 * CT channel is already disabled at this point. No new TLB requests can
	 * appear.
	 */

	mutex_lock(&gt->uc.guc.ct.lock);
	spin_lock_irq(&gt->tlb_invalidation.pending_lock);
	cancel_delayed_work(&gt->tlb_invalidation.fence_tdr);
	/*
	 * We might have various kworkers waiting for TLB flushes to complete
	 * which are not tracked with an explicit TLB fence, however at this
	 * stage that will never happen since the CT is already disabled, so
	 * make sure we signal them here under the assumption that we have
	 * completed a full GT reset.
	 */
	if (gt->tlb_invalidation.seqno == 1)
		pending_seqno = TLB_INVALIDATION_SEQNO_MAX - 1;
	else
		pending_seqno = gt->tlb_invalidation.seqno - 1;
	WRITE_ONCE(gt->tlb_invalidation.seqno_recv, pending_seqno);

	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link)
		invalidation_fence_signal(gt_to_xe(gt), fence);
	spin_unlock_irq(&gt->tlb_invalidation.pending_lock);
	mutex_unlock(&gt->uc.guc.ct.lock);
}

static bool tlb_invalidation_seqno_past(struct xe_gt *gt, int seqno)
{
	int seqno_recv = READ_ONCE(gt->tlb_invalidation.seqno_recv);

	if (seqno - seqno_recv < -(TLB_INVALIDATION_SEQNO_MAX / 2))
		return false;

	if (seqno - seqno_recv > (TLB_INVALIDATION_SEQNO_MAX / 2))
		return true;

	return seqno_recv >= seqno;
}

static int send_tlb_invalidation(struct xe_guc *guc,
				 struct xe_gt_tlb_invalidation_fence *fence,
				 u32 *action, int len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_device *xe = gt_to_xe(gt);
	int seqno;
	int ret;

	xe_gt_assert(gt, fence);

	/*
	 * XXX: The seqno algorithm relies on TLB invalidation being processed
	 * in order which they currently are, if that changes the algorithm will
	 * need to be updated.
	 */

	mutex_lock(&guc->ct.lock);
	seqno = gt->tlb_invalidation.seqno;
	fence->seqno = seqno;
	trace_xe_gt_tlb_invalidation_fence_send(xe, fence);
	action[1] = seqno;
	ret = xe_guc_ct_send_locked(&guc->ct, action, len,
				    G2H_LEN_DW_TLB_INVALIDATE, 1);
	if (!ret) {
		spin_lock_irq(&gt->tlb_invalidation.pending_lock);
		/*
		 * We haven't actually published the TLB fence as per
		 * pending_fences, but in theory our seqno could have already
		 * been written as we acquired the pending_lock. In such a case
		 * we can just go ahead and signal the fence here.
		 */
		if (tlb_invalidation_seqno_past(gt, seqno)) {
			__invalidation_fence_signal(xe, fence);
		} else {
			fence->invalidation_time = ktime_get();
			list_add_tail(&fence->link,
				      &gt->tlb_invalidation.pending_fences);

			if (list_is_singular(&gt->tlb_invalidation.pending_fences))
				queue_delayed_work(system_wq,
						   &gt->tlb_invalidation.fence_tdr,
						   tlb_timeout_jiffies(gt));
		}
		spin_unlock_irq(&gt->tlb_invalidation.pending_lock);
	} else {
		__invalidation_fence_signal(xe, fence);
	}
	if (!ret) {
		gt->tlb_invalidation.seqno = (gt->tlb_invalidation.seqno + 1) %
			TLB_INVALIDATION_SEQNO_MAX;
		if (!gt->tlb_invalidation.seqno)
			gt->tlb_invalidation.seqno = 1;
	}
	mutex_unlock(&guc->ct.lock);
	xe_gt_stats_incr(gt, XE_GT_STATS_ID_TLB_INVAL, 1);

	return ret;
}

#define MAKE_INVAL_OP(type)	((type << XE_GUC_TLB_INVAL_TYPE_SHIFT) | \
		XE_GUC_TLB_INVAL_MODE_HEAVY << XE_GUC_TLB_INVAL_MODE_SHIFT | \
		XE_GUC_TLB_INVAL_FLUSH_CACHE)

/**
 * xe_gt_tlb_invalidation_guc - Issue a TLB invalidation on this GT for the GuC
 * @gt: GT structure
 * @fence: invalidation fence which will be signal on TLB invalidation
 * completion
 *
 * Issue a TLB invalidation for the GuC. Completion of TLB is asynchronous and
 * caller can use the invalidation fence to wait for completion.
 *
 * Return: 0 on success, negative error code on error
 */
static int xe_gt_tlb_invalidation_guc(struct xe_gt *gt,
				      struct xe_gt_tlb_invalidation_fence *fence)
{
	u32 action[] = {
		XE_GUC_ACTION_TLB_INVALIDATION,
		0,  /* seqno, replaced in send_tlb_invalidation */
		MAKE_INVAL_OP(XE_GUC_TLB_INVAL_GUC),
	};
	int ret;

	ret = send_tlb_invalidation(&gt->uc.guc, fence, action,
				    ARRAY_SIZE(action));
	/*
	 * -ECANCELED indicates the CT is stopped for a GT reset. TLB caches
	 *  should be nuked on a GT reset so this error can be ignored.
	 */
	if (ret == -ECANCELED)
		return 0;

	return ret;
}

/**
 * xe_gt_tlb_invalidation_ggtt - Issue a TLB invalidation on this GT for the GGTT
 * @gt: GT structure
 *
 * Issue a TLB invalidation for the GGTT. Completion of TLB invalidation is
 * synchronous.
 *
 * Return: 0 on success, negative error code on error
 */
int xe_gt_tlb_invalidation_ggtt(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;

	if (xe_guc_ct_enabled(&gt->uc.guc.ct) &&
	    gt->uc.guc.submission_state.enabled) {
		struct xe_gt_tlb_invalidation_fence fence;
		int ret;

		xe_gt_tlb_invalidation_fence_init(gt, &fence, true);
		ret = xe_gt_tlb_invalidation_guc(gt, &fence);
		if (ret)
			return ret;

		xe_gt_tlb_invalidation_fence_wait(&fence);
	} else if (xe_device_uc_enabled(xe) && !xe_device_wedged(xe)) {
		struct xe_mmio *mmio = &gt->mmio;

		if (IS_SRIOV_VF(xe))
			return 0;

		fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
		if (xe->info.platform == XE_PVC || GRAPHICS_VER(xe) >= 20) {
			xe_mmio_write32(mmio, PVC_GUC_TLB_INV_DESC1,
					PVC_GUC_TLB_INV_DESC1_INVALIDATE);
			xe_mmio_write32(mmio, PVC_GUC_TLB_INV_DESC0,
					PVC_GUC_TLB_INV_DESC0_VALID);
		} else {
			xe_mmio_write32(mmio, GUC_TLB_INV_CR,
					GUC_TLB_INV_CR_INVALIDATE);
		}
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
	}

	return 0;
}

/**
 * xe_gt_tlb_invalidation_range - Issue a TLB invalidation on this GT for an
 * address range
 *
 * @gt: GT structure
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
int xe_gt_tlb_invalidation_range(struct xe_gt *gt,
				 struct xe_gt_tlb_invalidation_fence *fence,
				 u64 start, u64 end, u32 asid)
{
	struct xe_device *xe = gt_to_xe(gt);
#define MAX_TLB_INVALIDATION_LEN	7
	u32 action[MAX_TLB_INVALIDATION_LEN];
	int len = 0;

	xe_gt_assert(gt, fence);

	/* Execlists not supported */
	if (gt_to_xe(gt)->info.force_execlist) {
		__invalidation_fence_signal(xe, fence);
		return 0;
	}

	action[len++] = XE_GUC_ACTION_TLB_INVALIDATION;
	action[len++] = 0; /* seqno, replaced in send_tlb_invalidation */
	if (!xe->info.has_range_tlb_invalidation) {
		action[len++] = MAKE_INVAL_OP(XE_GUC_TLB_INVAL_FULL);
	} else {
		u64 orig_start = start;
		u64 length = end - start;
		u64 align;

		if (length < SZ_4K)
			length = SZ_4K;

		/*
		 * We need to invalidate a higher granularity if start address
		 * is not aligned to length. When start is not aligned with
		 * length we need to find the length large enough to create an
		 * address mask covering the required range.
		 */
		align = roundup_pow_of_two(length);
		start = ALIGN_DOWN(start, align);
		end = ALIGN(end, align);
		length = align;
		while (start + length < end) {
			length <<= 1;
			start = ALIGN_DOWN(orig_start, length);
		}

		/*
		 * Minimum invalidation size for a 2MB page that the hardware
		 * expects is 16MB
		 */
		if (length >= SZ_2M) {
			length = max_t(u64, SZ_16M, length);
			start = ALIGN_DOWN(orig_start, length);
		}

		xe_gt_assert(gt, length >= SZ_4K);
		xe_gt_assert(gt, is_power_of_2(length));
		xe_gt_assert(gt, !(length & GENMASK(ilog2(SZ_16M) - 1,
						    ilog2(SZ_2M) + 1)));
		xe_gt_assert(gt, IS_ALIGNED(start, length));

		action[len++] = MAKE_INVAL_OP(XE_GUC_TLB_INVAL_PAGE_SELECTIVE);
		action[len++] = asid;
		action[len++] = lower_32_bits(start);
		action[len++] = upper_32_bits(start);
		action[len++] = ilog2(length) - ilog2(SZ_4K);
	}

	xe_gt_assert(gt, len <= MAX_TLB_INVALIDATION_LEN);

	return send_tlb_invalidation(&gt->uc.guc, fence, action, len);
}

/**
 * xe_gt_tlb_invalidation_vma - Issue a TLB invalidation on this GT for a VMA
 * @gt: GT structure
 * @fence: invalidation fence which will be signal on TLB invalidation
 * completion, can be NULL
 * @vma: VMA to invalidate
 *
 * Issue a range based TLB invalidation if supported, if not fallback to a full
 * TLB invalidation. Completion of TLB is asynchronous and caller can use
 * the invalidation fence to wait for completion.
 *
 * Return: Negative error code on error, 0 on success
 */
int xe_gt_tlb_invalidation_vma(struct xe_gt *gt,
			       struct xe_gt_tlb_invalidation_fence *fence,
			       struct xe_vma *vma)
{
	xe_gt_assert(gt, vma);

	return xe_gt_tlb_invalidation_range(gt, fence, xe_vma_start(vma),
					    xe_vma_end(vma),
					    xe_vma_vm(vma)->usm.asid);
}

/**
 * xe_guc_tlb_invalidation_done_handler - TLB invalidation done handler
 * @guc: guc
 * @msg: message indicating TLB invalidation done
 * @len: length of message
 *
 * Parse seqno of TLB invalidation, wake any waiters for seqno, and signal any
 * invalidation fences for seqno. Algorithm for this depends on seqno being
 * received in-order and asserts this assumption.
 *
 * Return: 0 on success, -EPROTO for malformed messages.
 */
int xe_guc_tlb_invalidation_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_gt_tlb_invalidation_fence *fence, *next;
	unsigned long flags;

	if (unlikely(len != 1))
		return -EPROTO;

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
	spin_lock_irqsave(&gt->tlb_invalidation.pending_lock, flags);
	if (tlb_invalidation_seqno_past(gt, msg[0])) {
		spin_unlock_irqrestore(&gt->tlb_invalidation.pending_lock, flags);
		return 0;
	}

	WRITE_ONCE(gt->tlb_invalidation.seqno_recv, msg[0]);

	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link) {
		trace_xe_gt_tlb_invalidation_fence_recv(xe, fence);

		if (!tlb_invalidation_seqno_past(gt, fence->seqno))
			break;

		invalidation_fence_signal(xe, fence);
	}

	if (!list_empty(&gt->tlb_invalidation.pending_fences))
		mod_delayed_work(system_wq,
				 &gt->tlb_invalidation.fence_tdr,
				 tlb_timeout_jiffies(gt));
	else
		cancel_delayed_work(&gt->tlb_invalidation.fence_tdr);

	spin_unlock_irqrestore(&gt->tlb_invalidation.pending_lock, flags);

	return 0;
}

static const char *
invalidation_fence_get_driver_name(struct dma_fence *dma_fence)
{
	return "xe";
}

static const char *
invalidation_fence_get_timeline_name(struct dma_fence *dma_fence)
{
	return "invalidation_fence";
}

static const struct dma_fence_ops invalidation_fence_ops = {
	.get_driver_name = invalidation_fence_get_driver_name,
	.get_timeline_name = invalidation_fence_get_timeline_name,
};

/**
 * xe_gt_tlb_invalidation_fence_init - Initialize TLB invalidation fence
 * @gt: GT
 * @fence: TLB invalidation fence to initialize
 * @stack: fence is stack variable
 *
 * Initialize TLB invalidation fence for use. xe_gt_tlb_invalidation_fence_fini
 * will be automatically called when fence is signalled (all fences must signal),
 * even on error.
 */
void xe_gt_tlb_invalidation_fence_init(struct xe_gt *gt,
				       struct xe_gt_tlb_invalidation_fence *fence,
				       bool stack)
{
	xe_pm_runtime_get_noresume(gt_to_xe(gt));

	spin_lock_irq(&gt->tlb_invalidation.lock);
	dma_fence_init(&fence->base, &invalidation_fence_ops,
		       &gt->tlb_invalidation.lock,
		       dma_fence_context_alloc(1), 1);
	spin_unlock_irq(&gt->tlb_invalidation.lock);
	INIT_LIST_HEAD(&fence->link);
	if (stack)
		set_bit(FENCE_STACK_BIT, &fence->base.flags);
	else
		dma_fence_get(&fence->base);
	fence->gt = gt;
}
