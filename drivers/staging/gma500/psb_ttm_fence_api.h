/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */
#ifndef _TTM_FENCE_API_H_
#define _TTM_FENCE_API_H_

#include <linux/list.h>
#include <linux/kref.h>

#define TTM_FENCE_FLAG_EMIT (1 << 0)
#define TTM_FENCE_TYPE_EXE  (1 << 0)

struct ttm_fence_device;

/**
 * struct ttm_fence_info
 *
 * @fence_class:    The fence class.
 * @fence_type:     Bitfield indicating types for this fence.
 * @signaled_types: Bitfield indicating which types are signaled.
 * @error:          Last error reported from the device.
 *
 * Used as output from the ttm_fence_get_info
 */

struct ttm_fence_info {
	uint32_t signaled_types;
	uint32_t error;
};

/**
 * struct ttm_fence_object
 *
 * @fdev:            Pointer to the fence device struct.
 * @kref:            Holds the reference count of this fence object.
 * @ring:            List head used for the circular list of not-completely
 *                   signaled fences.
 * @info:            Data for fast retrieval using the ttm_fence_get_info()
 * function.
 * @timeout_jiffies: Absolute jiffies value indicating when this fence
 *                   object times out and, if waited on, calls ttm_fence_lockup
 *                   to check for and resolve a GPU lockup.
 * @sequence:        Fence sequence number.
 * @waiting_types:   Types currently waited on.
 * @destroy:         Called to free the fence object, when its refcount has
 *                   reached zero. If NULL, kfree is used.
 *
 * This struct is provided in the driver interface so that drivers can
 * derive from it and create their own fence implementation. All members
 * are private to the fence implementation and the fence driver callbacks.
 * Otherwise a driver may access the derived object using container_of().
 */

struct ttm_fence_object {
	struct ttm_fence_device *fdev;
	struct kref kref;
	uint32_t fence_class;
	uint32_t fence_type;

	/*
	 * The below fields are protected by the fence class
	 * manager spinlock.
	 */

	struct list_head ring;
	struct ttm_fence_info info;
	unsigned long timeout_jiffies;
	uint32_t sequence;
	uint32_t waiting_types;
	void (*destroy) (struct ttm_fence_object *);
};

/**
 * ttm_fence_object_init
 *
 * @fdev: Pointer to a struct ttm_fence_device.
 * @fence_class: Fence class for this fence.
 * @type: Fence type for this fence.
 * @create_flags: Flags indicating varios actions at init time. At this point
 * there's only TTM_FENCE_FLAG_EMIT, which triggers a sequence emission to
 * the command stream.
 * @destroy: Destroy function. If NULL, kfree() is used.
 * @fence: The struct ttm_fence_object to initialize.
 *
 * Initialize a pre-allocated fence object. This function, together with the
 * destroy function makes it possible to derive driver-specific fence objects.
 */

extern int
ttm_fence_object_init(struct ttm_fence_device *fdev,
		      uint32_t fence_class,
		      uint32_t type,
		      uint32_t create_flags,
		      void (*destroy) (struct ttm_fence_object *fence),
		      struct ttm_fence_object *fence);

/**
 * ttm_fence_object_create
 *
 * @fdev: Pointer to a struct ttm_fence_device.
 * @fence_class: Fence class for this fence.
 * @type: Fence type for this fence.
 * @create_flags: Flags indicating varios actions at init time. At this point
 * there's only TTM_FENCE_FLAG_EMIT, which triggers a sequence emission to
 * the command stream.
 * @c_fence: On successful termination, *(@c_fence) will point to the created
 * fence object.
 *
 * Create and initialize a struct ttm_fence_object. The destroy function will
 * be set to kfree().
 */

extern int
ttm_fence_object_create(struct ttm_fence_device *fdev,
			uint32_t fence_class,
			uint32_t type,
			uint32_t create_flags,
			struct ttm_fence_object **c_fence);

