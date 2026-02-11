// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_exec_queue.h"

#include <linux/nospec.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <uapi/drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_dep_scheduler.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sriov_pf.h"
#include "xe_gt_sriov_vf.h"
#include "xe_hw_engine_class_sysfs.h"
#include "xe_hw_engine_group.h"
#include "xe_irq.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_migrate.h"
#include "xe_pm.h"
#include "xe_trace.h"
#include "xe_vm.h"
#include "xe_pxp.h"

/**
 * DOC: Execution Queue
 *
 * An Execution queue is an interface for the HW context of execution.
 * The user creates an execution queue, submits the GPU jobs through those
 * queues and in the end destroys them.
 *
 * Execution queues can also be created by XeKMD itself for driver internal
 * operations like object migration etc.
 *
 * An execution queue is associated with a specified HW engine or a group of
 * engines (belonging to the same tile and engine class) and any GPU job
 * submitted on the queue will be run on one of these engines.
 *
 * An execution queue is tied to an address space (VM). It holds a reference
 * of the associated VM and the underlying Logical Ring Context/s (LRC/s)
 * until the queue is destroyed.
 *
 * The execution queue sits on top of the submission backend. It opaquely
 * handles the GuC and Execlist backends whichever the platform uses, and
 * the ring operations the different engine classes support.
 */

/**
 * DOC: Multi Queue Group
 *
 * Multi Queue Group is another mode of execution supported by the compute
 * and blitter copy command streamers (CCS and BCS, respectively). It is
 * an enhancement of the existing hardware architecture and leverages the
 * same submission model. It enables support for efficient, parallel
 * execution of multiple queues within a single shared context. The multi
 * queue group functionality is only supported with GuC submission backend.
 * All the queues of a group must use the same address space (VM).
 *
 * The DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE execution queue property
 * supports creating a multi queue group and adding queues to a queue group.
 *
 * The XE_EXEC_QUEUE_CREATE ioctl call with above property with value field
 * set to DRM_XE_MULTI_GROUP_CREATE, will create a new multi queue group with
 * the queue being created as the primary queue (aka q0) of the group. To add
 * secondary queues to the group, they need to be created with the above
 * property with id of the primary queue as the value. The properties of
 * the primary queue (like priority, time slice) applies to the whole group.
 * So, these properties can't be set for secondary queues of a group.
 *
 * The hardware does not support removing a queue from a multi-queue group.
 * However, queues can be dynamically added to the group. A group can have
 * up to 64 queues. To support this, XeKMD holds references to LRCs of the
 * queues even after the queues are destroyed by the user until the whole
 * group is destroyed. The secondary queues hold a reference to the primary
 * queue thus preventing the group from being destroyed when user destroys
 * the primary queue. Once the primary queue is destroyed, secondary queues
 * can't be added to the queue group and new job submissions on existing
 * secondary queues are not allowed.
 *
 * The queues of a multi queue group can set their priority within the group
 * through the DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE_PRIORITY property.
 * This multi queue priority can also be set dynamically through the
 * XE_EXEC_QUEUE_SET_PROPERTY ioctl. This is the only other property
 * supported by the secondary queues of a multi queue group, other than
 * DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE.
 *
 * When GuC reports an error on any of the queues of a multi queue group,
 * the queue cleanup mechanism is invoked for all the queues of the group
 * as hardware cannot make progress on the multi queue context.
 *
 * Refer :ref:`multi-queue-group-guc-interface` for multi queue group GuC
 * interface.
 */

enum xe_exec_queue_sched_prop {
	XE_EXEC_QUEUE_JOB_TIMEOUT = 0,
	XE_EXEC_QUEUE_TIMESLICE = 1,
	XE_EXEC_QUEUE_PREEMPT_TIMEOUT = 2,
	XE_EXEC_QUEUE_SCHED_PROP_MAX = 3,
};

static int exec_queue_user_extensions(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 extensions);

static void xe_exec_queue_group_cleanup(struct xe_exec_queue *q)
{
	struct xe_exec_queue_group *group = q->multi_queue.group;
	struct xe_lrc *lrc;
	unsigned long idx;

	if (xe_exec_queue_is_multi_queue_secondary(q)) {
		/*
		 * Put pairs with get from xe_exec_queue_lookup() call
		 * in xe_exec_queue_group_validate().
		 */
		xe_exec_queue_put(xe_exec_queue_multi_queue_primary(q));
		return;
	}

	if (!group)
		return;

	/* Primary queue cleanup */
	xa_for_each(&group->xa, idx, lrc)
		xe_lrc_put(lrc);

	xa_destroy(&group->xa);
	mutex_destroy(&group->list_lock);
	xe_bo_unpin_map_no_vm(group->cgp_bo);
	kfree(group);
}

static void __xe_exec_queue_free(struct xe_exec_queue *q)
{
	int i;

	for (i = 0; i < XE_EXEC_QUEUE_TLB_INVAL_COUNT; ++i)
		if (q->tlb_inval[i].dep_scheduler)
			xe_dep_scheduler_fini(q->tlb_inval[i].dep_scheduler);

	if (xe_exec_queue_uses_pxp(q))
		xe_pxp_exec_queue_remove(gt_to_xe(q->gt)->pxp, q);

	if (xe_exec_queue_is_multi_queue(q))
		xe_exec_queue_group_cleanup(q);

	if (q->vm)
		xe_vm_put(q->vm);

	if (q->xef)
		xe_file_put(q->xef);

	kvfree(q->replay_state);
	kfree(q);
}

static int alloc_dep_schedulers(struct xe_device *xe, struct xe_exec_queue *q)
{
	struct xe_tile *tile = gt_to_tile(q->gt);
	int i;

	for (i = 0; i < XE_EXEC_QUEUE_TLB_INVAL_COUNT; ++i) {
		struct xe_dep_scheduler *dep_scheduler;
		struct xe_gt *gt;
		struct workqueue_struct *wq;

		if (i == XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT)
			gt = tile->primary_gt;
		else
			gt = tile->media_gt;

		if (!gt)
			continue;

		wq = gt->tlb_inval.job_wq;

#define MAX_TLB_INVAL_JOBS	16	/* Picking a reasonable value */
		dep_scheduler = xe_dep_scheduler_create(xe, wq, q->name,
							MAX_TLB_INVAL_JOBS);
		if (IS_ERR(dep_scheduler))
			return PTR_ERR(dep_scheduler);

		q->tlb_inval[i].dep_scheduler = dep_scheduler;
	}
#undef MAX_TLB_INVAL_JOBS

	return 0;
}

