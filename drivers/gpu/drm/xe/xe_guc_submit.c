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

#include <drm/drm_managed.h>

#include "regs/xe_lrc_layout.h"
#include "xe_devcoredump.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_force_wake.h"
#include "xe_gpu_scheduler.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_engine_types.h"
#include "xe_guc_submit_types.h"
#include "xe_hw_engine.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_map.h"
#include "xe_mocs.h"
#include "xe_ring_ops_types.h"
#include "xe_sched_job.h"
#include "xe_trace.h"
#include "xe_vm.h"

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static struct xe_device *
guc_to_xe(struct xe_guc *guc)
{
	return gt_to_xe(guc_to_gt(guc));
}

static struct xe_guc *
engine_to_guc(struct xe_engine *e)
{
	return &e->gt->uc.guc;
}

/*
 * Helpers for engine state, using an atomic as some of the bits can transition
 * as the same time (e.g. a suspend can be happning at the same time as schedule
 * engine done being processed).
 */
#define ENGINE_STATE_REGISTERED		(1 << 0)
#define ENGINE_STATE_ENABLED		(1 << 1)
#define ENGINE_STATE_PENDING_ENABLE	(1 << 2)
#define ENGINE_STATE_PENDING_DISABLE	(1 << 3)
#define ENGINE_STATE_DESTROYED		(1 << 4)
#define ENGINE_STATE_SUSPENDED		(1 << 5)
#define ENGINE_STATE_RESET		(1 << 6)
#define ENGINE_STATE_KILLED		(1 << 7)

static bool engine_registered(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_REGISTERED;
}

static void set_engine_registered(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_REGISTERED, &e->guc->state);
}

static void clear_engine_registered(struct xe_engine *e)
{
	atomic_and(~ENGINE_STATE_REGISTERED, &e->guc->state);
}

static bool engine_enabled(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_ENABLED;
}

static void set_engine_enabled(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_ENABLED, &e->guc->state);
}

static void clear_engine_enabled(struct xe_engine *e)
{
	atomic_and(~ENGINE_STATE_ENABLED, &e->guc->state);
}

static bool engine_pending_enable(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_PENDING_ENABLE;
}

static void set_engine_pending_enable(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_PENDING_ENABLE, &e->guc->state);
}

static void clear_engine_pending_enable(struct xe_engine *e)
{
	atomic_and(~ENGINE_STATE_PENDING_ENABLE, &e->guc->state);
}

static bool engine_pending_disable(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_PENDING_DISABLE;
}

static void set_engine_pending_disable(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_PENDING_DISABLE, &e->guc->state);
}

static void clear_engine_pending_disable(struct xe_engine *e)
{
	atomic_and(~ENGINE_STATE_PENDING_DISABLE, &e->guc->state);
}

static bool engine_destroyed(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_DESTROYED;
}

static void set_engine_destroyed(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_DESTROYED, &e->guc->state);
}

static bool engine_banned(struct xe_engine *e)
{
	return (e->flags & ENGINE_FLAG_BANNED);
}

static void set_engine_banned(struct xe_engine *e)
{
	e->flags |= ENGINE_FLAG_BANNED;
}

static bool engine_suspended(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_SUSPENDED;
}

static void set_engine_suspended(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_SUSPENDED, &e->guc->state);
}

static void clear_engine_suspended(struct xe_engine *e)
{
	atomic_and(~ENGINE_STATE_SUSPENDED, &e->guc->state);
}

static bool engine_reset(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_RESET;
}

static void set_engine_reset(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_RESET, &e->guc->state);
}

static bool engine_killed(struct xe_engine *e)
{
	return atomic_read(&e->guc->state) & ENGINE_STATE_KILLED;
}

static void set_engine_killed(struct xe_engine *e)
{
	atomic_or(ENGINE_STATE_KILLED, &e->guc->state);
}

static bool engine_killed_or_banned(struct xe_engine *e)
{
	return engine_killed(e) || engine_banned(e);
}

static void guc_submit_fini(struct drm_device *drm, void *arg)
{
	struct xe_guc *guc = arg;

	xa_destroy(&guc->submission_state.engine_lookup);
	ida_destroy(&guc->submission_state.guc_ids);
	bitmap_free(guc->submission_state.guc_ids_bitmap);
}

#define GUC_ID_MAX		65535
#define GUC_ID_NUMBER_MLRC	4096
#define GUC_ID_NUMBER_SLRC	(GUC_ID_MAX - GUC_ID_NUMBER_MLRC)
#define GUC_ID_START_MLRC	GUC_ID_NUMBER_SLRC

static const struct xe_engine_ops guc_engine_ops;

static void primelockdep(struct xe_guc *guc)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);

	mutex_lock(&guc->submission_state.lock);
	might_lock(&guc->submission_state.suspend.lock);
	mutex_unlock(&guc->submission_state.lock);

	fs_reclaim_release(GFP_KERNEL);
}

int xe_guc_submit_init(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	int err;

	guc->submission_state.guc_ids_bitmap =
		bitmap_zalloc(GUC_ID_NUMBER_MLRC, GFP_KERNEL);
	if (!guc->submission_state.guc_ids_bitmap)
		return -ENOMEM;

	gt->engine_ops = &guc_engine_ops;

	mutex_init(&guc->submission_state.lock);
	xa_init(&guc->submission_state.engine_lookup);
	ida_init(&guc->submission_state.guc_ids);

	spin_lock_init(&guc->submission_state.suspend.lock);
	guc->submission_state.suspend.context = dma_fence_context_alloc(1);

	primelockdep(guc);

	err = drmm_add_action_or_reset(&xe->drm, guc_submit_fini, guc);
	if (err)
		return err;

	return 0;
}

static int alloc_guc_id(struct xe_guc *guc, struct xe_engine *e)
{
	int ret;
	void *ptr;

	/*
	 * Must use GFP_NOWAIT as this lock is in the dma fence signalling path,
	 * worse case user gets -ENOMEM on engine create and has to try again.
	 *
	 * FIXME: Have caller pre-alloc or post-alloc /w GFP_KERNEL to prevent
	 * failure.
	 */
	lockdep_assert_held(&guc->submission_state.lock);

	if (xe_engine_is_parallel(e)) {
		void *bitmap = guc->submission_state.guc_ids_bitmap;

		ret = bitmap_find_free_region(bitmap, GUC_ID_NUMBER_MLRC,
					      order_base_2(e->width));
	} else {
		ret = ida_simple_get(&guc->submission_state.guc_ids, 0,
				     GUC_ID_NUMBER_SLRC, GFP_NOWAIT);
	}
	if (ret < 0)
		return ret;

	e->guc->id = ret;
	if (xe_engine_is_parallel(e))
		e->guc->id += GUC_ID_START_MLRC;

	ptr = xa_store(&guc->submission_state.engine_lookup,
		       e->guc->id, e, GFP_NOWAIT);
	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		goto err_release;
	}

	return 0;

err_release:
	ida_simple_remove(&guc->submission_state.guc_ids, e->guc->id);
	return ret;
}

