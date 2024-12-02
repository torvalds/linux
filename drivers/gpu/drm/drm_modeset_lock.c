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

#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_print.h>

/**
 * DOC: kms locking
 *
 * As KMS moves toward more fine grained locking, and atomic ioctl where
 * userspace can indirectly control locking order, it becomes necessary
 * to use &ww_mutex and acquire-contexts to avoid deadlocks.  But because
 * the locking is more distributed around the driver code, we want a bit
 * of extra utility/tracking out of our acquire-ctx.  This is provided
 * by &struct drm_modeset_lock and &struct drm_modeset_acquire_ctx.
 *
 * For basic principles of &ww_mutex, see: Documentation/locking/ww-mutex-design.rst
 *
 * The basic usage pattern is to::
 *
 *     drm_modeset_acquire_init(ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE)
 *     retry:
 *     foreach (lock in random_ordered_set_of_locks) {
 *         ret = drm_modeset_lock(lock, ctx)
 *         if (ret == -EDEADLK) {
 *             ret = drm_modeset_backoff(ctx);
 *             if (!ret)
 *                 goto retry;
 *         }
 *         if (ret)
 *             goto out;
 *     }
 *     ... do stuff ...
 *     out:
 *     drm_modeset_drop_locks(ctx);
 *     drm_modeset_acquire_fini(ctx);
 *
 * For convenience this control flow is implemented in
 * DRM_MODESET_LOCK_ALL_BEGIN() and DRM_MODESET_LOCK_ALL_END() for the case
 * where all modeset locks need to be taken through drm_modeset_lock_all_ctx().
 *
 * If all that is needed is a single modeset lock, then the &struct
 * drm_modeset_acquire_ctx is not needed and the locking can be simplified
 * by passing a NULL instead of ctx in the drm_modeset_lock() call or
 * calling  drm_modeset_lock_single_interruptible(). To unlock afterwards
 * call drm_modeset_unlock().
 *
 * On top of these per-object locks using &ww_mutex there's also an overall
 * &drm_mode_config.mutex, for protecting everything else. Mostly this means
 * probe state of connectors, and preventing hotplug add/removal of connectors.
 *
 * Finally there's a bunch of dedicated locks to protect drm core internal
 * lists and lookup data structures.
 */

static DEFINE_WW_CLASS(crtc_ww_class);

#if IS_ENABLED(CONFIG_DRM_DEBUG_MODESET_LOCK)
static noinline depot_stack_handle_t __drm_stack_depot_save(void)
{
	unsigned long entries[8];
	unsigned int n;

	n = stack_trace_save(entries, ARRAY_SIZE(entries), 1);

	return stack_depot_save(entries, n, GFP_NOWAIT | __GFP_NOWARN);
}

static void __drm_stack_depot_print(depot_stack_handle_t stack_depot)
{
	struct drm_printer p = drm_debug_printer("drm_modeset_lock");
	unsigned long *entries;
	unsigned int nr_entries;
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
	if (!buf)
		return;

	nr_entries = stack_depot_fetch(stack_depot, &entries);
	stack_trace_snprint(buf, PAGE_SIZE, entries, nr_entries, 2);

	drm_printf(&p, "attempting to lock a contended lock without backoff:\n%s", buf);

	kfree(buf);
}

static void __drm_stack_depot_init(void)
{
	stack_depot_init();
}
#else /* CONFIG_DRM_DEBUG_MODESET_LOCK */
static depot_stack_handle_t __drm_stack_depot_save(void)
{
	return 0;
}
static void __drm_stack_depot_print(depot_stack_handle_t stack_depot)
{
}
static void __drm_stack_depot_init(void)
{
}
#endif /* CONFIG_DRM_DEBUG_MODESET_LOCK */

/**
 * drm_modeset_lock_all - take all modeset locks
 * @dev: DRM device
 *
 * This function takes all modeset locks, suitable where a more fine-grained
 * scheme isn't (yet) implemented. Locks must be dropped by calling the
 * drm_modeset_unlock_all() function.
 *
 * This function is deprecated. It allocates a lock acquisition context and
 * stores it in &drm_device.mode_config. This facilitate conversion of
 * existing code because it removes the need to manually deal with the
 * acquisition context, but it is also brittle because the context is global
 * and care must be taken not to nest calls. New code should use the
 * drm_modeset_lock_all_ctx() function and pass in the context explicitly.
 */
void drm_modeset_lock_all(struct drm_device *dev)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_modeset_acquire_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL | __GFP_NOFAIL);
	if (WARN_ON(!ctx))
		return;

	mutex_lock(&config->mutex);

	drm_modeset_acquire_init(ctx, 0);

retry:
	ret = drm_modeset_lock_all_ctx(dev, ctx);
	if (ret < 0) {
		if (ret == -EDEADLK) {
			drm_modeset_backoff(ctx);
			goto retry;
		}

		drm_modeset_acquire_fini(ctx);
		kfree(ctx);
		return;
	}
	ww_acquire_done(&ctx->ww_ctx);

	WARN_ON(config->acquire_ctx);

	/*
	 * We hold the locks now, so it is safe to stash the acquisition
	 * context for drm_modeset_unlock_all().
	 */
	config->acquire_ctx = ctx;

	drm_warn_on_modeset_not_all_locked(dev);
}
EXPORT_SYMBOL(drm_modeset_lock_all);

