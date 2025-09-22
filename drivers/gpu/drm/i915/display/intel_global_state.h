/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_GLOBAL_STATE_H__
#define __INTEL_GLOBAL_STATE_H__

#include <linux/kref.h>
#include <linux/list.h>

struct intel_atomic_state;
struct intel_display;
struct intel_global_commit;
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

struct intel_global_state {
	struct intel_global_obj *obj;
	struct intel_atomic_state *state;
	struct intel_global_commit *commit;
	struct kref ref;
	bool changed, serialized;
};

void intel_atomic_global_obj_init(struct intel_display *display,
				  struct intel_global_obj *obj,
				  struct intel_global_state *state,
				  const struct intel_global_state_funcs *funcs);
void intel_atomic_global_obj_cleanup(struct intel_display *display);

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

int intel_atomic_global_state_setup_commit(struct intel_atomic_state *state);
void intel_atomic_global_state_commit_done(struct intel_atomic_state *state);
int intel_atomic_global_state_wait_for_dependencies(struct intel_atomic_state *state);

bool intel_atomic_global_state_is_serialized(struct intel_atomic_state *state);

#endif