static struct xe_exec_queue *__xe_exec_queue_alloc(struct xe_device *xe,
						   struct xe_vm *vm,
						   u32 logical_mask,
						   u16 width, struct xe_hw_engine *hwe,
						   u32 flags, u64 extensions)
{
	struct xe_exec_queue *q;
	struct xe_gt *gt = hwe->gt;
	int err;

	/* only kernel queues can be permanent */
	XE_WARN_ON((flags & EXEC_QUEUE_FLAG_PERMANENT) && !(flags & EXEC_QUEUE_FLAG_KERNEL));

	q = kzalloc(struct_size(q, lrc, width), GFP_KERNEL);
	if (!q)
		return ERR_PTR(-ENOMEM);

	kref_init(&q->refcount);
	q->flags = flags;
	q->hwe = hwe;
	q->gt = gt;
	q->class = hwe->class;
	q->width = width;
	q->msix_vec = XE_IRQ_DEFAULT_MSIX;
	q->logical_mask = logical_mask;
	q->fence_irq = &gt->fence_irq[hwe->class];
	q->ring_ops = gt->ring_ops[hwe->class];
	q->ops = gt->exec_queue_ops;
	INIT_LIST_HEAD(&q->lr.link);
	INIT_LIST_HEAD(&q->multi_gt_link);
	INIT_LIST_HEAD(&q->hw_engine_group_link);
	INIT_LIST_HEAD(&q->pxp.link);
	q->multi_queue.priority = XE_MULTI_QUEUE_PRIORITY_NORMAL;

	q->sched_props.timeslice_us = hwe->eclass->sched_props.timeslice_us;
	q->sched_props.preempt_timeout_us =
				hwe->eclass->sched_props.preempt_timeout_us;
	q->sched_props.job_timeout_ms =
				hwe->eclass->sched_props.job_timeout_ms;
	if (q->flags & EXEC_QUEUE_FLAG_KERNEL &&
	    q->flags & EXEC_QUEUE_FLAG_HIGH_PRIORITY)
		q->sched_props.priority = XE_EXEC_QUEUE_PRIORITY_KERNEL;
	else
		q->sched_props.priority = XE_EXEC_QUEUE_PRIORITY_NORMAL;

	if (q->flags & (EXEC_QUEUE_FLAG_MIGRATE | EXEC_QUEUE_FLAG_VM)) {
		err = alloc_dep_schedulers(xe, q);
		if (err) {
			__xe_exec_queue_free(q);
			return ERR_PTR(err);
		}
	}

	if (vm)
		q->vm = xe_vm_get(vm);

	if (extensions) {
		/*
		 * may set q->usm, must come before xe_lrc_create(),
		 * may overwrite q->sched_props, must come before q->ops->init()
		 */
		err = exec_queue_user_extensions(xe, q, extensions);
		if (err) {
			__xe_exec_queue_free(q);
			return ERR_PTR(err);
		}
	}

	return q;
}

static int __xe_exec_queue_init(struct xe_exec_queue *q, u32 exec_queue_flags)
{
	int i, err;
	u32 flags = 0;

	/*
	 * PXP workloads executing on RCS or CCS must run in isolation (i.e. no
	 * other workload can use the EUs at the same time). On MTL this is done
	 * by setting the RUNALONE bit in the LRC, while starting on Xe2 there
	 * is a dedicated bit for it.
	 */
	if (xe_exec_queue_uses_pxp(q) &&
	    (q->class == XE_ENGINE_CLASS_RENDER || q->class == XE_ENGINE_CLASS_COMPUTE)) {
		if (GRAPHICS_VER(gt_to_xe(q->gt)) >= 20)
			flags |= XE_LRC_CREATE_PXP;
		else
			flags |= XE_LRC_CREATE_RUNALONE;
	}

	if (!(exec_queue_flags & EXEC_QUEUE_FLAG_KERNEL))
		flags |= XE_LRC_CREATE_USER_CTX;

	err = q->ops->init(q);
	if (err)
		return err;

	/*
	 * This must occur after q->ops->init to avoid race conditions during VF
	 * post-migration recovery, as the fixups for the LRC GGTT addresses
	 * depend on the queue being present in the backend tracking structure.
	 *
	 * In addition to above, we must wait on inflight GGTT changes to avoid
	 * writing out stale values here. Such wait provides a solid solution
	 * (without a race) only if the function can detect migration instantly
	 * from the moment vCPU resumes execution.
	 */
	for (i = 0; i < q->width; ++i) {
		struct xe_lrc *lrc;

		xe_gt_sriov_vf_wait_valid_ggtt(q->gt);
		lrc = xe_lrc_create(q->hwe, q->vm, q->replay_state,
				    xe_lrc_ring_size(), q->msix_vec, flags);
		if (IS_ERR(lrc)) {
			err = PTR_ERR(lrc);
			goto err_lrc;
		}

		/* Pairs with READ_ONCE to xe_exec_queue_contexts_hwsp_rebase */
		WRITE_ONCE(q->lrc[i], lrc);
	}

	return 0;

err_lrc:
	for (i = i - 1; i >= 0; --i)
		xe_lrc_put(q->lrc[i]);
	return err;
}

static void __xe_exec_queue_fini(struct xe_exec_queue *q)
{
	int i;

	q->ops->fini(q);

	for (i = 0; i < q->width; ++i)
		xe_lrc_put(q->lrc[i]);
}

struct xe_exec_queue *xe_exec_queue_create(struct xe_device *xe, struct xe_vm *vm,
					   u32 logical_mask, u16 width,
					   struct xe_hw_engine *hwe, u32 flags,
					   u64 extensions)
{
	struct xe_exec_queue *q;
	int err;

	/* VMs for GSCCS queues (and only those) must have the XE_VM_FLAG_GSC flag */
	xe_assert(xe, !vm || (!!(vm->flags & XE_VM_FLAG_GSC) == !!(hwe->engine_id == XE_HW_ENGINE_GSCCS0)));

	q = __xe_exec_queue_alloc(xe, vm, logical_mask, width, hwe, flags,
				  extensions);
	if (IS_ERR(q))
		return q;

	err = __xe_exec_queue_init(q, flags);
	if (err)
		goto err_post_alloc;

	/*
	 * We can only add the queue to the PXP list after the init is complete,
	 * because the PXP termination can call exec_queue_kill and that will
	 * go bad if the queue is only half-initialized. This means that we
	 * can't do it when we handle the PXP extension in __xe_exec_queue_alloc
	 * and we need to do it here instead.
	 */
	if (xe_exec_queue_uses_pxp(q)) {
		err = xe_pxp_exec_queue_add(xe->pxp, q);
		if (err)
			goto err_post_init;
	}

	return q;

err_post_init:
	__xe_exec_queue_fini(q);
err_post_alloc:
	__xe_exec_queue_free(q);
	return ERR_PTR(err);
}
ALLOW_ERROR_INJECTION(xe_exec_queue_create, ERRNO);

struct xe_exec_queue *xe_exec_queue_create_class(struct xe_device *xe, struct xe_gt *gt,
						 struct xe_vm *vm,
						 enum xe_engine_class class,
						 u32 flags, u64 extensions)
{
	struct xe_hw_engine *hwe, *hwe0 = NULL;
	enum xe_hw_engine_id id;
	u32 logical_mask = 0;

	for_each_hw_engine(hwe, gt, id) {
		if (xe_hw_engine_is_reserved(hwe))
			continue;

		if (hwe->class == class) {
			logical_mask |= BIT(hwe->logical_instance);
			if (!hwe0)
				hwe0 = hwe;
		}
	}

	if (!logical_mask)
		return ERR_PTR(-ENODEV);

	return xe_exec_queue_create(xe, vm, logical_mask, 1, hwe0, flags, extensions);
}