/**
 * drm_modeset_unlock_all - drop all modeset locks
 * @dev: DRM device
 *
 * This function drops all modeset locks taken by a previous call to the
 * drm_modeset_lock_all() function.
 *
 * This function is deprecated. It uses the lock acquisition context stored
 * in &drm_device.mode_config. This facilitates conversion of existing
 * code because it removes the need to manually deal with the acquisition
 * context, but it is also brittle because the context is global and care must
 * be taken not to nest calls. New code should pass the acquisition context
 * directly to the drm_modeset_drop_locks() function.
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
 * @flags: 0 or %DRM_MODESET_ACQUIRE_INTERRUPTIBLE
 *
 * When passing %DRM_MODESET_ACQUIRE_INTERRUPTIBLE to @flags,
 * all calls to drm_modeset_lock() will perform an interruptible
 * wait.
 */
void drm_modeset_acquire_init(struct drm_modeset_acquire_ctx *ctx,
		uint32_t flags)
{
	memset(ctx, 0, sizeof(*ctx));
	ww_acquire_init(&ctx->ww_ctx, &crtc_ww_class);
	INIT_LIST_HEAD(&ctx->locked);

	if (flags & DRM_MODESET_ACQUIRE_INTERRUPTIBLE)
		ctx->interruptible = true;
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
	if (WARN_ON(ctx->contended))
		__drm_stack_depot_print(ctx->stack_depot);

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

	if (WARN_ON(ctx->contended))
		__drm_stack_depot_print(ctx->stack_depot);

	if (ctx->trylock_only) {
		lockdep_assert_held(&ctx->ww_ctx);

		if (!ww_mutex_trylock(&lock->mutex, NULL))
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
		ctx->stack_depot = __drm_stack_depot_save();
	}

	return ret;
}

/**
 * drm_modeset_backoff - deadlock avoidance backoff
 * @ctx: the acquire context
 *
 * If deadlock is detected (ie. drm_modeset_lock() returns -EDEADLK),
 * you must call this function to drop all currently held locks and
 * block until the contended lock becomes available.
 *
 * This function returns 0 on success, or -ERESTARTSYS if this context
 * is initialized with %DRM_MODESET_ACQUIRE_INTERRUPTIBLE and the
 * wait has been interrupted.
 */
int drm_modeset_backoff(struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_modeset_lock *contended = ctx->contended;

	ctx->contended = NULL;
	ctx->stack_depot = 0;

	if (WARN_ON(!contended))
		return 0;

	drm_modeset_drop_locks(ctx);

	return modeset_lock(contended, ctx, ctx->interruptible, true);
}
EXPORT_SYMBOL(drm_modeset_backoff);

/**
 * drm_modeset_lock_init - initialize lock
 * @lock: lock to init
 */
void drm_modeset_lock_init(struct drm_modeset_lock *lock)
{
	ww_mutex_init(&lock->mutex, &crtc_ww_class);
	INIT_LIST_HEAD(&lock->head);
	__drm_stack_depot_init();
}
EXPORT_SYMBOL(drm_modeset_lock_init);

/**
 * drm_modeset_lock - take modeset lock
 * @lock: lock to take
 * @ctx: acquire ctx
 *
 * If @ctx is not NULL, then its ww acquire context is used and the
 * lock will be tracked by the context and can be released by calling
 * drm_modeset_drop_locks().  If -EDEADLK is returned, this means a
 * deadlock scenario has been detected and it is an error to attempt
 * to take any more locks without first calling drm_modeset_backoff().
 *
 * If the @ctx is not NULL and initialized with
 * %DRM_MODESET_ACQUIRE_INTERRUPTIBLE, this function will fail with
 * -ERESTARTSYS when interrupted.
 *
 * If @ctx is NULL then the function call behaves like a normal,
 * uninterruptible non-nesting mutex_lock() call.
 */
int drm_modeset_lock(struct drm_modeset_lock *lock,
		struct drm_modeset_acquire_ctx *ctx)
{
	if (ctx)
		return modeset_lock(lock, ctx, ctx->interruptible, false);

	ww_mutex_lock(&lock->mutex, NULL);
	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock);

/**
 * drm_modeset_lock_single_interruptible - take a single modeset lock
 * @lock: lock to take
 *
 * This function behaves as drm_modeset_lock() with a NULL context,
 * but performs interruptible waits.
 *
 * This function returns 0 on success, or -ERESTARTSYS when interrupted.
 */
int drm_modeset_lock_single_interruptible(struct drm_modeset_lock *lock)
{
	return ww_mutex_lock_interruptible(&lock->mutex, NULL);
}
EXPORT_SYMBOL(drm_modeset_lock_single_interruptible);

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

/**
 * drm_modeset_lock_all_ctx - take all modeset locks
 * @dev: DRM device
 * @ctx: lock acquisition context
 *
 * This function takes all modeset locks, suitable where a more fine-grained
 * scheme isn't (yet) implemented.
 *
 * Unlike drm_modeset_lock_all(), it doesn't take the &drm_mode_config.mutex
 * since that lock isn't required for modeset state changes. Callers which
 * need to grab that lock too need to do so outside of the acquire context
 * @ctx.
 *
 * Locks acquired with this function should be released by calling the
 * drm_modeset_drop_locks() function on @ctx.
 *
 * See also: DRM_MODESET_LOCK_ALL_BEGIN() and DRM_MODESET_LOCK_ALL_END()
 *
 * Returns: 0 on success or a negative error-code on failure.
 */
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_private_obj *privobj;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	int ret;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

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

	drm_for_each_privobj(privobj, dev) {
		ret = drm_modeset_lock(&privobj->lock, ctx);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_modeset_lock_all_ctx);
