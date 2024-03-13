// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_exec_queue.h"

#include <linux/nospec.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/xe_drm.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_hw_engine_class_sysfs.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_migrate.h"
#include "xe_pm.h"
#include "xe_ring_ops_types.h"
#include "xe_trace.h"
#include "xe_vm.h"

enum xe_exec_queue_sched_prop {
	XE_EXEC_QUEUE_JOB_TIMEOUT = 0,
	XE_EXEC_QUEUE_TIMESLICE = 1,
	XE_EXEC_QUEUE_PREEMPT_TIMEOUT = 2,
	XE_EXEC_QUEUE_SCHED_PROP_MAX = 3,
};

static struct xe_exec_queue *__xe_exec_queue_create(struct xe_device *xe,
						    struct xe_vm *vm,
						    u32 logical_mask,
						    u16 width, struct xe_hw_engine *hwe,
						    u32 flags)
{
	struct xe_exec_queue *q;
	struct xe_gt *gt = hwe->gt;
	int err;
	int i;

	/* only kernel queues can be permanent */
	XE_WARN_ON((flags & EXEC_QUEUE_FLAG_PERMANENT) && !(flags & EXEC_QUEUE_FLAG_KERNEL));

	q = kzalloc(sizeof(*q) + sizeof(struct xe_lrc) * width, GFP_KERNEL);
	if (!q)
		return ERR_PTR(-ENOMEM);

	kref_init(&q->refcount);
	q->flags = flags;
	q->hwe = hwe;
	q->gt = gt;
	if (vm)
		q->vm = xe_vm_get(vm);
	q->class = hwe->class;
	q->width = width;
	q->logical_mask = logical_mask;
	q->fence_irq = &gt->fence_irq[hwe->class];
	q->ring_ops = gt->ring_ops[hwe->class];
	q->ops = gt->exec_queue_ops;
	INIT_LIST_HEAD(&q->persistent.link);
	INIT_LIST_HEAD(&q->compute.link);
	INIT_LIST_HEAD(&q->multi_gt_link);

	q->sched_props.timeslice_us = hwe->eclass->sched_props.timeslice_us;
	q->sched_props.preempt_timeout_us =
				hwe->eclass->sched_props.preempt_timeout_us;
	if (q->flags & EXEC_QUEUE_FLAG_KERNEL &&
	    q->flags & EXEC_QUEUE_FLAG_HIGH_PRIORITY)
		q->sched_props.priority = XE_EXEC_QUEUE_PRIORITY_KERNEL;
	else
		q->sched_props.priority = XE_EXEC_QUEUE_PRIORITY_NORMAL;

	if (xe_exec_queue_is_parallel(q)) {
		q->parallel.composite_fence_ctx = dma_fence_context_alloc(1);
		q->parallel.composite_fence_seqno = XE_FENCE_INITIAL_SEQNO;
	}
	if (q->flags & EXEC_QUEUE_FLAG_VM) {
		q->bind.fence_ctx = dma_fence_context_alloc(1);
		q->bind.fence_seqno = XE_FENCE_INITIAL_SEQNO;
	}

	for (i = 0; i < width; ++i) {
		err = xe_lrc_init(q->lrc + i, hwe, q, vm, SZ_16K);
		if (err)
			goto err_lrc;
	}

	err = q->ops->init(q);
	if (err)
		goto err_lrc;

	/*
	 * Normally the user vm holds an rpm ref to keep the device
	 * awake, and the context holds a ref for the vm, however for
	 * some engines we use the kernels migrate vm underneath which offers no
	 * such rpm ref, or we lack a vm. Make sure we keep a ref here, so we
	 * can perform GuC CT actions when needed. Caller is expected to have
	 * already grabbed the rpm ref outside any sensitive locks.
	 */
	if (!(q->flags & EXEC_QUEUE_FLAG_PERMANENT) && (q->flags & EXEC_QUEUE_FLAG_VM || !vm))
		drm_WARN_ON(&xe->drm, !xe_device_mem_access_get_if_ongoing(xe));

	return q;

err_lrc:
	for (i = i - 1; i >= 0; --i)
		xe_lrc_finish(q->lrc + i);
	kfree(q);
	return ERR_PTR(err);
}

