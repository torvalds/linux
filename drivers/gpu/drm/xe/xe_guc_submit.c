// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_submit.h"

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/dma-fence-array.h>
#include <linux/math64.h>

#include <drm/drm_managed.h>

#include "abi/guc_actions_abi.h"
#include "abi/guc_klvs_abi.h"
#include "regs/xe_lrc_layout.h"
#include "xe_assert.h"
#include "xe_devcoredump.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_gpu_scheduler.h"
#include "xe_gt.h"
#include "xe_gt_clock.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_capture.h"
#include "xe_guc_ct.h"
#include "xe_guc_exec_queue_types.h"
#include "xe_guc_id_mgr.h"
#include "xe_guc_submit_types.h"
#include "xe_hw_engine.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_map.h"
#include "xe_mocs.h"
#include "xe_pm.h"
#include "xe_ring_ops_types.h"
#include "xe_sched_job.h"
#include "xe_trace.h"
#include "xe_vm.h"

static struct xe_guc *
exec_queue_to_guc(struct xe_exec_queue *q)
{
	return &q->gt->uc.guc;
}

/*
 * Helpers for engine state, using an atomic as some of the bits can transition
 * as the same time (e.g. a suspend can be happning at the same time as schedule
 * engine done being processed).
 */
#define EXEC_QUEUE_STATE_REGISTERED		(1 << 0)
#define EXEC_QUEUE_STATE_ENABLED		(1 << 1)
#define EXEC_QUEUE_STATE_PENDING_ENABLE		(1 << 2)
#define EXEC_QUEUE_STATE_PENDING_DISABLE	(1 << 3)
#define EXEC_QUEUE_STATE_DESTROYED		(1 << 4)
#define EXEC_QUEUE_STATE_SUSPENDED		(1 << 5)
#define EXEC_QUEUE_STATE_RESET			(1 << 6)
#define EXEC_QUEUE_STATE_KILLED			(1 << 7)
#define EXEC_QUEUE_STATE_WEDGED			(1 << 8)
#define EXEC_QUEUE_STATE_BANNED			(1 << 9)
#define EXEC_QUEUE_STATE_CHECK_TIMEOUT		(1 << 10)
#define EXEC_QUEUE_STATE_EXTRA_REF		(1 << 11)

static bool exec_queue_registered(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_REGISTERED;
}

static void set_exec_queue_registered(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_REGISTERED, &q->guc->state);
}

static void clear_exec_queue_registered(struct xe_exec_queue *q)
{
	atomic_and(~EXEC_QUEUE_STATE_REGISTERED, &q->guc->state);
}

static bool exec_queue_enabled(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_ENABLED;
}

static void set_exec_queue_enabled(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_ENABLED, &q->guc->state);
}

static void clear_exec_queue_enabled(struct xe_exec_queue *q)
{
	atomic_and(~EXEC_QUEUE_STATE_ENABLED, &q->guc->state);
}

static bool exec_queue_pending_enable(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_PENDING_ENABLE;
}

static void set_exec_queue_pending_enable(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_PENDING_ENABLE, &q->guc->state);
}

static void clear_exec_queue_pending_enable(struct xe_exec_queue *q)
{
	atomic_and(~EXEC_QUEUE_STATE_PENDING_ENABLE, &q->guc->state);
}

static bool exec_queue_pending_disable(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_PENDING_DISABLE;
}

static void set_exec_queue_pending_disable(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_PENDING_DISABLE, &q->guc->state);
}

static void clear_exec_queue_pending_disable(struct xe_exec_queue *q)
{
	atomic_and(~EXEC_QUEUE_STATE_PENDING_DISABLE, &q->guc->state);
}

static bool exec_queue_destroyed(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_DESTROYED;
}

static void set_exec_queue_destroyed(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_DESTROYED, &q->guc->state);
}

static bool exec_queue_banned(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_BANNED;
}

static void set_exec_queue_banned(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_BANNED, &q->guc->state);
}

static bool exec_queue_suspended(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_SUSPENDED;
}

static void set_exec_queue_suspended(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_SUSPENDED, &q->guc->state);
}

static void clear_exec_queue_suspended(struct xe_exec_queue *q)
{
	atomic_and(~EXEC_QUEUE_STATE_SUSPENDED, &q->guc->state);
}

static bool exec_queue_reset(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_RESET;
}

static void set_exec_queue_reset(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_RESET, &q->guc->state);
}

static bool exec_queue_killed(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_KILLED;
}

static void set_exec_queue_killed(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_KILLED, &q->guc->state);
}

static bool exec_queue_wedged(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_WEDGED;
}

static void set_exec_queue_wedged(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_WEDGED, &q->guc->state);
}

static bool exec_queue_check_timeout(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_CHECK_TIMEOUT;
}

static void set_exec_queue_check_timeout(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_CHECK_TIMEOUT, &q->guc->state);
}

static void clear_exec_queue_check_timeout(struct xe_exec_queue *q)
{
	atomic_and(~EXEC_QUEUE_STATE_CHECK_TIMEOUT, &q->guc->state);
}

static bool exec_queue_extra_ref(struct xe_exec_queue *q)
{
	return atomic_read(&q->guc->state) & EXEC_QUEUE_STATE_EXTRA_REF;
}

static void set_exec_queue_extra_ref(struct xe_exec_queue *q)
{
	atomic_or(EXEC_QUEUE_STATE_EXTRA_REF, &q->guc->state);
}

static bool exec_queue_killed_or_banned_or_wedged(struct xe_exec_queue *q)
{
	return (atomic_read(&q->guc->state) &
		(EXEC_QUEUE_STATE_WEDGED | EXEC_QUEUE_STATE_KILLED |
		 EXEC_QUEUE_STATE_BANNED));
}

static void guc_submit_fini(struct drm_device *drm, void *arg)
{
	struct xe_guc *guc = arg;

	xa_destroy(&guc->submission_state.exec_queue_lookup);
}

static void guc_submit_wedged_fini(void *arg)
{
	struct xe_guc *guc = arg;
	struct xe_exec_queue *q;
	unsigned long index;

	mutex_lock(&guc->submission_state.lock);
	xa_for_each(&guc->submission_state.exec_queue_lookup, index, q) {
		if (exec_queue_wedged(q)) {
			mutex_unlock(&guc->submission_state.lock);
			xe_exec_queue_put(q);
			mutex_lock(&guc->submission_state.lock);
		}
	}
	mutex_unlock(&guc->submission_state.lock);
}

static const struct xe_exec_queue_ops guc_exec_queue_ops;

static void primelockdep(struct xe_guc *guc)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);

	mutex_lock(&guc->submission_state.lock);
	mutex_unlock(&guc->submission_state.lock);

	fs_reclaim_release(GFP_KERNEL);
}

/**
 * xe_guc_submit_init() - Initialize GuC submission.
 * @guc: the &xe_guc to initialize
 * @num_ids: number of GuC context IDs to use
 *
 * The bare-metal or PF driver can pass ~0 as &num_ids to indicate that all
 * GuC context IDs supported by the GuC firmware should be used for submission.
 *
 * Only VF drivers will have to provide explicit number of GuC context IDs
 * that they can use for submission.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_guc_submit_init(struct xe_guc *guc, unsigned int num_ids)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	int err;

	err = drmm_mutex_init(&xe->drm, &guc->submission_state.lock);
	if (err)
		return err;

	err = xe_guc_id_mgr_init(&guc->submission_state.idm, num_ids);
	if (err)
		return err;

	gt->exec_queue_ops = &guc_exec_queue_ops;

	xa_init(&guc->submission_state.exec_queue_lookup);

	init_waitqueue_head(&guc->submission_state.fini_wq);

	primelockdep(guc);

	return drmm_add_action_or_reset(&xe->drm, guc_submit_fini, guc);
}

static void __release_guc_id(struct xe_guc *guc, struct xe_exec_queue *q, u32 xa_count)
{
	int i;

	lockdep_assert_held(&guc->submission_state.lock);

	for (i = 0; i < xa_count; ++i)
		xa_erase(&guc->submission_state.exec_queue_lookup, q->guc->id + i);

	xe_guc_id_mgr_release_locked(&guc->submission_state.idm,
				     q->guc->id, q->width);

	if (xa_empty(&guc->submission_state.exec_queue_lookup))
		wake_up(&guc->submission_state.fini_wq);
}

static int alloc_guc_id(struct xe_guc *guc, struct xe_exec_queue *q)
{
	int ret;
	int i;

	/*
	 * Must use GFP_NOWAIT as this lock is in the dma fence signalling path,
	 * worse case user gets -ENOMEM on engine create and has to try again.
	 *
	 * FIXME: Have caller pre-alloc or post-alloc /w GFP_KERNEL to prevent
	 * failure.
	 */
	lockdep_assert_held(&guc->submission_state.lock);

	ret = xe_guc_id_mgr_reserve_locked(&guc->submission_state.idm,
					   q->width);
	if (ret < 0)
		return ret;

	q->guc->id = ret;

	for (i = 0; i < q->width; ++i) {
		ret = xa_err(xa_store(&guc->submission_state.exec_queue_lookup,
				      q->guc->id + i, q, GFP_NOWAIT));
		if (ret)
			goto err_release;
	}

	return 0;

err_release:
	__release_guc_id(guc, q, i);

	return ret;
}

static void release_guc_id(struct xe_guc *guc, struct xe_exec_queue *q)
{
	mutex_lock(&guc->submission_state.lock);
	__release_guc_id(guc, q, q->width);
	mutex_unlock(&guc->submission_state.lock);
}

struct exec_queue_policy {
	u32 count;
	struct guc_update_exec_queue_policy h2g;
};

static u32 __guc_exec_queue_policy_action_size(struct exec_queue_policy *policy)
{
	size_t bytes = sizeof(policy->h2g.header) +
		       (sizeof(policy->h2g.klv[0]) * policy->count);

	return bytes / sizeof(u32);
}