/**
 * xe_exec_queue_create_bind() - Create bind exec queue.
 * @xe: Xe device.
 * @tile: tile which bind exec queue belongs to.
 * @flags: exec queue creation flags
 * @user_vm: The user VM which this exec queue belongs to
 * @extensions: exec queue creation extensions
 *
 * Normalize bind exec queue creation. Bind exec queue is tied to migration VM
 * for access to physical memory required for page table programming. On a
 * faulting devices the reserved copy engine instance must be used to avoid
 * deadlocking (user binds cannot get stuck behind faults as kernel binds which
 * resolve faults depend on user binds). On non-faulting devices any copy engine
 * can be used.
 *
 * Returns exec queue on success, ERR_PTR on failure
 */
struct xe_exec_queue *xe_exec_queue_create_bind(struct xe_device *xe,
						struct xe_tile *tile,
						struct xe_vm *user_vm,
						u32 flags, u64 extensions)
{
	struct xe_gt *gt = tile->primary_gt;
	struct xe_exec_queue *q;
	struct xe_vm *migrate_vm;

	migrate_vm = xe_migrate_get_vm(tile->migrate);
	if (xe->info.has_usm) {
		struct xe_hw_engine *hwe = xe_gt_hw_engine(gt,
							   XE_ENGINE_CLASS_COPY,
							   gt->usm.reserved_bcs_instance,
							   false);

		if (!hwe) {
			xe_vm_put(migrate_vm);
			return ERR_PTR(-EINVAL);
		}

		q = xe_exec_queue_create(xe, migrate_vm,
					 BIT(hwe->logical_instance), 1, hwe,
					 flags, extensions);
	} else {
		q = xe_exec_queue_create_class(xe, gt, migrate_vm,
					       XE_ENGINE_CLASS_COPY, flags,
					       extensions);
	}
	xe_vm_put(migrate_vm);

	if (!IS_ERR(q)) {
		int err = drm_syncobj_create(&q->ufence_syncobj,
					     DRM_SYNCOBJ_CREATE_SIGNALED,
					     NULL);
		if (err) {
			xe_exec_queue_put(q);
			return ERR_PTR(err);
		}

		if (user_vm)
			q->user_vm = xe_vm_get(user_vm);
	}

	return q;
}
ALLOW_ERROR_INJECTION(xe_exec_queue_create_bind, ERRNO);

void xe_exec_queue_destroy(struct kref *ref)
{
	struct xe_exec_queue *q = container_of(ref, struct xe_exec_queue, refcount);
	struct xe_exec_queue *eq, *next;
	int i;

	xe_assert(gt_to_xe(q->gt), atomic_read(&q->job_cnt) == 0);

	if (q->ufence_syncobj)
		drm_syncobj_put(q->ufence_syncobj);

	if (xe_exec_queue_uses_pxp(q))
		xe_pxp_exec_queue_remove(gt_to_xe(q->gt)->pxp, q);

	xe_exec_queue_last_fence_put_unlocked(q);
	for_each_tlb_inval(i)
		xe_exec_queue_tlb_inval_last_fence_put_unlocked(q, i);

	if (!(q->flags & EXEC_QUEUE_FLAG_BIND_ENGINE_CHILD)) {
		list_for_each_entry_safe(eq, next, &q->multi_gt_list,
					 multi_gt_link)
			xe_exec_queue_put(eq);
	}

	if (q->user_vm) {
		xe_vm_put(q->user_vm);
		q->user_vm = NULL;
	}

	q->ops->destroy(q);
}

void xe_exec_queue_fini(struct xe_exec_queue *q)
{
	/*
	 * Before releasing our ref to lrc and xef, accumulate our run ticks
	 * and wakeup any waiters.
	 */
	xe_exec_queue_update_run_ticks(q);
	if (q->xef && atomic_dec_and_test(&q->xef->exec_queue.pending_removal))
		wake_up_var(&q->xef->exec_queue.pending_removal);

	__xe_exec_queue_fini(q);
	__xe_exec_queue_free(q);
}

void xe_exec_queue_assign_name(struct xe_exec_queue *q, u32 instance)
{
	switch (q->class) {
	case XE_ENGINE_CLASS_RENDER:
		snprintf(q->name, sizeof(q->name), "rcs%d", instance);
		break;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		snprintf(q->name, sizeof(q->name), "vcs%d", instance);
		break;
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		snprintf(q->name, sizeof(q->name), "vecs%d", instance);
		break;
	case XE_ENGINE_CLASS_COPY:
		snprintf(q->name, sizeof(q->name), "bcs%d", instance);
		break;
	case XE_ENGINE_CLASS_COMPUTE:
		snprintf(q->name, sizeof(q->name), "ccs%d", instance);
		break;
	case XE_ENGINE_CLASS_OTHER:
		snprintf(q->name, sizeof(q->name), "gsccs%d", instance);
		break;
	default:
		XE_WARN_ON(q->class);
	}
}

struct xe_exec_queue *xe_exec_queue_lookup(struct xe_file *xef, u32 id)
{
	struct xe_exec_queue *q;

	mutex_lock(&xef->exec_queue.lock);
	q = xa_load(&xef->exec_queue.xa, id);
	if (q)
		xe_exec_queue_get(q);
	mutex_unlock(&xef->exec_queue.lock);

	return q;
}

enum xe_exec_queue_priority
xe_exec_queue_device_get_max_priority(struct xe_device *xe)
{
	return capable(CAP_SYS_NICE) ? XE_EXEC_QUEUE_PRIORITY_HIGH :
				       XE_EXEC_QUEUE_PRIORITY_NORMAL;
}

static int exec_queue_set_priority(struct xe_device *xe, struct xe_exec_queue *q,
				   u64 value)
{
	if (XE_IOCTL_DBG(xe, value > XE_EXEC_QUEUE_PRIORITY_HIGH))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, value > xe_exec_queue_device_get_max_priority(xe)))
		return -EPERM;

	q->sched_props.priority = value;
	return 0;
}

static bool xe_exec_queue_enforce_schedule_limit(void)
{
#if IS_ENABLED(CONFIG_DRM_XE_ENABLE_SCHEDTIMEOUT_LIMIT)
	return true;
#else
	return !capable(CAP_SYS_NICE);
#endif
}

