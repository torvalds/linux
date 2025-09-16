// SPDX-License-Identifier: GPL-2.0 OR MIT

#include <drm/drm_exec.h>
#include <drm/drm_gem.h>

#include <linux/dma-resv.h>
#include <linux/export.h>

/**
 * DOC: Overview
 *
 * This component mainly abstracts the retry loop necessary for locking
 * multiple GEM objects while preparing hardware operations (e.g. command
 * submissions, page table updates etc..).
 *
 * If a contention is detected while locking a GEM object the cleanup procedure
 * unlocks all previously locked GEM objects and locks the contended one first
 * before locking any further objects.
 *
 * After an object is locked fences slots can optionally be reserved on the
 * dma_resv object inside the GEM object.
 *
 * A typical usage pattern should look like this::
 *
 *	struct drm_gem_object *obj;
 *	struct drm_exec exec;
 *	unsigned long index;
 *	int ret;
 *
 *	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT);
 *	drm_exec_until_all_locked(&exec) {
 *		ret = drm_exec_prepare_obj(&exec, boA, 1);
 *		drm_exec_retry_on_contention(&exec);
 *		if (ret)
 *			goto error;
 *
 *		ret = drm_exec_prepare_obj(&exec, boB, 1);
 *		drm_exec_retry_on_contention(&exec);
 *		if (ret)
 *			goto error;
 *	}
 *
 *	drm_exec_for_each_locked_object(&exec, index, obj) {
 *		dma_resv_add_fence(obj->resv, fence, DMA_RESV_USAGE_READ);
 *		...
 *	}
 *	drm_exec_fini(&exec);
 *
 * See struct dma_exec for more details.
 */

/* Dummy value used to initially enter the retry loop */
#define DRM_EXEC_DUMMY ((void *)~0)

/* Unlock all objects and drop references */
static void drm_exec_unlock_all(struct drm_exec *exec)
{
	struct drm_gem_object *obj;
	unsigned long index;

	drm_exec_for_each_locked_object_reverse(exec, index, obj) {
		dma_resv_unlock(obj->resv);
		drm_gem_object_put(obj);
	}

	drm_gem_object_put(exec->prelocked);
	exec->prelocked = NULL;
}

/**
 * drm_exec_init - initialize a drm_exec object
 * @exec: the drm_exec object to initialize
 * @flags: controls locking behavior, see DRM_EXEC_* defines
 * @nr: the initial # of objects
 *
 * Initialize the object and make sure that we can track locked objects.
 *
 * If nr is non-zero then it is used as the initial objects table size.
 * In either case, the table will grow (be re-allocated) on demand.
 */
void drm_exec_init(struct drm_exec *exec, u32 flags, unsigned nr)
{
	if (!nr)
		nr = PAGE_SIZE / sizeof(void *);

	exec->flags = flags;
	exec->objects = kvmalloc_array(nr, sizeof(void *), GFP_KERNEL);

	/* If allocation here fails, just delay that till the first use */
	exec->max_objects = exec->objects ? nr : 0;
	exec->num_objects = 0;
	exec->contended = DRM_EXEC_DUMMY;
	exec->prelocked = NULL;
}
EXPORT_SYMBOL(drm_exec_init);

/**
 * drm_exec_fini - finalize a drm_exec object
 * @exec: the drm_exec object to finalize
 *
 * Unlock all locked objects, drop the references to objects and free all memory
 * used for tracking the state.
 */
void drm_exec_fini(struct drm_exec *exec)
{
	drm_exec_unlock_all(exec);
	kvfree(exec->objects);
	if (exec->contended != DRM_EXEC_DUMMY) {
		drm_gem_object_put(exec->contended);
		ww_acquire_fini(&exec->ticket);
	}
}
EXPORT_SYMBOL(drm_exec_fini);

/**
 * drm_exec_cleanup - cleanup when contention is detected
 * @exec: the drm_exec object to cleanup
 *
 * Cleanup the current state and return true if we should stay inside the retry
 * loop, false if there wasn't any contention detected and we can keep the
 * objects locked.
 */
bool drm_exec_cleanup(struct drm_exec *exec)
{
	if (likely(!exec->contended)) {
		ww_acquire_done(&exec->ticket);
		return false;
	}

	if (likely(exec->contended == DRM_EXEC_DUMMY)) {
		exec->contended = NULL;
		ww_acquire_init(&exec->ticket, &reservation_ww_class);
		return true;
	}

	drm_exec_unlock_all(exec);
	exec->num_objects = 0;
	return true;
}
EXPORT_SYMBOL(drm_exec_cleanup);