static void __guc_exec_queue_policy_start_klv(struct exec_queue_policy *policy,
					      u16 guc_id)
{
	policy->h2g.header.action =
		XE_GUC_ACTION_HOST2GUC_UPDATE_CONTEXT_POLICIES;
	policy->h2g.header.guc_id = guc_id;
	policy->count = 0;
}

#define MAKE_EXEC_QUEUE_POLICY_ADD(func, id) \
static void __guc_exec_queue_policy_add_##func(struct exec_queue_policy *policy, \
					   u32 data) \
{ \
	XE_WARN_ON(policy->count >= GUC_CONTEXT_POLICIES_KLV_NUM_IDS); \
\
	policy->h2g.klv[policy->count].kl = \
		FIELD_PREP(GUC_KLV_0_KEY, \
			   GUC_CONTEXT_POLICIES_KLV_ID_##id) | \
		FIELD_PREP(GUC_KLV_0_LEN, 1); \
	policy->h2g.klv[policy->count].value = data; \
	policy->count++; \
}

MAKE_EXEC_QUEUE_POLICY_ADD(execution_quantum, EXECUTION_QUANTUM)
MAKE_EXEC_QUEUE_POLICY_ADD(preemption_timeout, PREEMPTION_TIMEOUT)
MAKE_EXEC_QUEUE_POLICY_ADD(priority, SCHEDULING_PRIORITY)
#undef MAKE_EXEC_QUEUE_POLICY_ADD

static const int xe_exec_queue_prio_to_guc[] = {
	[XE_EXEC_QUEUE_PRIORITY_LOW] = GUC_CLIENT_PRIORITY_NORMAL,
	[XE_EXEC_QUEUE_PRIORITY_NORMAL] = GUC_CLIENT_PRIORITY_KMD_NORMAL,
	[XE_EXEC_QUEUE_PRIORITY_HIGH] = GUC_CLIENT_PRIORITY_HIGH,
	[XE_EXEC_QUEUE_PRIORITY_KERNEL] = GUC_CLIENT_PRIORITY_KMD_HIGH,
};

static void init_policies(struct xe_guc *guc, struct xe_exec_queue *q)
{
	struct exec_queue_policy policy;
	enum xe_exec_queue_priority prio = q->sched_props.priority;
	u32 timeslice_us = q->sched_props.timeslice_us;
	u32 preempt_timeout_us = q->sched_props.preempt_timeout_us;

	xe_gt_assert(guc_to_gt(guc), exec_queue_registered(q));

	__guc_exec_queue_policy_start_klv(&policy, q->guc->id);
	__guc_exec_queue_policy_add_priority(&policy, xe_exec_queue_prio_to_guc[prio]);
	__guc_exec_queue_policy_add_execution_quantum(&policy, timeslice_us);
	__guc_exec_queue_policy_add_preemption_timeout(&policy, preempt_timeout_us);

	xe_guc_ct_send(&guc->ct, (u32 *)&policy.h2g,
		       __guc_exec_queue_policy_action_size(&policy), 0, 0);
}

static void set_min_preemption_timeout(struct xe_guc *guc, struct xe_exec_queue *q)
{
	struct exec_queue_policy policy;

	__guc_exec_queue_policy_start_klv(&policy, q->guc->id);
	__guc_exec_queue_policy_add_preemption_timeout(&policy, 1);

	xe_guc_ct_send(&guc->ct, (u32 *)&policy.h2g,
		       __guc_exec_queue_policy_action_size(&policy), 0, 0);
}

#define parallel_read(xe_, map_, field_) \
	xe_map_rd_field(xe_, &map_, 0, struct guc_submit_parallel_scratch, \
			field_)
#define parallel_write(xe_, map_, field_, val_) \
	xe_map_wr_field(xe_, &map_, 0, struct guc_submit_parallel_scratch, \
			field_, val_)

static void __register_mlrc_exec_queue(struct xe_guc *guc,
				       struct xe_exec_queue *q,
				       struct guc_ctxt_registration_info *info)
{
#define MAX_MLRC_REG_SIZE      (13 + XE_HW_ENGINE_MAX_INSTANCE * 2)
	u32 action[MAX_MLRC_REG_SIZE];
	int len = 0;
	int i;

	xe_gt_assert(guc_to_gt(guc), xe_exec_queue_is_parallel(q));

	action[len++] = XE_GUC_ACTION_REGISTER_CONTEXT_MULTI_LRC;
	action[len++] = info->flags;
	action[len++] = info->context_idx;
	action[len++] = info->engine_class;
	action[len++] = info->engine_submit_mask;
	action[len++] = info->wq_desc_lo;
	action[len++] = info->wq_desc_hi;
	action[len++] = info->wq_base_lo;
	action[len++] = info->wq_base_hi;
	action[len++] = info->wq_size;
	action[len++] = q->width;
	action[len++] = info->hwlrca_lo;
	action[len++] = info->hwlrca_hi;

	for (i = 1; i < q->width; ++i) {
		struct xe_lrc *lrc = q->lrc[i];

		action[len++] = lower_32_bits(xe_lrc_descriptor(lrc));
		action[len++] = upper_32_bits(xe_lrc_descriptor(lrc));
	}

	xe_gt_assert(guc_to_gt(guc), len <= MAX_MLRC_REG_SIZE);
#undef MAX_MLRC_REG_SIZE

	xe_guc_ct_send(&guc->ct, action, len, 0, 0);
}

static void __register_exec_queue(struct xe_guc *guc,
				  struct guc_ctxt_registration_info *info)
{
	u32 action[] = {
		XE_GUC_ACTION_REGISTER_CONTEXT,
		info->flags,
		info->context_idx,
		info->engine_class,
		info->engine_submit_mask,
		info->wq_desc_lo,
		info->wq_desc_hi,
		info->wq_base_lo,
		info->wq_base_hi,
		info->wq_size,
		info->hwlrca_lo,
		info->hwlrca_hi,
	};

	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action), 0, 0);
}

static void register_exec_queue(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_lrc *lrc = q->lrc[0];
	struct guc_ctxt_registration_info info;

	xe_gt_assert(guc_to_gt(guc), !exec_queue_registered(q));

	memset(&info, 0, sizeof(info));
	info.context_idx = q->guc->id;
	info.engine_class = xe_engine_class_to_guc_class(q->class);
	info.engine_submit_mask = q->logical_mask;
	info.hwlrca_lo = lower_32_bits(xe_lrc_descriptor(lrc));
	info.hwlrca_hi = upper_32_bits(xe_lrc_descriptor(lrc));
	info.flags = CONTEXT_REGISTRATION_FLAG_KMD;

	if (xe_exec_queue_is_parallel(q)) {
		u64 ggtt_addr = xe_lrc_parallel_ggtt_addr(lrc);
		struct iosys_map map = xe_lrc_parallel_map(lrc);

		info.wq_desc_lo = lower_32_bits(ggtt_addr +
			offsetof(struct guc_submit_parallel_scratch, wq_desc));
		info.wq_desc_hi = upper_32_bits(ggtt_addr +
			offsetof(struct guc_submit_parallel_scratch, wq_desc));
		info.wq_base_lo = lower_32_bits(ggtt_addr +
			offsetof(struct guc_submit_parallel_scratch, wq[0]));
		info.wq_base_hi = upper_32_bits(ggtt_addr +
			offsetof(struct guc_submit_parallel_scratch, wq[0]));
		info.wq_size = WQ_SIZE;

		q->guc->wqi_head = 0;
		q->guc->wqi_tail = 0;
		xe_map_memset(xe, &map, 0, 0, PARALLEL_SCRATCH_SIZE - WQ_SIZE);
		parallel_write(xe, map, wq_desc.wq_status, WQ_STATUS_ACTIVE);
	}

	/*
	 * We must keep a reference for LR engines if engine is registered with
	 * the GuC as jobs signal immediately and can't destroy an engine if the
	 * GuC has a reference to it.
	 */
	if (xe_exec_queue_is_lr(q))
		xe_exec_queue_get(q);

	set_exec_queue_registered(q);
	trace_xe_exec_queue_register(q);
	if (xe_exec_queue_is_parallel(q))
		__register_mlrc_exec_queue(guc, q, &info);
	else
		__register_exec_queue(guc, &info);
	init_policies(guc, q);
}

static u32 wq_space_until_wrap(struct xe_exec_queue *q)
{
	return (WQ_SIZE - q->guc->wqi_tail);
}

static int wq_wait_for_space(struct xe_exec_queue *q, u32 wqi_size)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(q->lrc[0]);
	unsigned int sleep_period_ms = 1;

#define AVAILABLE_SPACE \
	CIRC_SPACE(q->guc->wqi_tail, q->guc->wqi_head, WQ_SIZE)
	if (wqi_size > AVAILABLE_SPACE) {
try_again:
		q->guc->wqi_head = parallel_read(xe, map, wq_desc.head);
		if (wqi_size > AVAILABLE_SPACE) {
			if (sleep_period_ms == 1024) {
				xe_gt_reset_async(q->gt);
				return -ENODEV;
			}

			msleep(sleep_period_ms);
			sleep_period_ms <<= 1;
			goto try_again;
		}
	}
#undef AVAILABLE_SPACE

	return 0;
}

static int wq_noop_append(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(q->lrc[0]);
	u32 len_dw = wq_space_until_wrap(q) / sizeof(u32) - 1;

	if (wq_wait_for_space(q, wq_space_until_wrap(q)))
		return -ENODEV;

	xe_gt_assert(guc_to_gt(guc), FIELD_FIT(WQ_LEN_MASK, len_dw));

	parallel_write(xe, map, wq[q->guc->wqi_tail / sizeof(u32)],
		       FIELD_PREP(WQ_TYPE_MASK, WQ_TYPE_NOOP) |
		       FIELD_PREP(WQ_LEN_MASK, len_dw));
	q->guc->wqi_tail = 0;

	return 0;
}