static void
xe_exec_queue_get_prop_minmax(struct xe_hw_engine_class_intf *eclass,
			      enum xe_exec_queue_sched_prop prop,
			      u32 *min, u32 *max)
{
	switch (prop) {
	case XE_EXEC_QUEUE_JOB_TIMEOUT:
		*min = eclass->sched_props.job_timeout_min;
		*max = eclass->sched_props.job_timeout_max;
		break;
	case XE_EXEC_QUEUE_TIMESLICE:
		*min = eclass->sched_props.timeslice_min;
		*max = eclass->sched_props.timeslice_max;
		break;
	case XE_EXEC_QUEUE_PREEMPT_TIMEOUT:
		*min = eclass->sched_props.preempt_timeout_min;
		*max = eclass->sched_props.preempt_timeout_max;
		break;
	default:
		break;
	}
#if IS_ENABLED(CONFIG_DRM_XE_ENABLE_SCHEDTIMEOUT_LIMIT)
	if (capable(CAP_SYS_NICE)) {
		switch (prop) {
		case XE_EXEC_QUEUE_JOB_TIMEOUT:
			*min = XE_HW_ENGINE_JOB_TIMEOUT_MIN;
			*max = XE_HW_ENGINE_JOB_TIMEOUT_MAX;
			break;
		case XE_EXEC_QUEUE_TIMESLICE:
			*min = XE_HW_ENGINE_TIMESLICE_MIN;
			*max = XE_HW_ENGINE_TIMESLICE_MAX;
			break;
		case XE_EXEC_QUEUE_PREEMPT_TIMEOUT:
			*min = XE_HW_ENGINE_PREEMPT_TIMEOUT_MIN;
			*max = XE_HW_ENGINE_PREEMPT_TIMEOUT_MAX;
			break;
		default:
			break;
		}
	}
#endif
}

static int exec_queue_set_timeslice(struct xe_device *xe, struct xe_exec_queue *q,
				    u64 value)
{
	u32 min = 0, max = 0;

	xe_exec_queue_get_prop_minmax(q->hwe->eclass,
				      XE_EXEC_QUEUE_TIMESLICE, &min, &max);

	if (xe_exec_queue_enforce_schedule_limit() &&
	    !xe_hw_engine_timeout_in_range(value, min, max))
		return -EINVAL;

	q->sched_props.timeslice_us = value;
	return 0;
}

static int
exec_queue_set_pxp_type(struct xe_device *xe, struct xe_exec_queue *q, u64 value)
{
	if (value == DRM_XE_PXP_TYPE_NONE)
		return 0;

	/* we only support HWDRM sessions right now */
	if (XE_IOCTL_DBG(xe, value != DRM_XE_PXP_TYPE_HWDRM))
		return -EINVAL;

	if (!xe_pxp_is_enabled(xe->pxp))
		return -ENODEV;

	return xe_pxp_exec_queue_set_type(xe->pxp, q, DRM_XE_PXP_TYPE_HWDRM);
}

static int exec_queue_set_hang_replay_state(struct xe_device *xe,
					    struct xe_exec_queue *q,
					    u64 value)
{
	size_t size = xe_gt_lrc_hang_replay_size(q->gt, q->class);
	u64 __user *address = u64_to_user_ptr(value);
	void *ptr;

	ptr = vmemdup_user(address, size);
	if (XE_IOCTL_DBG(xe, IS_ERR(ptr)))
		return PTR_ERR(ptr);

	q->replay_state = ptr;

	return 0;
}

static int xe_exec_queue_group_init(struct xe_device *xe, struct xe_exec_queue *q)
{
	struct xe_tile *tile = gt_to_tile(q->gt);
	struct xe_exec_queue_group *group;
	struct xe_bo *bo;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	bo = xe_bo_create_pin_map_novm(xe, tile, SZ_4K, ttm_bo_type_kernel,
				       XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				       XE_BO_FLAG_PINNED_LATE_RESTORE |
				       XE_BO_FLAG_FORCE_USER_VRAM |
				       XE_BO_FLAG_GGTT_INVALIDATE |
				       XE_BO_FLAG_GGTT, false);
	if (IS_ERR(bo)) {
		drm_err(&xe->drm, "CGP bo allocation for queue group failed: %ld\n",
			PTR_ERR(bo));
		kfree(group);
		return PTR_ERR(bo);
	}

	xe_map_memset(xe, &bo->vmap, 0, 0, SZ_4K);

	group->primary = q;
	group->cgp_bo = bo;
	INIT_LIST_HEAD(&group->list);
	xa_init_flags(&group->xa, XA_FLAGS_ALLOC1);
	mutex_init(&group->list_lock);
	q->multi_queue.group = group;

	/* group->list_lock is used in submission backend */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		might_lock(&group->list_lock);
		fs_reclaim_release(GFP_KERNEL);
	}

	return 0;
}

static inline bool xe_exec_queue_supports_multi_queue(struct xe_exec_queue *q)
{
	return q->gt->info.multi_queue_engine_class_mask & BIT(q->class);
}

static int xe_exec_queue_group_validate(struct xe_device *xe, struct xe_exec_queue *q,
					u32 primary_id)
{
	struct xe_exec_queue_group *group;
	struct xe_exec_queue *primary;
	int ret;

	/*
	 * Get from below xe_exec_queue_lookup() pairs with put
	 * in xe_exec_queue_group_cleanup().
	 */
	primary = xe_exec_queue_lookup(q->vm->xef, primary_id);
	if (XE_IOCTL_DBG(xe, !primary))
		return -ENOENT;

	if (XE_IOCTL_DBG(xe, !xe_exec_queue_is_multi_queue_primary(primary)) ||
	    XE_IOCTL_DBG(xe, q->vm != primary->vm) ||
	    XE_IOCTL_DBG(xe, q->logical_mask != primary->logical_mask)) {
		ret = -EINVAL;
		goto put_primary;
	}

	group = primary->multi_queue.group;
	q->multi_queue.valid = true;
	q->multi_queue.group = group;

	return 0;
put_primary:
	xe_exec_queue_put(primary);
	return ret;
}

#define XE_MAX_GROUP_SIZE	64
static int xe_exec_queue_group_add(struct xe_device *xe, struct xe_exec_queue *q)
{
	struct xe_exec_queue_group *group = q->multi_queue.group;
	u32 pos;
	int err;

	xe_assert(xe, xe_exec_queue_is_multi_queue_secondary(q));

	/* Primary queue holds a reference to LRCs of all secondary queues */
	err = xa_alloc(&group->xa, &pos, xe_lrc_get(q->lrc[0]),
		       XA_LIMIT(1, XE_MAX_GROUP_SIZE - 1), GFP_KERNEL);
	if (XE_IOCTL_DBG(xe, err)) {
		xe_lrc_put(q->lrc[0]);

		/* It is invalid if queue group limit is exceeded */
		if (err == -EBUSY)
			err = -EINVAL;

		return err;
	}

	q->multi_queue.pos = pos;

	return 0;
}

static void xe_exec_queue_group_delete(struct xe_device *xe, struct xe_exec_queue *q)
{
	struct xe_exec_queue_group *group = q->multi_queue.group;
	struct xe_lrc *lrc;

	xe_assert(xe, xe_exec_queue_is_multi_queue_secondary(q));

	lrc = xa_erase(&group->xa, q->multi_queue.pos);
	xe_assert(xe, lrc);
	xe_lrc_put(lrc);
}