static void release_guc_id(struct xe_guc *guc, struct xe_engine *e)
{
	mutex_lock(&guc->submission_state.lock);
	xa_erase(&guc->submission_state.engine_lookup, e->guc->id);
	if (xe_engine_is_parallel(e))
		bitmap_release_region(guc->submission_state.guc_ids_bitmap,
				      e->guc->id - GUC_ID_START_MLRC,
				      order_base_2(e->width));
	else
		ida_simple_remove(&guc->submission_state.guc_ids, e->guc->id);
	mutex_unlock(&guc->submission_state.lock);
}

struct engine_policy {
	u32 count;
	struct guc_update_engine_policy h2g;
};

static u32 __guc_engine_policy_action_size(struct engine_policy *policy)
{
	size_t bytes = sizeof(policy->h2g.header) +
		       (sizeof(policy->h2g.klv[0]) * policy->count);

	return bytes / sizeof(u32);
}

static void __guc_engine_policy_start_klv(struct engine_policy *policy,
					  u16 guc_id)
{
	policy->h2g.header.action =
		XE_GUC_ACTION_HOST2GUC_UPDATE_CONTEXT_POLICIES;
	policy->h2g.header.guc_id = guc_id;
	policy->count = 0;
}

#define MAKE_ENGINE_POLICY_ADD(func, id) \
static void __guc_engine_policy_add_##func(struct engine_policy *policy, \
					   u32 data) \
{ \
	XE_BUG_ON(policy->count >= GUC_CONTEXT_POLICIES_KLV_NUM_IDS); \
\
	policy->h2g.klv[policy->count].kl = \
		FIELD_PREP(GUC_KLV_0_KEY, \
			   GUC_CONTEXT_POLICIES_KLV_ID_##id) | \
		FIELD_PREP(GUC_KLV_0_LEN, 1); \
	policy->h2g.klv[policy->count].value = data; \
	policy->count++; \
}

MAKE_ENGINE_POLICY_ADD(execution_quantum, EXECUTION_QUANTUM)
MAKE_ENGINE_POLICY_ADD(preemption_timeout, PREEMPTION_TIMEOUT)
MAKE_ENGINE_POLICY_ADD(priority, SCHEDULING_PRIORITY)
#undef MAKE_ENGINE_POLICY_ADD

static const int xe_engine_prio_to_guc[] = {
	[XE_ENGINE_PRIORITY_LOW] = GUC_CLIENT_PRIORITY_NORMAL,
	[XE_ENGINE_PRIORITY_NORMAL] = GUC_CLIENT_PRIORITY_KMD_NORMAL,
	[XE_ENGINE_PRIORITY_HIGH] = GUC_CLIENT_PRIORITY_HIGH,
	[XE_ENGINE_PRIORITY_KERNEL] = GUC_CLIENT_PRIORITY_KMD_HIGH,
};

static void init_policies(struct xe_guc *guc, struct xe_engine *e)
{
	struct engine_policy policy;
	enum xe_engine_priority prio = e->priority;
	u32 timeslice_us = e->sched_props.timeslice_us;
	u32 preempt_timeout_us = e->sched_props.preempt_timeout_us;

	XE_BUG_ON(!engine_registered(e));

	__guc_engine_policy_start_klv(&policy, e->guc->id);
	__guc_engine_policy_add_priority(&policy, xe_engine_prio_to_guc[prio]);
	__guc_engine_policy_add_execution_quantum(&policy, timeslice_us);
	__guc_engine_policy_add_preemption_timeout(&policy, preempt_timeout_us);

	xe_guc_ct_send(&guc->ct, (u32 *)&policy.h2g,
		       __guc_engine_policy_action_size(&policy), 0, 0);
}

static void set_min_preemption_timeout(struct xe_guc *guc, struct xe_engine *e)
{
	struct engine_policy policy;

	__guc_engine_policy_start_klv(&policy, e->guc->id);
	__guc_engine_policy_add_preemption_timeout(&policy, 1);

	xe_guc_ct_send(&guc->ct, (u32 *)&policy.h2g,
		       __guc_engine_policy_action_size(&policy), 0, 0);
}

#define parallel_read(xe_, map_, field_) \
	xe_map_rd_field(xe_, &map_, 0, struct guc_submit_parallel_scratch, \
			field_)
#define parallel_write(xe_, map_, field_, val_) \
	xe_map_wr_field(xe_, &map_, 0, struct guc_submit_parallel_scratch, \
			field_, val_)

static void __register_mlrc_engine(struct xe_guc *guc,
				   struct xe_engine *e,
				   struct guc_ctxt_registration_info *info)
{
#define MAX_MLRC_REG_SIZE      (13 + XE_HW_ENGINE_MAX_INSTANCE * 2)
	u32 action[MAX_MLRC_REG_SIZE];
	int len = 0;
	int i;

	XE_BUG_ON(!xe_engine_is_parallel(e));

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
	action[len++] = e->width;
	action[len++] = info->hwlrca_lo;
	action[len++] = info->hwlrca_hi;

	for (i = 1; i < e->width; ++i) {
		struct xe_lrc *lrc = e->lrc + i;

		action[len++] = lower_32_bits(xe_lrc_descriptor(lrc));
		action[len++] = upper_32_bits(xe_lrc_descriptor(lrc));
	}

	XE_BUG_ON(len > MAX_MLRC_REG_SIZE);
#undef MAX_MLRC_REG_SIZE

	xe_guc_ct_send(&guc->ct, action, len, 0, 0);
}

static void __register_engine(struct xe_guc *guc,
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

static void register_engine(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_lrc *lrc = e->lrc;
	struct guc_ctxt_registration_info info;

	XE_BUG_ON(engine_registered(e));

	memset(&info, 0, sizeof(info));
	info.context_idx = e->guc->id;
	info.engine_class = xe_engine_class_to_guc_class(e->class);
	info.engine_submit_mask = e->logical_mask;
	info.hwlrca_lo = lower_32_bits(xe_lrc_descriptor(lrc));
	info.hwlrca_hi = upper_32_bits(xe_lrc_descriptor(lrc));
	info.flags = CONTEXT_REGISTRATION_FLAG_KMD;

	if (xe_engine_is_parallel(e)) {
		u32 ggtt_addr = xe_lrc_parallel_ggtt_addr(lrc);
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

		e->guc->wqi_head = 0;
		e->guc->wqi_tail = 0;
		xe_map_memset(xe, &map, 0, 0, PARALLEL_SCRATCH_SIZE - WQ_SIZE);
		parallel_write(xe, map, wq_desc.wq_status, WQ_STATUS_ACTIVE);
	}

	/*
	 * We must keep a reference for LR engines if engine is registered with
	 * the GuC as jobs signal immediately and can't destroy an engine if the
	 * GuC has a reference to it.
	 */
	if (xe_engine_is_lr(e))
		xe_engine_get(e);

	set_engine_registered(e);
	trace_xe_engine_register(e);
	if (xe_engine_is_parallel(e))
		__register_mlrc_engine(guc, e, &info);
	else
		__register_engine(guc, &info);
	init_policies(guc, e);
}

static u32 wq_space_until_wrap(struct xe_engine *e)
{
	return (WQ_SIZE - e->guc->wqi_tail);
}

static int wq_wait_for_space(struct xe_engine *e, u32 wqi_size)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(e->lrc);
	unsigned int sleep_period_ms = 1;

#define AVAILABLE_SPACE \
	CIRC_SPACE(e->guc->wqi_tail, e->guc->wqi_head, WQ_SIZE)
	if (wqi_size > AVAILABLE_SPACE) {
try_again:
		e->guc->wqi_head = parallel_read(xe, map, wq_desc.head);
		if (wqi_size > AVAILABLE_SPACE) {
			if (sleep_period_ms == 1024) {
				xe_gt_reset_async(e->gt);
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

static int wq_noop_append(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(e->lrc);
	u32 len_dw = wq_space_until_wrap(e) / sizeof(u32) - 1;

	if (wq_wait_for_space(e, wq_space_until_wrap(e)))
		return -ENODEV;

	XE_BUG_ON(!FIELD_FIT(WQ_LEN_MASK, len_dw));

	parallel_write(xe, map, wq[e->guc->wqi_tail / sizeof(u32)],
		       FIELD_PREP(WQ_TYPE_MASK, WQ_TYPE_NOOP) |
		       FIELD_PREP(WQ_LEN_MASK, len_dw));
	e->guc->wqi_tail = 0;

	return 0;
}

static void wq_item_append(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(e->lrc);
	u32 wqi[XE_HW_ENGINE_MAX_INSTANCE + 3];
	u32 wqi_size = (e->width + 3) * sizeof(u32);
	u32 len_dw = (wqi_size / sizeof(u32)) - 1;
	int i = 0, j;

	if (wqi_size > wq_space_until_wrap(e)) {
		if (wq_noop_append(e))
			return;
	}
	if (wq_wait_for_space(e, wqi_size))
		return;

	wqi[i++] = FIELD_PREP(WQ_TYPE_MASK, WQ_TYPE_MULTI_LRC) |
		FIELD_PREP(WQ_LEN_MASK, len_dw);
	wqi[i++] = xe_lrc_descriptor(e->lrc);
	wqi[i++] = FIELD_PREP(WQ_GUC_ID_MASK, e->guc->id) |
		FIELD_PREP(WQ_RING_TAIL_MASK, e->lrc->ring.tail / sizeof(u64));
	wqi[i++] = 0;
	for (j = 1; j < e->width; ++j) {
		struct xe_lrc *lrc = e->lrc + j;

		wqi[i++] = lrc->ring.tail / sizeof(u64);
	}

	XE_BUG_ON(i != wqi_size / sizeof(u32));

	iosys_map_incr(&map, offsetof(struct guc_submit_parallel_scratch,
				      wq[e->guc->wqi_tail / sizeof(u32)]));
	xe_map_memcpy_to(xe, &map, 0, wqi, wqi_size);
	e->guc->wqi_tail += wqi_size;
	XE_BUG_ON(e->guc->wqi_tail > WQ_SIZE);

	xe_device_wmb(xe);

	map = xe_lrc_parallel_map(e->lrc);
	parallel_write(xe, map, wq_desc.tail, e->guc->wqi_tail);
}

#define RESUME_PENDING	~0x0ull
static void submit_engine(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_lrc *lrc = e->lrc;
	u32 action[3];
	u32 g2h_len = 0;
	u32 num_g2h = 0;
	int len = 0;
	bool extra_submit = false;

	XE_BUG_ON(!engine_registered(e));

	if (xe_engine_is_parallel(e))
		wq_item_append(e);
	else
		xe_lrc_write_ctx_reg(lrc, CTX_RING_TAIL, lrc->ring.tail);

	if (engine_suspended(e) && !xe_engine_is_parallel(e))
		return;

	if (!engine_enabled(e) && !engine_suspended(e)) {
		action[len++] = XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET;
		action[len++] = e->guc->id;
		action[len++] = GUC_CONTEXT_ENABLE;
		g2h_len = G2H_LEN_DW_SCHED_CONTEXT_MODE_SET;
		num_g2h = 1;
		if (xe_engine_is_parallel(e))
			extra_submit = true;

		e->guc->resume_time = RESUME_PENDING;
		set_engine_pending_enable(e);
		set_engine_enabled(e);
		trace_xe_engine_scheduling_enable(e);
	} else {
		action[len++] = XE_GUC_ACTION_SCHED_CONTEXT;
		action[len++] = e->guc->id;
		trace_xe_engine_submit(e);
	}

	xe_guc_ct_send(&guc->ct, action, len, g2h_len, num_g2h);

	if (extra_submit) {
		len = 0;
		action[len++] = XE_GUC_ACTION_SCHED_CONTEXT;
		action[len++] = e->guc->id;
		trace_xe_engine_submit(e);

		xe_guc_ct_send(&guc->ct, action, len, 0, 0);
	}
}

static struct dma_fence *
guc_engine_run_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);
	struct xe_engine *e = job->engine;
	bool lr = xe_engine_is_lr(e);

	XE_BUG_ON((engine_destroyed(e) || engine_pending_disable(e)) &&
		  !engine_banned(e) && !engine_suspended(e));

	trace_xe_sched_job_run(job);

	if (!engine_killed_or_banned(e) && !xe_sched_job_is_error(job)) {
		if (!engine_registered(e))
			register_engine(e);
		if (!lr)	/* LR jobs are emitted in the exec IOCTL */
			e->ring_ops->emit_job(job);
		submit_engine(e);
	}

	if (lr) {
		xe_sched_job_set_error(job, -EOPNOTSUPP);
		return NULL;
	} else if (test_and_set_bit(JOB_FLAG_SUBMIT, &job->fence->flags)) {
		return job->fence;
	} else {
		return dma_fence_get(job->fence);
	}
}

static void guc_engine_free_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);

	trace_xe_sched_job_free(job);
	xe_sched_job_put(job);
}

static int guc_read_stopped(struct xe_guc *guc)
{
	return atomic_read(&guc->submission_state.stopped);
}

#define MAKE_SCHED_CONTEXT_ACTION(e, enable_disable)			\
	u32 action[] = {						\
		XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET,			\
		e->guc->id,						\
		GUC_CONTEXT_##enable_disable,				\
	}

static void disable_scheduling_deregister(struct xe_guc *guc,
					  struct xe_engine *e)
{
	MAKE_SCHED_CONTEXT_ACTION(e, DISABLE);
	int ret;

	set_min_preemption_timeout(guc, e);
	smp_rmb();
	ret = wait_event_timeout(guc->ct.wq, !engine_pending_enable(e) ||
				 guc_read_stopped(guc), HZ * 5);
	if (!ret) {
		struct xe_gpu_scheduler *sched = &e->guc->sched;

		XE_WARN_ON("Pending enable failed to respond");
		xe_sched_submission_start(sched);
		xe_gt_reset_async(e->gt);
		xe_sched_tdr_queue_imm(sched);
		return;
	}

	clear_engine_enabled(e);
	set_engine_pending_disable(e);
	set_engine_destroyed(e);
	trace_xe_engine_scheduling_disable(e);

	/*
	 * Reserve space for both G2H here as the 2nd G2H is sent from a G2H
	 * handler and we are not allowed to reserved G2H space in handlers.
	 */
	xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
		       G2H_LEN_DW_SCHED_CONTEXT_MODE_SET +
		       G2H_LEN_DW_DEREGISTER_CONTEXT, 2);
}

