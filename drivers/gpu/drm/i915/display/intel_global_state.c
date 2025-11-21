// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/string.h>

#include <drm/drm_print.h>

#include "intel_atomic.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_global_state.h"

#define for_each_new_global_obj_in_state(__state, obj, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_global_objs && \
		     ((obj) = (__state)->global_objs[__i].ptr, \
		      (new_obj_state) = (__state)->global_objs[__i].new_state, 1); \
	     (__i)++) \
		for_each_if(obj)

#define for_each_old_global_obj_in_state(__state, obj, old_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_global_objs && \
		     ((obj) = (__state)->global_objs[__i].ptr, \
		      (old_obj_state) = (__state)->global_objs[__i].old_state, 1); \
	     (__i)++) \
		for_each_if(obj)

#define for_each_oldnew_global_obj_in_state(__state, obj, old_obj_state, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_global_objs && \
		     ((obj) = (__state)->global_objs[__i].ptr, \
		      (old_obj_state) = (__state)->global_objs[__i].old_state, \
		      (new_obj_state) = (__state)->global_objs[__i].new_state, 1); \
	     (__i)++) \
		for_each_if(obj)

struct intel_global_objs_state {
	struct intel_global_obj *ptr;
	struct intel_global_state *state, *old_state, *new_state;
};

struct intel_global_commit {
	struct kref ref;
	struct completion done;
};

static struct intel_global_commit *commit_new(void)
{
	struct intel_global_commit *commit;

	commit = kzalloc(sizeof(*commit), GFP_KERNEL);
	if (!commit)
		return NULL;

	init_completion(&commit->done);
	kref_init(&commit->ref);

	return commit;
}

static void __commit_free(struct kref *kref)
{
	struct intel_global_commit *commit =
		container_of(kref, typeof(*commit), ref);

	kfree(commit);
}

static struct intel_global_commit *commit_get(struct intel_global_commit *commit)
{
	if (commit)
		kref_get(&commit->ref);

	return commit;
}

static void commit_put(struct intel_global_commit *commit)
{
	if (commit)
		kref_put(&commit->ref, __commit_free);
}

static void __intel_atomic_global_state_free(struct kref *kref)
{
	struct intel_global_state *obj_state =
		container_of(kref, struct intel_global_state, ref);
	struct intel_global_obj *obj = obj_state->obj;

	commit_put(obj_state->commit);

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

void intel_atomic_global_obj_init(struct intel_display *display,
				  struct intel_global_obj *obj,
				  struct intel_global_state *state,
				  const struct intel_global_state_funcs *funcs)
{
	memset(obj, 0, sizeof(*obj));

	state->obj = obj;

	kref_init(&state->ref);

	obj->state = state;
	obj->funcs = funcs;
	list_add_tail(&obj->head, &display->global.obj_list);
}

void intel_atomic_global_obj_cleanup(struct intel_display *display)
{
	struct intel_global_obj *obj, *next;

	list_for_each_entry_safe(obj, next, &display->global.obj_list, head) {
		list_del(&obj->head);

		drm_WARN_ON(display->drm, kref_read(&obj->state->ref) != 1);
		intel_atomic_global_state_put(obj->state);
	}
}

static void assert_global_state_write_locked(struct intel_display *display)
{
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc)
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
	struct intel_display *display = to_intel_display(state);
	struct drm_modeset_acquire_ctx *ctx = state->base.acquire_ctx;
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
		if (modeset_lock_is_held(ctx, &crtc->base.mutex))
			return;
	}

	drm_WARN(display->drm, 1, "Global state not read locked\n");
}

struct intel_global_state *
intel_atomic_get_global_obj_state(struct intel_atomic_state *state,
				  struct intel_global_obj *obj)
{
	struct intel_display *display = to_intel_display(state);
	int index, num_objs, i;
	size_t size;
	struct intel_global_objs_state *arr;
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
	obj_state->serialized = false;
	obj_state->commit = NULL;

	kref_init(&obj_state->ref);

	state->global_objs[index].state = obj_state;
	state->global_objs[index].old_state =
		intel_atomic_global_state_get(obj->state);
	state->global_objs[index].new_state = obj_state;
	state->global_objs[index].ptr = obj;
	obj_state->state = state;

	state->num_global_objs = num_objs;

	drm_dbg_atomic(display->drm, "Added new global object %p state %p to %p\n",
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
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *old_obj_state, *new_obj_state;
	struct intel_global_obj *obj;
	int i;

	for_each_oldnew_global_obj_in_state(state, obj, old_obj_state,
					    new_obj_state, i) {
		drm_WARN_ON(display->drm, obj->state != old_obj_state);

		/*
		 * If the new state wasn't modified (and properly
		 * locked for write access) we throw it away.
		 */
		if (!new_obj_state->changed)
			continue;

		assert_global_state_write_locked(display);

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
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
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
	int ret;

	ret = intel_atomic_lock_global_state(obj_state);
	if (ret)
		return ret;

	obj_state->serialized = true;

	return 0;
}

bool
intel_atomic_global_state_is_serialized(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc)
		if (!intel_atomic_get_new_crtc_state(state, crtc))
			return false;
	return true;
}

int
intel_atomic_global_state_setup_commit(struct intel_atomic_state *state)
{
	const struct intel_global_state *old_obj_state;
	struct intel_global_state *new_obj_state;
	struct intel_global_obj *obj;
	int i;

	for_each_oldnew_global_obj_in_state(state, obj, old_obj_state,
					    new_obj_state, i) {
		struct intel_global_commit *commit = NULL;

		if (new_obj_state->serialized) {
			/*
			 * New commit which is going to be completed
			 * after the hardware reprogramming is done.
			 */
			commit = commit_new();
			if (!commit)
				return -ENOMEM;
		} else if (new_obj_state->changed) {
			/*
			 * We're going to swap to this state, so carry the
			 * previous commit along, in case it's not yet done.
			 */
			commit = commit_get(old_obj_state->commit);
		}

		new_obj_state->commit = commit;
	}

	return 0;
}

int
intel_atomic_global_state_wait_for_dependencies(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_global_state *old_obj_state;
	struct intel_global_obj *obj;
	int i;

	for_each_old_global_obj_in_state(state, obj, old_obj_state, i) {
		struct intel_global_commit *commit = old_obj_state->commit;
		long ret;

		if (!commit)
			continue;

		ret = wait_for_completion_timeout(&commit->done, 10 * HZ);
		if (ret == 0) {
			drm_err(display->drm, "global state timed out\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

void
intel_atomic_global_state_commit_done(struct intel_atomic_state *state)
{
	const struct intel_global_state *new_obj_state;
	struct intel_global_obj *obj;
	int i;

	for_each_new_global_obj_in_state(state, obj, new_obj_state, i) {
		struct intel_global_commit *commit = new_obj_state->commit;

		if (!new_obj_state->serialized)
			continue;

		complete_all(&commit->done);
	}
}
