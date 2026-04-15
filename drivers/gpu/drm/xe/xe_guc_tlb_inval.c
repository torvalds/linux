// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "abi/guc_actions_abi.h"

#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_exec_queue_types.h"
#include "xe_gt_stats.h"
#include "xe_gt_types.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_exec_queue_types.h"
#include "xe_guc_tlb_inval.h"
#include "xe_force_wake.h"
#include "xe_mmio.h"
#include "xe_sa.h"
#include "xe_tlb_inval.h"
#include "xe_vm.h"

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

#define MAKE_INVAL_OP_FLUSH(type, flush_cache)	((type << XE_GUC_TLB_INVAL_TYPE_SHIFT) | \
		XE_GUC_TLB_INVAL_MODE_HEAVY << XE_GUC_TLB_INVAL_MODE_SHIFT | \
		(flush_cache ? \
		XE_GUC_TLB_INVAL_FLUSH_CACHE : 0))

#define MAKE_INVAL_OP(type)	MAKE_INVAL_OP_FLUSH(type, true)

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

		if (IS_SRIOV_VF(xe))
			return -ECANCELED;

		CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
		if (xe->info.platform == XE_PVC || GRAPHICS_VER(xe) >= 20) {
			xe_mmio_write32(mmio, PVC_GUC_TLB_INV_DESC1,
					PVC_GUC_TLB_INV_DESC1_INVALIDATE);
			xe_mmio_write32(mmio, PVC_GUC_TLB_INV_DESC0,
					PVC_GUC_TLB_INV_DESC0_VALID);
		} else {
			xe_mmio_write32(mmio, GUC_TLB_INV_CR,
					GUC_TLB_INV_CR_INVALIDATE);
		}
	}

	return -ECANCELED;
}

static int send_page_reclaim(struct xe_guc *guc, u32 seqno,
			     u64 gpu_addr)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 action[] = {
		XE_GUC_ACTION_PAGE_RECLAMATION,
		seqno,
		lower_32_bits(gpu_addr),
		upper_32_bits(gpu_addr),
	};

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_PRL_ISSUED_COUNT, 1);

	return xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
			      G2H_LEN_DW_PAGE_RECLAMATION, 1);
}

static u64 normalize_invalidation_range(struct xe_gt *gt, u64 *start, u64 *end)
{
	u64 orig_start = *start;
	u64 length = *end - *start;
	u64 align;

	if (length < SZ_4K)
		length = SZ_4K;

	align = roundup_pow_of_two(length);
	*start = ALIGN_DOWN(*start, align);
	*end = ALIGN(*end, align);
	length = align;
	while (*start + length < *end) {
		length <<= 1;
		*start = ALIGN_DOWN(orig_start, length);
	}

	if (length >= SZ_2M) {
		length = max_t(u64, SZ_16M, length);
		*start = ALIGN_DOWN(orig_start, length);
	}

	xe_gt_assert(gt, length >= SZ_4K);
	xe_gt_assert(gt, is_power_of_2(length));
	xe_gt_assert(gt, !(length & GENMASK(ilog2(SZ_16M) - 1,
					    ilog2(SZ_2M) + 1)));
	xe_gt_assert(gt, IS_ALIGNED(*start, length));

	return length;
}

/*
 * Ensure that roundup_pow_of_two(length) doesn't overflow.
 * Note that roundup_pow_of_two() operates on unsigned long,
 * not on u64.
 */
#define MAX_RANGE_TLB_INVALIDATION_LENGTH (rounddown_pow_of_two(ULONG_MAX))

static int send_tlb_inval_ppgtt(struct xe_guc *guc, u32 seqno, u64 start,
				u64 end, u32 id, u32 type,
				struct drm_suballoc *prl_sa)
{
#define MAX_TLB_INVALIDATION_LEN	7
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_device *xe = guc_to_xe(guc);
	u32 action[MAX_TLB_INVALIDATION_LEN];
	u64 length = end - start;
	int len = 0, err;