static void guc_engine_print(struct xe_engine *e, struct drm_printer *p);

#if IS_ENABLED(CONFIG_DRM_XE_SIMPLE_ERROR_CAPTURE)
static void simple_error_capture(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct drm_printer p = drm_err_printer("");
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	u32 adj_logical_mask = e->logical_mask;
	u32 width_mask = (0x1 << e->width) - 1;
	int i;
	bool cookie;

	if (e->vm && !e->vm->error_capture.capture_once) {
		e->vm->error_capture.capture_once = true;
		cookie = dma_fence_begin_signalling();
		for (i = 0; e->width > 1 && i < XE_HW_ENGINE_MAX_INSTANCE;) {
			if (adj_logical_mask & BIT(i)) {
				adj_logical_mask |= width_mask << i;
				i += e->width;
			} else {
				++i;
			}
		}

		xe_force_wake_get(gt_to_fw(guc_to_gt(guc)), XE_FORCEWAKE_ALL);
		xe_guc_ct_print(&guc->ct, &p, true);
		guc_engine_print(e, &p);
		for_each_hw_engine(hwe, guc_to_gt(guc), id) {
			if (hwe->class != e->hwe->class ||
			    !(BIT(hwe->logical_instance) & adj_logical_mask))
				continue;
			xe_hw_engine_print(hwe, &p);
		}
		xe_analyze_vm(&p, e->vm, e->gt->info.id);
		xe_force_wake_put(gt_to_fw(guc_to_gt(guc)), XE_FORCEWAKE_ALL);
		dma_fence_end_signalling(cookie);
	}
}
#else
static void simple_error_capture(struct xe_engine *e)
{
}
#endif