struct xe_exec_queue *xe_exec_queue_create(struct xe_device *xe, struct xe_vm *vm,
					   u32 logical_mask, u16 width,
					   struct xe_hw_engine *hwe, u32 flags)
{
	struct xe_exec_queue *q;
	int err;

	if (vm) {
		err = xe_vm_lock(vm, true);
		if (err)
			return ERR_PTR(err);
	}
	q = __xe_exec_queue_create(xe, vm, logical_mask, width, hwe, flags);
	if (vm)
		xe_vm_unlock(vm);

	return q;
}

struct xe_exec_queue *xe_exec_queue_create_class(struct xe_device *xe, struct xe_gt *gt,
						 struct xe_vm *vm,
						 enum xe_engine_class class, u32 flags)
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

	return xe_exec_queue_create(xe, vm, logical_mask, 1, hwe0, flags);
}

void xe_exec_queue_destroy(struct kref *ref)
{
	struct xe_exec_queue *q = container_of(ref, struct xe_exec_queue, refcount);
	struct xe_exec_queue *eq, *next;

	xe_exec_queue_last_fence_put_unlocked(q);
	if (!(q->flags & EXEC_QUEUE_FLAG_BIND_ENGINE_CHILD)) {
		list_for_each_entry_safe(eq, next, &q->multi_gt_list,
					 multi_gt_link)
			xe_exec_queue_put(eq);
	}

	q->ops->fini(q);
}

void xe_exec_queue_fini(struct xe_exec_queue *q)
{
	int i;

	for (i = 0; i < q->width; ++i)
		xe_lrc_finish(q->lrc + i);
	if (!(q->flags & EXEC_QUEUE_FLAG_PERMANENT) && (q->flags & EXEC_QUEUE_FLAG_VM || !q->vm))
		xe_device_mem_access_put(gt_to_xe(q->gt));
	if (q->vm)
		xe_vm_put(q->vm);

	kfree(q);
}

void xe_exec_queue_assign_name(struct xe_exec_queue *q, u32 instance)
{
	switch (q->class) {
	case XE_ENGINE_CLASS_RENDER:
		sprintf(q->name, "rcs%d", instance);
		break;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		sprintf(q->name, "vcs%d", instance);
		break;
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		sprintf(q->name, "vecs%d", instance);
		break;
	case XE_ENGINE_CLASS_COPY:
		sprintf(q->name, "bcs%d", instance);
		break;
	case XE_ENGINE_CLASS_COMPUTE:
		sprintf(q->name, "ccs%d", instance);
		break;
	case XE_ENGINE_CLASS_OTHER:
		sprintf(q->name, "gsccs%d", instance);
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
				   u64 value, bool create)
{
	if (XE_IOCTL_DBG(xe, value > XE_EXEC_QUEUE_PRIORITY_HIGH))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, value > xe_exec_queue_device_get_max_priority(xe)))
		return -EPERM;

	return q->ops->set_priority(q, value);
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
				    u64 value, bool create)
{
	u32 min = 0, max = 0;

	xe_exec_queue_get_prop_minmax(q->hwe->eclass,
				      XE_EXEC_QUEUE_TIMESLICE, &min, &max);

	if (xe_exec_queue_enforce_schedule_limit() &&
	    !xe_hw_engine_timeout_in_range(value, min, max))
		return -EINVAL;

	return q->ops->set_timeslice(q, value);
}

static int exec_queue_set_preemption_timeout(struct xe_device *xe,
					     struct xe_exec_queue *q, u64 value,
					     bool create)
{
	u32 min = 0, max = 0;

	xe_exec_queue_get_prop_minmax(q->hwe->eclass,
				      XE_EXEC_QUEUE_PREEMPT_TIMEOUT, &min, &max);

	if (xe_exec_queue_enforce_schedule_limit() &&
	    !xe_hw_engine_timeout_in_range(value, min, max))
		return -EINVAL;

	return q->ops->set_preempt_timeout(q, value);
}

static int exec_queue_set_persistence(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 value, bool create)
{
	if (XE_IOCTL_DBG(xe, !create))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, xe_vm_in_preempt_fence_mode(q->vm)))
		return -EINVAL;

	if (value)
		q->flags |= EXEC_QUEUE_FLAG_PERSISTENT;
	else
		q->flags &= ~EXEC_QUEUE_FLAG_PERSISTENT;

	return 0;
}

