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

struct i915_frontbuffer {
	struct intel_frontbuffer base;
	struct drm_i915_gem_object *obj;
	struct i915_active write;
	struct rcu_head rcu;
	struct kref ref;
};

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

struct i915_frontbuffer *i915_gem_object_frontbuffer_get(struct drm_i915_gem_object *obj);
void i915_gem_object_frontbuffer_ref(struct i915_frontbuffer *front);
void i915_gem_object_frontbuffer_put(struct i915_frontbuffer *front);

/**
 * i915_gem_object_frontbuffer_lookup - Look up the object's frontbuffer
 * @obj: The object whose frontbuffer to look up.
 *
 * Get pointer to object's frontbuffer if such exists. Please note that RCU
 * mechanism is used to handle e.g. ongoing removal of frontbuffer pointer.
 *
 * Return: pointer to object's frontbuffer is such exists or NULL
 */
static inline struct i915_frontbuffer *
i915_gem_object_frontbuffer_lookup(const struct drm_i915_gem_object *obj)
{
	struct i915_frontbuffer *front;

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

		i915_gem_object_frontbuffer_put(front);
	} while (1);
	rcu_read_unlock();

	return front;
}

#endif