static void xe_guc_engine_trigger_cleanup(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);

	if (xe_engine_is_lr(e))
		queue_work(guc_to_gt(guc)->ordered_wq, &e->guc->lr_tdr);
	else
		xe_sched_tdr_queue_imm(&e->guc->sched);
}

static void xe_guc_engine_lr_cleanup(struct work_struct *w)
{
	struct xe_guc_engine *ge =
		container_of(w, struct xe_guc_engine, lr_tdr);
	struct xe_engine *e = ge->engine;
	struct xe_gpu_scheduler *sched = &ge->sched;

	XE_WARN_ON(!xe_engine_is_lr(e));
	trace_xe_engine_lr_cleanup(e);

	/* Kill the run_job / process_msg entry points */
	xe_sched_submission_stop(sched);

	/* Engine state now stable, disable scheduling / deregister if needed */
	if (engine_registered(e)) {
		struct xe_guc *guc = engine_to_guc(e);
		int ret;

		set_engine_banned(e);
		disable_scheduling_deregister(guc, e);

		/*
		 * Must wait for scheduling to be disabled before signalling
		 * any fences, if GT broken the GT reset code should signal us.
		 */
		ret = wait_event_timeout(guc->ct.wq,
					 !engine_pending_disable(e) ||
					 guc_read_stopped(guc), HZ * 5);
		if (!ret) {
			XE_WARN_ON("Schedule disable failed to respond");
			xe_sched_submission_start(sched);
			xe_gt_reset_async(e->gt);
			return;
		}
	}

	xe_sched_submission_start(sched);
}

static enum drm_gpu_sched_stat
guc_engine_timedout_job(struct drm_sched_job *drm_job)
{
	struct xe_sched_job *job = to_xe_sched_job(drm_job);
	struct xe_sched_job *tmp_job;
	struct xe_engine *e = job->engine;
	struct xe_gpu_scheduler *sched = &e->guc->sched;
	struct xe_device *xe = guc_to_xe(engine_to_guc(e));
	int err = -ETIME;
	int i = 0;

	if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &job->fence->flags)) {
		XE_WARN_ON(e->flags & ENGINE_FLAG_KERNEL);
		XE_WARN_ON(e->flags & ENGINE_FLAG_VM && !engine_killed(e));

		drm_notice(&xe->drm, "Timedout job: seqno=%u, guc_id=%d, flags=0x%lx",
			   xe_sched_job_seqno(job), e->guc->id, e->flags);
		simple_error_capture(e);
		xe_devcoredump(e);
	} else {
		drm_dbg(&xe->drm, "Timedout signaled job: seqno=%u, guc_id=%d, flags=0x%lx",
			 xe_sched_job_seqno(job), e->guc->id, e->flags);
	}
	trace_xe_sched_job_timedout(job);

	/* Kill the run_job entry point */
	xe_sched_submission_stop(sched);

	/*
	 * Kernel jobs should never fail, nor should VM jobs if they do
	 * somethings has gone wrong and the GT needs a reset
	 */
	if (e->flags & ENGINE_FLAG_KERNEL ||
	    (e->flags & ENGINE_FLAG_VM && !engine_killed(e))) {
		if (!xe_sched_invalidate_job(job, 2)) {
			xe_sched_add_pending_job(sched, job);
			xe_sched_submission_start(sched);
			xe_gt_reset_async(e->gt);
			goto out;
		}
	}

	/* Engine state now stable, disable scheduling if needed */
	if (engine_enabled(e)) {
		struct xe_guc *guc = engine_to_guc(e);
		int ret;

		if (engine_reset(e))
			err = -EIO;
		set_engine_banned(e);
		xe_engine_get(e);
		disable_scheduling_deregister(guc, e);

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
					 !engine_pending_disable(e) ||
					 guc_read_stopped(guc), HZ * 5);
		if (!ret) {
			XE_WARN_ON("Schedule disable failed to respond");
			xe_sched_add_pending_job(sched, job);
			xe_sched_submission_start(sched);
			xe_gt_reset_async(e->gt);
			xe_sched_tdr_queue_imm(sched);
			goto out;
		}
	}

	/* Stop fence signaling */
	xe_hw_fence_irq_stop(e->fence_irq);

	/*
	 * Fence state now stable, stop / start scheduler which cleans up any
	 * fences that are complete
	 */
	xe_sched_add_pending_job(sched, job);
	xe_sched_submission_start(sched);
	xe_guc_engine_trigger_cleanup(e);

	/* Mark all outstanding jobs as bad, thus completing them */
	spin_lock(&sched->base.job_list_lock);
	list_for_each_entry(tmp_job, &sched->base.pending_list, drm.list)
		xe_sched_job_set_error(tmp_job, !i++ ? err : -ECANCELED);
	spin_unlock(&sched->base.job_list_lock);

	/* Start fence signaling */
	xe_hw_fence_irq_start(e->fence_irq);

out:
	return DRM_GPU_SCHED_STAT_NOMINAL;
}