static int exec_queue_set_multi_group(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 value)
{
	if (XE_IOCTL_DBG(xe, !xe_exec_queue_supports_multi_queue(q)))
		return -ENODEV;

	if (XE_IOCTL_DBG(xe, !xe_device_uc_enabled(xe)))
		return -EOPNOTSUPP;

	if (XE_IOCTL_DBG(xe, !q->vm->xef))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, xe_exec_queue_is_parallel(q)))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, xe_exec_queue_is_multi_queue(q)))
		return -EINVAL;

	if (value & DRM_XE_MULTI_GROUP_CREATE) {
		if (XE_IOCTL_DBG(xe, value & ~DRM_XE_MULTI_GROUP_CREATE))
			return -EINVAL;

		q->multi_queue.valid = true;
		q->multi_queue.is_primary = true;
		q->multi_queue.pos = 0;
		return 0;
	}

	/* While adding secondary queues, the upper 32 bits must be 0 */
	if (XE_IOCTL_DBG(xe, value & (~0ull << 32)))
		return -EINVAL;

	return xe_exec_queue_group_validate(xe, q, value);
}

static int exec_queue_set_multi_queue_priority(struct xe_device *xe, struct xe_exec_queue *q,
					       u64 value)
{
	if (XE_IOCTL_DBG(xe, value > XE_MULTI_QUEUE_PRIORITY_HIGH))
		return -EINVAL;

	/* For queue creation time (!q->xef) setting, just store the priority value */
	if (!q->xef) {
		q->multi_queue.priority = value;
		return 0;
	}

	if (!xe_exec_queue_is_multi_queue(q))
		return -EINVAL;

	return q->ops->set_multi_queue_priority(q, value);
}

typedef int (*xe_exec_queue_set_property_fn)(struct xe_device *xe,
					     struct xe_exec_queue *q,
					     u64 value);

static const xe_exec_queue_set_property_fn exec_queue_set_property_funcs[] = {
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY] = exec_queue_set_priority,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_TIMESLICE] = exec_queue_set_timeslice,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_PXP_TYPE] = exec_queue_set_pxp_type,
	[DRM_XE_EXEC_QUEUE_SET_HANG_REPLAY_STATE] = exec_queue_set_hang_replay_state,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_GROUP] = exec_queue_set_multi_group,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE_PRIORITY] =
							exec_queue_set_multi_queue_priority,
};

int xe_exec_queue_set_property_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_exec_queue_set_property *args = data;
	struct xe_exec_queue *q;
	int ret;
	u32 idx;

	if (XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->property !=
			 DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE_PRIORITY))
		return -EINVAL;

	q = xe_exec_queue_lookup(xef, args->exec_queue_id);
	if (XE_IOCTL_DBG(xe, !q))
		return -ENOENT;

	idx = array_index_nospec(args->property,
				 ARRAY_SIZE(exec_queue_set_property_funcs));
	ret = exec_queue_set_property_funcs[idx](xe, q, args->value);
	if (XE_IOCTL_DBG(xe, ret))
		goto err_post_lookup;

	xe_exec_queue_put(q);
	return 0;

 err_post_lookup:
	xe_exec_queue_put(q);
	return ret;
}

static int exec_queue_user_ext_check(struct xe_exec_queue *q, u64 properties)
{
	u64 secondary_queue_valid_props = BIT_ULL(DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_GROUP) |
				  BIT_ULL(DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE_PRIORITY);

	/*
	 * Only MULTI_QUEUE_PRIORITY property is valid for secondary queues of a
	 * multi-queue group.
	 */
	if (xe_exec_queue_is_multi_queue_secondary(q) &&
	    properties & ~secondary_queue_valid_props)
		return -EINVAL;

	return 0;
}

static int exec_queue_user_ext_check_final(struct xe_exec_queue *q, u64 properties)
{
	/* MULTI_QUEUE_PRIORITY only applies to multi-queue group queues */
	if ((properties & BIT_ULL(DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE_PRIORITY)) &&
	    !(properties & BIT_ULL(DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_GROUP)))
		return -EINVAL;

	return 0;
}

static int exec_queue_user_ext_set_property(struct xe_device *xe,
					    struct xe_exec_queue *q,
					    u64 extension, u64 *properties)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_ext_set_property ext;
	int err;
	u32 idx;

	err = copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, ext.property >=
			 ARRAY_SIZE(exec_queue_set_property_funcs)) ||
	    XE_IOCTL_DBG(xe, ext.pad) ||
	    XE_IOCTL_DBG(xe, ext.property != DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY &&
			 ext.property != DRM_XE_EXEC_QUEUE_SET_PROPERTY_TIMESLICE &&
			 ext.property != DRM_XE_EXEC_QUEUE_SET_PROPERTY_PXP_TYPE &&
			 ext.property != DRM_XE_EXEC_QUEUE_SET_HANG_REPLAY_STATE &&
			 ext.property != DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_GROUP &&
			 ext.property != DRM_XE_EXEC_QUEUE_SET_PROPERTY_MULTI_QUEUE_PRIORITY))
		return -EINVAL;

	idx = array_index_nospec(ext.property, ARRAY_SIZE(exec_queue_set_property_funcs));
	if (!exec_queue_set_property_funcs[idx])
		return -EINVAL;

	*properties |= BIT_ULL(idx);
	err = exec_queue_user_ext_check(q, *properties);
	if (XE_IOCTL_DBG(xe, err))
		return err;

	return exec_queue_set_property_funcs[idx](xe, q, ext.value);
}

typedef int (*xe_exec_queue_user_extension_fn)(struct xe_device *xe,
					       struct xe_exec_queue *q,
					       u64 extension, u64 *properties);

static const xe_exec_queue_user_extension_fn exec_queue_user_extension_funcs[] = {
	[DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY] = exec_queue_user_ext_set_property,
};

#define MAX_USER_EXTENSIONS	16
static int __exec_queue_user_extensions(struct xe_device *xe, struct xe_exec_queue *q,
					u64 extensions, int ext_number, u64 *properties)
{
	u64 __user *address = u64_to_user_ptr(extensions);
	struct drm_xe_user_extension ext;
	int err;
	u32 idx;

	if (XE_IOCTL_DBG(xe, ext_number >= MAX_USER_EXTENSIONS))
		return -E2BIG;

	err = copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, ext.pad) ||
	    XE_IOCTL_DBG(xe, ext.name >=
			 ARRAY_SIZE(exec_queue_user_extension_funcs)))
		return -EINVAL;

	idx = array_index_nospec(ext.name,
				 ARRAY_SIZE(exec_queue_user_extension_funcs));
	err = exec_queue_user_extension_funcs[idx](xe, q, extensions, properties);
	if (XE_IOCTL_DBG(xe, err))
		return err;

	if (ext.next_extension)
		return __exec_queue_user_extensions(xe, q, ext.next_extension,
						    ++ext_number, properties);

	return 0;
}

static int exec_queue_user_extensions(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 extensions)
{
	u64 properties = 0;
	int err;

	err = __exec_queue_user_extensions(xe, q, extensions, 0, &properties);
	if (XE_IOCTL_DBG(xe, err))
		return err;

	err = exec_queue_user_ext_check_final(q, properties);
	if (XE_IOCTL_DBG(xe, err))
		return err;

	if (xe_exec_queue_is_multi_queue_primary(q)) {
		err = xe_exec_queue_group_init(xe, q);
		if (XE_IOCTL_DBG(xe, err))
			return err;
	}

	return 0;
}

