/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_lock.h>

/**
 * DOC: kms locking
 *
 * As KMS moves toward more fine grained locking, and atomic ioctl where
 * userspace can indirectly control locking order, it becomes necessary
 * to use ww_mutex and acquire-contexts to avoid deadlocks.  But because
 * the locking is more distributed around the driver code, we want a bit
 * of extra utility/tracking out of our acquire-ctx.  This is provided
 * by drm_modeset_lock / drm_modeset_acquire_ctx.
 *
 * For basic principles of ww_mutex, see: Documentation/ww-mutex-design.txt
 *
 * The basic usage pattern is to:
 *
 *     drm_modeset_acquire_init(&ctx)
 *   retry:
 *     foreach (lock in random_ordered_set_of_locks) {
 *       ret = drm_modeset_lock(lock, &ctx)
 *       if (ret == -EDEADLK) {
 *          drm_modeset_backoff(&ctx);
 *          goto retry;
 *       }
 *     }
 *
 *     ... do stuff ...
 *
 *     drm_modeset_drop_locks(&ctx);
 *     drm_modeset_acquire_fini(&ctx);
 */


/**
 * drm_modeset_acquire_init - initialize acquire context
 * @ctx: the acquire context
 * @flags: for future
 */
void drm_modeset_acquire_init(struct drm_modeset_acquire_ctx *ctx,
		uint32_t flags)
{
	memset(ctx, 0, sizeof(*ctx));
	ww_acquire_init(&ctx->ww_ctx, &crtc_ww_class);
	INIT_LIST_HEAD(&ctx->locked);
}
EXPORT_SYMBOL(drm_modeset_acquire_init);

/**
 * drm_modeset_acquire_fini - cleanup acquire context
 * @ctx: the acquire context
 */
void drm_modeset_acquire_fini(struct drm_modeset_acquire_ctx *ctx)
{
	ww_acquire_fini(&ctx->ww_ctx);
}
EXPORT_SYMBOL(drm_modeset_acquire_fini);

/**
 * drm_modeset_drop_locks - drop all locks
 * @ctx: the acquire context
 *
 * Drop all locks currently held against this acquire context.
 */
void drm_modeset_drop_locks(struct drm_modeset_acquire_ctx *ctx)
{
	WARN_ON(ctx->contended);
	while (!list_empty(&ctx->locked)) {
		struct drm_modeset_lock *lock;

		lock = list_first_entry(&ctx->locked,
				struct drm_modeset_lock, head);

		drm_modeset_unlock(lock);
	}
}
EXPORT_SYMBOL(drm_modeset_drop_locks);

static inline int modeset_lock(struct drm_modeset_lock *lock,
		struct drm_modeset_acquire_ctx *ctx,
		bool interruptible, bool slow)
{
	int ret;

	WARN_ON(ctx->contended);

	if (interruptible && slow) {
		ret = ww_mutex_lock_slow_interruptible(&lock->mutex, &ctx->ww_ctx);
	} else if (interruptible) {
		ret = ww_mutex_lock_interruptible(&lock->mutex, &ctx->ww_ctx);
	} else if (slow) {
		ww_mutex_lock_slow(&lock->mutex, &ctx->ww_ctx);
		ret = 0;
	} else {
		ret = ww_mutex_lock(&lock->mutex, &ctx->ww_ctx);
	}
	if (!ret) {
		WARN_ON(!list_empty(&lock->head));
		list_add(&lock->head, &ctx->locked);
	} else if (ret == -EALREADY) {
		/* we already hold the lock.. this is fine.  For atomic
		 * we will need to be able to drm_modeset_lock() things
		 * without having to keep track of what is already locked
		 * or not.
		 */
		ret = 0;
	} else if (ret == -EDEADLK) {
		ctx->contended = lock;
	}

	return ret;
}

static int modeset_backoff(struct drm_modeset_acquire_ctx *ctx,
		bool interruptible)
{
	struct drm_modeset_lock *contended = ctx->contended;

	ctx->contended = NULL;

	if (WARN_ON(!contended))
		return 0;

	drm_modeset_drop_locks(ctx);

	return modeset_lock(contended, ctx, interruptible, true);
}

/**
 * drm_modeset_backoff - deadlock avoidance backoff
 * @ctx: the acquire context
 *
 * If deadlock is detected (ie. drm_modeset_lock() returns -EDEADLK),
 * you must call this function to drop all currently held locks and
 * block until the contended lock becomes available.
 */
void drm_modeset_backoff(struct drm_modeset_acquire_ctx *ctx)
{
	modeset_backoff(ctx, false);
}
EXPORT_SYMBOL(drm_modeset_backoff);

/**
 * drm_modeset_backoff_interruptible - deadlock avoidance backoff
 * @ctx: the acquire context
 *
 * Interruptible version of drm_modeset_backoff()
 */
int drm_modeset_backoff_interruptible(struct drm_modeset_acquire_ctx *ctx)
{
	return modeset_backoff(ctx, true);
}
EXPORT_SYMBOL(drm_modeset_backoff_interruptible);

/**
 * drm_modeset_lock - take modeset lock
 * @lock: lock to take
 * @ctx: acquire ctx
 *
 * If ctx is not NULL, then its ww acquire context is used and the
 * lock will be tracked by the context and can be released by calling
 * drm_modeset_drop_locks().  If -EDEADLK is returned, this means a
 * deadlock scenario has been detected and it is an error to attempt
 * to take any more locks without first calling drm_modeset_backoff().
 */
int drm_modeset_lock(struct drm_modeset_lock *lock,
		struct drm_modeset_acquire_ctx *ctx)
{
	if (ctx)
		return modeset_lock(lock, ctx, false, false);

	ww_mutex_lock(&lock->mutex, NULL);
	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock);

/**
 * drm_modeset_lock_interruptible - take modeset lock
 * @lock: lock to take
 * @ctx: acquire ctx
 *
 * Interruptible version of drm_modeset_lock()
 */
int drm_modeset_lock_interruptible(struct drm_modeset_lock *lock,
		struct drm_modeset_acquire_ctx *ctx)
{
	if (ctx)
		return modeset_lock(lock, ctx, true, false);

	return ww_mutex_lock_interruptible(&lock->mutex, NULL);
}
EXPORT_SYMBOL(drm_modeset_lock_interruptible);

/**
 * drm_modeset_unlock - drop modeset lock
 * @lock: lock to release
 */
void drm_modeset_unlock(struct drm_modeset_lock *lock)
{
	list_del_init(&lock->head);
	ww_mutex_unlock(&lock->mutex);
}
EXPORT_SYMBOL(drm_modeset_unlock);

/* Temporary.. until we have sufficiently fine grained locking, there
 * are a couple scenarios where it is convenient to grab all crtc locks.
 * It is planned to remove this:
 */
int drm_modeset_lock_all_crtcs(struct drm_device *dev,
		struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_crtc *crtc;
	int ret = 0;

	list_for_each_entry(crtc, &config->crtc_list, head) {
		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock_all_crtcs);