	xe_gt_assert(gt, (type == XE_GUC_TLB_INVAL_PAGE_SELECTIVE &&
			  !xe->info.has_ctx_tlb_inval) ||
		     (type == XE_GUC_TLB_INVAL_PAGE_SELECTIVE_CTX &&
		      xe->info.has_ctx_tlb_inval));

	action[len++] = XE_GUC_ACTION_TLB_INVALIDATION;
	action[len++] = !prl_sa ? seqno : TLB_INVALIDATION_SEQNO_INVALID;
	if (!gt_to_xe(gt)->info.has_range_tlb_inval ||
	    length > MAX_RANGE_TLB_INVALIDATION_LENGTH) {
		action[len++] = MAKE_INVAL_OP(XE_GUC_TLB_INVAL_FULL);
	} else {
		u64 normalize_len = normalize_invalidation_range(gt, &start,
								 &end);
		bool need_flush = !prl_sa &&
			seqno != TLB_INVALIDATION_SEQNO_INVALID;

		/* Flush on NULL case, Media is not required to modify flush due to no PPC so NOP */
		action[len++] = MAKE_INVAL_OP_FLUSH(type, need_flush);
		action[len++] = id;
		action[len++] = lower_32_bits(start);
		action[len++] = upper_32_bits(start);
		action[len++] = ilog2(normalize_len) - ilog2(SZ_4K);
	}

	xe_gt_assert(gt, len <= MAX_TLB_INVALIDATION_LEN);
#undef MAX_TLB_INVALIDATION_LEN

	err = send_tlb_inval(guc, action, len);
	if (!err && prl_sa) {
		xe_gt_assert(gt, seqno != TLB_INVALIDATION_SEQNO_INVALID);
		err = send_page_reclaim(guc, seqno, xe_sa_bo_gpu_addr(prl_sa));
	}
	return err;
}

static int send_tlb_inval_asid_ppgtt(struct xe_tlb_inval *tlb_inval, u32 seqno,
				     u64 start, u64 end, u32 asid,
				     struct drm_suballoc *prl_sa)
{
	struct xe_guc *guc = tlb_inval->private;

	lockdep_assert_held(&tlb_inval->seqno_lock);

	if (guc_to_xe(guc)->info.force_execlist)
		return -ECANCELED;

	return send_tlb_inval_ppgtt(guc, seqno, start, end, asid,
				    XE_GUC_TLB_INVAL_PAGE_SELECTIVE, prl_sa);
}

static int send_tlb_inval_ctx_ppgtt(struct xe_tlb_inval *tlb_inval, u32 seqno,
				    u64 start, u64 end, u32 asid,
				    struct drm_suballoc *prl_sa)
{
	struct xe_guc *guc = tlb_inval->private;
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_exec_queue *q, *next, *last_q = NULL;
	struct xe_vm *vm;
	LIST_HEAD(tlb_inval_list);
	int err = 0, id = guc_to_gt(guc)->info.id;

	lockdep_assert_held(&tlb_inval->seqno_lock);

	if (xe->info.force_execlist)
		return -ECANCELED;

	vm = xe_device_asid_to_vm(xe, asid);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	down_read(&vm->exec_queues.lock);

	/*
	 * XXX: Randomly picking a threshold for now. This will need to be
	 * tuned based on expected UMD queue counts and performance profiling.
	 */
#define EXEC_QUEUE_COUNT_FULL_THRESHOLD	8
	if (vm->exec_queues.count[id] >= EXEC_QUEUE_COUNT_FULL_THRESHOLD) {
		u32 action[] = {
			XE_GUC_ACTION_TLB_INVALIDATION,
			seqno,
			MAKE_INVAL_OP(XE_GUC_TLB_INVAL_FULL),
		};

		err = send_tlb_inval(guc, action, ARRAY_SIZE(action));
		goto err_unlock;
	}
#undef EXEC_QUEUE_COUNT_FULL_THRESHOLD

