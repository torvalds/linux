// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_wait_user_fence.h"

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_utils.h>
#include <drm/xe_drm.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_macros.h"
#include "xe_vm.h"

static int do_compare(u64 addr, u64 value, u64 mask, u16 op)
{
	u64 rvalue;
	int err;
	bool passed;

	err = copy_from_user(&rvalue, u64_to_user_ptr(addr), sizeof(rvalue));
	if (err)
		return -EFAULT;

	switch (op) {
	case DRM_XE_UFENCE_WAIT_EQ:
		passed = (rvalue & mask) == (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_NEQ:
		passed = (rvalue & mask) != (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_GT:
		passed = (rvalue & mask) > (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_GTE:
		passed = (rvalue & mask) >= (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_LT:
		passed = (rvalue & mask) < (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_LTE:
		passed = (rvalue & mask) <= (value & mask);
		break;
	default:
		XE_BUG_ON("Not possible");
	}

	return passed ? 0 : 1;
}

static const enum xe_engine_class user_to_xe_engine_class[] = {
	[DRM_XE_ENGINE_CLASS_RENDER] = XE_ENGINE_CLASS_RENDER,
	[DRM_XE_ENGINE_CLASS_COPY] = XE_ENGINE_CLASS_COPY,
	[DRM_XE_ENGINE_CLASS_VIDEO_DECODE] = XE_ENGINE_CLASS_VIDEO_DECODE,
	[DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE] = XE_ENGINE_CLASS_VIDEO_ENHANCE,
	[DRM_XE_ENGINE_CLASS_COMPUTE] = XE_ENGINE_CLASS_COMPUTE,
};

static int check_hw_engines(struct xe_device *xe,
			    struct drm_xe_engine_class_instance *eci,
			    int num_engines)
{
	int i;

	for (i = 0; i < num_engines; ++i) {
		enum xe_engine_class user_class =
			user_to_xe_engine_class[eci[i].engine_class];

		if (eci[i].gt_id >= xe->info.tile_count)
			return -EINVAL;

		if (!xe_gt_hw_engine(xe_device_get_gt(xe, eci[i].gt_id),
				     user_class, eci[i].engine_instance, true))
			return -EINVAL;
	}

	return 0;
}

#define VALID_FLAGS	(DRM_XE_UFENCE_WAIT_SOFT_OP | \
			 DRM_XE_UFENCE_WAIT_ABSTIME | \
			 DRM_XE_UFENCE_WAIT_VM_ERROR)
#define MAX_OP		DRM_XE_UFENCE_WAIT_LTE

static unsigned long to_jiffies_timeout(struct drm_xe_wait_user_fence *args)
{
	unsigned long timeout;

	if (args->flags & DRM_XE_UFENCE_WAIT_ABSTIME)
		return drm_timeout_abs_to_jiffies(args->timeout);

	if (args->timeout == MAX_SCHEDULE_TIMEOUT || args->timeout == 0)
		return args->timeout;

	timeout = nsecs_to_jiffies(args->timeout);

	return timeout ?: 1;
}

int xe_wait_user_fence_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	DEFINE_WAIT_FUNC(w_wait, woken_wake_function);
	struct drm_xe_wait_user_fence *args = data;
	struct drm_xe_engine_class_instance eci[XE_HW_ENGINE_MAX_INSTANCE];
	struct drm_xe_engine_class_instance __user *user_eci =
		u64_to_user_ptr(args->instances);
	struct xe_vm *vm = NULL;
	u64 addr = args->addr;
	int err;
	bool no_engines = args->flags & DRM_XE_UFENCE_WAIT_SOFT_OP ||
		args->flags & DRM_XE_UFENCE_WAIT_VM_ERROR;
	unsigned long timeout;
	ktime_t start;

	if (XE_IOCTL_DBG(xe, args->extensions) || XE_IOCTL_DBG(xe, args->pad) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & ~VALID_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->op > MAX_OP))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, no_engines &&
			 (args->num_engines || args->instances)))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !no_engines && !args->num_engines))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !(args->flags & DRM_XE_UFENCE_WAIT_VM_ERROR) &&
			 addr & 0x7))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->num_engines > XE_HW_ENGINE_MAX_INSTANCE))
		return -EINVAL;

	if (!no_engines) {
		err = copy_from_user(eci, user_eci,
				     sizeof(struct drm_xe_engine_class_instance) *
			     args->num_engines);
		if (XE_IOCTL_DBG(xe, err))
			return -EFAULT;

		if (XE_IOCTL_DBG(xe, check_hw_engines(xe, eci,
						      args->num_engines)))
			return -EINVAL;
	}

	if (args->flags & DRM_XE_UFENCE_WAIT_VM_ERROR) {
		if (XE_IOCTL_DBG(xe, args->vm_id >> 32))
			return -EINVAL;

		vm = xe_vm_lookup(to_xe_file(file), args->vm_id);
		if (XE_IOCTL_DBG(xe, !vm))
			return -ENOENT;

		if (XE_IOCTL_DBG(xe, !vm->async_ops.error_capture.addr)) {
			xe_vm_put(vm);
			return -EOPNOTSUPP;
		}

		addr = vm->async_ops.error_capture.addr;
	}

	/*
	 * For negative timeout we want to wait "forever" by setting
	 * MAX_SCHEDULE_TIMEOUT. But we have to assign this value also
	 * to args->timeout to avoid being zeroed on the signal delivery
	 * (see arithmetics after wait).
	 */
	if (args->timeout < 0)
		args->timeout = MAX_SCHEDULE_TIMEOUT;

	timeout = to_jiffies_timeout(args);

	start = ktime_get();

	/*
	 * FIXME: Very simple implementation at the moment, single wait queue
	 * for everything. Could be optimized to have a wait queue for every
	 * hardware engine. Open coding as 'do_compare' can sleep which doesn't
	 * work with the wait_event_* macros.
	 */
	if (vm)
		add_wait_queue(&vm->async_ops.error_capture.wq, &w_wait);
	else
		add_wait_queue(&xe->ufence_wq, &w_wait);
	for (;;) {
		if (vm && xe_vm_is_closed(vm)) {
			err = -ENODEV;
			break;
		}
		err = do_compare(addr, args->value, args->mask, args->op);
		if (err <= 0)
			break;

		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}

		if (!timeout) {
			err = -ETIME;
			break;
		}

		timeout = wait_woken(&w_wait, TASK_INTERRUPTIBLE, timeout);
	}
	if (vm) {
		remove_wait_queue(&vm->async_ops.error_capture.wq, &w_wait);
		xe_vm_put(vm);
	} else {
		remove_wait_queue(&xe->ufence_wq, &w_wait);
	}

	if (!(args->flags & DRM_XE_UFENCE_WAIT_ABSTIME)) {
		args->timeout -= ktime_to_ns(ktime_sub(ktime_get(), start));
		if (args->timeout < 0)
			args->timeout = 0;
	}

	if (XE_IOCTL_DBG(xe, err < 0))
		return err;
	else if (XE_IOCTL_DBG(xe, !timeout))
		return -ETIME;

	return 0;
}
