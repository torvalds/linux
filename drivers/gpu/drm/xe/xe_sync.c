// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_sync.h"

#include <linux/dma-fence-array.h>
#include <linux/kthread.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>

#include <drm/drm_print.h>
#include <drm/drm_syncobj.h>
#include <drm/xe_drm.h>

#include "xe_device_types.h"
#include "xe_exec_queue.h"
#include "xe_macros.h"
#include "xe_sched_job_types.h"

struct xe_user_fence {
	struct xe_device *xe;
	struct kref refcount;
	struct dma_fence_cb cb;
	struct work_struct worker;
	struct mm_struct *mm;
	u64 __user *addr;
	u64 value;
	int signalled;
};

static void user_fence_destroy(struct kref *kref)
{
	struct xe_user_fence *ufence = container_of(kref, struct xe_user_fence,
						 refcount);

	mmdrop(ufence->mm);
	kfree(ufence);
}

static void user_fence_get(struct xe_user_fence *ufence)
{
	kref_get(&ufence->refcount);
}

static void user_fence_put(struct xe_user_fence *ufence)
{
	kref_put(&ufence->refcount, user_fence_destroy);
}

static struct xe_user_fence *user_fence_create(struct xe_device *xe, u64 addr,
					       u64 value)
{
	struct xe_user_fence *ufence;
	u64 __user *ptr = u64_to_user_ptr(addr);

	if (!access_ok(ptr, sizeof(*ptr)))
		return ERR_PTR(-EFAULT);

	ufence = kmalloc(sizeof(*ufence), GFP_KERNEL);
	if (!ufence)
		return ERR_PTR(-ENOMEM);

	ufence->xe = xe;
	kref_init(&ufence->refcount);
	ufence->addr = ptr;
	ufence->value = value;
	ufence->mm = current->mm;
	mmgrab(ufence->mm);

	return ufence;
}

static void user_fence_worker(struct work_struct *w)
{
	struct xe_user_fence *ufence = container_of(w, struct xe_user_fence, worker);

	if (mmget_not_zero(ufence->mm)) {
		kthread_use_mm(ufence->mm);
		if (copy_to_user(ufence->addr, &ufence->value, sizeof(ufence->value)))
			XE_WARN_ON("Copy to user failed");
		kthread_unuse_mm(ufence->mm);
		mmput(ufence->mm);
	}

	wake_up_all(&ufence->xe->ufence_wq);
	WRITE_ONCE(ufence->signalled, 1);
	user_fence_put(ufence);
}

static void kick_ufence(struct xe_user_fence *ufence, struct dma_fence *fence)
{
	INIT_WORK(&ufence->worker, user_fence_worker);
	queue_work(ufence->xe->ordered_wq, &ufence->worker);
	dma_fence_put(fence);
}

static void user_fence_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct xe_user_fence *ufence = container_of(cb, struct xe_user_fence, cb);

	kick_ufence(ufence, fence);
}

int xe_sync_entry_parse(struct xe_device *xe, struct xe_file *xef,
			struct xe_sync_entry *sync,
			struct drm_xe_sync __user *sync_user,
			unsigned int flags)
{
	struct drm_xe_sync sync_in;
	int err;
	bool exec = flags & SYNC_PARSE_FLAG_EXEC;
	bool in_lr_mode = flags & SYNC_PARSE_FLAG_LR_MODE;
	bool disallow_user_fence = flags & SYNC_PARSE_FLAG_DISALLOW_USER_FENCE;
	bool signal;

	if (copy_from_user(&sync_in, sync_user, sizeof(*sync_user)))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, sync_in.flags & ~DRM_XE_SYNC_FLAG_SIGNAL) ||
	    XE_IOCTL_DBG(xe, sync_in.reserved[0] || sync_in.reserved[1]))
		return -EINVAL;

	signal = sync_in.flags & DRM_XE_SYNC_FLAG_SIGNAL;
	switch (sync_in.type) {
	case DRM_XE_SYNC_TYPE_SYNCOBJ:
		if (XE_IOCTL_DBG(xe, in_lr_mode && signal))
			return -EOPNOTSUPP;

		if (XE_IOCTL_DBG(xe, upper_32_bits(sync_in.addr)))
			return -EINVAL;

		sync->syncobj = drm_syncobj_find(xef->drm, sync_in.handle);
		if (XE_IOCTL_DBG(xe, !sync->syncobj))
			return -ENOENT;

		if (!signal) {
			sync->fence = drm_syncobj_fence_get(sync->syncobj);
			if (XE_IOCTL_DBG(xe, !sync->fence))
				return -EINVAL;
		}
		break;

	case DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ:
		if (XE_IOCTL_DBG(xe, in_lr_mode && signal))
			return -EOPNOTSUPP;

		if (XE_IOCTL_DBG(xe, upper_32_bits(sync_in.addr)))
			return -EINVAL;

		if (XE_IOCTL_DBG(xe, sync_in.timeline_value == 0))
			return -EINVAL;

		sync->syncobj = drm_syncobj_find(xef->drm, sync_in.handle);
		if (XE_IOCTL_DBG(xe, !sync->syncobj))
			return -ENOENT;

		if (signal) {
			sync->chain_fence = dma_fence_chain_alloc();
			if (!sync->chain_fence)
				return -ENOMEM;
		} else {
			sync->fence = drm_syncobj_fence_get(sync->syncobj);
			if (XE_IOCTL_DBG(xe, !sync->fence))
				return -EINVAL;

			err = dma_fence_chain_find_seqno(&sync->fence,
							 sync_in.timeline_value);
			if (err)
				return err;
		}
		break;

	case DRM_XE_SYNC_TYPE_USER_FENCE:
		if (XE_IOCTL_DBG(xe, disallow_user_fence))
			return -EOPNOTSUPP;

		if (XE_IOCTL_DBG(xe, !signal))
			return -EOPNOTSUPP;

		if (XE_IOCTL_DBG(xe, sync_in.addr & 0x7))
			return -EINVAL;

		if (exec) {
			sync->addr = sync_in.addr;
		} else {
			sync->ufence = user_fence_create(xe, sync_in.addr,
							 sync_in.timeline_value);
			if (XE_IOCTL_DBG(xe, IS_ERR(sync->ufence)))
				return PTR_ERR(sync->ufence);
		}

		break;

	default:
		return -EINVAL;
	}

	sync->type = sync_in.type;
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

