// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

int xe_gt_tlb_invalidation_init(struct xe_gt *gt)
{
	gt->usm.tlb_invalidation_seqno = 1;

	return 0;
}

static int send_tlb_invalidation(struct xe_guc *guc)
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
	seqno = gt->usm.tlb_invalidation_seqno;
	action[1] = seqno;
	gt->usm.tlb_invalidation_seqno = (gt->usm.tlb_invalidation_seqno + 1) %
		TLB_INVALIDATION_SEQNO_MAX;
	if (!gt->usm.tlb_invalidation_seqno)
		gt->usm.tlb_invalidation_seqno = 1;
	ret = xe_guc_ct_send_locked(&guc->ct, action, ARRAY_SIZE(action),
				    G2H_LEN_DW_TLB_INVALIDATE, 1);
	if (!ret)
		ret = seqno;
	mutex_unlock(&guc->ct.lock);

	return ret;
}

int xe_gt_tlb_invalidation(struct xe_gt *gt)
{
	return send_tlb_invalidation(&gt->uc.guc);
}

static bool tlb_invalidation_seqno_past(struct xe_gt *gt, int seqno)
{
	if (gt->usm.tlb_invalidation_seqno_recv >= seqno)
		return true;

	if (seqno - gt->usm.tlb_invalidation_seqno_recv >
	    (TLB_INVALIDATION_SEQNO_MAX / 2))
		return true;

	return false;
}

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
			seqno, gt->usm.tlb_invalidation_seqno_recv);
		return -ETIME;
	}

	return 0;
}

int xe_guc_tlb_invalidation_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	int expected_seqno;

	if (unlikely(len != 1))
		return -EPROTO;

	/* Sanity check on seqno */
	expected_seqno = (gt->usm.tlb_invalidation_seqno_recv + 1) %
		TLB_INVALIDATION_SEQNO_MAX;
	XE_WARN_ON(expected_seqno != msg[0]);

	gt->usm.tlb_invalidation_seqno_recv = msg[0];
	smp_wmb();
	wake_up_all(&guc->ct.wq);

	return 0;
}
