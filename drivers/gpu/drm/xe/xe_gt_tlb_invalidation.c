// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_trace.h"

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
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
	spin_lock_init(&gt->tlb_invalidation.lock);
	gt->tlb_invalidation.fence_context = dma_fence_context_alloc(1);

	return 0;
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

	mutex_lock(&gt->uc.guc.ct.lock);
	list_for_each_entry_safe(fence, next,
				 &gt->tlb_invalidation.pending_fences, link) {
		list_del(&fence->link);
		dma_fence_signal(&fence->base);
		dma_fence_put(&fence->base);
	}
	mutex_unlock(&gt->uc.guc.ct.lock);
}

static int send_tlb_invalidation(struct xe_guc *guc,
				 struct xe_gt_tlb_invalidation_fence *fence)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 action[] = {
		XE_GUC_ACTION_TLB_INVALIDATION,
		0,
		XE_GUC_TLB_INVAL_FULL << XE_GUC_TLB_INVAL_TYPE_SHIFT |
		XE_GUC_TLB_INVAL_MODE_HEAVY << XE_GUC_TLB_INVAL_MODE_SHIFT |
		XE_GUC_TLB_INVAL_FLUSH_CACHE,
	};
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
		/*
		 * FIXME: How to deal TLB invalidation timeout, right now we
		 * just have an endless fence which isn't ideal.
		 */
		fence->seqno = seqno;
		list_add_tail(&fence->link,
			      &gt->tlb_invalidation.pending_fences);
		trace_xe_gt_tlb_invalidation_fence_send(fence);
	}
	action[1] = seqno;
	gt->tlb_invalidation.seqno = (gt->tlb_invalidation.seqno + 1) %
		TLB_INVALIDATION_SEQNO_MAX;
	if (!gt->tlb_invalidation.seqno)
		gt->tlb_invalidation.seqno = 1;
	ret = xe_guc_ct_send_locked(&guc->ct, action, ARRAY_SIZE(action),
				    G2H_LEN_DW_TLB_INVALIDATE, 1);
	if (!ret)
		ret = seqno;
	mutex_unlock(&guc->ct.lock);

	return ret;
}

/**
 * xe_gt_tlb_invalidation - Issue a TLB invalidation on this GT
 * @gt: graphics tile
 * @fence: invalidation fence which will be signal on TLB invalidation
 * completion, can be NULL
 *
 * Issue a full TLB invalidation on the GT. Completion of TLB is asynchronous
 * and caller can either use the invalidation fence or seqno +
 * xe_gt_tlb_invalidation_wait to wait for completion.
 *
 * Return: Seqno which can be passed to xe_gt_tlb_invalidation_wait on success,
 * negative error code on error.
 */
int xe_gt_tlb_invalidation(struct xe_gt *gt,
			   struct xe_gt_tlb_invalidation_fence *fence)
{
	return send_tlb_invalidation(&gt->uc.guc, fence);
}

static bool tlb_invalidation_seqno_past(struct xe_gt *gt, int seqno)
{
	if (gt->tlb_invalidation.seqno_recv >= seqno)
		return true;

	if (seqno - gt->tlb_invalidation.seqno_recv >
	    (TLB_INVALIDATION_SEQNO_MAX / 2))
		return true;

	return false;
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
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_guc *guc = &gt->uc.guc;
	int ret;

	/*
	 * XXX: See above, this algorithm only works if seqno are always in
	 * order
	 */
	ret = wait_event_timeout(guc->ct.wq,
				 tlb_invalidation_seqno_past(gt, seqno),
				 HZ / 5);
	if (!ret) {
		drm_err(&xe->drm, "TLB invalidation time'd out, seqno=%d, recv=%d\n",
			seqno, gt->tlb_invalidation.seqno_recv);
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
	struct xe_gt_tlb_invalidation_fence *fence;
	int expected_seqno;

	lockdep_assert_held(&guc->ct.lock);

	if (unlikely(len != 1))
		return -EPROTO;

	/* Sanity check on seqno */
	expected_seqno = (gt->tlb_invalidation.seqno_recv + 1) %
		TLB_INVALIDATION_SEQNO_MAX;
	XE_WARN_ON(expected_seqno != msg[0]);

	gt->tlb_invalidation.seqno_recv = msg[0];
	smp_wmb();
	wake_up_all(&guc->ct.wq);

	fence = list_first_entry_or_null(&gt->tlb_invalidation.pending_fences,
					 typeof(*fence), link);
	if (fence)
		trace_xe_gt_tlb_invalidation_fence_recv(fence);
	if (fence && tlb_invalidation_seqno_past(gt, fence->seqno)) {
		trace_xe_gt_tlb_invalidation_fence_signal(fence);
		list_del(&fence->link);
		dma_fence_signal(&fence->base);
		dma_fence_put(&fence->base);
	}

	return 0;
}