void xe_sync_entry_signal(struct xe_sync_entry *sync, struct dma_fence *fence)
{
	if (!(sync->flags & DRM_XE_SYNC_FLAG_SIGNAL))
		return;

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
	}
}

void xe_sync_entry_cleanup(struct xe_sync_entry *sync)
{
	if (sync->syncobj)
		drm_syncobj_put(sync->syncobj);
	if (sync->fence)
		dma_fence_put(sync->fence);
	if (sync->chain_fence)
		dma_fence_chain_free(sync->chain_fence);
	if (sync->ufence)
		user_fence_put(sync->ufence);
}

/**
 * xe_sync_in_fence_get() - Get a fence from syncs, exec queue, and VM
 * @sync: input syncs
 * @num_sync: number of syncs
 * @q: exec queue
 * @vm: VM
 *
 * Get a fence from syncs, exec queue, and VM. If syncs contain in-fences create
 * and return a composite fence of all in-fences + last fence. If no in-fences
 * return last fence on  input exec queue. Caller must drop reference to
 * returned fence.
 *
 * Return: fence on success, ERR_PTR(-ENOMEM) on failure
 */
struct dma_fence *
xe_sync_in_fence_get(struct xe_sync_entry *sync, int num_sync,
		     struct xe_exec_queue *q, struct xe_vm *vm)
{
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct dma_fence *fence;
	int i, num_in_fence = 0, current_fence = 0;

	lockdep_assert_held(&vm->lock);

	/* Count in-fences */
	for (i = 0; i < num_sync; ++i) {
		if (sync[i].fence) {
			++num_in_fence;
			fence = sync[i].fence;
		}
	}

	/* Easy case... */
	if (!num_in_fence) {
		fence = xe_exec_queue_last_fence_get(q, vm);
		return fence;
	}

	/* Create composite fence */
	fences = kmalloc_array(num_in_fence + 1, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		return ERR_PTR(-ENOMEM);
	for (i = 0; i < num_sync; ++i) {
		if (sync[i].fence) {
			dma_fence_get(sync[i].fence);
			fences[current_fence++] = sync[i].fence;
		}
	}
	fences[current_fence++] = xe_exec_queue_last_fence_get(q, vm);
	cf = dma_fence_array_create(num_in_fence, fences,
				    vm->composite_fence_ctx,
				    vm->composite_fence_seqno++,
				    false);
	if (!cf) {
		--vm->composite_fence_seqno;
		goto err_out;
	}

	return &cf->base;

err_out:
	while (current_fence)
		dma_fence_put(fences[--current_fence]);
	kfree(fences);
	kfree(cf);

	return ERR_PTR(-ENOMEM);
}

/**
 * __xe_sync_ufence_get() - Get user fence from user fence
 * @ufence: input user fence
 *
 * Get a user fence reference from user fence
 *
 * Return: xe_user_fence pointer with reference
 */
struct xe_user_fence *__xe_sync_ufence_get(struct xe_user_fence *ufence)
{
	user_fence_get(ufence);

	return ufence;
}

/**
 * xe_sync_ufence_get() - Get user fence from sync
 * @sync: input sync
 *
 * Get a user fence reference from sync.
 *
 * Return: xe_user_fence pointer with reference
 */
struct xe_user_fence *xe_sync_ufence_get(struct xe_sync_entry *sync)
{
	user_fence_get(sync->ufence);

	return sync->ufence;
}

/**
 * xe_sync_ufence_put() - Put user fence reference
 * @ufence: user fence reference
 *
 */
void xe_sync_ufence_put(struct xe_user_fence *ufence)
{
	user_fence_put(ufence);
}

/**
 * xe_sync_ufence_get_status() - Get user fence status
 * @ufence: user fence
 *
 * Return: 1 if signalled, 0 not signalled, <0 on error
 */
int xe_sync_ufence_get_status(struct xe_user_fence *ufence)
{
	return READ_ONCE(ufence->signalled);
}
