/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I915_GEM_OBJECT_FRONTBUFFER_H__
#define __I915_GEM_OBJECT_FRONTBUFFER_H__

#include <linux/kref.h>
#include <linux/rcupdate.h>

#include "display/intel_frontbuffer.h"
#include "i915_gem_object_types.h"

void __i915_gem_object_flush_frontbuffer(struct drm_i915_gem_object *obj,
					 enum fb_op_origin origin);
void __i915_gem_object_invalidate_frontbuffer(struct drm_i915_gem_object *obj,
					      enum fb_op_origin origin);

static inline void
i915_gem_object_flush_frontbuffer(struct drm_i915_gem_object *obj,
				  enum fb_op_origin origin)
{
	if (unlikely(rcu_access_pointer(obj->frontbuffer)))
		__i915_gem_object_flush_frontbuffer(obj, origin);
}

static inline void
i915_gem_object_invalidate_frontbuffer(struct drm_i915_gem_object *obj,
				       enum fb_op_origin origin)
{
	if (unlikely(rcu_access_pointer(obj->frontbuffer)))
		__i915_gem_object_invalidate_frontbuffer(obj, origin);
}

/**
 * i915_gem_object_get_frontbuffer - Get the object's frontbuffer
 * @obj: The object whose frontbuffer to get.
 *
 * Get pointer to object's frontbuffer if such exists. Please note that RCU
 * mechanism is used to handle e.g. ongoing removal of frontbuffer pointer.
 *
 * Return: pointer to object's frontbuffer is such exists or NULL
 */
static inline struct intel_frontbuffer *
i915_gem_object_get_frontbuffer(const struct drm_i915_gem_object *obj)
{
	struct intel_frontbuffer *front;

	if (likely(!rcu_access_pointer(obj->frontbuffer)))
		return NULL;

	rcu_read_lock();
	do {
		front = rcu_dereference(obj->frontbuffer);
		if (!front)
			break;

		if (unlikely(!kref_get_unless_zero(&front->ref)))
			continue;

		if (likely(front == rcu_access_pointer(obj->frontbuffer)))
			break;

		intel_frontbuffer_put(front);
	} while (1);
	rcu_read_unlock();

	return front;
}

/**
 * i915_gem_object_set_frontbuffer - Set the object's frontbuffer
 * @obj: The object whose frontbuffer to set.
 * @front: The frontbuffer to set
 *
 * Set object's frontbuffer pointer. If frontbuffer is already set for the
 * object keep it and return it's pointer to the caller. Please note that RCU
 * mechanism is used to handle e.g. ongoing removal of frontbuffer pointer. This
 * function is protected by i915->display->fb_tracking.lock
 *
 * Return: pointer to frontbuffer which was set.
 */
static inline struct intel_frontbuffer *
i915_gem_object_set_frontbuffer(struct drm_i915_gem_object *obj,
				struct intel_frontbuffer *front)
{
	struct intel_frontbuffer *cur = front;

	if (!front) {
		RCU_INIT_POINTER(obj->frontbuffer, NULL);
	} else if (rcu_access_pointer(obj->frontbuffer)) {
		cur = rcu_dereference_protected(obj->frontbuffer, true);
		kref_get(&cur->ref);
	} else {
		rcu_assign_pointer(obj->frontbuffer, front);
	}

	return cur;
}

#endif
