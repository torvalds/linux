// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gt_tlb_invalidation.h"

#include "abi/guc_actions_abi.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_trace.h"

#define TLB_TIMEOUT	(HZ / 4)

static void xe_gt_tlb_fence_timeout(struct work_struct *work)
{
	struct xe_gt *gt = container_of(work, struct xe_gt,
					tlb_invalidation.fence_tdr.work);
	struct xe_gt_tlb_invalidation_fence *fence, *next;

	spin_lock_irq(&gt->tlb_invalidation.pending_lock);
	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link) {
		s64 since_inval_ms = ktime_ms_delta(ktime_get(),
						    fence->invalidation_time);

		if (msecs_to_jiffies(since_inval_ms) < TLB_TIMEOUT)
			break;

		trace_xe_gt_tlb_invalidation_fence_timeout(fence);
		xe_gt_err(gt, "TLB invalidation fence timeout, seqno=%d recv=%d",
			  fence->seqno, gt->tlb_invalidation.seqno_recv);

		list_del(&fence->link);
		fence->base.error = -ETIME;
		dma_fence_signal(&fence->base);
		dma_fence_put(&fence->base);
	}
	if (!list_empty(&gt->tlb_invalidation.pending_fences))
		queue_delayed_work(system_wq,
				   &gt->tlb_invalidation.fence_tdr,
				   TLB_TIMEOUT);
	spin_unlock_irq(&gt->tlb_invalidation.pending_lock);
}

/**
 * xe_gt_tlb_invalidation_init - Initialize GT TLB invalidation state
 * @gt: graphics tile
 *
 * Initialize GT TLB invalidation state, purely software initialization, should
 * be called once during driver load.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_gt_tlb_invalidation_init(struct xe_gt *gt)
{
	gt->tlb_invalidation.seqno = 1;
	INIT_LIST_HEAD(&gt->tlb_invalidation.pending_fences);
	spin_lock_init(&gt->tlb_invalidation.pending_lock);
	spin_lock_init(&gt->tlb_invalidation.lock);
	INIT_DELAYED_WORK(&gt->tlb_invalidation.fence_tdr,
			  xe_gt_tlb_fence_timeout);

	return 0;
}

static void
__invalidation_fence_signal(struct xe_gt_tlb_invalidation_fence *fence)
{
	trace_xe_gt_tlb_invalidation_fence_signal(fence);
	dma_fence_signal(&fence->base);
	dma_fence_put(&fence->base);
}

static void
invalidation_fence_signal(struct xe_gt_tlb_invalidation_fence *fence)
{
	list_del(&fence->link);
	__invalidation_fence_signal(fence);
}

/**
 * xe_gt_tlb_invalidation_reset - Initialize GT TLB invalidation reset
 * @gt: graphics tile
 *
 * Signal any pending invalidation fences, should be called during a GT reset
 */
void xe_gt_tlb_invalidation_reset(struct xe_gt *gt)
{
	struct xe_gt_tlb_invalidation_fence *fence, *next;
	struct xe_guc *guc = &gt->uc.guc;
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
	wake_up_all(&guc->ct.wq);

	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link)
		invalidation_fence_signal(fence);
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
	int seqno;
	int ret;

	/*
	 * XXX: The seqno algorithm relies on TLB invalidation being processed
	 * in order which they currently are, if that changes the algorithm will
	 * need to be updated.
	 */

	mutex_lock(&guc->ct.lock);
	seqno = gt->tlb_invalidation.seqno;
	if (fence) {
		fence->seqno = seqno;
		trace_xe_gt_tlb_invalidation_fence_send(fence);
	}
	action[1] = seqno;
	ret = xe_guc_ct_send_locked(&guc->ct, action, len,
				    G2H_LEN_DW_TLB_INVALIDATE, 1);
	if (!ret && fence) {
		spin_lock_irq(&gt->tlb_invalidation.pending_lock);
		/*
		 * We haven't actually published the TLB fence as per
		 * pending_fences, but in theory our seqno could have already
		 * been written as we acquired the pending_lock. In such a case
		 * we can just go ahead and signal the fence here.
		 */
		if (tlb_invalidation_seqno_past(gt, seqno)) {
			__invalidation_fence_signal(fence);
		} else {
			fence->invalidation_time = ktime_get();
			list_add_tail(&fence->link,
				      &gt->tlb_invalidation.pending_fences);

			if (list_is_singular(&gt->tlb_invalidation.pending_fences))
				queue_delayed_work(system_wq,
						   &gt->tlb_invalidation.fence_tdr,
						   TLB_TIMEOUT);
		}
		spin_unlock_irq(&gt->tlb_invalidation.pending_lock);
	} else if (ret < 0 && fence) {
		__invalidation_fence_signal(fence);
	}
	if (!ret) {
		gt->tlb_invalidation.seqno = (gt->tlb_invalidation.seqno + 1) %
			TLB_INVALIDATION_SEQNO_MAX;
		if (!gt->tlb_invalidation.seqno)
			gt->tlb_invalidation.seqno = 1;
		ret = seqno;
	}
	mutex_unlock(&guc->ct.lock);

	return ret;
}