static u32 calc_validate_logical_mask(struct xe_device *xe,
				      struct drm_xe_engine_class_instance *eci,
				      u16 width, u16 num_placements)
{
	int len = width * num_placements;
	int i, j, n;
	u16 class;
	u16 gt_id;
	u32 return_mask = 0, prev_mask;

	if (XE_IOCTL_DBG(xe, !xe_device_uc_enabled(xe) &&
			 len > 1))
		return 0;

	for (i = 0; i < width; ++i) {
		u32 current_mask = 0;

		for (j = 0; j < num_placements; ++j) {
			struct xe_hw_engine *hwe;

			n = j * width + i;

			hwe = xe_hw_engine_lookup(xe, eci[n]);
			if (XE_IOCTL_DBG(xe, !hwe))
				return 0;

			if (XE_IOCTL_DBG(xe, xe_hw_engine_is_reserved(hwe)))
				return 0;

			if (XE_IOCTL_DBG(xe, n && eci[n].gt_id != gt_id) ||
			    XE_IOCTL_DBG(xe, n && eci[n].engine_class != class))
				return 0;

			class = eci[n].engine_class;
			gt_id = eci[n].gt_id;

			if (width == 1 || !i)
				return_mask |= BIT(eci[n].engine_instance);
			current_mask |= BIT(eci[n].engine_instance);
		}

		/* Parallel submissions must be logically contiguous */
		if (i && XE_IOCTL_DBG(xe, current_mask != prev_mask << 1))
			return 0;

		prev_mask = current_mask;
	}

	return return_mask;
}

static bool has_sched_groups(struct xe_gt *gt)
{
	if (IS_SRIOV_PF(gt_to_xe(gt)) && xe_gt_sriov_pf_sched_groups_enabled(gt))
		return true;

	if (IS_SRIOV_VF(gt_to_xe(gt)) && xe_gt_sriov_vf_sched_groups_enabled(gt))
		return true;

	return false;
}

int xe_exec_queue_create_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_exec_queue_create *args = data;
	struct drm_xe_engine_class_instance eci[XE_HW_ENGINE_MAX_INSTANCE];
	struct drm_xe_engine_class_instance __user *user_eci =
		u64_to_user_ptr(args->instances);
	struct xe_hw_engine *hwe;
	struct xe_vm *vm;
	struct xe_tile *tile;
	struct xe_exec_queue *q = NULL;
	u32 logical_mask;
	u32 flags = 0;
	u32 id;
	u32 len;
	int err;

	if (XE_IOCTL_DBG(xe, args->flags & ~DRM_XE_EXEC_QUEUE_LOW_LATENCY_HINT) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	len = args->width * args->num_placements;
	if (XE_IOCTL_DBG(xe, !len || len > XE_HW_ENGINE_MAX_INSTANCE))
		return -EINVAL;

	err = copy_from_user(eci, user_eci,
			     sizeof(struct drm_xe_engine_class_instance) * len);
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, !xe_device_get_gt(xe, eci[0].gt_id)))
		return -EINVAL;

	if (args->flags & DRM_XE_EXEC_QUEUE_LOW_LATENCY_HINT)
		flags |= EXEC_QUEUE_FLAG_LOW_LATENCY;

	if (eci[0].engine_class == DRM_XE_ENGINE_CLASS_VM_BIND) {
		if (XE_IOCTL_DBG(xe, args->width != 1) ||
		    XE_IOCTL_DBG(xe, args->num_placements != 1) ||
		    XE_IOCTL_DBG(xe, eci[0].engine_instance != 0))
			return -EINVAL;

		vm = xe_vm_lookup(xef, args->vm_id);
		if (XE_IOCTL_DBG(xe, !vm))
			return -ENOENT;

		err = down_read_interruptible(&vm->lock);
		if (err) {
			xe_vm_put(vm);
			return err;
		}

		if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
			up_read(&vm->lock);
			xe_vm_put(vm);
			return -ENOENT;
		}

		for_each_tile(tile, xe, id) {
			struct xe_exec_queue *new;

			flags |= EXEC_QUEUE_FLAG_VM;
			if (id)
				flags |= EXEC_QUEUE_FLAG_BIND_ENGINE_CHILD;

			new = xe_exec_queue_create_bind(xe, tile, vm, flags,
							args->extensions);
			if (IS_ERR(new)) {
				up_read(&vm->lock);
				xe_vm_put(vm);
				err = PTR_ERR(new);
				if (q)
					goto put_exec_queue;
				return err;
			}
			if (id == 0)
				q = new;
			else
				list_add_tail(&new->multi_gt_list,
					      &q->multi_gt_link);
		}
		up_read(&vm->lock);
		xe_vm_put(vm);
	} else {
		logical_mask = calc_validate_logical_mask(xe, eci,
							  args->width,
							  args->num_placements);
		if (XE_IOCTL_DBG(xe, !logical_mask))
			return -EINVAL;

		hwe = xe_hw_engine_lookup(xe, eci[0]);
		if (XE_IOCTL_DBG(xe, !hwe))
			return -EINVAL;

		vm = xe_vm_lookup(xef, args->vm_id);
		if (XE_IOCTL_DBG(xe, !vm))
			return -ENOENT;

		err = down_read_interruptible(&vm->lock);
		if (err) {
			xe_vm_put(vm);
			return err;
		}

		if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
			up_read(&vm->lock);
			xe_vm_put(vm);
			return -ENOENT;
		}

		/* SRIOV sched groups are not compatible with multi-lrc */
		if (XE_IOCTL_DBG(xe, args->width > 1 && has_sched_groups(hwe->gt))) {
			up_read(&vm->lock);
			xe_vm_put(vm);
			return -EINVAL;
		}

		q = xe_exec_queue_create(xe, vm, logical_mask,
					 args->width, hwe, flags,
					 args->extensions);
		up_read(&vm->lock);
		xe_vm_put(vm);
		if (IS_ERR(q))
			return PTR_ERR(q);

		if (xe_exec_queue_is_multi_queue_secondary(q)) {
			err = xe_exec_queue_group_add(xe, q);
			if (XE_IOCTL_DBG(xe, err))
				goto put_exec_queue;
		}

		if (xe_vm_in_preempt_fence_mode(vm)) {
			q->lr.context = dma_fence_context_alloc(1);

			err = xe_vm_add_compute_exec_queue(vm, q);
			if (XE_IOCTL_DBG(xe, err))
				goto delete_queue_group;
		}

		if (q->vm && q->hwe->hw_engine_group) {
			err = xe_hw_engine_group_add_exec_queue(q->hwe->hw_engine_group, q);
			if (err)
				goto put_exec_queue;
		}
	}

	q->xef = xe_file_get(xef);

	/* user id alloc must always be last in ioctl to prevent UAF */
	err = xa_alloc(&xef->exec_queue.xa, &id, q, xa_limit_32b, GFP_KERNEL);
	if (err)
		goto kill_exec_queue;

	args->exec_queue_id = id;

	return 0;