static void __guc_engine_fini_async(struct work_struct *w)
{
	struct xe_guc_engine *ge =
		container_of(w, struct xe_guc_engine, fini_async);
	struct xe_engine *e = ge->engine;
	struct xe_guc *guc = engine_to_guc(e);

	trace_xe_engine_destroy(e);

	if (xe_engine_is_lr(e))
		cancel_work_sync(&ge->lr_tdr);
	if (e->flags & ENGINE_FLAG_PERSISTENT)
		xe_device_remove_persistent_engines(gt_to_xe(e->gt), e);
	release_guc_id(guc, e);
	xe_sched_entity_fini(&ge->entity);
	xe_sched_fini(&ge->sched);

	if (!(e->flags & ENGINE_FLAG_KERNEL)) {
		kfree(ge);
		xe_engine_fini(e);
	}
}

static void guc_engine_fini_async(struct xe_engine *e)
{
	bool kernel = e->flags & ENGINE_FLAG_KERNEL;

	INIT_WORK(&e->guc->fini_async, __guc_engine_fini_async);
	queue_work(system_wq, &e->guc->fini_async);

	/* We must block on kernel engines so slabs are empty on driver unload */
	if (kernel) {
		struct xe_guc_engine *ge = e->guc;

		flush_work(&ge->fini_async);
		kfree(ge);
		xe_engine_fini(e);
	}
}

static void __guc_engine_fini(struct xe_guc *guc, struct xe_engine *e)
{
	/*
	 * Might be done from within the GPU scheduler, need to do async as we
	 * fini the scheduler when the engine is fini'd, the scheduler can't
	 * complete fini within itself (circular dependency). Async resolves
	 * this we and don't really care when everything is fini'd, just that it
	 * is.
	 */
	guc_engine_fini_async(e);
}

static void __guc_engine_process_msg_cleanup(struct xe_sched_msg *msg)
{
	struct xe_engine *e = msg->private_data;
	struct xe_guc *guc = engine_to_guc(e);

	XE_BUG_ON(e->flags & ENGINE_FLAG_KERNEL);
	trace_xe_engine_cleanup_entity(e);

	if (engine_registered(e))
		disable_scheduling_deregister(guc, e);
	else
		__guc_engine_fini(guc, e);
}

static bool guc_engine_allowed_to_change_state(struct xe_engine *e)
{
	return !engine_killed_or_banned(e) && engine_registered(e);
}

static void __guc_engine_process_msg_set_sched_props(struct xe_sched_msg *msg)
{
	struct xe_engine *e = msg->private_data;
	struct xe_guc *guc = engine_to_guc(e);

	if (guc_engine_allowed_to_change_state(e))
		init_policies(guc, e);
	kfree(msg);
}

static void suspend_fence_signal(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);

	XE_BUG_ON(!engine_suspended(e) && !engine_killed(e) &&
		  !guc_read_stopped(guc));
	XE_BUG_ON(!e->guc->suspend_pending);

	e->guc->suspend_pending = false;
	smp_wmb();
	wake_up(&e->guc->suspend_wait);
}

static void __guc_engine_process_msg_suspend(struct xe_sched_msg *msg)
{
	struct xe_engine *e = msg->private_data;
	struct xe_guc *guc = engine_to_guc(e);

	if (guc_engine_allowed_to_change_state(e) && !engine_suspended(e) &&
	    engine_enabled(e)) {
		wait_event(guc->ct.wq, e->guc->resume_time != RESUME_PENDING ||
			   guc_read_stopped(guc));

		if (!guc_read_stopped(guc)) {
			MAKE_SCHED_CONTEXT_ACTION(e, DISABLE);
			s64 since_resume_ms =
				ktime_ms_delta(ktime_get(),
					       e->guc->resume_time);
			s64 wait_ms = e->vm->preempt.min_run_period_ms -
				since_resume_ms;

			if (wait_ms > 0 && e->guc->resume_time)
				msleep(wait_ms);

			set_engine_suspended(e);
			clear_engine_enabled(e);
			set_engine_pending_disable(e);
			trace_xe_engine_scheduling_disable(e);

			xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
				       G2H_LEN_DW_SCHED_CONTEXT_MODE_SET, 1);
		}
	} else if (e->guc->suspend_pending) {
		set_engine_suspended(e);
		suspend_fence_signal(e);
	}
}

static void __guc_engine_process_msg_resume(struct xe_sched_msg *msg)
{
	struct xe_engine *e = msg->private_data;
	struct xe_guc *guc = engine_to_guc(e);

	if (guc_engine_allowed_to_change_state(e)) {
		MAKE_SCHED_CONTEXT_ACTION(e, ENABLE);

		e->guc->resume_time = RESUME_PENDING;
		clear_engine_suspended(e);
		set_engine_pending_enable(e);
		set_engine_enabled(e);
		trace_xe_engine_scheduling_enable(e);

		xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action),
			       G2H_LEN_DW_SCHED_CONTEXT_MODE_SET, 1);
	} else {
		clear_engine_suspended(e);
	}
}

#define CLEANUP		1	/* Non-zero values to catch uninitialized msg */
#define SET_SCHED_PROPS	2
#define SUSPEND		3
#define RESUME		4

static void guc_engine_process_msg(struct xe_sched_msg *msg)
{
	trace_xe_sched_msg_recv(msg);

	switch (msg->opcode) {
	case CLEANUP:
		__guc_engine_process_msg_cleanup(msg);
		break;
	case SET_SCHED_PROPS:
		__guc_engine_process_msg_set_sched_props(msg);
		break;
	case SUSPEND:
		__guc_engine_process_msg_suspend(msg);
		break;
	case RESUME:
		__guc_engine_process_msg_resume(msg);
		break;
	default:
		XE_BUG_ON("Unknown message type");
	}
}

static const struct drm_sched_backend_ops drm_sched_ops = {
	.run_job = guc_engine_run_job,
	.free_job = guc_engine_free_job,
	.timedout_job = guc_engine_timedout_job,
};

static const struct xe_sched_backend_ops xe_sched_ops = {
	.process_msg = guc_engine_process_msg,
};

