/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_GLOBAL_STATE_H__
#define __INTEL_GLOBAL_STATE_H__

#include <linux/kref.h>
#include <linux/list.h>

struct drm_i915_private;
struct intel_atomic_state;
struct intel_global_obj;
struct intel_global_state;

struct intel_global_state_funcs {
	struct intel_global_state *(*atomic_duplicate_state)(struct intel_global_obj *obj);
	void (*atomic_destroy_state)(struct intel_global_obj *obj,
				     struct intel_global_state *state);
};

struct intel_global_obj {
	struct list_head head;
	struct intel_global_state *state;
	const struct intel_global_state_funcs *funcs;
};

#define intel_for_each_global_obj(obj, dev_priv) \
	list_for_each_entry(obj, &(dev_priv)->global_obj_list, head)

#define for_each_new_global_obj_in_state(__state, obj, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_global_objs && \
		     ((obj) = (__state)->global_objs[__i].ptr, \
		      (new_obj_state) = (__state)->global_objs[__i].new_state, 1); \
	     (__i)++) \
		for_each_if(obj)

#define for_each_old_global_obj_in_state(__state, obj, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_global_objs && \
		     ((obj) = (__state)->global_objs[__i].ptr, \
		      (new_obj_state) = (__state)->global_objs[__i].old_state, 1); \
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

struct intel_global_state {
	struct intel_global_obj *obj;
	struct intel_atomic_state *state;
	struct kref ref;
	bool changed;
};

struct __intel_global_objs_state {
	struct intel_global_obj *ptr;
	struct intel_global_state *state, *old_state, *new_state;
};

void intel_atomic_global_obj_init(struct drm_i915_private *dev_priv,
				  struct intel_global_obj *obj,
				  struct intel_global_state *state,
				  const struct intel_global_state_funcs *funcs);
void intel_atomic_global_obj_cleanup(struct drm_i915_private *dev_priv);

struct intel_global_state *
intel_atomic_get_global_obj_state(struct intel_atomic_state *state,
				  struct intel_global_obj *obj);
struct intel_global_state *
intel_atomic_get_old_global_obj_state(struct intel_atomic_state *state,
				      struct intel_global_obj *obj);
struct intel_global_state *
intel_atomic_get_new_global_obj_state(struct intel_atomic_state *state,
				      struct intel_global_obj *obj);

void intel_atomic_swap_global_state(struct intel_atomic_state *state);
void intel_atomic_clear_global_state(struct intel_atomic_state *state);
int intel_atomic_lock_global_state(struct intel_global_state *obj_state);
int intel_atomic_serialize_global_state(struct intel_global_state *obj_state);

#endif
