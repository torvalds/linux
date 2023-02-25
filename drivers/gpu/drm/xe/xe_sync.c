// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_sync.h"

#include <linux/kthread.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>

#include <drm/drm_print.h>
#include <drm/drm_syncobj.h>
#include <drm/xe_drm.h>

#include "xe_device_types.h"
#include "xe_macros.h"
#include "xe_sched_job_types.h"

#define SYNC_FLAGS_TYPE_MASK 0x3
#define SYNC_FLAGS_FENCE_INSTALLED	0x10000

struct user_fence {
	struct xe_device *xe;
	struct kref refcount;
	struct dma_fence_cb cb;
	struct work_struct worker;
	struct mm_struct *mm;
	u64 __user *addr;
	u64 value;
};

static void user_fence_destroy(struct kref *kref)
{
	struct user_fence *ufence = container_of(kref, struct user_fence,
						 refcount);

	mmdrop(ufence->mm);
	kfree(ufence);
}

static void user_fence_get(struct user_fence *ufence)
{
	kref_get(&ufence->refcount);
}

static void user_fence_put(struct user_fence *ufence)
{
	kref_put(&ufence->refcount, user_fence_destroy);
}

static struct user_fence *user_fence_create(struct xe_device *xe, u64 addr,
					    u64 value)
{
	struct user_fence *ufence;

	ufence = kmalloc(sizeof(*ufence), GFP_KERNEL);
	if (!ufence)
		return NULL;

	ufence->xe = xe;
	kref_init(&ufence->refcount);
	ufence->addr = u64_to_user_ptr(addr);
	ufence->value = value;
	ufence->mm = current->mm;
	mmgrab(ufence->mm);

	return ufence;
}

static void user_fence_worker(struct work_struct *w)
{
	struct user_fence *ufence = container_of(w, struct user_fence, worker);

	if (mmget_not_zero(ufence->mm)) {
		kthread_use_mm(ufence->mm);
		if (copy_to_user(ufence->addr, &ufence->value, sizeof(ufence->value)))
			XE_WARN_ON("Copy to user failed");
		kthread_unuse_mm(ufence->mm);
		mmput(ufence->mm);
	}

	wake_up_all(&ufence->xe->ufence_wq);
	user_fence_put(ufence);
}

static void kick_ufence(struct user_fence *ufence, struct dma_fence *fence)
{
	INIT_WORK(&ufence->worker, user_fence_worker);
	queue_work(ufence->xe->ordered_wq, &ufence->worker);
	dma_fence_put(fence);
}

static void user_fence_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct user_fence *ufence = container_of(cb, struct user_fence, cb);

	kick_ufence(ufence, fence);
}