static int guc_engine_init(struct xe_engine *e)
{
	struct xe_gpu_scheduler *sched;
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_guc_engine *ge;
	long timeout;
	int err;

	XE_BUG_ON(!xe_device_guc_submission_enabled(guc_to_xe(guc)));

	ge = kzalloc(sizeof(*ge), GFP_KERNEL);
	if (!ge)
		return -ENOMEM;

	e->guc = ge;
	ge->engine = e;
	init_waitqueue_head(&ge->suspend_wait);

	timeout = xe_vm_no_dma_fences(e->vm) ? MAX_SCHEDULE_TIMEOUT : HZ * 5;
	err = xe_sched_init(&ge->sched, &drm_sched_ops, &xe_sched_ops, NULL,
			     e->lrc[0].ring.size / MAX_JOB_SIZE_BYTES,
			     64, timeout, guc_to_gt(guc)->ordered_wq, NULL,
			     e->name, gt_to_xe(e->gt)->drm.dev);
	if (err)
		goto err_free;

	sched = &ge->sched;
	err = xe_sched_entity_init(&ge->entity, sched);
	if (err)
		goto err_sched;
	e->priority = XE_ENGINE_PRIORITY_NORMAL;

	if (xe_engine_is_lr(e))
		INIT_WORK(&e->guc->lr_tdr, xe_guc_engine_lr_cleanup);

	mutex_lock(&guc->submission_state.lock);

	err = alloc_guc_id(guc, e);
	if (err)
		goto err_entity;

	e->entity = &ge->entity;

	if (guc_read_stopped(guc))
		xe_sched_stop(sched);

	mutex_unlock(&guc->submission_state.lock);

	switch (e->class) {
	case XE_ENGINE_CLASS_RENDER:
		sprintf(e->name, "rcs%d", e->guc->id);
		break;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		sprintf(e->name, "vcs%d", e->guc->id);
		break;
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		sprintf(e->name, "vecs%d", e->guc->id);
		break;
	case XE_ENGINE_CLASS_COPY:
		sprintf(e->name, "bcs%d", e->guc->id);
		break;
	case XE_ENGINE_CLASS_COMPUTE:
		sprintf(e->name, "ccs%d", e->guc->id);
		break;
	default:
		XE_WARN_ON(e->class);
	}

	trace_xe_engine_create(e);

	return 0;

err_entity:
	xe_sched_entity_fini(&ge->entity);
err_sched:
	xe_sched_fini(&ge->sched);
err_free:
	kfree(ge);

	return err;
}

static void guc_engine_kill(struct xe_engine *e)
{
	trace_xe_engine_kill(e);
	set_engine_killed(e);
	xe_guc_engine_trigger_cleanup(e);
}

static void guc_engine_add_msg(struct xe_engine *e, struct xe_sched_msg *msg,
			       u32 opcode)
{
	INIT_LIST_HEAD(&msg->link);
	msg->opcode = opcode;
	msg->private_data = e;

	trace_xe_sched_msg_add(msg);
	xe_sched_add_msg(&e->guc->sched, msg);
}

#define STATIC_MSG_CLEANUP	0
#define STATIC_MSG_SUSPEND	1
#define STATIC_MSG_RESUME	2
static void guc_engine_fini(struct xe_engine *e)
{
	struct xe_sched_msg *msg = e->guc->static_msgs + STATIC_MSG_CLEANUP;

	if (!(e->flags & ENGINE_FLAG_KERNEL))
		guc_engine_add_msg(e, msg, CLEANUP);
	else
		__guc_engine_fini(engine_to_guc(e), e);
}

static int guc_engine_set_priority(struct xe_engine *e,
				   enum xe_engine_priority priority)
{
	struct xe_sched_msg *msg;

	if (e->priority == priority || engine_killed_or_banned(e))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	guc_engine_add_msg(e, msg, SET_SCHED_PROPS);
	e->priority = priority;

	return 0;
}

static int guc_engine_set_timeslice(struct xe_engine *e, u32 timeslice_us)
{
	struct xe_sched_msg *msg;

	if (e->sched_props.timeslice_us == timeslice_us ||
	    engine_killed_or_banned(e))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	e->sched_props.timeslice_us = timeslice_us;
	guc_engine_add_msg(e, msg, SET_SCHED_PROPS);

	return 0;
}

static int guc_engine_set_preempt_timeout(struct xe_engine *e,
					  u32 preempt_timeout_us)
{
	struct xe_sched_msg *msg;

	if (e->sched_props.preempt_timeout_us == preempt_timeout_us ||
	    engine_killed_or_banned(e))
		return 0;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	e->sched_props.preempt_timeout_us = preempt_timeout_us;
	guc_engine_add_msg(e, msg, SET_SCHED_PROPS);

	return 0;
}

static int guc_engine_set_job_timeout(struct xe_engine *e, u32 job_timeout_ms)
{
	struct xe_gpu_scheduler *sched = &e->guc->sched;

	XE_BUG_ON(engine_registered(e));
	XE_BUG_ON(engine_banned(e));
	XE_BUG_ON(engine_killed(e));

	sched->base.timeout = job_timeout_ms;

	return 0;
}

static int guc_engine_suspend(struct xe_engine *e)
{
	struct xe_sched_msg *msg = e->guc->static_msgs + STATIC_MSG_SUSPEND;

	if (engine_killed_or_banned(e) || e->guc->suspend_pending)
		return -EINVAL;

	e->guc->suspend_pending = true;
	guc_engine_add_msg(e, msg, SUSPEND);

	return 0;
}

static void guc_engine_suspend_wait(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);

	wait_event(e->guc->suspend_wait, !e->guc->suspend_pending ||
		   guc_read_stopped(guc));
}

static void guc_engine_resume(struct xe_engine *e)
{
	struct xe_sched_msg *msg = e->guc->static_msgs + STATIC_MSG_RESUME;

	XE_BUG_ON(e->guc->suspend_pending);

	guc_engine_add_msg(e, msg, RESUME);
}

/*
 * All of these functions are an abstraction layer which other parts of XE can
 * use to trap into the GuC backend. All of these functions, aside from init,
 * really shouldn't do much other than trap into the DRM scheduler which
 * synchronizes these operations.
 */
static const struct xe_engine_ops guc_engine_ops = {
	.init = guc_engine_init,
	.kill = guc_engine_kill,
	.fini = guc_engine_fini,
	.set_priority = guc_engine_set_priority,
	.set_timeslice = guc_engine_set_timeslice,
	.set_preempt_timeout = guc_engine_set_preempt_timeout,
	.set_job_timeout = guc_engine_set_job_timeout,
	.suspend = guc_engine_suspend,
	.suspend_wait = guc_engine_suspend_wait,
	.resume = guc_engine_resume,
};