/* Track the locked object in the array */
static int drm_exec_obj_locked(struct drm_exec *exec,
			       struct drm_gem_object *obj)
{
	if (unlikely(exec->num_objects == exec->max_objects)) {
		size_t size = exec->max_objects * sizeof(void *);
		void *tmp;

		tmp = kvrealloc(exec->objects, size + PAGE_SIZE, GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;

		exec->objects = tmp;
		exec->max_objects += PAGE_SIZE / sizeof(void *);
	}
	drm_gem_object_get(obj);
	exec->objects[exec->num_objects++] = obj;

	return 0;
}

/* Make sure the contended object is locked first */
static int drm_exec_lock_contended(struct drm_exec *exec)
{
	struct drm_gem_object *obj = exec->contended;
	int ret;

	if (likely(!obj))
		return 0;

	/* Always cleanup the contention so that error handling can kick in */
	exec->contended = NULL;
	if (exec->flags & DRM_EXEC_INTERRUPTIBLE_WAIT) {
		ret = dma_resv_lock_slow_interruptible(obj->resv,
						       &exec->ticket);
		if (unlikely(ret))
			goto error_dropref;
	} else {
		dma_resv_lock_slow(obj->resv, &exec->ticket);
	}

	ret = drm_exec_obj_locked(exec, obj);
	if (unlikely(ret))
		goto error_unlock;

	exec->prelocked = obj;
	return 0;

error_unlock:
	dma_resv_unlock(obj->resv);

error_dropref:
	drm_gem_object_put(obj);
	return ret;
}

/**
 * drm_exec_lock_obj - lock a GEM object for use
 * @exec: the drm_exec object with the state
 * @obj: the GEM object to lock
 *
 * Lock a GEM object for use and grab a reference to it.
 *
 * Returns: -EDEADLK if a contention is detected, -EALREADY when object is
 * already locked (can be suppressed by setting the DRM_EXEC_IGNORE_DUPLICATES
 * flag), -ENOMEM when memory allocation failed and zero for success.
 */
int drm_exec_lock_obj(struct drm_exec *exec, struct drm_gem_object *obj)
{
	int ret;

	ret = drm_exec_lock_contended(exec);
	if (unlikely(ret))
		return ret;

	if (exec->prelocked == obj) {
		drm_gem_object_put(exec->prelocked);
		exec->prelocked = NULL;
		return 0;
	}

	if (exec->flags & DRM_EXEC_INTERRUPTIBLE_WAIT)
		ret = dma_resv_lock_interruptible(obj->resv, &exec->ticket);
	else
		ret = dma_resv_lock(obj->resv, &exec->ticket);

	if (unlikely(ret == -EDEADLK)) {
		drm_gem_object_get(obj);
		exec->contended = obj;
		return -EDEADLK;
	}

	if (unlikely(ret == -EALREADY) &&
	    exec->flags & DRM_EXEC_IGNORE_DUPLICATES)
		return 0;

	if (unlikely(ret))
		return ret;

	ret = drm_exec_obj_locked(exec, obj);
	if (ret)
		goto error_unlock;

	return 0;

error_unlock:
	dma_resv_unlock(obj->resv);
	return ret;
}
EXPORT_SYMBOL(drm_exec_lock_obj);

/**
 * drm_exec_unlock_obj - unlock a GEM object in this exec context
 * @exec: the drm_exec object with the state
 * @obj: the GEM object to unlock
 *
 * Unlock the GEM object and remove it from the collection of locked objects.
 * Should only be used to unlock the most recently locked objects. It's not time
 * efficient to unlock objects locked long ago.
 */
void drm_exec_unlock_obj(struct drm_exec *exec, struct drm_gem_object *obj)
{
	unsigned int i;

	for (i = exec->num_objects; i--;) {
		if (exec->objects[i] == obj) {
			dma_resv_unlock(obj->resv);
			for (++i; i < exec->num_objects; ++i)
				exec->objects[i - 1] = exec->objects[i];
			--exec->num_objects;
			drm_gem_object_put(obj);
			return;
		}

	}
}
EXPORT_SYMBOL(drm_exec_unlock_obj);

/**
 * drm_exec_prepare_obj - prepare a GEM object for use
 * @exec: the drm_exec object with the state
 * @obj: the GEM object to prepare
 * @num_fences: how many fences to reserve
 *
 * Prepare a GEM object for use by locking it and reserving fence slots.
 *
 * Returns: -EDEADLK if a contention is detected, -EALREADY when object is
 * already locked, -ENOMEM when memory allocation failed and zero for success.
 */
int drm_exec_prepare_obj(struct drm_exec *exec, struct drm_gem_object *obj,
			 unsigned int num_fences)
{
	int ret;

	ret = drm_exec_lock_obj(exec, obj);
	if (ret)
		return ret;

	ret = dma_resv_reserve_fences(obj->resv, num_fences);
	if (ret) {
		drm_exec_unlock_obj(exec, obj);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_exec_prepare_obj);

/**
 * drm_exec_prepare_array - helper to prepare an array of objects
 * @exec: the drm_exec object with the state
 * @objects: array of GEM object to prepare
 * @num_objects: number of GEM objects in the array
 * @num_fences: number of fences to reserve on each GEM object
 *
 * Prepares all GEM objects in an array, aborts on first error.
 * Reserves @num_fences on each GEM object after locking it.
 *
 * Returns: -EDEADLOCK on contention, -EALREADY when object is already locked,
 * -ENOMEM when memory allocation failed and zero for success.
 */
int drm_exec_prepare_array(struct drm_exec *exec,
			   struct drm_gem_object **objects,
			   unsigned int num_objects,
			   unsigned int num_fences)
{
	int ret;

	for (unsigned int i = 0; i < num_objects; ++i) {
		ret = drm_exec_prepare_obj(exec, objects[i], num_fences);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_exec_prepare_array);

MODULE_DESCRIPTION("DRM execution context");
MODULE_LICENSE("Dual MIT/GPL");
