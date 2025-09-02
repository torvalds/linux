// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "abi/guc_actions_abi.h"

#include "xe_device.h"
#include "xe_gt_stats.h"
#include "xe_gt_types.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_tlb_inval.h"
#include "xe_force_wake.h"
#include "xe_mmio.h"
#include "xe_tlb_inval.h"

#include "regs/xe_guc_regs.h"

/*
 * XXX: The seqno algorithm relies on TLB invalidation being processed in order
 * which they currently are by the GuC, if that changes the algorithm will need
 * to be updated.
 */

static int send_tlb_inval(struct xe_guc *guc, const u32 *action, int len)
{
	struct xe_gt *gt = guc_to_gt(guc);

	xe_gt_assert(gt, action[1]);	/* Seqno */

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_TLB_INVAL, 1);
	return xe_guc_ct_send(&guc->ct, action, len,
			      G2H_LEN_DW_TLB_INVALIDATE, 1);
}

#define MAKE_INVAL_OP(type)	((type << XE_GUC_TLB_INVAL_TYPE_SHIFT) | \
		XE_GUC_TLB_INVAL_MODE_HEAVY << XE_GUC_TLB_INVAL_MODE_SHIFT | \
		XE_GUC_TLB_INVAL_FLUSH_CACHE)

static int send_tlb_inval_all(struct xe_tlb_inval *tlb_inval, u32 seqno)
{
	struct xe_guc *guc = tlb_inval->private;
	u32 action[] = {
		XE_GUC_ACTION_TLB_INVALIDATION_ALL,
		seqno,
		MAKE_INVAL_OP(XE_GUC_TLB_INVAL_FULL),
	};

	return send_tlb_inval(guc, action, ARRAY_SIZE(action));
}

static int send_tlb_inval_ggtt(struct xe_tlb_inval *tlb_inval, u32 seqno)
{
	struct xe_guc *guc = tlb_inval->private;
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_device *xe = guc_to_xe(guc);

	/*
	 * Returning -ECANCELED in this function is squashed at the caller and
	 * signals waiters.
	 */

	if (xe_guc_ct_enabled(&guc->ct) && guc->submission_state.enabled) {
		u32 action[] = {
			XE_GUC_ACTION_TLB_INVALIDATION,
			seqno,
			MAKE_INVAL_OP(XE_GUC_TLB_INVAL_GUC),
		};

		return send_tlb_inval(guc, action, ARRAY_SIZE(action));
	} else if (xe_device_uc_enabled(xe) && !xe_device_wedged(xe)) {
		struct xe_mmio *mmio = &gt->mmio;
		unsigned int fw_ref;

		if (IS_SRIOV_VF(xe))
			return -ECANCELED;

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

	return -ECANCELED;
}

/*
 * Ensure that roundup_pow_of_two(length) doesn't overflow.
 * Note that roundup_pow_of_two() operates on unsigned long,
 * not on u64.
 */
#define MAX_RANGE_TLB_INVALIDATION_LENGTH (rounddown_pow_of_two(ULONG_MAX))

static int send_tlb_inval_ppgtt(struct xe_tlb_inval *tlb_inval, u32 seqno,
				u64 start, u64 end, u32 asid)
{
#define MAX_TLB_INVALIDATION_LEN	7
	struct xe_guc *guc = tlb_inval->private;
	struct xe_gt *gt = guc_to_gt(guc);
	u32 action[MAX_TLB_INVALIDATION_LEN];
	u64 length = end - start;
	int len = 0;

	if (guc_to_xe(guc)->info.force_execlist)
		return -ECANCELED;

	action[len++] = XE_GUC_ACTION_TLB_INVALIDATION;
	action[len++] = seqno;
	if (!gt_to_xe(gt)->info.has_range_tlb_inval ||
	    length > MAX_RANGE_TLB_INVALIDATION_LENGTH) {
		action[len++] = MAKE_INVAL_OP(XE_GUC_TLB_INVAL_FULL);
	} else {
		u64 orig_start = start;
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

	return send_tlb_inval(guc, action, len);
}

static bool tlb_inval_initialized(struct xe_tlb_inval *tlb_inval)
{
	struct xe_guc *guc = tlb_inval->private;

	return xe_guc_ct_initialized(&guc->ct);
}

static void tlb_inval_flush(struct xe_tlb_inval *tlb_inval)
{
	struct xe_guc *guc = tlb_inval->private;

	LNL_FLUSH_WORK(&guc->ct.g2h_worker);
}

static long tlb_inval_timeout_delay(struct xe_tlb_inval *tlb_inval)
{
	struct xe_guc *guc = tlb_inval->private;

	/* this reflects what HW/GuC needs to process TLB inv request */
	const long hw_tlb_timeout = HZ / 4;

	/* this estimates actual delay caused by the CTB transport */
	long delay = xe_guc_ct_queue_proc_time_jiffies(&guc->ct);

	return hw_tlb_timeout + 2 * delay;
}

static const struct xe_tlb_inval_ops guc_tlb_inval_ops = {
	.all = send_tlb_inval_all,
	.ggtt = send_tlb_inval_ggtt,
	.ppgtt = send_tlb_inval_ppgtt,
	.initialized = tlb_inval_initialized,
	.flush = tlb_inval_flush,
	.timeout_delay = tlb_inval_timeout_delay,
};

/**
 * xe_guc_tlb_inval_init_early() - Init GuC TLB invalidation early
 * @guc: GuC object
 * @tlb_inval: TLB invalidation client
 *
 * Inititialize GuC TLB invalidation by setting back pointer in TLB invalidation
 * client to the GuC and setting GuC backend ops.
 */
void xe_guc_tlb_inval_init_early(struct xe_guc *guc,
				 struct xe_tlb_inval *tlb_inval)
{
	tlb_inval->private = guc;
	tlb_inval->ops = &guc_tlb_inval_ops;
}

/**
 * xe_guc_tlb_inval_done_handler() - TLB invalidation done handler
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
int xe_guc_tlb_inval_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);

	if (unlikely(len != 1))
		return -EPROTO;

	xe_tlb_inval_done_handler(&gt->tlb_inval, msg[0]);

	return 0;
}