/**
 * ttm_fence_object_wait
 *
 * @fence: The fence object to wait on.
 * @lazy: Allow sleeps to reduce the cpu-usage if polling.
 * @interruptible: Sleep interruptible when waiting.
 * @type_mask: Wait for the given type_mask to signal.
 *
 * Wait for a fence to signal the given type_mask. The function will
 * perform a fence_flush using type_mask. (See ttm_fence_object_flush).
 *
 * Returns
 * -ERESTART if interrupted by a signal.
 * May return driver-specific error codes if timed-out.
 */

extern int
ttm_fence_object_wait(struct ttm_fence_object *fence,
		      bool lazy, bool interruptible, uint32_t type_mask);

/**
 * ttm_fence_object_flush
 *
 * @fence: The fence object to flush.
 * @flush_mask: Fence types to flush.
 *
 * Make sure that the given fence eventually signals the
 * types indicated by @flush_mask. Note that this may or may not
 * map to a CPU or GPU flush.
 */

extern int
ttm_fence_object_flush(struct ttm_fence_object *fence, uint32_t flush_mask);

/**
 * ttm_fence_get_info
 *
 * @fence: The fence object.
 *
 * Copy the info block from the fence while holding relevant locks.
 */

struct ttm_fence_info ttm_fence_get_info(struct ttm_fence_object *fence);

/**
 * ttm_fence_object_ref
 *
 * @fence: The fence object.
 *
 * Return a ref-counted pointer to the fence object indicated by @fence.
 */

static inline struct ttm_fence_object *ttm_fence_object_ref(struct
							    ttm_fence_object
							    *fence)
{
	kref_get(&fence->kref);
	return fence;
}

/**
 * ttm_fence_object_unref
 *
 * @p_fence: Pointer to a ref-counted pinter to a struct ttm_fence_object.
 *
 * Unreference the fence object pointed to by *(@p_fence), clearing
 * *(p_fence).
 */

extern void ttm_fence_object_unref(struct ttm_fence_object **p_fence);

/**
 * ttm_fence_object_signaled
 *
 * @fence: Pointer to the struct ttm_fence_object.
 * @mask: Type mask to check whether signaled.
 *
 * This function checks (without waiting) whether the fence object
 * pointed to by @fence has signaled the types indicated by @mask,
 * and returns 1 if true, 0 if false. This function does NOT perform
 * an implicit fence flush.
 */

extern bool
ttm_fence_object_signaled(struct ttm_fence_object *fence, uint32_t mask);

/**
 * ttm_fence_class
 *
 * @fence: Pointer to the struct ttm_fence_object.
 *
 * Convenience function that returns the fence class of a
 * struct ttm_fence_object.
 */

static inline uint32_t ttm_fence_class(const struct ttm_fence_object *fence)
{
	return fence->fence_class;
}

/**
 * ttm_fence_types
 *
 * @fence: Pointer to the struct ttm_fence_object.
 *
 * Convenience function that returns the fence types of a
 * struct ttm_fence_object.
 */

static inline uint32_t ttm_fence_types(const struct ttm_fence_object *fence)
{
	return fence->fence_type;
}

/*
 * The functions below are wrappers to the above functions, with
 * similar names but with sync_obj omitted. These wrappers are intended
 * to be plugged directly into the buffer object driver's sync object
 * API, if the driver chooses to use ttm_fence_objects as buffer object
 * sync objects. In the prototypes below, a sync_obj is cast to a
 * struct ttm_fence_object, whereas a sync_arg is cast to an
 * uint32_t representing a fence_type argument.
 */

extern bool ttm_fence_sync_obj_signaled(void *sync_obj, void *sync_arg);
extern int ttm_fence_sync_obj_wait(void *sync_obj, void *sync_arg,
				   bool lazy, bool interruptible);
extern int ttm_fence_sync_obj_flush(void *sync_obj, void *sync_arg);
extern void ttm_fence_sync_obj_unref(void **sync_obj);
extern void *ttm_fence_sync_obj_ref(void *sync_obj);

#endif