static void wq_item_append(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(q->lrc[0]);
#define WQ_HEADER_SIZE	4	/* Includes 1 LRC address too */
	u32 wqi[XE_HW_ENGINE_MAX_INSTANCE + (WQ_HEADER_SIZE - 1)];
	u32 wqi_size = (q->width + (WQ_HEADER_SIZE - 1)) * sizeof(u32);
	u32 len_dw = (wqi_size / sizeof(u32)) - 1;
	int i = 0, j;

	if (wqi_size > wq_space_until_wrap(q)) {
		if (wq_noop_append(q))
			return;
	}
	if (wq_wait_for_space(q, wqi_size))
		return;

	wqi[i++] = FIELD_PREP(WQ_TYPE_MASK, WQ_TYPE_MULTI_LRC) |
		FIELD_PREP(WQ_LEN_MASK, len_dw);
	wqi[i++] = xe_lrc_descriptor(q->lrc[0]);
	wqi[i++] = FIELD_PREP(WQ_GUC_ID_MASK, q->guc->id) |
		FIELD_PREP(WQ_RING_TAIL_MASK, q->lrc[0]->ring.tail / sizeof(u64));
	wqi[i++] = 0;
	for (j = 1; j < q->width; ++j) {
		struct xe_lrc *lrc = q->lrc[j];

		wqi[i++] = lrc->ring.tail / sizeof(u64);
	}

	xe_gt_assert(guc_to_gt(guc), i == wqi_size / sizeof(u32));

	iosys_map_incr(&map, offsetof(struct guc_submit_parallel_scratch,
				      wq[q->guc->wqi_tail / sizeof(u32)]));
	xe_map_memcpy_to(xe, &map, 0, wqi, wqi_size);
	q->guc->wqi_tail += wqi_size;
	xe_gt_assert(guc_to_gt(guc), q->guc->wqi_tail <= WQ_SIZE);

	xe_device_wmb(xe);

	map = xe_lrc_parallel_map(q->lrc[0]);
	parallel_write(xe, map, wq_desc.tail, q->guc->wqi_tail);
}

#define RESUME_PENDING	~0x0ull
static void submit_exec_queue(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_lrc *lrc = q->lrc[0];
	u32 action[3];
	u32 g2h_len = 0;
	u32 num_g2h = 0;
	int len = 0;
	bool extra_submit = false;

	xe_gt_assert(guc_to_gt(guc), exec_queue_registered(q));

	if (xe_exec_queue_is_parallel(q))
		wq_item_append(q);
	else
		xe_lrc_set_ring_tail(lrc, lrc->ring.tail);

	if (exec_queue_suspended(q) && !xe_exec_queue_is_parallel(q))
		return;

	if (!exec_queue_enabled(q) && !exec_queue_suspended(q)) {
		action[len++] = XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET;
		action[len++] = q->guc->id;
		action[len++] = GUC_CONTEXT_ENABLE;
		g2h_len = G2H_LEN_DW_SCHED_CONTEXT_MODE_SET;
		num_g2h = 1;
		if (xe_exec_queue_is_parallel(q))
			extra_submit = true;

		q->guc->resume_time = RESUME_PENDING;
		set_exec_queue_pending_enable(q);
		set_exec_queue_enabled(q);
		trace_xe_exec_queue_scheduling_enable(q);
	} else {
		action[len++] = XE_GUC_ACTION_SCHED_CONTEXT;
		action[len++] = q->guc->id;
		trace_xe_exec_queue_submit(q);
	}

	xe_guc_ct_send(&guc->ct, action, len, g2h_len, num_g2h);

	if (extra_submit) {
		len = 0;
		action[len++] = XE_GUC_ACTION_SCHED_CONTEXT;
		action[len++] = q->guc->id;
		trace_xe_exec_queue_submit(q);

		xe_guc_ct_send(&guc->ct, action, len, 0, 0);
	}
}

static struct dma_fence *
guc_exec_queue_run_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);
	struct xe_exec_queue *q = job->q;
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct dma_fence *fence = NULL;
	bool lr = xe_exec_queue_is_lr(q);

	xe_gt_assert(guc_to_gt(guc), !(exec_queue_destroyed(q) || exec_queue_pending_disable(q)) ||
		     exec_queue_banned(q) || exec_queue_suspended(q));

	trace_xe_sched_job_run(job);

	if (!exec_queue_killed_or_banned_or_wedged(q) && !xe_sched_job_is_error(job)) {
		if (!exec_queue_registered(q))
			register_exec_queue(q);
		if (!lr)	/* LR jobs are emitted in the exec IOCTL */
			q->ring_ops->emit_job(job);
		submit_exec_queue(q);
	}

	if (lr) {
		xe_sched_job_set_error(job, -EOPNOTSUPP);
		dma_fence_put(job->fence);	/* Drop ref from xe_sched_job_arm */
	} else {
		fence = job->fence;
	}

	return fence;
}

static void guc_exec_queue_free_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);

	trace_xe_sched_job_free(job);
	xe_sched_job_put(job);
}

int xe_guc_read_stopped(struct xe_guc *guc)
{
	return atomic_read(&guc->submission_state.stopped);
}

#define MAKE_SCHED_CONTEXT_ACTION(q, enable_disable)			\
	u32 action[] = {						\
		XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET,			\
		q->guc->id,						\
		GUC_CONTEXT_##enable_disable,				\
	}

static void disable_scheduling_deregister(struct xe_guc *guc,
					  struct xe_exec_queue *q)
{
	MAKE_SCHED_CONTEXT_ACTION(q, DISABLE);
	int ret;

	set_min_preemption_timeout(guc, q);
	smp_rmb();
	ret = wait_event_timeout(guc->ct.wq,
				 (!exec_queue_pending_enable(q) &&
				  !exec_queue_pending_disable(q)) ||
					 xe_guc_read_stopped(guc),
				 HZ * 5);
	if (!ret) {
		struct xe_gpu_scheduler *sched = &q->guc->sched;

		xe_gt_warn(q->gt, "Pending enable/disable failed to respond\n");
		xe_sched_submission_start(sched);
		xe_gt_reset_async(q->gt);
		xe_sched_tdr_queue_imm(sched);
		return;
	}

	clear_exec_queue_enabled(q);
	set_exec_queue_pending_disable(q);
	set_exec_queue_destroyed(q);
	trace_xe_exec_queue_scheduling_disable(q);

	/*
	 * Reserve space for both G2H here as the 2nd G2H is sent from a G2H
	 * handler and we are not allowed to reserved G2H space in handlers.
	 */
	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
		       G2H_LEN_DW_SCHED_CONTEXT_MODE_SET +
		       G2H_LEN_DW_DEREGISTER_CONTEXT, 2);
}

static void xe_guc_exec_queue_trigger_cleanup(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);

	/** to wakeup xe_wait_user_fence ioctl if exec queue is reset */
	wake_up_all(&xe->ufence_wq);

	if (xe_exec_queue_is_lr(q))
		queue_work(guc_to_gt(guc)->ordered_wq, &q->guc->lr_tdr);
	else
		xe_sched_tdr_queue_imm(&q->guc->sched);
}

/**
 * xe_guc_submit_wedge() - Wedge GuC submission
 * @guc: the GuC object
 *
 * Save exec queue's registered with GuC state by taking a ref to each queue.
 * Register a DRMM handler to drop refs upon driver unload.
 */
void xe_guc_submit_wedge(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_exec_queue *q;
	unsigned long index;
	int err;

	xe_gt_assert(guc_to_gt(guc), guc_to_xe(guc)->wedged.mode);

	err = devm_add_action_or_reset(guc_to_xe(guc)->drm.dev,
				       guc_submit_wedged_fini, guc);
	if (err) {
		xe_gt_err(gt, "Failed to register clean-up on wedged.mode=2; "
			  "Although device is wedged.\n");
		return;
	}

	mutex_lock(&guc->submission_state.lock);
	xa_for_each(&guc->submission_state.exec_queue_lookup, index, q)
		if (xe_exec_queue_get_unless_zero(q))
			set_exec_queue_wedged(q);
	mutex_unlock(&guc->submission_state.lock);
}

static bool guc_submit_hint_wedged(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);

	if (xe->wedged.mode != 2)
		return false;

	if (xe_device_wedged(xe))
		return true;

	xe_device_declare_wedged(xe);

	return true;
}

static void xe_guc_exec_queue_lr_cleanup(struct work_struct *w)
{
	struct xe_guc_exec_queue *ge =
		container_of(w, struct xe_guc_exec_queue, lr_tdr);
	struct xe_exec_queue *q = ge->q;
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_gpu_scheduler *sched = &ge->sched;
	bool wedged;

	xe_gt_assert(guc_to_gt(guc), xe_exec_queue_is_lr(q));
	trace_xe_exec_queue_lr_cleanup(q);

	wedged = guc_submit_hint_wedged(exec_queue_to_guc(q));

	/* Kill the run_job / process_msg entry points */
	xe_sched_submission_stop(sched);

	/*
	 * Engine state now mostly stable, disable scheduling / deregister if
	 * needed. This cleanup routine might be called multiple times, where
	 * the actual async engine deregister drops the final engine ref.
	 * Calling disable_scheduling_deregister will mark the engine as
	 * destroyed and fire off the CT requests to disable scheduling /
	 * deregister, which we only want to do once. We also don't want to mark
	 * the engine as pending_disable again as this may race with the
	 * xe_guc_deregister_done_handler() which treats it as an unexpected
	 * state.
	 */
	if (!wedged && exec_queue_registered(q) && !exec_queue_destroyed(q)) {
		struct xe_guc *guc = exec_queue_to_guc(q);
		int ret;

		set_exec_queue_banned(q);
		disable_scheduling_deregister(guc, q);

		/*
		 * Must wait for scheduling to be disabled before signalling
		 * any fences, if GT broken the GT reset code should signal us.
		 */
		ret = wait_event_timeout(guc->ct.wq,
					 !exec_queue_pending_disable(q) ||
					 xe_guc_read_stopped(guc), HZ * 5);
		if (!ret) {
			xe_gt_warn(q->gt, "Schedule disable failed to respond, guc_id=%d\n",
				   q->guc->id);
			xe_devcoredump(q, NULL, "Schedule disable failed to respond, guc_id=%d\n",
				       q->guc->id);
			xe_sched_submission_start(sched);
			xe_gt_reset_async(q->gt);
			return;
		}
	}

	if (!exec_queue_killed(q) && !xe_lrc_ring_is_idle(q->lrc[0]))
		xe_devcoredump(q, NULL, "LR job cleanup, guc_id=%d", q->guc->id);

	xe_sched_submission_start(sched);
}