kill_exec_queue:
	xe_exec_queue_kill(q);
delete_queue_group:
	if (xe_exec_queue_is_multi_queue_secondary(q))
		xe_exec_queue_group_delete(xe, q);
put_exec_queue:
	xe_exec_queue_put(q);
	return err;
}

int xe_exec_queue_get_property_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_exec_queue_get_property *args = data;
	struct xe_exec_queue *q;
	int ret;

	if (XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	q = xe_exec_queue_lookup(xef, args->exec_queue_id);
	if (XE_IOCTL_DBG(xe, !q))
		return -ENOENT;

	switch (args->property) {
	case DRM_XE_EXEC_QUEUE_GET_PROPERTY_BAN:
		args->value = q->ops->reset_status(q);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	xe_exec_queue_put(q);

	return ret;
}

/**
 * xe_exec_queue_lrc() - Get the LRC from exec queue.
 * @q: The exec_queue.
 *
 * Retrieves the primary LRC for the exec queue. Note that this function
 * returns only the first LRC instance, even when multiple parallel LRCs
 * are configured.
 *
 * Return: Pointer to LRC on success, error on failure
 */
struct xe_lrc *xe_exec_queue_lrc(struct xe_exec_queue *q)
{
	return q->lrc[0];
}

/**
 * xe_exec_queue_is_lr() - Whether an exec_queue is long-running
 * @q: The exec_queue
 *
 * Return: True if the exec_queue is long-running, false otherwise.
 */
bool xe_exec_queue_is_lr(struct xe_exec_queue *q)
{
	return q->vm && xe_vm_in_lr_mode(q->vm) &&
		!(q->flags & EXEC_QUEUE_FLAG_VM);
}

/**
 * xe_exec_queue_is_idle() - Whether an exec_queue is idle.
 * @q: The exec_queue
 *
 * FIXME: Need to determine what to use as the short-lived
 * timeline lock for the exec_queues, so that the return value
 * of this function becomes more than just an advisory
 * snapshot in time. The timeline lock must protect the
 * seqno from racing submissions on the same exec_queue.
 * Typically vm->resv, but user-created timeline locks use the migrate vm
 * and never grabs the migrate vm->resv so we have a race there.
 *
 * Return: True if the exec_queue is idle, false otherwise.
 */
bool xe_exec_queue_is_idle(struct xe_exec_queue *q)
{
	if (xe_exec_queue_is_parallel(q)) {
		int i;

		for (i = 0; i < q->width; ++i) {
			if (xe_lrc_seqno(q->lrc[i]) !=
			    q->lrc[i]->fence_ctx.next_seqno - 1)
				return false;
		}

		return true;
	}

	return xe_lrc_seqno(q->lrc[0]) ==
		q->lrc[0]->fence_ctx.next_seqno - 1;
}

/**
 * xe_exec_queue_update_run_ticks() - Update run time in ticks for this exec queue
 * from hw
 * @q: The exec queue
 *
 * Update the timestamp saved by HW for this exec queue and save run ticks
 * calculated by using the delta from last update.
 */
void xe_exec_queue_update_run_ticks(struct xe_exec_queue *q)
{
	struct xe_device *xe = gt_to_xe(q->gt);
	struct xe_lrc *lrc;
	u64 old_ts, new_ts;
	int idx;

	/*
	 * Jobs that are executed by kernel doesn't have a corresponding xe_file
	 * and thus are not accounted.
	 */
	if (!q->xef)
		return;

	/* Synchronize with unbind while holding the xe file open */
	if (!drm_dev_enter(&xe->drm, &idx))
		return;
	/*
	 * Only sample the first LRC. For parallel submission, all of them are
	 * scheduled together and we compensate that below by multiplying by
	 * width - this may introduce errors if that premise is not true and
	 * they don't exit 100% aligned. On the other hand, looping through
	 * the LRCs and reading them in different time could also introduce
	 * errors.
	 */
	lrc = q->lrc[0];
	new_ts = xe_lrc_update_timestamp(lrc, &old_ts);
	q->xef->run_ticks[q->class] += (new_ts - old_ts) * q->width;

	drm_dev_exit(idx);
}

/**
 * xe_exec_queue_kill - permanently stop all execution from an exec queue
 * @q: The exec queue
 *
 * This function permanently stops all activity on an exec queue. If the queue
 * is actively executing on the HW, it will be kicked off the engine; any
 * pending jobs are discarded and all future submissions are rejected.
 * This function is safe to call multiple times.
 */
void xe_exec_queue_kill(struct xe_exec_queue *q)
{
	struct xe_exec_queue *eq = q, *next;

	list_for_each_entry_safe(eq, next, &eq->multi_gt_list,
				 multi_gt_link) {
		q->ops->kill(eq);
		xe_vm_remove_compute_exec_queue(q->vm, eq);
	}

	q->ops->kill(q);
	xe_vm_remove_compute_exec_queue(q->vm, q);
}

int xe_exec_queue_destroy_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_exec_queue_destroy *args = data;
	struct xe_exec_queue *q;

	if (XE_IOCTL_DBG(xe, args->pad) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	mutex_lock(&xef->exec_queue.lock);
	q = xa_erase(&xef->exec_queue.xa, args->exec_queue_id);
	if (q)
		atomic_inc(&xef->exec_queue.pending_removal);
	mutex_unlock(&xef->exec_queue.lock);

	if (XE_IOCTL_DBG(xe, !q))
		return -ENOENT;

	if (q->vm && q->hwe->hw_engine_group)
		xe_hw_engine_group_del_exec_queue(q->hwe->hw_engine_group, q);

	xe_exec_queue_kill(q);

	trace_xe_exec_queue_close(q);
	xe_exec_queue_put(q);

	return 0;
}

static void xe_exec_queue_last_fence_lockdep_assert(struct xe_exec_queue *q,
						    struct xe_vm *vm)
{
	if (q->flags & EXEC_QUEUE_FLAG_MIGRATE) {
		xe_migrate_job_lock_assert(q);
	} else if (q->flags & EXEC_QUEUE_FLAG_VM) {
		lockdep_assert_held(&vm->lock);
	} else {
		xe_vm_assert_held(vm);
		lockdep_assert_held(&q->hwe->hw_engine_group->mode_sem);
	}
}

/**
 * xe_exec_queue_last_fence_put() - Drop ref to last fence
 * @q: The exec queue
 * @vm: The VM the engine does a bind or exec for
 */
void xe_exec_queue_last_fence_put(struct xe_exec_queue *q, struct xe_vm *vm)
{
	xe_exec_queue_last_fence_lockdep_assert(q, vm);

	xe_exec_queue_last_fence_put_unlocked(q);
}

/**
 * xe_exec_queue_last_fence_put_unlocked() - Drop ref to last fence unlocked
 * @q: The exec queue
 *
 * Only safe to be called from xe_exec_queue_destroy().
 */
void xe_exec_queue_last_fence_put_unlocked(struct xe_exec_queue *q)
{
	if (q->last_fence) {
		dma_fence_put(q->last_fence);
		q->last_fence = NULL;
	}
}

/**
 * xe_exec_queue_last_fence_get() - Get last fence
 * @q: The exec queue
 * @vm: The VM the engine does a bind or exec for
 *
 * Get last fence, takes a ref
 *
 * Returns: last fence if not signaled, dma fence stub if signaled
 */
struct dma_fence *xe_exec_queue_last_fence_get(struct xe_exec_queue *q,
					       struct xe_vm *vm)
{
	struct dma_fence *fence;

	xe_exec_queue_last_fence_lockdep_assert(q, vm);

	if (q->last_fence &&
	    test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &q->last_fence->flags))
		xe_exec_queue_last_fence_put(q, vm);

	fence = q->last_fence ? q->last_fence : dma_fence_get_stub();
	dma_fence_get(fence);
	return fence;
}

