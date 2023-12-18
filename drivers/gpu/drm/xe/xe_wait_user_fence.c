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
#include "xe_exec_queue.h"

static int do_compare(u64 addr, u64 value, u64 mask, u16 op)
{
	u64 rvalue;
	int err;
	bool passed;

	err = copy_from_user(&rvalue, u64_to_user_ptr(addr), sizeof(rvalue));
	if (err)
		return -EFAULT;

	switch (op) {
	case DRM_XE_UFENCE_WAIT_OP_EQ:
		passed = (rvalue & mask) == (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_OP_NEQ:
		passed = (rvalue & mask) != (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_OP_GT:
		passed = (rvalue & mask) > (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_OP_GTE:
		passed = (rvalue & mask) >= (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_OP_LT:
		passed = (rvalue & mask) < (value & mask);
		break;
	case DRM_XE_UFENCE_WAIT_OP_LTE:
		passed = (rvalue & mask) <= (value & mask);
		break;
	default:
		XE_WARN_ON("Not possible");
		return -EINVAL;
	}

	return passed ? 0 : 1;
}

#define VALID_FLAGS	DRM_XE_UFENCE_WAIT_FLAG_ABSTIME
#define MAX_OP		DRM_XE_UFENCE_WAIT_OP_LTE

static long to_jiffies_timeout(struct xe_device *xe,
			       struct drm_xe_wait_user_fence *args)
{
	unsigned long long t;
	long timeout;

	/*
	 * For negative timeout we want to wait "forever" by setting
	 * MAX_SCHEDULE_TIMEOUT. But we have to assign this value also
	 * to args->timeout to avoid being zeroed on the signal delivery
	 * (see arithmetics after wait).
	 */
	if (args->timeout < 0) {
		args->timeout = MAX_SCHEDULE_TIMEOUT;
		return MAX_SCHEDULE_TIMEOUT;
	}

	if (args->timeout == 0)
		return 0;

	/*
	 * Save the timeout to an u64 variable because nsecs_to_jiffies
	 * might return a value that overflows s32 variable.
	 */
	if (args->flags & DRM_XE_UFENCE_WAIT_FLAG_ABSTIME)
		t = drm_timeout_abs_to_jiffies(args->timeout);
	else
		t = nsecs_to_jiffies(args->timeout);

	/*
	 * Anything greater then MAX_SCHEDULE_TIMEOUT is meaningless,
	 * also we don't want to cap it at MAX_SCHEDULE_TIMEOUT because
	 * apparently user doesn't mean to wait forever, otherwise the
	 * args->timeout should have been set to a negative value.
	 */
	if (t > MAX_SCHEDULE_TIMEOUT)
		timeout = MAX_SCHEDULE_TIMEOUT - 1;
	else
		timeout = t;

	return timeout ?: 1;
}

int xe_wait_user_fence_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	DEFINE_WAIT_FUNC(w_wait, woken_wake_function);
	struct drm_xe_wait_user_fence *args = data;
	struct xe_exec_queue *q = NULL;
	u64 addr = args->addr;
	int err = 0;
	long timeout;
	ktime_t start;

	if (XE_IOCTL_DBG(xe, args->extensions) || XE_IOCTL_DBG(xe, args->pad) ||
	    XE_IOCTL_DBG(xe, args->pad2) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & ~VALID_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->op > MAX_OP))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, addr & 0x7))
		return -EINVAL;

	if (args->exec_queue_id) {
		q = xe_exec_queue_lookup(xef, args->exec_queue_id);
		if (XE_IOCTL_DBG(xe, !q))
			return -ENOENT;
	}

	timeout = to_jiffies_timeout(xe, args);

	start = ktime_get();

	add_wait_queue(&xe->ufence_wq, &w_wait);
	for (;;) {
		err = do_compare(addr, args->value, args->mask, args->op);
		if (err <= 0)
			break;

		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}

		if (q) {
			if (q->ops->reset_status(q)) {
				drm_info(&xe->drm, "exec gueue reset detected\n");
				err = -EIO;
				break;
			}
		}

		if (!timeout) {
			err = -ETIME;
			break;
		}

		timeout = wait_woken(&w_wait, TASK_INTERRUPTIBLE, timeout);
	}
	remove_wait_queue(&xe->ufence_wq, &w_wait);

	if (!(args->flags & DRM_XE_UFENCE_WAIT_FLAG_ABSTIME)) {
		args->timeout -= ktime_to_ns(ktime_sub(ktime_get(), start));
		if (args->timeout < 0)
			args->timeout = 0;
	}

	if (!timeout && !(err < 0))
		err = -ETIME;

	if (q)
		xe_exec_queue_put(q);

	return err;
}