#define MAKE_INVAL_OP(type)	((type << XE_GUC_TLB_INVAL_TYPE_SHIFT) | \
		XE_GUC_TLB_INVAL_MODE_HEAVY << XE_GUC_TLB_INVAL_MODE_SHIFT | \
		XE_GUC_TLB_INVAL_FLUSH_CACHE)

/**
 * xe_gt_tlb_invalidation_guc - Issue a TLB invalidation on this GT for the GuC
 * @gt: graphics tile
 *
 * Issue a TLB invalidation for the GuC. Completion of TLB is asynchronous and
 * caller can use seqno + xe_gt_tlb_invalidation_wait to wait for completion.
 *
 * Return: Seqno which can be passed to xe_gt_tlb_invalidation_wait on success,
 * negative error code on error.
 */
int xe_gt_tlb_invalidation_guc(struct xe_gt *gt)
{
	u32 action[] = {
		XE_GUC_ACTION_TLB_INVALIDATION,
		0,  /* seqno, replaced in send_tlb_invalidation */
		MAKE_INVAL_OP(XE_GUC_TLB_INVAL_GUC),
	};

	return send_tlb_invalidation(&gt->uc.guc, NULL, action,
				     ARRAY_SIZE(action));
}

/**
 * xe_gt_tlb_invalidation_vma - Issue a TLB invalidation on this GT for a VMA
 * @gt: graphics tile
 * @fence: invalidation fence which will be signal on TLB invalidation
 * completion, can be NULL
 * @vma: VMA to invalidate
 *
 * Issue a range based TLB invalidation if supported, if not fallback to a full
 * TLB invalidation. Completion of TLB is asynchronous and caller can either use
 * the invalidation fence or seqno + xe_gt_tlb_invalidation_wait to wait for
 * completion.
 *
 * Return: Seqno which can be passed to xe_gt_tlb_invalidation_wait on success,
 * negative error code on error.
 */
int xe_gt_tlb_invalidation_vma(struct xe_gt *gt,
			       struct xe_gt_tlb_invalidation_fence *fence,
			       struct xe_vma *vma)
{
	struct xe_device *xe = gt_to_xe(gt);
#define MAX_TLB_INVALIDATION_LEN	7
	u32 action[MAX_TLB_INVALIDATION_LEN];
	int len = 0;

	xe_gt_assert(gt, vma);

	/* Execlists not supported */
	if (gt_to_xe(gt)->info.force_execlist) {
		if (fence)
			__invalidation_fence_signal(fence);

		return 0;
	}