static int exec_queue_set_job_timeout(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 value, bool create)
{
	u32 min = 0, max = 0;

	if (XE_IOCTL_DBG(xe, !create))
		return -EINVAL;

	xe_exec_queue_get_prop_minmax(q->hwe->eclass,
				      XE_EXEC_QUEUE_JOB_TIMEOUT, &min, &max);

	if (xe_exec_queue_enforce_schedule_limit() &&
	    !xe_hw_engine_timeout_in_range(value, min, max))
		return -EINVAL;

	return q->ops->set_job_timeout(q, value);
}

static int exec_queue_set_acc_trigger(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 value, bool create)
{
	if (XE_IOCTL_DBG(xe, !create))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !xe->info.has_usm))
		return -EINVAL;

	q->usm.acc_trigger = value;

	return 0;
}

static int exec_queue_set_acc_notify(struct xe_device *xe, struct xe_exec_queue *q,
				     u64 value, bool create)
{
	if (XE_IOCTL_DBG(xe, !create))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !xe->info.has_usm))
		return -EINVAL;

	q->usm.acc_notify = value;

	return 0;
}

static int exec_queue_set_acc_granularity(struct xe_device *xe, struct xe_exec_queue *q,
					  u64 value, bool create)
{
	if (XE_IOCTL_DBG(xe, !create))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !xe->info.has_usm))
		return -EINVAL;

	if (value > DRM_XE_ACC_GRANULARITY_64M)
		return -EINVAL;

	q->usm.acc_granularity = value;

	return 0;
}

typedef int (*xe_exec_queue_set_property_fn)(struct xe_device *xe,
					     struct xe_exec_queue *q,
					     u64 value, bool create);

static const xe_exec_queue_set_property_fn exec_queue_set_property_funcs[] = {
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY] = exec_queue_set_priority,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_TIMESLICE] = exec_queue_set_timeslice,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_PREEMPTION_TIMEOUT] = exec_queue_set_preemption_timeout,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_PERSISTENCE] = exec_queue_set_persistence,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_JOB_TIMEOUT] = exec_queue_set_job_timeout,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_TRIGGER] = exec_queue_set_acc_trigger,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_NOTIFY] = exec_queue_set_acc_notify,
	[DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_GRANULARITY] = exec_queue_set_acc_granularity,
};

static int exec_queue_user_ext_set_property(struct xe_device *xe,
					    struct xe_exec_queue *q,
					    u64 extension,
					    bool create)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_ext_set_property ext;
	int err;
	u32 idx;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, ext.property >=
			 ARRAY_SIZE(exec_queue_set_property_funcs)) ||
	    XE_IOCTL_DBG(xe, ext.pad))
		return -EINVAL;

	idx = array_index_nospec(ext.property, ARRAY_SIZE(exec_queue_set_property_funcs));
	return exec_queue_set_property_funcs[idx](xe, q, ext.value,  create);
}

typedef int (*xe_exec_queue_user_extension_fn)(struct xe_device *xe,
					       struct xe_exec_queue *q,
					       u64 extension,
					       bool create);

static const xe_exec_queue_set_property_fn exec_queue_user_extension_funcs[] = {
	[DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY] = exec_queue_user_ext_set_property,
};

#define MAX_USER_EXTENSIONS	16
static int exec_queue_user_extensions(struct xe_device *xe, struct xe_exec_queue *q,
				      u64 extensions, int ext_number, bool create)
{
	u64 __user *address = u64_to_user_ptr(extensions);
	struct drm_xe_user_extension ext;
	int err;
	u32 idx;

	if (XE_IOCTL_DBG(xe, ext_number >= MAX_USER_EXTENSIONS))
		return -E2BIG;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, ext.pad) ||
	    XE_IOCTL_DBG(xe, ext.name >=
			 ARRAY_SIZE(exec_queue_user_extension_funcs)))
		return -EINVAL;

	idx = array_index_nospec(ext.name,
				 ARRAY_SIZE(exec_queue_user_extension_funcs));
	err = exec_queue_user_extension_funcs[idx](xe, q, extensions, create);
	if (XE_IOCTL_DBG(xe, err))
		return err;

	if (ext.next_extension)
		return exec_queue_user_extensions(xe, q, ext.next_extension,
					      ++ext_number, create);

	return 0;
}