/**
 * xe_exec_queue_last_fence_get_for_resume() - Get last fence
 * @q: The exec queue
 * @vm: The VM the engine does a bind or exec for
 *
 * Get last fence, takes a ref. Only safe to be called in the context of
 * resuming the hw engine group's long-running exec queue, when the group
 * semaphore is held.
 *
 * Returns: last fence if not signaled, dma fence stub if signaled
 */
struct dma_fence *xe_exec_queue_last_fence_get_for_resume(struct xe_exec_queue *q,
							  struct xe_vm *vm)
{
	struct dma_fence *fence;

	lockdep_assert_held_write(&q->hwe->hw_engine_group->mode_sem);

	if (q->last_fence &&
	    test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &q->last_fence->flags))
		xe_exec_queue_last_fence_put_unlocked(q);

	fence = q->last_fence ? q->last_fence : dma_fence_get_stub();
	dma_fence_get(fence);
	return fence;
}

/**
 * xe_exec_queue_last_fence_set() - Set last fence
 * @q: The exec queue
 * @vm: The VM the engine does a bind or exec for
 * @fence: The fence
 *
 * Set the last fence for the engine. Increases reference count for fence, when
 * closing engine xe_exec_queue_last_fence_put should be called.
 */
void xe_exec_queue_last_fence_set(struct xe_exec_queue *q, struct xe_vm *vm,
				  struct dma_fence *fence)
{
	xe_exec_queue_last_fence_lockdep_assert(q, vm);
	xe_assert(vm->xe, !dma_fence_is_container(fence));

	xe_exec_queue_last_fence_put(q, vm);
	q->last_fence = dma_fence_get(fence);
}

/**
 * xe_exec_queue_tlb_inval_last_fence_put() - Drop ref to last TLB invalidation fence
 * @q: The exec queue
 * @vm: The VM the engine does a bind for
 * @type: Either primary or media GT
 */
void xe_exec_queue_tlb_inval_last_fence_put(struct xe_exec_queue *q,
					    struct xe_vm *vm,
					    unsigned int type)
{
	xe_exec_queue_last_fence_lockdep_assert(q, vm);
	xe_assert(vm->xe, type == XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT ||
		  type == XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT);

	xe_exec_queue_tlb_inval_last_fence_put_unlocked(q, type);
}

/**
 * xe_exec_queue_tlb_inval_last_fence_put_unlocked() - Drop ref to last TLB
 * invalidation fence unlocked
 * @q: The exec queue
 * @type: Either primary or media GT
 *
 * Only safe to be called from xe_exec_queue_destroy().
 */
void xe_exec_queue_tlb_inval_last_fence_put_unlocked(struct xe_exec_queue *q,
						     unsigned int type)
{
	xe_assert(q->vm->xe, type == XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT ||
		  type == XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT);

	dma_fence_put(q->tlb_inval[type].last_fence);
	q->tlb_inval[type].last_fence = NULL;
}

/**
 * xe_exec_queue_tlb_inval_last_fence_get() - Get last fence for TLB invalidation
 * @q: The exec queue
 * @vm: The VM the engine does a bind for
 * @type: Either primary or media GT
 *
 * Get last fence, takes a ref
 *
 * Returns: last fence if not signaled, dma fence stub if signaled
 */
struct dma_fence *xe_exec_queue_tlb_inval_last_fence_get(struct xe_exec_queue *q,
							 struct xe_vm *vm,
							 unsigned int type)
{
	struct dma_fence *fence;

	xe_exec_queue_last_fence_lockdep_assert(q, vm);
	xe_assert(vm->xe, type == XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT ||
		  type == XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT);
	xe_assert(vm->xe, q->flags & (EXEC_QUEUE_FLAG_VM |
				      EXEC_QUEUE_FLAG_MIGRATE));

	if (q->tlb_inval[type].last_fence &&
	    test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
		     &q->tlb_inval[type].last_fence->flags))
		xe_exec_queue_tlb_inval_last_fence_put(q, vm, type);

	fence = q->tlb_inval[type].last_fence ?: dma_fence_get_stub();
	dma_fence_get(fence);
	return fence;
}

/**
 * xe_exec_queue_tlb_inval_last_fence_set() - Set last fence for TLB invalidation
 * @q: The exec queue
 * @vm: The VM the engine does a bind for
 * @fence: The fence
 * @type: Either primary or media GT
 *
 * Set the last fence for the tlb invalidation type on the queue. Increases
 * reference count for fence, when closing queue
 * xe_exec_queue_tlb_inval_last_fence_put should be called.
 */
void xe_exec_queue_tlb_inval_last_fence_set(struct xe_exec_queue *q,
					    struct xe_vm *vm,
					    struct dma_fence *fence,
					    unsigned int type)
{
	xe_exec_queue_last_fence_lockdep_assert(q, vm);
	xe_assert(vm->xe, type == XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT ||
		  type == XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT);
	xe_assert(vm->xe, q->flags & (EXEC_QUEUE_FLAG_VM |
				      EXEC_QUEUE_FLAG_MIGRATE));
	xe_assert(vm->xe, !dma_fence_is_container(fence));

	xe_exec_queue_tlb_inval_last_fence_put(q, vm, type);
	q->tlb_inval[type].last_fence = dma_fence_get(fence);
}

/**
 * xe_exec_queue_contexts_hwsp_rebase - Re-compute GGTT references
 * within all LRCs of a queue.
 * @q: the &xe_exec_queue struct instance containing target LRCs
 * @scratch: scratch buffer to be used as temporary storage
 *
 * Returns: zero on success, negative error code on failure
 */
int xe_exec_queue_contexts_hwsp_rebase(struct xe_exec_queue *q, void *scratch)
{
	int i;
	int err = 0;

	for (i = 0; i < q->width; ++i) {
		struct xe_lrc *lrc;

		/* Pairs with WRITE_ONCE in __xe_exec_queue_init  */
		lrc = READ_ONCE(q->lrc[i]);
		if (!lrc)
			continue;

		xe_lrc_update_memirq_regs_with_address(lrc, q->hwe, scratch);
		xe_lrc_update_hwctx_regs_with_address(lrc);
		err = xe_lrc_setup_wa_bb_with_scratch(lrc, q->hwe, scratch);
		if (err)
			break;
	}

	return err;
}