static void guc_engine_stop(struct xe_guc *guc, struct xe_engine *e)
{
	struct xe_gpu_scheduler *sched = &e->guc->sched;

	/* Stop scheduling + flush any DRM scheduler operations */
	xe_sched_submission_stop(sched);

	/* Clean up lost G2H + reset engine state */
	if (engine_registered(e)) {
		if ((engine_banned(e) && engine_destroyed(e)) ||
		    xe_engine_is_lr(e))
			xe_engine_put(e);
		else if (engine_destroyed(e))
			__guc_engine_fini(guc, e);
	}
	if (e->guc->suspend_pending) {
		set_engine_suspended(e);
		suspend_fence_signal(e);
	}
	atomic_and(ENGINE_STATE_DESTROYED | ENGINE_STATE_SUSPENDED,
		   &e->guc->state);
	e->guc->resume_time = 0;
	trace_xe_engine_stop(e);

	/*
	 * Ban any engine (aside from kernel and engines used for VM ops) with a
	 * started but not complete job or if a job has gone through a GT reset
	 * more than twice.
	 */
	if (!(e->flags & (ENGINE_FLAG_KERNEL | ENGINE_FLAG_VM))) {
		struct xe_sched_job *job = xe_sched_first_pending_job(sched);

		if (job) {
			if ((xe_sched_job_started(job) &&
			    !xe_sched_job_completed(job)) ||
			    xe_sched_invalidate_job(job, 2)) {
				trace_xe_sched_job_ban(job);
				xe_sched_tdr_queue_imm(&e->guc->sched);
				set_engine_banned(e);
			}
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
	wait_event(guc->ct.wq, !guc_read_stopped(guc));
}

int xe_guc_submit_stop(struct xe_guc *guc)
{
	struct xe_engine *e;
	unsigned long index;

	XE_BUG_ON(guc_read_stopped(guc) != 1);

	mutex_lock(&guc->submission_state.lock);

	xa_for_each(&guc->submission_state.engine_lookup, index, e)
		guc_engine_stop(guc, e);

	mutex_unlock(&guc->submission_state.lock);

	/*
	 * No one can enter the backend at this point, aside from new engine
	 * creation which is protected by guc->submission_state.lock.
	 */

	return 0;
}

static void guc_engine_start(struct xe_engine *e)
{
	struct xe_gpu_scheduler *sched = &e->guc->sched;

	if (!engine_killed_or_banned(e)) {
		int i;

		trace_xe_engine_resubmit(e);
		for (i = 0; i < e->width; ++i)
			xe_lrc_set_ring_head(e->lrc + i, e->lrc[i].ring.tail);
		xe_sched_resubmit_jobs(sched);
	}

	xe_sched_submission_start(sched);
}

int xe_guc_submit_start(struct xe_guc *guc)
{
	struct xe_engine *e;
	unsigned long index;

	XE_BUG_ON(guc_read_stopped(guc) != 1);

	mutex_lock(&guc->submission_state.lock);
	atomic_dec(&guc->submission_state.stopped);
	xa_for_each(&guc->submission_state.engine_lookup, index, e)
		guc_engine_start(e);
	mutex_unlock(&guc->submission_state.lock);

	wake_up_all(&guc->ct.wq);

	return 0;
}

static struct xe_engine *
g2h_engine_lookup(struct xe_guc *guc, u32 guc_id)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_engine *e;

	if (unlikely(guc_id >= GUC_ID_MAX)) {
		drm_err(&xe->drm, "Invalid guc_id %u", guc_id);
		return NULL;
	}

	e = xa_load(&guc->submission_state.engine_lookup, guc_id);
	if (unlikely(!e)) {
		drm_err(&xe->drm, "Not engine present for guc_id %u", guc_id);
		return NULL;
	}

	XE_BUG_ON(e->guc->id != guc_id);

	return e;
}

static void deregister_engine(struct xe_guc *guc, struct xe_engine *e)
{
	u32 action[] = {
		XE_GUC_ACTION_DEREGISTER_CONTEXT,
		e->guc->id,
	};

	trace_xe_engine_deregister(e);

	xe_guc_ct_send_g2h_handler(&guc->ct, action, ARRAY_SIZE(action));
}

int xe_guc_sched_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_engine *e;
	u32 guc_id = msg[0];

	if (unlikely(len < 2)) {
		drm_err(&xe->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	e = g2h_engine_lookup(guc, guc_id);
	if (unlikely(!e))
		return -EPROTO;

	if (unlikely(!engine_pending_enable(e) &&
		     !engine_pending_disable(e))) {
		drm_err(&xe->drm, "Unexpected engine state 0x%04x",
			atomic_read(&e->guc->state));
		return -EPROTO;
	}

	trace_xe_engine_scheduling_done(e);

	if (engine_pending_enable(e)) {
		e->guc->resume_time = ktime_get();
		clear_engine_pending_enable(e);
		smp_wmb();
		wake_up_all(&guc->ct.wq);
	} else {
		clear_engine_pending_disable(e);
		if (e->guc->suspend_pending) {
			suspend_fence_signal(e);
		} else {
			if (engine_banned(e)) {
				smp_wmb();
				wake_up_all(&guc->ct.wq);
			}
			deregister_engine(guc, e);
		}
	}

	return 0;
}

int xe_guc_deregister_done_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_engine *e;
	u32 guc_id = msg[0];

	if (unlikely(len < 1)) {
		drm_err(&xe->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	e = g2h_engine_lookup(guc, guc_id);
	if (unlikely(!e))
		return -EPROTO;

	if (!engine_destroyed(e) || engine_pending_disable(e) ||
	    engine_pending_enable(e) || engine_enabled(e)) {
		drm_err(&xe->drm, "Unexpected engine state 0x%04x",
			atomic_read(&e->guc->state));
		return -EPROTO;
	}

	trace_xe_engine_deregister_done(e);

	clear_engine_registered(e);

	if (engine_banned(e) || xe_engine_is_lr(e))
		xe_engine_put(e);
	else
		__guc_engine_fini(guc, e);

	return 0;
}

int xe_guc_engine_reset_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_engine *e;
	u32 guc_id = msg[0];

	if (unlikely(len < 1)) {
		drm_err(&xe->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	e = g2h_engine_lookup(guc, guc_id);
	if (unlikely(!e))
		return -EPROTO;

	drm_info(&xe->drm, "Engine reset: guc_id=%d", guc_id);

	/* FIXME: Do error capture, most likely async */

	trace_xe_engine_reset(e);

	/*
	 * A banned engine is a NOP at this point (came from
	 * guc_engine_timedout_job). Otherwise, kick drm scheduler to cancel
	 * jobs by setting timeout of the job to the minimum value kicking
	 * guc_engine_timedout_job.
	 */
	set_engine_reset(e);
	if (!engine_banned(e))
		xe_guc_engine_trigger_cleanup(e);

	return 0;
}

int xe_guc_engine_memory_cat_error_handler(struct xe_guc *guc, u32 *msg,
					   u32 len)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_engine *e;
	u32 guc_id = msg[0];

	if (unlikely(len < 1)) {
		drm_err(&xe->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	e = g2h_engine_lookup(guc, guc_id);
	if (unlikely(!e))
		return -EPROTO;

	drm_warn(&xe->drm, "Engine memory cat error: guc_id=%d", guc_id);
	trace_xe_engine_memory_cat_error(e);

	/* Treat the same as engine reset */
	set_engine_reset(e);
	if (!engine_banned(e))
		xe_guc_engine_trigger_cleanup(e);

	return 0;
}

int xe_guc_engine_reset_failure_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_device *xe = guc_to_xe(guc);
	u8 guc_class, instance;
	u32 reason;

	if (unlikely(len != 3)) {
		drm_err(&xe->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	guc_class = msg[0];
	instance = msg[1];
	reason = msg[2];

	/* Unexpected failure of a hardware feature, log an actual error */
	drm_err(&xe->drm, "GuC engine reset request failed on %d:%d because 0x%08X",
		guc_class, instance, reason);

	xe_gt_reset_async(guc_to_gt(guc));

	return 0;
}

static void
guc_engine_wq_snapshot_capture(struct xe_engine *e,
			       struct xe_guc_submit_engine_snapshot *snapshot)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_device *xe = guc_to_xe(guc);
	struct iosys_map map = xe_lrc_parallel_map(e->lrc);
	int i;

	snapshot->guc.wqi_head = e->guc->wqi_head;
	snapshot->guc.wqi_tail = e->guc->wqi_tail;
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
guc_engine_wq_snapshot_print(struct xe_guc_submit_engine_snapshot *snapshot,
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
 * xe_guc_engine_snapshot_capture - Take a quick snapshot of the GuC Engine.
 * @e: Xe Engine.
 *
 * This can be printed out in a later stage like during dev_coredump
 * analysis.
 *
 * Returns: a GuC Submit Engine snapshot object that must be freed by the
 * caller, using `xe_guc_engine_snapshot_free`.
 */
struct xe_guc_submit_engine_snapshot *
xe_guc_engine_snapshot_capture(struct xe_engine *e)
{
	struct xe_guc *guc = engine_to_guc(e);
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gpu_scheduler *sched = &e->guc->sched;
	struct xe_sched_job *job;
	struct xe_guc_submit_engine_snapshot *snapshot;
	int i;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);

	if (!snapshot) {
		drm_err(&xe->drm, "Skipping GuC Engine snapshot entirely.\n");
		return NULL;
	}

	snapshot->guc.id = e->guc->id;
	memcpy(&snapshot->name, &e->name, sizeof(snapshot->name));
	snapshot->class = e->class;
	snapshot->logical_mask = e->logical_mask;
	snapshot->width = e->width;
	snapshot->refcount = kref_read(&e->refcount);
	snapshot->sched_timeout = sched->base.timeout;
	snapshot->sched_props.timeslice_us = e->sched_props.timeslice_us;
	snapshot->sched_props.preempt_timeout_us =
		e->sched_props.preempt_timeout_us;

	snapshot->lrc = kmalloc_array(e->width, sizeof(struct lrc_snapshot),
				      GFP_ATOMIC);

	if (!snapshot->lrc) {
		drm_err(&xe->drm, "Skipping GuC Engine LRC snapshot.\n");
	} else {
		for (i = 0; i < e->width; ++i) {
			struct xe_lrc *lrc = e->lrc + i;

			snapshot->lrc[i].context_desc =
				lower_32_bits(xe_lrc_ggtt_addr(lrc));
			snapshot->lrc[i].head = xe_lrc_ring_head(lrc);
			snapshot->lrc[i].tail.internal = lrc->ring.tail;
			snapshot->lrc[i].tail.memory =
				xe_lrc_read_ctx_reg(lrc, CTX_RING_TAIL);
			snapshot->lrc[i].start_seqno = xe_lrc_start_seqno(lrc);
			snapshot->lrc[i].seqno = xe_lrc_seqno(lrc);
		}
	}

	snapshot->schedule_state = atomic_read(&e->guc->state);
	snapshot->engine_flags = e->flags;

	snapshot->parallel_execution = xe_engine_is_parallel(e);
	if (snapshot->parallel_execution)
		guc_engine_wq_snapshot_capture(e, snapshot);

	spin_lock(&sched->base.job_list_lock);
	snapshot->pending_list_size = list_count_nodes(&sched->base.pending_list);
	snapshot->pending_list = kmalloc_array(snapshot->pending_list_size,
					       sizeof(struct pending_list_snapshot),
					       GFP_ATOMIC);

	if (!snapshot->pending_list) {
		drm_err(&xe->drm, "Skipping GuC Engine pending_list snapshot.\n");
	} else {
		i = 0;
		list_for_each_entry(job, &sched->base.pending_list, drm.list) {
			snapshot->pending_list[i].seqno =
				xe_sched_job_seqno(job);
			snapshot->pending_list[i].fence =
				dma_fence_is_signaled(job->fence) ? 1 : 0;
			snapshot->pending_list[i].finished =
				dma_fence_is_signaled(&job->drm.s_fence->finished)
				? 1 : 0;
			i++;
		}
	}

	spin_unlock(&sched->base.job_list_lock);

	return snapshot;
}

/**
 * xe_guc_engine_snapshot_print - Print out a given GuC Engine snapshot.
 * @snapshot: GuC Submit Engine snapshot object.
 * @p: drm_printer where it will be printed out.
 *
 * This function prints out a given GuC Submit Engine snapshot object.
 */
void
xe_guc_engine_snapshot_print(struct xe_guc_submit_engine_snapshot *snapshot,
			     struct drm_printer *p)
{
	int i;

	if (!snapshot)
		return;

	drm_printf(p, "\nGuC ID: %d\n", snapshot->guc.id);
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

	for (i = 0; snapshot->lrc && i < snapshot->width; ++i) {
		drm_printf(p, "\tHW Context Desc: 0x%08x\n",
			   snapshot->lrc[i].context_desc);
		drm_printf(p, "\tLRC Head: (memory) %u\n",
			   snapshot->lrc[i].head);
		drm_printf(p, "\tLRC Tail: (internal) %u, (memory) %u\n",
			   snapshot->lrc[i].tail.internal,
			   snapshot->lrc[i].tail.memory);
		drm_printf(p, "\tStart seqno: (memory) %d\n",
			   snapshot->lrc[i].start_seqno);
		drm_printf(p, "\tSeqno: (memory) %d\n", snapshot->lrc[i].seqno);
	}
	drm_printf(p, "\tSchedule State: 0x%x\n", snapshot->schedule_state);
	drm_printf(p, "\tFlags: 0x%lx\n", snapshot->engine_flags);

	if (snapshot->parallel_execution)
		guc_engine_wq_snapshot_print(snapshot, p);

	for (i = 0; snapshot->pending_list && i < snapshot->pending_list_size;
	     i++)
		drm_printf(p, "\tJob: seqno=%d, fence=%d, finished=%d\n",
			   snapshot->pending_list[i].seqno,
			   snapshot->pending_list[i].fence,
			   snapshot->pending_list[i].finished);
}

/**
 * xe_guc_engine_snapshot_free - Free all allocated objects for a given
 * snapshot.
 * @snapshot: GuC Submit Engine snapshot object.
 *
 * This function free all the memory that needed to be allocated at capture
 * time.
 */
void xe_guc_engine_snapshot_free(struct xe_guc_submit_engine_snapshot *snapshot)
{
	if (!snapshot)
		return;

	kfree(snapshot->lrc);
	kfree(snapshot->pending_list);
	kfree(snapshot);
}

static void guc_engine_print(struct xe_engine *e, struct drm_printer *p)
{
	struct xe_guc_submit_engine_snapshot *snapshot;

	snapshot = xe_guc_engine_snapshot_capture(e);
	xe_guc_engine_snapshot_print(snapshot, p);
	xe_guc_engine_snapshot_free(snapshot);
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
	struct xe_engine *e;
	unsigned long index;

	if (!xe_device_guc_submission_enabled(guc_to_xe(guc)))
		return;

	mutex_lock(&guc->submission_state.lock);
	xa_for_each(&guc->submission_state.engine_lookup, index, e)
		guc_engine_print(e, p);
	mutex_unlock(&guc->submission_state.lock);
}