	/*
	 * Move exec queues to a temporary list to issue invalidations. The exec
	 * queue must active and a reference must be taken to prevent concurrent
	 * deregistrations.
	 *
	 * List modification is safe because we hold 'vm->exec_queues.lock' for
	 * reading, which prevents external modifications. Using a per-GT list
	 * is also safe since 'tlb_inval->seqno_lock' ensures no other GT users
	 * can enter this code path.
	 */
	list_for_each_entry_safe(q, next, &vm->exec_queues.list[id],
				 vm_exec_queue_link) {
		if (q->ops->active(q) && xe_exec_queue_get_unless_zero(q)) {
			last_q = q;
			list_move_tail(&q->vm_exec_queue_link, &tlb_inval_list);
		}
	}

	if (!last_q) {
		/*
		 * We can't break fence ordering for TLB invalidation jobs, if
		 * TLB invalidations are inflight issue a dummy invalidation to
		 * maintain ordering. Nor can we move safely the seqno_recv when
		 * returning -ECANCELED if TLB invalidations are in flight. Use
		 * GGTT invalidation as dummy invalidation given ASID
		 * invalidations are unsupported here.
		 */
		if (xe_tlb_inval_idle(tlb_inval))
			err = -ECANCELED;
		else
			err = send_tlb_inval_ggtt(tlb_inval, seqno);
		goto err_unlock;
	}

	list_for_each_entry_safe(q, next, &tlb_inval_list, vm_exec_queue_link) {
		struct drm_suballoc *__prl_sa = NULL;
		int __seqno = TLB_INVALIDATION_SEQNO_INVALID;
		u32 type = XE_GUC_TLB_INVAL_PAGE_SELECTIVE_CTX;

		xe_assert(xe, q->vm == vm);

		if (err)
			goto unref;

		if (last_q == q) {
			__prl_sa = prl_sa;
			__seqno = seqno;
		}

		err = send_tlb_inval_ppgtt(guc, __seqno, start, end,
					   q->guc->id, type, __prl_sa);

unref:
		/*
		 * Must always return exec queue to original list / drop
		 * reference
		 */
		list_move_tail(&q->vm_exec_queue_link,
			       &vm->exec_queues.list[id]);
		xe_exec_queue_put(q);
	}

err_unlock:
	up_read(&vm->exec_queues.lock);
	xe_vm_put(vm);

	return err;
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

static const struct xe_tlb_inval_ops guc_tlb_inval_asid_ops = {
	.all = send_tlb_inval_all,
	.ggtt = send_tlb_inval_ggtt,
	.ppgtt = send_tlb_inval_asid_ppgtt,
	.initialized = tlb_inval_initialized,
	.flush = tlb_inval_flush,
	.timeout_delay = tlb_inval_timeout_delay,
};

static const struct xe_tlb_inval_ops guc_tlb_inval_ctx_ops = {
	.ggtt = send_tlb_inval_ggtt,
	.all = send_tlb_inval_all,
	.ppgtt = send_tlb_inval_ctx_ppgtt,
	.initialized = tlb_inval_initialized,
	.flush = tlb_inval_flush,
	.timeout_delay = tlb_inval_timeout_delay,
};

/**
 * xe_guc_tlb_inval_init_early() - Init GuC TLB invalidation early
 * @guc: GuC object
 * @tlb_inval: TLB invalidation client
 *
 * Initialize GuC TLB invalidation by setting back pointer in TLB invalidation
 * client to the GuC and setting GuC backend ops.
 */
void xe_guc_tlb_inval_init_early(struct xe_guc *guc,
				 struct xe_tlb_inval *tlb_inval)
{
	struct xe_device *xe = guc_to_xe(guc);

	tlb_inval->private = guc;

	if (xe->info.has_ctx_tlb_inval)
		tlb_inval->ops = &guc_tlb_inval_ctx_ops;
	else
		tlb_inval->ops = &guc_tlb_inval_asid_ops;
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