#define ADJUST_FIVE_PERCENT(__t)	mul_u64_u32_div(__t, 105, 100)

static bool check_timeout(struct xe_exec_queue *q, struct xe_sched_job *job)
{
	struct xe_gt *gt = guc_to_gt(exec_queue_to_guc(q));
	u32 ctx_timestamp, ctx_job_timestamp;
	u32 timeout_ms = q->sched_props.job_timeout_ms;
	u32 diff;
	u64 running_time_ms;

	if (!xe_sched_job_started(job)) {
		xe_gt_warn(gt, "Check job timeout: seqno=%u, lrc_seqno=%u, guc_id=%d, not started",
			   xe_sched_job_seqno(job), xe_sched_job_lrc_seqno(job),
			   q->guc->id);

		return xe_sched_invalidate_job(job, 2);
	}

	ctx_timestamp = xe_lrc_ctx_timestamp(q->lrc[0]);
	ctx_job_timestamp = xe_lrc_ctx_job_timestamp(q->lrc[0]);

	/*
	 * Counter wraps at ~223s at the usual 19.2MHz, be paranoid catch
	 * possible overflows with a high timeout.
	 */
	xe_gt_assert(gt, timeout_ms < 100 * MSEC_PER_SEC);

	if (ctx_timestamp < ctx_job_timestamp)
		diff = ctx_timestamp + U32_MAX - ctx_job_timestamp;
	else
		diff = ctx_timestamp - ctx_job_timestamp;

	/*
	 * Ensure timeout is within 5% to account for an GuC scheduling latency
	 */
	running_time_ms =
		ADJUST_FIVE_PERCENT(xe_gt_clock_interval_to_ms(gt, diff));

	xe_gt_dbg(gt,
		  "Check job timeout: seqno=%u, lrc_seqno=%u, guc_id=%d, running_time_ms=%llu, timeout_ms=%u, diff=0x%08x",
		  xe_sched_job_seqno(job), xe_sched_job_lrc_seqno(job),
		  q->guc->id, running_time_ms, timeout_ms, diff);

	return running_time_ms >= timeout_ms;
}

static void enable_scheduling(struct xe_exec_queue *q)
{
	MAKE_SCHED_CONTEXT_ACTION(q, ENABLE);
	struct xe_guc *guc = exec_queue_to_guc(q);
	int ret;

	xe_gt_assert(guc_to_gt(guc), !exec_queue_destroyed(q));
	xe_gt_assert(guc_to_gt(guc), exec_queue_registered(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_disable(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_enable(q));

	set_exec_queue_pending_enable(q);
	set_exec_queue_enabled(q);
	trace_xe_exec_queue_scheduling_enable(q);

	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
		       G2H_LEN_DW_SCHED_CONTEXT_MODE_SET, 1);

	ret = wait_event_timeout(guc->ct.wq,
				 !exec_queue_pending_enable(q) ||
				 xe_guc_read_stopped(guc), HZ * 5);
	if (!ret || xe_guc_read_stopped(guc)) {
		xe_gt_warn(guc_to_gt(guc), "Schedule enable failed to respond");
		set_exec_queue_banned(q);
		xe_gt_reset_async(q->gt);
		xe_sched_tdr_queue_imm(&q->guc->sched);
	}
}

static void disable_scheduling(struct xe_exec_queue *q, bool immediate)
{
	MAKE_SCHED_CONTEXT_ACTION(q, DISABLE);
	struct xe_guc *guc = exec_queue_to_guc(q);

	xe_gt_assert(guc_to_gt(guc), !exec_queue_destroyed(q));
	xe_gt_assert(guc_to_gt(guc), exec_queue_registered(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_disable(q));

	if (immediate)
		set_min_preemption_timeout(guc, q);
	clear_exec_queue_enabled(q);
	set_exec_queue_pending_disable(q);
	trace_xe_exec_queue_scheduling_disable(q);

	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
		       G2H_LEN_DW_SCHED_CONTEXT_MODE_SET, 1);
}

static void __deregister_exec_queue(struct xe_guc *guc, struct xe_exec_queue *q)
{
	u32 action[] = {
		XE_GUC_ACTION_DEREGISTER_CONTEXT,
		q->guc->id,
	};

	xe_gt_assert(guc_to_gt(guc), !exec_queue_destroyed(q));
	xe_gt_assert(guc_to_gt(guc), exec_queue_registered(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_enable(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_disable(q));

	set_exec_queue_destroyed(q);
	trace_xe_exec_queue_deregister(q);

	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
		       G2H_LEN_DW_DEREGISTER_CONTEXT, 1);
}

static enum drm_gpu_sched_stat
guc_exec_queue_timedout_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);
	struct xe_sched_job *tmp_job;
	struct xe_exec_queue *q = job->q;
	struct xe_gpu_scheduler *sched = &q->guc->sched;
	struct xe_guc *guc = exec_queue_to_guc(q);
	const char *process_name = "no process";
	struct xe_device *xe = guc_to_xe(guc);
	unsigned int fw_ref;
	int err = -ETIME;
	pid_t pid = -1;
	int i = 0;
	bool wedged, skip_timeout_check;

	/*
	 * TDR has fired before free job worker. Common if exec queue
	 * immediately closed after last fence signaled. Add back to pending
	 * list so job can be freed and kick scheduler ensuring free job is not
	 * lost.
	 */
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &job->fence->flags)) {
		xe_sched_add_pending_job(sched, job);
		xe_sched_submission_start(sched);

		return DRM_GPU_SCHED_STAT_NOMINAL;
	}

	/* Kill the run_job entry point */
	xe_sched_submission_stop(sched);

	/* Must check all state after stopping scheduler */
	skip_timeout_check = exec_queue_reset(q) ||
		exec_queue_killed_or_banned_or_wedged(q) ||
		exec_queue_destroyed(q);

	/*
	 * If devcoredump not captured and GuC capture for the job is not ready
	 * do manual capture first and decide later if we need to use it
	 */
	if (!exec_queue_killed(q) && !xe->devcoredump.captured &&
	    !xe_guc_capture_get_matching_and_lock(q)) {
		/* take force wake before engine register manual capture */
		fw_ref = xe_force_wake_get(gt_to_fw(q->gt), XE_FORCEWAKE_ALL);
		if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL))
			xe_gt_info(q->gt, "failed to get forcewake for coredump capture\n");

		xe_engine_snapshot_capture_for_queue(q);

		xe_force_wake_put(gt_to_fw(q->gt), fw_ref);
	}

	/*
	 * XXX: Sampling timeout doesn't work in wedged mode as we have to
	 * modify scheduling state to read timestamp. We could read the
	 * timestamp from a register to accumulate current running time but this
	 * doesn't work for SRIOV. For now assuming timeouts in wedged mode are
	 * genuine timeouts.
	 */
	wedged = guc_submit_hint_wedged(exec_queue_to_guc(q));

	/* Engine state now stable, disable scheduling to check timestamp */
	if (!wedged && exec_queue_registered(q)) {
		int ret;

		if (exec_queue_reset(q))
			err = -EIO;

		if (!exec_queue_destroyed(q)) {
			/*
			 * Wait for any pending G2H to flush out before
			 * modifying state
			 */
			ret = wait_event_timeout(guc->ct.wq,
						 (!exec_queue_pending_enable(q) &&
						  !exec_queue_pending_disable(q)) ||
						 xe_guc_read_stopped(guc), HZ * 5);
			if (!ret || xe_guc_read_stopped(guc))
				goto trigger_reset;

			/*
			 * Flag communicates to G2H handler that schedule
			 * disable originated from a timeout check. The G2H then
			 * avoid triggering cleanup or deregistering the exec
			 * queue.
			 */
			set_exec_queue_check_timeout(q);
			disable_scheduling(q, skip_timeout_check);
		}

		/*
		 * Must wait for scheduling to be disabled before signalling
		 * any fences, if GT broken the GT reset code should signal us.
		 *
		 * FIXME: Tests can generate a ton of 0x6000 (IOMMU CAT fault
		 * error) messages which can cause the schedule disable to get
		 * lost. If this occurs, trigger a GT reset to recover.
		 */
		smp_rmb();
		ret = wait_event_timeout(guc->ct.wq,
					 !exec_queue_pending_disable(q) ||
					 xe_guc_read_stopped(guc), HZ * 5);
		if (!ret || xe_guc_read_stopped(guc)) {
trigger_reset:
			if (!ret)
				xe_gt_warn(guc_to_gt(guc),
					   "Schedule disable failed to respond, guc_id=%d",
					   q->guc->id);
			xe_devcoredump(q, job,
				       "Schedule disable failed to respond, guc_id=%d, ret=%d, guc_read=%d",
				       q->guc->id, ret, xe_guc_read_stopped(guc));
			set_exec_queue_extra_ref(q);
			xe_exec_queue_get(q);	/* GT reset owns this */
			set_exec_queue_banned(q);
			xe_gt_reset_async(q->gt);
			xe_sched_tdr_queue_imm(sched);
			goto rearm;
		}
	}

	/*
	 * Check if job is actually timed out, if so restart job execution and TDR
	 */
	if (!wedged && !skip_timeout_check && !check_timeout(q, job) &&
	    !exec_queue_reset(q) && exec_queue_registered(q)) {
		clear_exec_queue_check_timeout(q);
		goto sched_enable;
	}

	if (q->vm && q->vm->xef) {
		process_name = q->vm->xef->process_name;
		pid = q->vm->xef->pid;
	}
	xe_gt_notice(guc_to_gt(guc), "Timedout job: seqno=%u, lrc_seqno=%u, guc_id=%d, flags=0x%lx in %s [%d]",
		     xe_sched_job_seqno(job), xe_sched_job_lrc_seqno(job),
		     q->guc->id, q->flags, process_name, pid);

	trace_xe_sched_job_timedout(job);

	if (!exec_queue_killed(q))
		xe_devcoredump(q, job,
			       "Timedout job - seqno=%u, lrc_seqno=%u, guc_id=%d, flags=0x%lx",
			       xe_sched_job_seqno(job), xe_sched_job_lrc_seqno(job),
			       q->guc->id, q->flags);

	/*
	 * Kernel jobs should never fail, nor should VM jobs if they do
	 * somethings has gone wrong and the GT needs a reset
	 */
	xe_gt_WARN(q->gt, q->flags & EXEC_QUEUE_FLAG_KERNEL,
		   "Kernel-submitted job timed out\n");
	xe_gt_WARN(q->gt, q->flags & EXEC_QUEUE_FLAG_VM && !exec_queue_killed(q),
		   "VM job timed out on non-killed execqueue\n");
	if (!wedged && (q->flags & EXEC_QUEUE_FLAG_KERNEL ||
			(q->flags & EXEC_QUEUE_FLAG_VM && !exec_queue_killed(q)))) {
		if (!xe_sched_invalidate_job(job, 2)) {
			clear_exec_queue_check_timeout(q);
			xe_gt_reset_async(q->gt);
			goto rearm;
		}
	}

	/* Finish cleaning up exec queue via deregister */
	set_exec_queue_banned(q);
	if (!wedged && exec_queue_registered(q) && !exec_queue_destroyed(q)) {
		set_exec_queue_extra_ref(q);
		xe_exec_queue_get(q);
		__deregister_exec_queue(guc, q);
	}

	/* Stop fence signaling */
	xe_hw_fence_irq_stop(q->fence_irq);

	/*
	 * Fence state now stable, stop / start scheduler which cleans up any
	 * fences that are complete
	 */
	xe_sched_add_pending_job(sched, job);
	xe_sched_submission_start(sched);

	xe_guc_exec_queue_trigger_cleanup(q);

	/* Mark all outstanding jobs as bad, thus completing them */
	spin_lock(&sched->base.job_list_lock);
	list_for_each_entry(tmp_job, &sched->base.pending_list, drm.list)
		xe_sched_job_set_error(tmp_job, !i++ ? err : -ECANCELED);
	spin_unlock(&sched->base.job_list_lock);

	/* Start fence signaling */
	xe_hw_fence_irq_start(q->fence_irq);

	return DRM_GPU_SCHED_STAT_NOMINAL;

