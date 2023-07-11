// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/string.h>

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_display_types.h"
#include "intel_global_state.h"

static void __intel_atomic_global_state_free(struct kref *kref)
{
	struct intel_global_state *obj_state =
		container_of(kref, struct intel_global_state, ref);
	struct intel_global_obj *obj = obj_state->obj;

	obj->funcs->atomic_destroy_state(obj, obj_state);
}

static void intel_atomic_global_state_put(struct intel_global_state *obj_state)
{
	kref_put(&obj_state->ref, __intel_atomic_global_state_free);
}

static struct intel_global_state *
intel_atomic_global_state_get(struct intel_global_state *obj_state)
{
	kref_get(&obj_state->ref);

	return obj_state;
}

void intel_atomic_global_obj_init(struct drm_i915_private *dev_priv,
				  struct intel_global_obj *obj,
				  struct intel_global_state *state,
				  const struct intel_global_state_funcs *funcs)
{
	memset(obj, 0, sizeof(*obj));

	state->obj = obj;

	kref_init(&state->ref);

	obj->state = state;
	obj->funcs = funcs;
	list_add_tail(&obj->head, &dev_priv->display.global.obj_list);
}

void intel_atomic_global_obj_cleanup(struct drm_i915_private *dev_priv)
{
	struct intel_global_obj *obj, *next;

	list_for_each_entry_safe(obj, next, &dev_priv->display.global.obj_list, head) {
		list_del(&obj->head);

		drm_WARN_ON(&dev_priv->drm, kref_read(&obj->state->ref) != 1);
		intel_atomic_global_state_put(obj->state);
	}
}

static void assert_global_state_write_locked(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	for_each_intel_crtc(&dev_priv->drm, crtc)
		drm_modeset_lock_assert_held(&crtc->base.mutex);
}

static bool modeset_lock_is_held(struct drm_modeset_acquire_ctx *ctx,
				 struct drm_modeset_lock *lock)
{
	struct drm_modeset_lock *l;

	list_for_each_entry(l, &ctx->locked, head) {
		if (lock == l)
			return true;
	}

	return false;
}

static void assert_global_state_read_locked(struct intel_atomic_state *state)
{
	struct drm_modeset_acquire_ctx *ctx = state->base.acquire_ctx;
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc *crtc;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		if (modeset_lock_is_held(ctx, &crtc->base.mutex))
			return;
	}

	drm_WARN(&dev_priv->drm, 1, "Global state not read locked\n");
}

struct intel_global_state *
intel_atomic_get_global_obj_state(struct intel_atomic_state *state,
				  struct intel_global_obj *obj)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	int index, num_objs, i;
	size_t size;
	struct __intel_global_objs_state *arr;
	struct intel_global_state *obj_state;

	for (i = 0; i < state->num_global_objs; i++)
		if (obj == state->global_objs[i].ptr)
			return state->global_objs[i].state;

	assert_global_state_read_locked(state);

	num_objs = state->num_global_objs + 1;
	size = sizeof(*state->global_objs) * num_objs;
	arr = krealloc(state->global_objs, size, GFP_KERNEL);
	if (!arr)
		return ERR_PTR(-ENOMEM);

	state->global_objs = arr;
	index = state->num_global_objs;
	memset(&state->global_objs[index], 0, sizeof(*state->global_objs));

	obj_state = obj->funcs->atomic_duplicate_state(obj);
	if (!obj_state)
		return ERR_PTR(-ENOMEM);

	obj_state->obj = obj;
	obj_state->changed = false;

	kref_init(&obj_state->ref);

	state->global_objs[index].state = obj_state;
	state->global_objs[index].old_state =
		intel_atomic_global_state_get(obj->state);
	state->global_objs[index].new_state = obj_state;
	state->global_objs[index].ptr = obj;
	obj_state->state = state;

	state->num_global_objs = num_objs;

	drm_dbg_atomic(&i915->drm, "Added new global object %p state %p to %p\n",
		       obj, obj_state, state);

	return obj_state;
}

struct intel_global_state *
intel_atomic_get_old_global_obj_state(struct intel_atomic_state *state,
				      struct intel_global_obj *obj)
{
	int i;

	for (i = 0; i < state->num_global_objs; i++)
		if (obj == state->global_objs[i].ptr)
			return state->global_objs[i].old_state;

	return NULL;
}

struct intel_global_state *
intel_atomic_get_new_global_obj_state(struct intel_atomic_state *state,
				      struct intel_global_obj *obj)
{
	int i;

	for (i = 0; i < state->num_global_objs; i++)
		if (obj == state->global_objs[i].ptr)
			return state->global_objs[i].new_state;

	return NULL;
}

void intel_atomic_swap_global_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *old_obj_state, *new_obj_state;
	struct intel_global_obj *obj;
	int i;

	for_each_oldnew_global_obj_in_state(state, obj, old_obj_state,
					    new_obj_state, i) {
		drm_WARN_ON(&dev_priv->drm, obj->state != old_obj_state);

		/*
		 * If the new state wasn't modified (and properly
		 * locked for write access) we throw it away.
		 */
		if (!new_obj_state->changed)
			continue;

		assert_global_state_write_locked(dev_priv);

		old_obj_state->state = state;
		new_obj_state->state = NULL;

		state->global_objs[i].state = old_obj_state;

		intel_atomic_global_state_put(obj->state);
		obj->state = intel_atomic_global_state_get(new_obj_state);
	}
}

void intel_atomic_clear_global_state(struct intel_atomic_state *state)
{
	int i;

	for (i = 0; i < state->num_global_objs; i++) {
		intel_atomic_global_state_put(state->global_objs[i].old_state);
		intel_atomic_global_state_put(state->global_objs[i].new_state);

		state->global_objs[i].ptr = NULL;
		state->global_objs[i].state = NULL;
		state->global_objs[i].old_state = NULL;
		state->global_objs[i].new_state = NULL;
	}
	state->num_global_objs = 0;
}

int intel_atomic_lock_global_state(struct intel_global_state *obj_state)
{
	struct intel_atomic_state *state = obj_state->state;
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc *crtc;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		int ret;

		ret = drm_modeset_lock(&crtc->base.mutex,
				       state->base.acquire_ctx);
		if (ret)
			return ret;
	}

	obj_state->changed = true;

	return 0;
}

int intel_atomic_serialize_global_state(struct intel_global_state *obj_state)
{
	struct intel_atomic_state *state = obj_state->state;
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc *crtc;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state;

		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	obj_state->changed = true;

	return 0;
}

bool
intel_atomic_global_state_is_serialized(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc *crtc;

	for_each_intel_crtc(&i915->drm, crtc)
		if (!intel_atomic_get_new_crtc_state(state, crtc))
			return false;
	return true;
}
