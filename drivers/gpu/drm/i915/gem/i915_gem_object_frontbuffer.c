// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/intel/display_parent_interface.h>

#include "i915_drv.h"
#include "i915_gem_object_frontbuffer.h"

static int frontbuffer_active(struct i915_active *ref)
{
	struct i915_frontbuffer *front =
		container_of(ref, typeof(*front), write);

	kref_get(&front->ref);
	return 0;
}

static void frontbuffer_retire(struct i915_active *ref)
{
	struct i915_frontbuffer *front =
		container_of(ref, typeof(*front), write);

	intel_frontbuffer_flush(&front->base, ORIGIN_CS);
	i915_gem_object_frontbuffer_put(front);
}

struct i915_frontbuffer *
i915_gem_object_frontbuffer_get(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_frontbuffer *front, *cur;

	front = i915_gem_object_frontbuffer_lookup(obj);
	if (front)
		return front;

	front = kmalloc_obj(*front);
	if (!front)
		return NULL;

	intel_frontbuffer_init(&front->base, &i915->drm);

	kref_init(&front->ref);
	i915_gem_object_get(obj);
	front->obj = obj;

	i915_active_init(&front->write,
			 frontbuffer_active,
			 frontbuffer_retire,
			 I915_ACTIVE_RETIRE_SLEEPS);

	spin_lock(&i915->frontbuffer_lock);
	if (rcu_access_pointer(obj->frontbuffer)) {
		cur = rcu_dereference_protected(obj->frontbuffer, true);
		kref_get(&cur->ref);
	} else {
		cur = front;
		rcu_assign_pointer(obj->frontbuffer, front);
	}
	spin_unlock(&i915->frontbuffer_lock);

	if (cur != front) {
		i915_gem_object_put(obj);
		intel_frontbuffer_fini(&front->base);
		kfree(front);
	}

	return cur;
}

void i915_gem_object_frontbuffer_ref(struct i915_frontbuffer *front)
{
	kref_get(&front->ref);
}

static void frontbuffer_release(struct kref *ref)
	__releases(&i915->frontbuffer_lock)
{
	struct i915_frontbuffer *front =
		container_of(ref, typeof(*front), ref);
	struct drm_i915_gem_object *obj = front->obj;
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	i915_ggtt_clear_scanout(obj);

	RCU_INIT_POINTER(obj->frontbuffer, NULL);

	spin_unlock(&i915->frontbuffer_lock);

	i915_active_fini(&front->write);

	i915_gem_object_put(obj);

	intel_frontbuffer_fini(&front->base);

	kfree_rcu(front, rcu);
}

void i915_gem_object_frontbuffer_put(struct i915_frontbuffer *front)
{
	struct drm_i915_private *i915 = to_i915(front->obj->base.dev);

	kref_put_lock(&front->ref, frontbuffer_release,
		      &i915->frontbuffer_lock);
}

void __i915_gem_object_frontbuffer_flush(struct drm_i915_gem_object *obj,
					 enum fb_op_origin origin)
{
	struct i915_frontbuffer *front;

	front = i915_gem_object_frontbuffer_lookup(obj);
	if (front) {
		intel_frontbuffer_flush(&front->base, origin);
		i915_gem_object_frontbuffer_put(front);
	}
}

void __i915_gem_object_frontbuffer_invalidate(struct drm_i915_gem_object *obj,
					      enum fb_op_origin origin)
{
	struct i915_frontbuffer *front;

	front = i915_gem_object_frontbuffer_lookup(obj);
	if (front) {
		intel_frontbuffer_invalidate(&front->base, origin);
		i915_gem_object_frontbuffer_put(front);
	}
}

static struct intel_frontbuffer *i915_frontbuffer_get(struct drm_gem_object *_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	struct i915_frontbuffer *front;

	front = i915_gem_object_frontbuffer_get(obj);
	if (!front)
		return NULL;

	return &front->base;
}

static void i915_frontbuffer_ref(struct intel_frontbuffer *_front)
{
	struct i915_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	i915_gem_object_frontbuffer_ref(front);
}

static void i915_frontbuffer_put(struct intel_frontbuffer *_front)
{
	struct i915_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	return i915_gem_object_frontbuffer_put(front);
}

static void i915_frontbuffer_flush_for_display(struct intel_frontbuffer *_front)
{
	struct i915_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	i915_gem_object_flush_if_display(front->obj);
}

const struct intel_display_frontbuffer_interface i915_display_frontbuffer_interface = {
	.get = i915_frontbuffer_get,
	.ref = i915_frontbuffer_ref,
	.put = i915_frontbuffer_put,
	.flush_for_display = i915_frontbuffer_flush_for_display,
};