sched_enable:
	enable_scheduling(q);
rearm:
	/*
	 * XXX: Ideally want to adjust timeout based on current execution time
	 * but there is not currently an easy way to do in DRM scheduler. With
	 * some thought, do this in a follow up.
	 */
	xe_sched_add_pending_job(sched, job);
	xe_sched_submission_start(sched);

	return DRM_GPU_SCHED_STAT_NOMINAL;
}

static void __guc_exec_queue_fini_async(struct work_struct *w)
{
	struct xe_guc_exec_queue *ge =
		container_of(w, struct xe_guc_exec_queue, fini_async);
	struct xe_exec_queue *q = ge->q;
	struct xe_guc *guc = exec_queue_to_guc(q);

	xe_pm_runtime_get(guc_to_xe(guc));
	trace_xe_exec_queue_destroy(q);

	release_guc_id(guc, q);
	if (xe_exec_queue_is_lr(q))
		cancel_work_sync(&ge->lr_tdr);
	/* Confirm no work left behind accessing device structures */
	cancel_delayed_work_sync(&ge->sched.base.work_tdr);
	xe_sched_entity_fini(&ge->entity);
	xe_sched_fini(&ge->sched);

	kfree(ge);
	xe_exec_queue_fini(q);
	xe_pm_runtime_put(guc_to_xe(guc));
}

static void guc_exec_queue_fini_async(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);

	INIT_WORK(&q->guc->fini_async, __guc_exec_queue_fini_async);

	/* We must block on kernel engines so slabs are empty on driver unload */
	if (q->flags & EXEC_QUEUE_FLAG_PERMANENT || exec_queue_wedged(q))
		__guc_exec_queue_fini_async(&q->guc->fini_async);
	else
		queue_work(xe->destroy_wq, &q->guc->fini_async);
}

static void __guc_exec_queue_fini(struct xe_guc *guc, struct xe_exec_queue *q)
{
	/*
	 * Might be done from within the GPU scheduler, need to do async as we
	 * fini the scheduler when the engine is fini'd, the scheduler can't
	 * complete fini within itself (circular dependency). Async resolves
	 * this we and don't really care when everything is fini'd, just that it
	 * is.
	 */
	guc_exec_queue_fini_async(q);
}

static void __guc_exec_queue_process_msg_cleanup(struct xe_sched_msg *msg)
{
	struct xe_exec_queue *q = msg->private_data;
	struct xe_guc *guc = exec_queue_to_guc(q);

	xe_gt_assert(guc_to_gt(guc), !(q->flags & EXEC_QUEUE_FLAG_PERMANENT));
	trace_xe_exec_queue_cleanup_entity(q);

	if (exec_queue_registered(q))
		disable_scheduling_deregister(guc, q);
	else
		__guc_exec_queue_fini(guc, q);
}

static bool guc_exec_queue_allowed_to_change_state(struct xe_exec_queue *q)
{
	return !exec_queue_killed_or_banned_or_wedged(q) && exec_queue_registered(q);
}

static void __guc_exec_queue_process_msg_set_sched_props(struct xe_sched_msg *msg)
{
	struct xe_exec_queue *q = msg->private_data;
	struct xe_guc *guc = exec_queue_to_guc(q);

	if (guc_exec_queue_allowed_to_change_state(q))
		init_policies(guc, q);
	kfree(msg);
}

static void __suspend_fence_signal(struct xe_exec_queue *q)
{
	if (!q->guc->suspend_pending)
		return;

	WRITE_ONCE(q->guc->suspend_pending, false);
	wake_up(&q->guc->suspend_wait);
}

static void suspend_fence_signal(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);

	xe_gt_assert(guc_to_gt(guc), exec_queue_suspended(q) || exec_queue_killed(q) ||
		     xe_guc_read_stopped(guc));
	xe_gt_assert(guc_to_gt(guc), q->guc->suspend_pending);

	__suspend_fence_signal(q);
}

static void __guc_exec_queue_process_msg_suspend(struct xe_sched_msg *msg)
{
	struct xe_exec_queue *q = msg->private_data;
	struct xe_guc *guc = exec_queue_to_guc(q);

	if (guc_exec_queue_allowed_to_change_state(q) && !exec_queue_suspended(q) &&
	    exec_queue_enabled(q)) {
		wait_event(guc->ct.wq, (q->guc->resume_time != RESUME_PENDING ||
			   xe_guc_read_stopped(guc)) && !exec_queue_pending_disable(q));

		if (!xe_guc_read_stopped(guc)) {
			s64 since_resume_ms =
				ktime_ms_delta(ktime_get(),
					       q->guc->resume_time);
			s64 wait_ms = q->vm->preempt.min_run_period_ms -
				since_resume_ms;

			if (wait_ms > 0 && q->guc->resume_time)
				msleep(wait_ms);

			set_exec_queue_suspended(q);
			disable_scheduling(q, false);
		}
	} else if (q->guc->suspend_pending) {
		set_exec_queue_suspended(q);
		suspend_fence_signal(q);
	}
}

static void __guc_exec_queue_process_msg_resume(struct xe_sched_msg *msg)
{
	struct xe_exec_queue *q = msg->private_data;

	if (guc_exec_queue_allowed_to_change_state(q)) {
		clear_exec_queue_suspended(q);
		if (!exec_queue_enabled(q)) {
			q->guc->resume_time = RESUME_PENDING;
			enable_scheduling(q);
		}
	} else {
		clear_exec_queue_suspended(q);
	}
}

#define CLEANUP		1	/* Non-zero values to catch uninitialized msg */
#define SET_SCHED_PROPS	2
#define SUSPEND		3
#define RESUME		4
#define OPCODE_MASK	0xf
#define MSG_LOCKED	BIT(8)

static void guc_exec_queue_process_msg(struct xe_sched_msg *msg)
{
	struct xe_device *xe = guc_to_xe(exec_queue_to_guc(msg->private_data));

	trace_xe_sched_msg_recv(msg);

	switch (msg->opcode) {
	case CLEANUP:
		__guc_exec_queue_process_msg_cleanup(msg);
		break;
	case SET_SCHED_PROPS:
		__guc_exec_queue_process_msg_set_sched_props(msg);
		break;
	case SUSPEND:
		__guc_exec_queue_process_msg_suspend(msg);
		break;
	case RESUME:
		__guc_exec_queue_process_msg_resume(msg);
		break;
	default:
		XE_WARN_ON("Unknown message type");
	}

	xe_pm_runtime_put(xe);
}

static const struct drm_sched_backend_ops drm_sched_ops = {
	.run_job = guc_exec_queue_run_job,
	.free_job = guc_exec_queue_free_job,
	.timedout_job = guc_exec_queue_timedout_job,
};

static const struct xe_sched_backend_ops xe_sched_ops = {
	.process_msg = guc_exec_queue_process_msg,
};