int xe_sync_entry_parse(struct xe_device *xe, struct xe_file *xef,
			struct xe_sync_entry *sync,
			struct drm_xe_sync __user *sync_user,
			bool exec, bool no_dma_fences)
{
	struct drm_xe_sync sync_in;
	int err;

	if (copy_from_user(&sync_in, sync_user, sizeof(*sync_user)))
		return -EFAULT;

	if (XE_IOCTL_ERR(xe, sync_in.flags &
			 ~(SYNC_FLAGS_TYPE_MASK | DRM_XE_SYNC_SIGNAL)))
		return -EINVAL;

	switch (sync_in.flags & SYNC_FLAGS_TYPE_MASK) {
	case DRM_XE_SYNC_SYNCOBJ:
		if (XE_IOCTL_ERR(xe, no_dma_fences))
			return -ENOTSUPP;

		if (XE_IOCTL_ERR(xe, upper_32_bits(sync_in.addr)))
			return -EINVAL;

		sync->syncobj = drm_syncobj_find(xef->drm, sync_in.handle);
		if (XE_IOCTL_ERR(xe, !sync->syncobj))
			return -ENOENT;

		if (!(sync_in.flags & DRM_XE_SYNC_SIGNAL)) {
			sync->fence = drm_syncobj_fence_get(sync->syncobj);
			if (XE_IOCTL_ERR(xe, !sync->fence))
				return -EINVAL;
		}
		break;

	case DRM_XE_SYNC_TIMELINE_SYNCOBJ:
		if (XE_IOCTL_ERR(xe, no_dma_fences))
			return -ENOTSUPP;

		if (XE_IOCTL_ERR(xe, upper_32_bits(sync_in.addr)))
			return -EINVAL;

		if (XE_IOCTL_ERR(xe, sync_in.timeline_value == 0))
			return -EINVAL;

		sync->syncobj = drm_syncobj_find(xef->drm, sync_in.handle);
		if (XE_IOCTL_ERR(xe, !sync->syncobj))
			return -ENOENT;

		if (sync_in.flags & DRM_XE_SYNC_SIGNAL) {
			sync->chain_fence = dma_fence_chain_alloc();
			if (!sync->chain_fence)
				return -ENOMEM;
		} else {
			sync->fence = drm_syncobj_fence_get(sync->syncobj);
			if (XE_IOCTL_ERR(xe, !sync->fence))
				return -EINVAL;

			err = dma_fence_chain_find_seqno(&sync->fence,
							 sync_in.timeline_value);
			if (err)
				return err;
		}
		break;

	case DRM_XE_SYNC_DMA_BUF:
		if (XE_IOCTL_ERR(xe, "TODO"))
			return -EINVAL;
		break;

	case DRM_XE_SYNC_USER_FENCE:
		if (XE_IOCTL_ERR(xe, !(sync_in.flags & DRM_XE_SYNC_SIGNAL)))
			return -ENOTSUPP;

		if (XE_IOCTL_ERR(xe, sync_in.addr & 0x7))
			return -EINVAL;

		if (exec) {
			sync->addr = sync_in.addr;
		} else {
			sync->ufence = user_fence_create(xe, sync_in.addr,
							 sync_in.timeline_value);
			if (XE_IOCTL_ERR(xe, !sync->ufence))
				return -ENOMEM;
		}

		break;

	default:
		return -EINVAL;
	}

	sync->flags = sync_in.flags;
	sync->timeline_value = sync_in.timeline_value;

	return 0;
}

int xe_sync_entry_wait(struct xe_sync_entry *sync)
{
	if (sync->fence)
		dma_fence_wait(sync->fence, true);

	return 0;
}

int xe_sync_entry_add_deps(struct xe_sync_entry *sync, struct xe_sched_job *job)
{
	int err;

	if (sync->fence) {
		err = drm_sched_job_add_dependency(&job->drm,
						   dma_fence_get(sync->fence));
		if (err) {
			dma_fence_put(sync->fence);
			return err;
		}
	}

	return 0;
}

bool xe_sync_entry_signal(struct xe_sync_entry *sync, struct xe_sched_job *job,
			  struct dma_fence *fence)
{
	if (!(sync->flags & DRM_XE_SYNC_SIGNAL) ||
	    sync->flags & SYNC_FLAGS_FENCE_INSTALLED)
		return false;

	if (sync->chain_fence) {
		drm_syncobj_add_point(sync->syncobj, sync->chain_fence,
				      fence, sync->timeline_value);
		/*
		 * The chain's ownership is transferred to the
		 * timeline.
		 */
		sync->chain_fence = NULL;
	} else if (sync->syncobj) {
		drm_syncobj_replace_fence(sync->syncobj, fence);
	} else if (sync->ufence) {
		int err;

		dma_fence_get(fence);
		user_fence_get(sync->ufence);
		err = dma_fence_add_callback(fence, &sync->ufence->cb,
					     user_fence_cb);
		if (err == -ENOENT) {
			kick_ufence(sync->ufence, fence);
		} else if (err) {
			XE_WARN_ON("failed to add user fence");
			user_fence_put(sync->ufence);
			dma_fence_put(fence);
		}
	} else if ((sync->flags & SYNC_FLAGS_TYPE_MASK) ==
		   DRM_XE_SYNC_USER_FENCE) {
		job->user_fence.used = true;
		job->user_fence.addr = sync->addr;
		job->user_fence.value = sync->timeline_value;
	}

	/* TODO: external BO? */

	sync->flags |= SYNC_FLAGS_FENCE_INSTALLED;

	return true;
}

void xe_sync_entry_cleanup(struct xe_sync_entry *sync)
{
	if (sync->syncobj)
		drm_syncobj_put(sync->syncobj);
	if (sync->fence)
		dma_fence_put(sync->fence);
	if (sync->chain_fence)
		dma_fence_put(&sync->chain_fence->base);
	if (sync->ufence)
		user_fence_put(sync->ufence);
}
