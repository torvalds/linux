/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_ADMIN_STATE_H
#define VDO_ADMIN_STATE_H

#include "completion.h"
#include "types.h"

struct admin_state_code {
	const char *name;
	/* Normal operation, data_vios may be active */
	bool normal;
	/* I/O is draining, new requests should not start */
	bool draining;
	/* This is a startup time operation */
	bool loading;
	/* The next state will be quiescent */
	bool quiescing;
	/* The VDO is quiescent, there should be no I/O */
	bool quiescent;
	/* Whether an operation is in progress and so no other operation may be started */
	bool operating;
};

extern const struct admin_state_code *VDO_ADMIN_STATE_NORMAL_OPERATION;
extern const struct admin_state_code *VDO_ADMIN_STATE_OPERATING;
extern const struct admin_state_code *VDO_ADMIN_STATE_FORMATTING;
extern const struct admin_state_code *VDO_ADMIN_STATE_PRE_LOADING;
extern const struct admin_state_code *VDO_ADMIN_STATE_PRE_LOADED;
extern const struct admin_state_code *VDO_ADMIN_STATE_LOADING;
extern const struct admin_state_code *VDO_ADMIN_STATE_LOADING_FOR_RECOVERY;
extern const struct admin_state_code *VDO_ADMIN_STATE_LOADING_FOR_REBUILD;
extern const struct admin_state_code *VDO_ADMIN_STATE_WAITING_FOR_RECOVERY;
extern const struct admin_state_code *VDO_ADMIN_STATE_NEW;
extern const struct admin_state_code *VDO_ADMIN_STATE_INITIALIZED;
extern const struct admin_state_code *VDO_ADMIN_STATE_RECOVERING;
extern const struct admin_state_code *VDO_ADMIN_STATE_REBUILDING;
extern const struct admin_state_code *VDO_ADMIN_STATE_SAVING;
extern const struct admin_state_code *VDO_ADMIN_STATE_SAVED;
extern const struct admin_state_code *VDO_ADMIN_STATE_SCRUBBING;
extern const struct admin_state_code *VDO_ADMIN_STATE_SAVE_FOR_SCRUBBING;
extern const struct admin_state_code *VDO_ADMIN_STATE_STOPPING;
extern const struct admin_state_code *VDO_ADMIN_STATE_STOPPED;
extern const struct admin_state_code *VDO_ADMIN_STATE_SUSPENDING;
extern const struct admin_state_code *VDO_ADMIN_STATE_SUSPENDED;
extern const struct admin_state_code *VDO_ADMIN_STATE_SUSPENDED_OPERATION;
extern const struct admin_state_code *VDO_ADMIN_STATE_RESUMING;

struct admin_state {
	const struct admin_state_code *current_state;
	/* The next administrative state (when the current operation finishes) */
	const struct admin_state_code *next_state;
	/* A completion waiting on a state change */
	struct vdo_completion *waiter;
	/* Whether an operation is being initiated */
	bool starting;
	/* Whether an operation has completed in the initiator */
	bool complete;
};

/**
 * typedef vdo_admin_initiator_fn - A method to be called once an admin operation may be initiated.
 */
typedef void (*vdo_admin_initiator_fn)(struct admin_state *state);

static inline const struct admin_state_code * __must_check
vdo_get_admin_state_code(const struct admin_state *state)
{
	return READ_ONCE(state->current_state);
}

/**
 * vdo_set_admin_state_code() - Set the current admin state code.
 *
 * This function should be used primarily for initialization and by adminState internals. Most uses
 * should go through the operation interfaces.
 */
static inline void vdo_set_admin_state_code(struct admin_state *state,
					    const struct admin_state_code *code)
{
	WRITE_ONCE(state->current_state, code);
}

static inline bool __must_check vdo_is_state_normal(const struct admin_state *state)
{
	return vdo_get_admin_state_code(state)->normal;
}

static inline bool __must_check vdo_is_state_suspending(const struct admin_state *state)
{
	return (vdo_get_admin_state_code(state) == VDO_ADMIN_STATE_SUSPENDING);
}

static inline bool __must_check vdo_is_state_saving(const struct admin_state *state)
{
	return (vdo_get_admin_state_code(state) == VDO_ADMIN_STATE_SAVING);
}

static inline bool __must_check vdo_is_state_saved(const struct admin_state *state)
{
	return (vdo_get_admin_state_code(state) == VDO_ADMIN_STATE_SAVED);
}

static inline bool __must_check vdo_is_state_draining(const struct admin_state *state)
{
	return vdo_get_admin_state_code(state)->draining;
}

static inline bool __must_check vdo_is_state_loading(const struct admin_state *state)
{
	return vdo_get_admin_state_code(state)->loading;
}

static inline bool __must_check vdo_is_state_resuming(const struct admin_state *state)
{
	return (vdo_get_admin_state_code(state) == VDO_ADMIN_STATE_RESUMING);
}

static inline bool __must_check vdo_is_state_clean_load(const struct admin_state *state)
{
	const struct admin_state_code *code = vdo_get_admin_state_code(state);

	return ((code == VDO_ADMIN_STATE_FORMATTING) || (code == VDO_ADMIN_STATE_LOADING));
}

static inline bool __must_check vdo_is_state_quiescing(const struct admin_state *state)
{
	return vdo_get_admin_state_code(state)->quiescing;
}

static inline bool __must_check vdo_is_state_quiescent(const struct admin_state *state)
{
	return vdo_get_admin_state_code(state)->quiescent;
}

bool __must_check vdo_assert_load_operation(const struct admin_state_code *operation,
					    struct vdo_completion *waiter);

bool vdo_start_loading(struct admin_state *state,
		       const struct admin_state_code *operation,
		       struct vdo_completion *waiter, vdo_admin_initiator_fn initiator);

bool vdo_finish_loading(struct admin_state *state);

bool vdo_finish_loading_with_result(struct admin_state *state, int result);

bool vdo_start_resuming(struct admin_state *state,
			const struct admin_state_code *operation,
			struct vdo_completion *waiter, vdo_admin_initiator_fn initiator);

bool vdo_finish_resuming(struct admin_state *state);

bool vdo_finish_resuming_with_result(struct admin_state *state, int result);

int vdo_resume_if_quiescent(struct admin_state *state);

bool vdo_start_draining(struct admin_state *state,
			const struct admin_state_code *operation,
			struct vdo_completion *waiter, vdo_admin_initiator_fn initiator);

bool vdo_finish_draining(struct admin_state *state);

bool vdo_finish_draining_with_result(struct admin_state *state, int result);

int vdo_start_operation(struct admin_state *state,
			const struct admin_state_code *operation);

int vdo_start_operation_with_waiter(struct admin_state *state,
				    const struct admin_state_code *operation,
				    struct vdo_completion *waiter,
				    vdo_admin_initiator_fn initiator);

bool vdo_finish_operation(struct admin_state *state, int result);

#endif /* VDO_ADMIN_STATE_H */