static int guc_exec_queue_init(struct xe_exec_queue *q)
{
	struct xe_gpu_scheduler *sched;
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_guc_exec_queue *ge;
	long timeout;
	int err, i;

	xe_gt_assert(guc_to_gt(guc), xe_device_uc_enabled(guc_to_xe(guc)));

	ge = kzalloc(sizeof(*ge), GFP_KERNEL);
	if (!ge)
		return -ENOMEM;

	q->guc = ge;
	ge->q = q;
	init_waitqueue_head(&ge->suspend_wait);

	for (i = 0; i < MAX_STATIC_MSG_TYPE; ++i)
		INIT_LIST_HEAD(&ge->static_msgs[i].link);

	timeout = (q->vm && xe_vm_in_lr_mode(q->vm)) ? MAX_SCHEDULE_TIMEOUT :
		  msecs_to_jiffies(q->sched_props.job_timeout_ms);
	err = xe_sched_init(&ge->sched, &drm_sched_ops, &xe_sched_ops,
			    NULL, q->lrc[0]->ring.size / MAX_JOB_SIZE_BYTES, 64,
			    timeout, guc_to_gt(guc)->ordered_wq, NULL,
			    q->name, gt_to_xe(q->gt)->drm.dev);
	if (err)
		goto err_free;

	sched = &ge->sched;
	err = xe_sched_entity_init(&ge->entity, sched);
	if (err)
		goto err_sched;

	if (xe_exec_queue_is_lr(q))
		INIT_WORK(&q->guc->lr_tdr, xe_guc_exec_queue_lr_cleanup);

	mutex_lock(&guc->submission_state.lock);

	err = alloc_guc_id(guc, q);
	if (err)
		goto err_entity;

	q->entity = &ge->entity;

	if (xe_guc_read_stopped(guc))
		xe_sched_stop(sched);

	mutex_unlock(&guc->submission_state.lock);

	xe_exec_queue_assign_name(q, q->guc->id);

	trace_xe_exec_queue_create(q);

	return 0;

err_entity:
	mutex_unlock(&guc->submission_state.lock);
	xe_sched_entity_fini(&ge->entity);
err_sched:
	xe_sched_fini(&ge->sched);
err_free:
	kfree(ge);

	return err;
}

static void guc_exec_queue_kill(struct xe_exec_queue *q)
{
	trace_xe_exec_queue_kill(q);
	set_exec_queue_killed(q);
	__suspend_fence_signal(q);
	xe_guc_exec_queue_trigger_cleanup(q);
}

static void guc_exec_queue_add_msg(struct xe_exec_queue *q, struct xe_sched_msg *msg,
				   u32 opcode)
{
	xe_pm_runtime_get_noresume(guc_to_xe(exec_queue_to_guc(q)));

	INIT_LIST_HEAD(&msg->link);
	msg->opcode = opcode & OPCODE_MASK;
	msg->private_data = q;

	trace_xe_sched_msg_add(msg);
	if (opcode & MSG_LOCKED)
		xe_sched_add_msg_locked(&q->guc->sched, msg);
	else
		xe_sched_add_msg(&q->guc->sched, msg);
}

static bool guc_exec_queue_try_add_msg(struct xe_exec_queue *q,
				       struct xe_sched_msg *msg,
				       u32 opcode)
{
	if (!list_empty(&msg->link))
		return false;

	guc_exec_queue_add_msg(q, msg, opcode | MSG_LOCKED);

	return true;
}

#define STATIC_MSG_CLEANUP	0
#define STATIC_MSG_SUSPEND	1
#define STATIC_MSG_RESUME	2
static void guc_exec_queue_fini(struct xe_exec_queue *q)
{
	struct xe_sched_msg *msg = q->guc->static_msgs + STATIC_MSG_CLEANUP;

	if (!(q->flags & EXEC_QUEUE_FLAG_PERMANENT) && !exec_queue_wedged(q))
		guc_exec_queue_add_msg(q, msg, CLEANUP);
	else
		__guc_exec_queue_fini(exec_queue_to_guc(q), q);
}

static int guc_exec_queue_set_priority(struct xe_exec_queue *q,
				       enum xe_exec_queue_priority priority)
{
	struct xe_sched_msg *msg;

	if (q->sched_props.priority == priority ||
	    exec_queue_killed_or_banned_or_wedged(q))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	q->sched_props.priority = priority;
	guc_exec_queue_add_msg(q, msg, SET_SCHED_PROPS);

	return 0;
}

static int guc_exec_queue_set_timeslice(struct xe_exec_queue *q, u32 timeslice_us)
{
	struct xe_sched_msg *msg;

	if (q->sched_props.timeslice_us == timeslice_us ||
	    exec_queue_killed_or_banned_or_wedged(q))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	q->sched_props.timeslice_us = timeslice_us;
	guc_exec_queue_add_msg(q, msg, SET_SCHED_PROPS);

	return 0;
}

static int guc_exec_queue_set_preempt_timeout(struct xe_exec_queue *q,
					      u32 preempt_timeout_us)
{
	struct xe_sched_msg *msg;

	if (q->sched_props.preempt_timeout_us == preempt_timeout_us ||
	    exec_queue_killed_or_banned_or_wedged(q))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	q->sched_props.preempt_timeout_us = preempt_timeout_us;
	guc_exec_queue_add_msg(q, msg, SET_SCHED_PROPS);

	return 0;
}

static int guc_exec_queue_suspend(struct xe_exec_queue *q)
{
	struct xe_gpu_scheduler *sched = &q->guc->sched;
	struct xe_sched_msg *msg = q->guc->static_msgs + STATIC_MSG_SUSPEND;

	if (exec_queue_killed_or_banned_or_wedged(q))
		return -EINVAL;

	xe_sched_msg_lock(sched);
	if (guc_exec_queue_try_add_msg(q, msg, SUSPEND))
		q->guc->suspend_pending = true;
	xe_sched_msg_unlock(sched);

	return 0;
}

static int guc_exec_queue_suspend_wait(struct xe_exec_queue *q)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	int ret;

	/*
	 * Likely don't need to check exec_queue_killed() as we clear
	 * suspend_pending upon kill but to be paranoid but races in which
	 * suspend_pending is set after kill also check kill here.
	 */
	ret = wait_event_interruptible_timeout(q->guc->suspend_wait,
					       !READ_ONCE(q->guc->suspend_pending) ||
					       exec_queue_killed(q) ||
					       xe_guc_read_stopped(guc),
					       HZ * 5);

	if (!ret) {
		xe_gt_warn(guc_to_gt(guc),
			   "Suspend fence, guc_id=%d, failed to respond",
			   q->guc->id);
		/* XXX: Trigger GT reset? */
		return -ETIME;
	}

	return ret < 0 ? ret : 0;
}

static void guc_exec_queue_resume(struct xe_exec_queue *q)
{
	struct xe_gpu_scheduler *sched = &q->guc->sched;
	struct xe_sched_msg *msg = q->guc->static_msgs + STATIC_MSG_RESUME;
	struct xe_guc *guc = exec_queue_to_guc(q);

	xe_gt_assert(guc_to_gt(guc), !q->guc->suspend_pending);

	xe_sched_msg_lock(sched);
	guc_exec_queue_try_add_msg(q, msg, RESUME);
	xe_sched_msg_unlock(sched);
}

static bool guc_exec_queue_reset_status(struct xe_exec_queue *q)
{
	return exec_queue_reset(q) || exec_queue_killed_or_banned_or_wedged(q);
}

/*
 * All of these functions are an abstraction layer which other parts of XE can
 * use to trap into the GuC backend. All of these functions, aside from init,
 * really shouldn't do much other than trap into the DRM scheduler which
 * synchronizes these operations.
 */
static const struct xe_exec_queue_ops guc_exec_queue_ops = {
	.init = guc_exec_queue_init,
	.kill = guc_exec_queue_kill,
	.fini = guc_exec_queue_fini,
	.set_priority = guc_exec_queue_set_priority,
	.set_timeslice = guc_exec_queue_set_timeslice,
	.set_preempt_timeout = guc_exec_queue_set_preempt_timeout,
	.suspend = guc_exec_queue_suspend,
	.suspend_wait = guc_exec_queue_suspend_wait,
	.resume = guc_exec_queue_resume,
	.reset_status = guc_exec_queue_reset_status,
};

static void guc_exec_queue_stop(struct xe_guc *guc, struct xe_exec_queue *q)
{
	struct xe_gpu_scheduler *sched = &q->guc->sched;

	/* Stop scheduling + flush any DRM scheduler operations */
	xe_sched_submission_stop(sched);

	/* Clean up lost G2H + reset engine state */
	if (exec_queue_registered(q)) {
		if (exec_queue_extra_ref(q) || xe_exec_queue_is_lr(q))
			xe_exec_queue_put(q);
		else if (exec_queue_destroyed(q))
			__guc_exec_queue_fini(guc, q);
	}
	if (q->guc->suspend_pending) {
		set_exec_queue_suspended(q);
		suspend_fence_signal(q);
	}
	atomic_and(EXEC_QUEUE_STATE_WEDGED | EXEC_QUEUE_STATE_BANNED |
		   EXEC_QUEUE_STATE_KILLED | EXEC_QUEUE_STATE_DESTROYED |
		   EXEC_QUEUE_STATE_SUSPENDED,
		   &q->guc->state);
	q->guc->resume_time = 0;
	trace_xe_exec_queue_stop(q);

	/*
	 * Ban any engine (aside from kernel and engines used for VM ops) with a
	 * started but not complete job or if a job has gone through a GT reset
	 * more than twice.
	 */
	if (!(q->flags & (EXEC_QUEUE_FLAG_KERNEL | EXEC_QUEUE_FLAG_VM))) {
		struct xe_sched_job *job = xe_sched_first_pending_job(sched);
		bool ban = false;

		if (job) {
			if ((xe_sched_job_started(job) &&
			    !xe_sched_job_completed(job)) ||
			    xe_sched_invalidate_job(job, 2)) {
				trace_xe_sched_job_ban(job);
				ban = true;
			}
		} else if (xe_exec_queue_is_lr(q) &&
			   !xe_lrc_ring_is_idle(q->lrc[0])) {
			ban = true;
		}

		if (ban) {
			set_exec_queue_banned(q);
			xe_guc_exec_queue_trigger_cleanup(q);
		}
	}
}

int xe_guc_submit_reset_prepare(struct xe_guc *guc)
{
	int ret;

	/*
	 * Using an atomic here rather than submission_state.lock as this
	 * function can be called while holding the CT lock (engine reset
	 * failure). submission_state.lock needs the CT lock to resubmit jobs.
	 * Atomic is not ideal, but it works to prevent against concurrent reset
	 * and releasing any TDRs waiting on guc->submission_state.stopped.
	 */
	ret = atomic_fetch_or(1, &guc->submission_state.stopped);
	smp_wmb();
	wake_up_all(&guc->ct.wq);

	return ret;
}