static const enum xe_engine_class user_to_xe_engine_class[] = {
	[DRM_XE_ENGINE_CLASS_RENDER] = XE_ENGINE_CLASS_RENDER,
	[DRM_XE_ENGINE_CLASS_COPY] = XE_ENGINE_CLASS_COPY,
	[DRM_XE_ENGINE_CLASS_VIDEO_DECODE] = XE_ENGINE_CLASS_VIDEO_DECODE,
	[DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE] = XE_ENGINE_CLASS_VIDEO_ENHANCE,
	[DRM_XE_ENGINE_CLASS_COMPUTE] = XE_ENGINE_CLASS_COMPUTE,
};

static struct xe_hw_engine *
find_hw_engine(struct xe_device *xe,
	       struct drm_xe_engine_class_instance eci)
{
	u32 idx;

	if (eci.engine_class > ARRAY_SIZE(user_to_xe_engine_class))
		return NULL;

	if (eci.gt_id >= xe->info.gt_count)
		return NULL;

	idx = array_index_nospec(eci.engine_class,
				 ARRAY_SIZE(user_to_xe_engine_class));

	return xe_gt_hw_engine(xe_device_get_gt(xe, eci.gt_id),
			       user_to_xe_engine_class[idx],
			       eci.engine_instance, true);
}

static u32 bind_exec_queue_logical_mask(struct xe_device *xe, struct xe_gt *gt,
					struct drm_xe_engine_class_instance *eci,
					u16 width, u16 num_placements)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	u32 logical_mask = 0;

	if (XE_IOCTL_DBG(xe, width != 1))
		return 0;
	if (XE_IOCTL_DBG(xe, num_placements != 1))
		return 0;
	if (XE_IOCTL_DBG(xe, eci[0].engine_instance != 0))
		return 0;

	eci[0].engine_class = DRM_XE_ENGINE_CLASS_COPY;

	for_each_hw_engine(hwe, gt, id) {
		if (xe_hw_engine_is_reserved(hwe))
			continue;

		if (hwe->class ==
		    user_to_xe_engine_class[DRM_XE_ENGINE_CLASS_COPY])
			logical_mask |= BIT(hwe->logical_instance);
	}

	return logical_mask;
}