	action[len++] = XE_GUC_ACTION_TLB_INVALIDATION;
	action[len++] = 0; /* seqno, replaced in send_tlb_invalidation */
	if (!xe->info.has_range_tlb_invalidation) {
		action[len++] = MAKE_INVAL_OP(XE_GUC_TLB_INVAL_FULL);
	} else {
		u64 start = xe_vma_start(vma);
		u64 length = xe_vma_size(vma);
		u64 align, end;

		if (length < SZ_4K)
			length = SZ_4K;

		/*
		 * We need to invalidate a higher granularity if start address
		 * is not aligned to length. When start is not aligned with
		 * length we need to find the length large enough to create an
		 * address mask covering the required range.
		 */
		align = roundup_pow_of_two(length);
		start = ALIGN_DOWN(xe_vma_start(vma), align);
		end = ALIGN(xe_vma_end(vma), align);
		length = align;
		while (start + length < end) {
			length <<= 1;
			start = ALIGN_DOWN(xe_vma_start(vma), length);
		}

		/*
		 * Minimum invalidation size for a 2MB page that the hardware
		 * expects is 16MB
		 */
		if (length >= SZ_2M) {
			length = max_t(u64, SZ_16M, length);
			start = ALIGN_DOWN(xe_vma_start(vma), length);
		}

		xe_gt_assert(gt, length >= SZ_4K);
		xe_gt_assert(gt, is_power_of_2(length));
		xe_gt_assert(gt, !(length & GENMASK(ilog2(SZ_16M) - 1, ilog2(SZ_2M) + 1)));
		xe_gt_assert(gt, IS_ALIGNED(start, length));

		action[len++] = MAKE_INVAL_OP(XE_GUC_TLB_INVAL_PAGE_SELECTIVE);
		action[len++] = xe_vma_vm(vma)->usm.asid;
		action[len++] = lower_32_bits(start);
		action[len++] = upper_32_bits(start);
		action[len++] = ilog2(length) - ilog2(SZ_4K);
	}

	xe_gt_assert(gt, len <= MAX_TLB_INVALIDATION_LEN);

	return send_tlb_invalidation(&gt->uc.guc, fence, action, len);
}

/**
 * xe_gt_tlb_invalidation_wait - Wait for TLB to complete
 * @gt: graphics tile
 * @seqno: seqno to wait which was returned from xe_gt_tlb_invalidation
 *
 * Wait for 200ms for a TLB invalidation to complete, in practice we always
 * should receive the TLB invalidation within 200ms.
 *
 * Return: 0 on success, -ETIME on TLB invalidation timeout
 */
int xe_gt_tlb_invalidation_wait(struct xe_gt *gt, int seqno)
{
	struct xe_guc *guc = &gt->uc.guc;
	int ret;

	/* Execlists not supported */
	if (gt_to_xe(gt)->info.force_execlist)
		return 0;

	/*
	 * XXX: See above, this algorithm only works if seqno are always in
	 * order
	 */
	ret = wait_event_timeout(guc->ct.wq,
				 tlb_invalidation_seqno_past(gt, seqno),
				 TLB_TIMEOUT);
	if (!ret) {
		struct drm_printer p = xe_gt_err_printer(gt);

		xe_gt_err(gt, "TLB invalidation time'd out, seqno=%d, recv=%d\n",
			  seqno, gt->tlb_invalidation.seqno_recv);
		xe_guc_ct_print(&guc->ct, &p, true);
		return -ETIME;
	}

	return 0;
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

	/*
	 * wake_up_all() and wait_event_timeout() already have the correct
	 * barriers.
	 */
	WRITE_ONCE(gt->tlb_invalidation.seqno_recv, msg[0]);
	wake_up_all(&guc->ct.wq);

	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link) {
		trace_xe_gt_tlb_invalidation_fence_recv(fence);

		if (!tlb_invalidation_seqno_past(gt, fence->seqno))
			break;

		invalidation_fence_signal(fence);
	}

	if (!list_empty(&gt->tlb_invalidation.pending_fences))
		mod_delayed_work(system_wq,
				 &gt->tlb_invalidation.fence_tdr,
				 TLB_TIMEOUT);
	else
		cancel_delayed_work(&gt->tlb_invalidation.fence_tdr);

	spin_unlock_irqrestore(&gt->tlb_invalidation.pending_lock, flags);

	return 0;
}