void xe_guc_submit_reset_wait(struct xe_guc *guc)
{
	wait_event(guc->ct.wq, xe_device_wedged(guc_to_xe(guc)) ||
		   !xe_guc_read_stopped(guc));
}

void xe_guc_submit_stop(struct xe_guc *guc)
{
	struct xe_exec_queue *q;
	unsigned long index;

	xe_gt_assert(guc_to_gt(guc), xe_guc_read_stopped(guc) == 1);

	mutex_lock(&guc->submission_state.lock);

	xa_for_each(&guc->submission_state.exec_queue_lookup, index, q) {
		/* Prevent redundant attempts to stop parallel queues */
		if (q->guc->id != index)
			continue;

		guc_exec_queue_stop(guc, q);
	}

	mutex_unlock(&guc->submission_state.lock);

	/*
	 * No one can enter the backend at this point, aside from new engine
	 * creation which is protected by guc->submission_state.lock.
	 */

}

static void guc_exec_queue_start(struct xe_exec_queue *q)
{
	struct xe_gpu_scheduler *sched = &q->guc->sched;

	if (!exec_queue_killed_or_banned_or_wedged(q)) {
		int i;

		trace_xe_exec_queue_resubmit(q);
		for (i = 0; i < q->width; ++i)
			xe_lrc_set_ring_head(q->lrc[i], q->lrc[i]->ring.tail);
		xe_sched_resubmit_jobs(sched);
	}

	xe_sched_submission_start(sched);
	xe_sched_submission_resume_tdr(sched);
}

int xe_guc_submit_start(struct xe_guc *guc)
{
	struct xe_exec_queue *q;
	unsigned long index;

	xe_gt_assert(guc_to_gt(guc), xe_guc_read_stopped(guc) == 1);

	mutex_lock(&guc->submission_state.lock);
	atomic_dec(&guc->submission_state.stopped);
	xa_for_each(&guc->submission_state.exec_queue_lookup, index, q) {
		/* Prevent redundant attempts to start parallel queues */
		if (q->guc->id != index)
			continue;

		guc_exec_queue_start(q);
	}
	mutex_unlock(&guc->submission_state.lock);

	wake_up_all(&guc->ct.wq);

	return 0;
}

static struct xe_exec_queue *
g2h_exec_queue_lookup(struct xe_guc *guc, u32 guc_id)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_exec_queue *q;

	if (unlikely(guc_id >= GUC_ID_MAX)) {
		xe_gt_err(gt, "Invalid guc_id %u\n", guc_id);
		return NULL;
	}

	q = xa_load(&guc->submission_state.exec_queue_lookup, guc_id);
	if (unlikely(!q)) {
		xe_gt_err(gt, "Not engine present for guc_id %u\n", guc_id);
		return NULL;
	}

	xe_gt_assert(guc_to_gt(guc), guc_id >= q->guc->id);
	xe_gt_assert(guc_to_gt(guc), guc_id < (q->guc->id + q->width));

	return q;
}

static void deregister_exec_queue(struct xe_guc *guc, struct xe_exec_queue *q)
{
	u32 action[] = {
		XE_GUC_ACTION_DEREGISTER_CONTEXT,
		q->guc->id,
	};

	xe_gt_assert(guc_to_gt(guc), exec_queue_destroyed(q));
	xe_gt_assert(guc_to_gt(guc), exec_queue_registered(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_disable(q));
	xe_gt_assert(guc_to_gt(guc), !exec_queue_pending_enable(q));

	trace_xe_exec_queue_deregister(q);

	xe_guc_ct_send_g2h_handler(&guc->ct, action, ARRAY_SIZE(action));
}

static void handle_sched_done(struct xe_guc *guc, struct xe_exec_queue *q,
			      u32 runnable_state)
{
	trace_xe_exec_queue_scheduling_done(q);

	if (runnable_state == 1) {
		xe_gt_assert(guc_to_gt(guc), exec_queue_pending_enable(q));

		q->guc->resume_time = ktime_get();
		clear_exec_queue_pending_enable(q);
		smp_wmb();
		wake_up_all(&guc->ct.wq);
	} else {
		bool check_timeout = exec_queue_check_timeout(q);

		xe_gt_assert(guc_to_gt(guc), runnable_state == 0);
		xe_gt_assert(guc_to_gt(guc), exec_queue_pending_disable(q));

		if (q->guc->suspend_pending) {
			suspend_fence_signal(q);
			clear_exec_queue_pending_disable(q);
		} else {
			if (exec_queue_banned(q) || check_timeout) {
				smp_wmb();
				wake_up_all(&guc->ct.wq);
			}
			if (!check_timeout && exec_queue_destroyed(q)) {
				/*
				 * Make sure to clear the pending_disable only
				 * after sampling the destroyed state. We want
				 * to ensure we don't trigger the unregister too
				 * early with something intending to only
				 * disable scheduling. The caller doing the
				 * destroy must wait for an ongoing
				 * pending_disable before marking as destroyed.
				 */
				clear_exec_queue_pending_disable(q);
				deregister_exec_queue(guc, q);
			} else {
				clear_exec_queue_pending_disable(q);
			}
		}
	}
}

int xe_guc_sched_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_exec_queue *q;
	u32 guc_id, runnable_state;

	if (unlikely(len < 2))
		return -EPROTO;

	guc_id = msg[0];
	runnable_state = msg[1];

	q = g2h_exec_queue_lookup(guc, guc_id);
	if (unlikely(!q))
		return -EPROTO;

	if (unlikely(!exec_queue_pending_enable(q) &&
		     !exec_queue_pending_disable(q))) {
		xe_gt_err(guc_to_gt(guc),
			  "SCHED_DONE: Unexpected engine state 0x%04x, guc_id=%d, runnable_state=%u",
			  atomic_read(&q->guc->state), q->guc->id,
			  runnable_state);
		return -EPROTO;
	}

	handle_sched_done(guc, q, runnable_state);

	return 0;
}

static void handle_deregister_done(struct xe_guc *guc, struct xe_exec_queue *q)
{
	trace_xe_exec_queue_deregister_done(q);

	clear_exec_queue_registered(q);

	if (exec_queue_extra_ref(q) || xe_exec_queue_is_lr(q))
		xe_exec_queue_put(q);
	else
		__guc_exec_queue_fini(guc, q);
}

int xe_guc_deregister_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_exec_queue *q;
	u32 guc_id;

	if (unlikely(len < 1))
		return -EPROTO;

	guc_id = msg[0];

	q = g2h_exec_queue_lookup(guc, guc_id);
	if (unlikely(!q))
		return -EPROTO;

	if (!exec_queue_destroyed(q) || exec_queue_pending_disable(q) ||
	    exec_queue_pending_enable(q) || exec_queue_enabled(q)) {
		xe_gt_err(guc_to_gt(guc),
			  "DEREGISTER_DONE: Unexpected engine state 0x%04x, guc_id=%d",
			  atomic_read(&q->guc->state), q->guc->id);
		return -EPROTO;
	}

	handle_deregister_done(guc, q);

	return 0;
}

int xe_guc_exec_queue_reset_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_exec_queue *q;
	u32 guc_id;

	if (unlikely(len < 1))
		return -EPROTO;

	guc_id = msg[0];

	q = g2h_exec_queue_lookup(guc, guc_id);
	if (unlikely(!q))
		return -EPROTO;

	xe_gt_info(gt, "Engine reset: engine_class=%s, logical_mask: 0x%x, guc_id=%d",
		   xe_hw_engine_class_to_str(q->class), q->logical_mask, guc_id);

	trace_xe_exec_queue_reset(q);

	/*
	 * A banned engine is a NOP at this point (came from
	 * guc_exec_queue_timedout_job). Otherwise, kick drm scheduler to cancel
	 * jobs by setting timeout of the job to the minimum value kicking
	 * guc_exec_queue_timedout_job.
	 */
	set_exec_queue_reset(q);
	if (!exec_queue_banned(q) && !exec_queue_check_timeout(q))
		xe_guc_exec_queue_trigger_cleanup(q);

	return 0;
}

/*
 * xe_guc_error_capture_handler - Handler of GuC captured message
 * @guc: The GuC object
 * @msg: Point to the message
 * @len: The message length
 *
 * When GuC captured data is ready, GuC will send message
 * XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION to host, this function will be
 * called 1st to check status before process the data comes with the message.
 *
 * Returns: error code. 0 if success
 */
int xe_guc_error_capture_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	u32 status;

	if (unlikely(len != XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION_DATA_LEN))
		return -EPROTO;

	status = msg[0] & XE_GUC_STATE_CAPTURE_EVENT_STATUS_MASK;
	if (status == XE_GUC_STATE_CAPTURE_EVENT_STATUS_NOSPACE)
		xe_gt_warn(guc_to_gt(guc), "G2H-Error capture no space");

	xe_guc_capture_process(guc);

	return 0;
}

int xe_guc_exec_queue_memory_cat_error_handler(struct xe_guc *guc, u32 *msg,
					       u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_exec_queue *q;
	u32 guc_id;

	if (unlikely(len < 1))
		return -EPROTO;

	guc_id = msg[0];

	if (guc_id == GUC_ID_UNKNOWN) {
		/*
		 * GuC uses GUC_ID_UNKNOWN if it can not map the CAT fault to any PF/VF
		 * context. In such case only PF will be notified about that fault.
		 */
		xe_gt_err_ratelimited(gt, "Memory CAT error reported by GuC!\n");
		return 0;
	}

	q = g2h_exec_queue_lookup(guc, guc_id);
	if (unlikely(!q))
		return -EPROTO;

	xe_gt_dbg(gt, "Engine memory cat error: engine_class=%s, logical_mask: 0x%x, guc_id=%d",
		  xe_hw_engine_class_to_str(q->class), q->logical_mask, guc_id);

	trace_xe_exec_queue_memory_cat_error(q);

	/* Treat the same as engine reset */
	set_exec_queue_reset(q);
	if (!exec_queue_banned(q) && !exec_queue_check_timeout(q))
		xe_guc_exec_queue_trigger_cleanup(q);

	return 0;
}

