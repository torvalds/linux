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
 * For basic principles of ww_mutex, see: Documentation/locking/ww-mutex-design.txt
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
 * __drm_modeset_lock_all - internal helper to grab all modeset locks
 * @dev: DRM device
 * @trylock: trylock mode for atomic contexts
 *
 * This is a special version of drm_modeset_lock_all() which can also be used in
 * atomic contexts. Then @trylock must be set to true.
 *
 * Returns:
 * 0 on success or negative error code on failure.
 */
int __drm_modeset_lock_all(struct drm_device *dev,
			   bool trylock)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_modeset_acquire_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx),
		      trylock ? GFP_ATOMIC : GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (trylock) {
		if (!mutex_trylock(&config->mutex)) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		mutex_lock(&config->mutex);
	}

	drm_modeset_acquire_init(ctx, 0);
	ctx->trylock_only = trylock;

retry:
	ret = drm_modeset_lock(&config->connection_mutex, ctx);
	if (ret)
		goto fail;
	ret = drm_modeset_lock_all_crtcs(dev, ctx);
	if (ret)
		goto fail;

	WARN_ON(config->acquire_ctx);

	/* now we hold the locks, so now that it is safe, stash the
	 * ctx for drm_modeset_unlock_all():
	 */
	config->acquire_ctx = ctx;

	drm_warn_on_modeset_not_all_locked(dev);

	return 0;

fail:
	if (ret == -EDEADLK) {
		drm_modeset_backoff(ctx);
		goto retry;
	}

out:
	kfree(ctx);
	return ret;
}
EXPORT_SYMBOL(__drm_modeset_lock_all);

/**
 * drm_modeset_lock_all - take all modeset locks
 * @dev: drm device
 *
 * This function takes all modeset locks, suitable where a more fine-grained
 * scheme isn't (yet) implemented. Locks must be dropped with
 * drm_modeset_unlock_all.
 */
void drm_modeset_lock_all(struct drm_device *dev)
{
	WARN_ON(__drm_modeset_lock_all(dev, false) != 0);
}
EXPORT_SYMBOL(drm_modeset_lock_all);

/**
 * drm_modeset_unlock_all - drop all modeset locks
 * @dev: device
 *
 * This function drop all modeset locks taken by drm_modeset_lock_all.
 */
void drm_modeset_unlock_all(struct drm_device *dev)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_modeset_acquire_ctx *ctx = config->acquire_ctx;

	if (WARN_ON(!ctx))
		return;

	config->acquire_ctx = NULL;
	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);

	kfree(ctx);

	mutex_unlock(&dev->mode_config.mutex);
}
EXPORT_SYMBOL(drm_modeset_unlock_all);

/**
 * drm_modeset_lock_crtc - lock crtc with hidden acquire ctx for a plane update
 * @crtc: DRM CRTC
 * @plane: DRM plane to be updated on @crtc
 *
 * This function locks the given crtc and plane (which should be either the
 * primary or cursor plane) using a hidden acquire context. This is necessary so
 * that drivers internally using the atomic interfaces can grab further locks
 * with the lock acquire context.
 *
 * Note that @plane can be NULL, e.g. when the cursor support hasn't yet been
 * converted to universal planes yet.
 */
void drm_modeset_lock_crtc(struct drm_crtc *crtc,
			   struct drm_plane *plane)
{
	struct drm_modeset_acquire_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (WARN_ON(!ctx))
		return;

	drm_modeset_acquire_init(ctx, 0);

retry:
	ret = drm_modeset_lock(&crtc->mutex, ctx);
	if (ret)
		goto fail;

	if (plane) {
		ret = drm_modeset_lock(&plane->mutex, ctx);
		if (ret)
			goto fail;

		if (plane->crtc) {
			ret = drm_modeset_lock(&plane->crtc->mutex, ctx);
			if (ret)
				goto fail;
		}
	}

	WARN_ON(crtc->acquire_ctx);

	/* now we hold the locks, so now that it is safe, stash the
	 * ctx for drm_modeset_unlock_crtc():
	 */
	crtc->acquire_ctx = ctx;

	return;

fail:
	if (ret == -EDEADLK) {
		drm_modeset_backoff(ctx);
		goto retry;
	}
}
EXPORT_SYMBOL(drm_modeset_lock_crtc);

/**
 * drm_modeset_legacy_acquire_ctx - find acquire ctx for legacy ioctls
 * @crtc: drm crtc
 *
 * Legacy ioctl operations like cursor updates or page flips only have per-crtc
 * locking, and store the acquire ctx in the corresponding crtc. All other
 * legacy operations take all locks and use a global acquire context. This
 * function grabs the right one.
 */
struct drm_modeset_acquire_ctx *
drm_modeset_legacy_acquire_ctx(struct drm_crtc *crtc)
{
	if (crtc->acquire_ctx)
		return crtc->acquire_ctx;

	WARN_ON(!crtc->dev->mode_config.acquire_ctx);

	return crtc->dev->mode_config.acquire_ctx;
}
EXPORT_SYMBOL(drm_modeset_legacy_acquire_ctx);

/**
 * drm_modeset_unlock_crtc - drop crtc lock
 * @crtc: drm crtc
 *
 * This drops the crtc lock acquire with drm_modeset_lock_crtc() and all other
 * locks acquired through the hidden context.
 */
void drm_modeset_unlock_crtc(struct drm_crtc *crtc)
{
	struct drm_modeset_acquire_ctx *ctx = crtc->acquire_ctx;

	if (WARN_ON(!ctx))
		return;

	crtc->acquire_ctx = NULL;
	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);

	kfree(ctx);
}
EXPORT_SYMBOL(drm_modeset_unlock_crtc);

/**
 * drm_warn_on_modeset_not_all_locked - check that all modeset locks are locked
 * @dev: device
 *
 * Useful as a debug assert.
 */
void drm_warn_on_modeset_not_all_locked(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	/* Locking is currently fubar in the panic handler. */
	if (oops_in_progress)
		return;

	drm_for_each_crtc(crtc, dev)
		WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));
	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));
}
EXPORT_SYMBOL(drm_warn_on_modeset_not_all_locked);

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

	if (ctx->trylock_only) {
		if (!ww_mutex_trylock(&lock->mutex))
			return -EBUSY;
		else
			return 0;
	} else if (interruptible && slow) {
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

/* In some legacy codepaths it's convenient to just grab all the crtc and plane
 * related locks. */
int drm_modeset_lock_all_crtcs(struct drm_device *dev,
		struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	int ret = 0;

	drm_for_each_crtc(crtc, dev) {
		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			return ret;
	}

	drm_for_each_plane(plane, dev) {
		ret = drm_modeset_lock(&plane->mutex, ctx);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock_all_crtcs);