static u32 calc_validate_logical_mask(struct xe_device *xe, struct xe_gt *gt,
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

			hwe = find_hw_engine(xe, eci[n]);
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
	struct xe_vm *vm, *migrate_vm;
	struct xe_gt *gt;
	struct xe_exec_queue *q = NULL;
	u32 logical_mask;
	u32 id;
	u32 len;
	int err;

	if (XE_IOCTL_DBG(xe, args->flags) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	len = args->width * args->num_placements;
	if (XE_IOCTL_DBG(xe, !len || len > XE_HW_ENGINE_MAX_INSTANCE))
		return -EINVAL;

	err = __copy_from_user(eci, user_eci,
			       sizeof(struct drm_xe_engine_class_instance) *
			       len);
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, eci[0].gt_id >= xe->info.gt_count))
		return -EINVAL;

	if (eci[0].engine_class == DRM_XE_ENGINE_CLASS_VM_BIND) {
		for_each_gt(gt, xe, id) {
			struct xe_exec_queue *new;

			if (xe_gt_is_media_type(gt))
				continue;

			eci[0].gt_id = gt->info.id;
			logical_mask = bind_exec_queue_logical_mask(xe, gt, eci,
								    args->width,
								    args->num_placements);
			if (XE_IOCTL_DBG(xe, !logical_mask))
				return -EINVAL;

			hwe = find_hw_engine(xe, eci[0]);
			if (XE_IOCTL_DBG(xe, !hwe))
				return -EINVAL;

			/* The migration vm doesn't hold rpm ref */
			xe_device_mem_access_get(xe);

			migrate_vm = xe_migrate_get_vm(gt_to_tile(gt)->migrate);
			new = xe_exec_queue_create(xe, migrate_vm, logical_mask,
						   args->width, hwe,
						   EXEC_QUEUE_FLAG_PERSISTENT |
						   EXEC_QUEUE_FLAG_VM |
						   (id ?
						    EXEC_QUEUE_FLAG_BIND_ENGINE_CHILD :
						    0));

			xe_device_mem_access_put(xe); /* now held by engine */

			xe_vm_put(migrate_vm);
			if (IS_ERR(new)) {
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
	} else {
		gt = xe_device_get_gt(xe, eci[0].gt_id);
		logical_mask = calc_validate_logical_mask(xe, gt, eci,
							  args->width,
							  args->num_placements);
		if (XE_IOCTL_DBG(xe, !logical_mask))
			return -EINVAL;

		hwe = find_hw_engine(xe, eci[0]);
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

		q = xe_exec_queue_create(xe, vm, logical_mask,
					 args->width, hwe,
					 xe_vm_in_lr_mode(vm) ? 0 :
					 EXEC_QUEUE_FLAG_PERSISTENT);
		up_read(&vm->lock);
		xe_vm_put(vm);
		if (IS_ERR(q))
			return PTR_ERR(q);

		if (xe_vm_in_preempt_fence_mode(vm)) {
			q->compute.context = dma_fence_context_alloc(1);
			spin_lock_init(&q->compute.lock);

			err = xe_vm_add_compute_exec_queue(vm, q);
			if (XE_IOCTL_DBG(xe, err))
				goto put_exec_queue;
		}
	}

	if (args->extensions) {
		err = exec_queue_user_extensions(xe, q, args->extensions, 0, true);
		if (XE_IOCTL_DBG(xe, err))
			goto kill_exec_queue;
	}

	q->persistent.xef = xef;

	mutex_lock(&xef->exec_queue.lock);
	err = xa_alloc(&xef->exec_queue.xa, &id, q, xa_limit_32b, GFP_KERNEL);
	mutex_unlock(&xef->exec_queue.lock);
	if (err)
		goto kill_exec_queue;

	args->exec_queue_id = id;

	return 0;

kill_exec_queue:
	xe_exec_queue_kill(q);
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
		args->value = !!(q->flags & EXEC_QUEUE_FLAG_BANNED);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	xe_exec_queue_put(q);

	return ret;
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

static s32 xe_exec_queue_num_job_inflight(struct xe_exec_queue *q)
{
	return q->lrc->fence_ctx.next_seqno - xe_lrc_seqno(q->lrc) - 1;
}

/**
 * xe_exec_queue_ring_full() - Whether an exec_queue's ring is full
 * @q: The exec_queue
 *
 * Return: True if the exec_queue's ring is full, false otherwise.
 */
bool xe_exec_queue_ring_full(struct xe_exec_queue *q)
{
	struct xe_lrc *lrc = q->lrc;
	s32 max_job = lrc->ring.size / MAX_JOB_SIZE_BYTES;

	return xe_exec_queue_num_job_inflight(q) >= max_job;
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
			if (xe_lrc_seqno(&q->lrc[i]) !=
			    q->lrc[i].fence_ctx.next_seqno - 1)
				return false;
		}

		return true;
	}

	return xe_lrc_seqno(&q->lrc[0]) ==
		q->lrc[0].fence_ctx.next_seqno - 1;
}

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
	mutex_unlock(&xef->exec_queue.lock);
	if (XE_IOCTL_DBG(xe, !q))
		return -ENOENT;

	if (!(q->flags & EXEC_QUEUE_FLAG_PERSISTENT))
		xe_exec_queue_kill(q);
	else
		xe_device_add_persistent_exec_queues(xe, q);

	trace_xe_exec_queue_close(q);
	xe_exec_queue_put(q);

	return 0;
}

static void xe_exec_queue_last_fence_lockdep_assert(struct xe_exec_queue *q,
						    struct xe_vm *vm)
{
	if (q->flags & EXEC_QUEUE_FLAG_VM)
		lockdep_assert_held(&vm->lock);
	else
		xe_vm_assert_held(vm);
}

/**
 * xe_exec_queue_last_fence_put() - Drop ref to last fence
 * @q: The exec queue
 * @vm: The VM the engine does a bind or exec for
 */
void xe_exec_queue_last_fence_put(struct xe_exec_queue *q, struct xe_vm *vm)
{
	xe_exec_queue_last_fence_lockdep_assert(q, vm);

	if (q->last_fence) {
		dma_fence_put(q->last_fence);
		q->last_fence = NULL;
	}
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

	xe_exec_queue_last_fence_put(q, vm);
	q->last_fence = dma_fence_get(fence);
}