int xe_guc_exec_queue_reset_failure_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u8 guc_class, instance;
	u32 reason;

	if (unlikely(len != 3))
		return -EPROTO;

	guc_class = msg[0];
	instance = msg[1];
	reason = msg[2];

	/* Unexpected failure of a hardware feature, log an actual error */
	xe_gt_err(gt, "GuC engine reset request failed on %d:%d because 0x%08X",
		  guc_class, instance, reason);

	xe_gt_reset_async(gt);

	return 0;
}

static void
guc_exec_queue_wq_snapshot_capture(struct xe_exec_queue *q,
				   struct xe_guc_submit_exec_queue_snapshot *snapshot)
{
	struct xe_guc *guc = exec_queue_to_guc(q);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(q->lrc[0]);
	int i;

	snapshot->guc.wqi_head = q->guc->wqi_head;
	snapshot->guc.wqi_tail = q->guc->wqi_tail;
	snapshot->parallel.wq_desc.head = parallel_read(xe, map, wq_desc.head);
	snapshot->parallel.wq_desc.tail = parallel_read(xe, map, wq_desc.tail);
	snapshot->parallel.wq_desc.status = parallel_read(xe, map,
							  wq_desc.wq_status);

	if (snapshot->parallel.wq_desc.head !=
	    snapshot->parallel.wq_desc.tail) {
		for (i = snapshot->parallel.wq_desc.head;
		     i != snapshot->parallel.wq_desc.tail;
		     i = (i + sizeof(u32)) % WQ_SIZE)
			snapshot->parallel.wq[i / sizeof(u32)] =
				parallel_read(xe, map, wq[i / sizeof(u32)]);
	}
}

static void
guc_exec_queue_wq_snapshot_print(struct xe_guc_submit_exec_queue_snapshot *snapshot,
				 struct drm_printer *p)
{
	int i;

	drm_printf(p, "\tWQ head: %u (internal), %d (memory)\n",
		   snapshot->guc.wqi_head, snapshot->parallel.wq_desc.head);
	drm_printf(p, "\tWQ tail: %u (internal), %d (memory)\n",
		   snapshot->guc.wqi_tail, snapshot->parallel.wq_desc.tail);
	drm_printf(p, "\tWQ status: %u\n", snapshot->parallel.wq_desc.status);

	if (snapshot->parallel.wq_desc.head !=
	    snapshot->parallel.wq_desc.tail) {
		for (i = snapshot->parallel.wq_desc.head;
		     i != snapshot->parallel.wq_desc.tail;
		     i = (i + sizeof(u32)) % WQ_SIZE)
			drm_printf(p, "\tWQ[%zu]: 0x%08x\n", i / sizeof(u32),
				   snapshot->parallel.wq[i / sizeof(u32)]);
	}
}

/**
 * xe_guc_exec_queue_snapshot_capture - Take a quick snapshot of the GuC Engine.
 * @q: faulty exec queue
 *
 * This can be printed out in a later stage like during dev_coredump
 * analysis.
 *
 * Returns: a GuC Submit Engine snapshot object that must be freed by the
 * caller, using `xe_guc_exec_queue_snapshot_free`.
 */
struct xe_guc_submit_exec_queue_snapshot *
xe_guc_exec_queue_snapshot_capture(struct xe_exec_queue *q)
{
	struct xe_gpu_scheduler *sched = &q->guc->sched;
	struct xe_guc_submit_exec_queue_snapshot *snapshot;
	int i;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);

	if (!snapshot)
		return NULL;

	snapshot->guc.id = q->guc->id;
	memcpy(&snapshot->name, &q->name, sizeof(snapshot->name));
	snapshot->class = q->class;
	snapshot->logical_mask = q->logical_mask;
	snapshot->width = q->width;
	snapshot->refcount = kref_read(&q->refcount);
	snapshot->sched_timeout = sched->base.timeout;
	snapshot->sched_props.timeslice_us = q->sched_props.timeslice_us;
	snapshot->sched_props.preempt_timeout_us =
		q->sched_props.preempt_timeout_us;

	snapshot->lrc = kmalloc_array(q->width, sizeof(struct xe_lrc_snapshot *),
				      GFP_ATOMIC);

	if (snapshot->lrc) {
		for (i = 0; i < q->width; ++i) {
			struct xe_lrc *lrc = q->lrc[i];

			snapshot->lrc[i] = xe_lrc_snapshot_capture(lrc);
		}
	}

	snapshot->schedule_state = atomic_read(&q->guc->state);
	snapshot->exec_queue_flags = q->flags;

	snapshot->parallel_execution = xe_exec_queue_is_parallel(q);
	if (snapshot->parallel_execution)
		guc_exec_queue_wq_snapshot_capture(q, snapshot);

	spin_lock(&sched->base.job_list_lock);
	snapshot->pending_list_size = list_count_nodes(&sched->base.pending_list);
	snapshot->pending_list = kmalloc_array(snapshot->pending_list_size,
					       sizeof(struct pending_list_snapshot),
					       GFP_ATOMIC);

	if (snapshot->pending_list) {
		struct xe_sched_job *job_iter;

		i = 0;
		list_for_each_entry(job_iter, &sched->base.pending_list, drm.list) {
			snapshot->pending_list[i].seqno =
				xe_sched_job_seqno(job_iter);
			snapshot->pending_list[i].fence =
				dma_fence_is_signaled(job_iter->fence) ? 1 : 0;
			snapshot->pending_list[i].finished =
				dma_fence_is_signaled(&job_iter->drm.s_fence->finished)
				? 1 : 0;
			i++;
		}
	}

	spin_unlock(&sched->base.job_list_lock);

	return snapshot;
}

/**
 * xe_guc_exec_queue_snapshot_capture_delayed - Take delayed part of snapshot of the GuC Engine.
 * @snapshot: Previously captured snapshot of job.
 *
 * This captures some data that requires taking some locks, so it cannot be done in signaling path.
 */
void
xe_guc_exec_queue_snapshot_capture_delayed(struct xe_guc_submit_exec_queue_snapshot *snapshot)
{
	int i;

	if (!snapshot || !snapshot->lrc)
		return;

	for (i = 0; i < snapshot->width; ++i)
		xe_lrc_snapshot_capture_delayed(snapshot->lrc[i]);
}

/**
 * xe_guc_exec_queue_snapshot_print - Print out a given GuC Engine snapshot.
 * @snapshot: GuC Submit Engine snapshot object.
 * @p: drm_printer where it will be printed out.
 *
 * This function prints out a given GuC Submit Engine snapshot object.
 */
void
xe_guc_exec_queue_snapshot_print(struct xe_guc_submit_exec_queue_snapshot *snapshot,
				 struct drm_printer *p)
{
	int i;

	if (!snapshot)
		return;

	drm_printf(p, "GuC ID: %d\n", snapshot->guc.id);
	drm_printf(p, "\tName: %s\n", snapshot->name);
	drm_printf(p, "\tClass: %d\n", snapshot->class);
	drm_printf(p, "\tLogical mask: 0x%x\n", snapshot->logical_mask);
	drm_printf(p, "\tWidth: %d\n", snapshot->width);
	drm_printf(p, "\tRef: %d\n", snapshot->refcount);
	drm_printf(p, "\tTimeout: %ld (ms)\n", snapshot->sched_timeout);
	drm_printf(p, "\tTimeslice: %u (us)\n",
		   snapshot->sched_props.timeslice_us);
	drm_printf(p, "\tPreempt timeout: %u (us)\n",
		   snapshot->sched_props.preempt_timeout_us);

	for (i = 0; snapshot->lrc && i < snapshot->width; ++i)
		xe_lrc_snapshot_print(snapshot->lrc[i], p);

	drm_printf(p, "\tSchedule State: 0x%x\n", snapshot->schedule_state);
	drm_printf(p, "\tFlags: 0x%lx\n", snapshot->exec_queue_flags);

	if (snapshot->parallel_execution)
		guc_exec_queue_wq_snapshot_print(snapshot, p);

	for (i = 0; snapshot->pending_list && i < snapshot->pending_list_size;
	     i++)
		drm_printf(p, "\tJob: seqno=%d, fence=%d, finished=%d\n",
			   snapshot->pending_list[i].seqno,
			   snapshot->pending_list[i].fence,
			   snapshot->pending_list[i].finished);
}

/**
 * xe_guc_exec_queue_snapshot_free - Free all allocated objects for a given
 * snapshot.
 * @snapshot: GuC Submit Engine snapshot object.
 *
 * This function free all the memory that needed to be allocated at capture
 * time.
 */
void xe_guc_exec_queue_snapshot_free(struct xe_guc_submit_exec_queue_snapshot *snapshot)
{
	int i;

	if (!snapshot)
		return;

	if (snapshot->lrc) {
		for (i = 0; i < snapshot->width; i++)
			xe_lrc_snapshot_free(snapshot->lrc[i]);
		kfree(snapshot->lrc);
	}
	kfree(snapshot->pending_list);
	kfree(snapshot);
}

static void guc_exec_queue_print(struct xe_exec_queue *q, struct drm_printer *p)
{
	struct xe_guc_submit_exec_queue_snapshot *snapshot;

	snapshot = xe_guc_exec_queue_snapshot_capture(q);
	xe_guc_exec_queue_snapshot_print(snapshot, p);
	xe_guc_exec_queue_snapshot_free(snapshot);
}

/**
 * xe_guc_submit_print - GuC Submit Print.
 * @guc: GuC.
 * @p: drm_printer where it will be printed out.
 *
 * This function capture and prints snapshots of **all** GuC Engines.
 */
void xe_guc_submit_print(struct xe_guc *guc, struct drm_printer *p)
{
	struct xe_exec_queue *q;
	unsigned long index;

	if (!xe_device_uc_enabled(guc_to_xe(guc)))
		return;

	mutex_lock(&guc->submission_state.lock);
	xa_for_each(&guc->submission_state.exec_queue_lookup, index, q)
		guc_exec_queue_print(q, p);
	mutex_unlock(&guc->submission_state.lock);
}
