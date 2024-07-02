/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_COMPLETION_H
#define VDO_COMPLETION_H

#include "permassert.h"

#include "status-codes.h"
#include "types.h"

/**
 * vdo_run_completion() - Run a completion's callback or error handler on the current thread.
 *
 * Context: This function must be called from the correct callback thread.
 */
static inline void vdo_run_completion(struct vdo_completion *completion)
{
	if ((completion->result != VDO_SUCCESS) && (completion->error_handler != NULL)) {
		completion->error_handler(completion);
		return;
	}

	completion->callback(completion);
}

void vdo_set_completion_result(struct vdo_completion *completion, int result);

void vdo_initialize_completion(struct vdo_completion *completion, struct vdo *vdo,
			       enum vdo_completion_type type);

/**
 * vdo_reset_completion() - Reset a completion to a clean state, while keeping the type, vdo and
 *                          parent information.
 */
static inline void vdo_reset_completion(struct vdo_completion *completion)
{
	completion->result = VDO_SUCCESS;
	completion->complete = false;
}

void vdo_launch_completion_with_priority(struct vdo_completion *completion,
					 enum vdo_completion_priority priority);

/**
 * vdo_launch_completion() - Launch a completion with default priority.
 */
static inline void vdo_launch_completion(struct vdo_completion *completion)
{
	vdo_launch_completion_with_priority(completion, VDO_WORK_Q_DEFAULT_PRIORITY);
}

/**
 * vdo_continue_completion() - Continue processing a completion.
 * @result: The current result (will not mask older errors).
 *
 * Continue processing a completion by setting the current result and calling
 * vdo_launch_completion().
 */
static inline void vdo_continue_completion(struct vdo_completion *completion, int result)
{
	vdo_set_completion_result(completion, result);
	vdo_launch_completion(completion);
}

void vdo_finish_completion(struct vdo_completion *completion);

/**
 * vdo_fail_completion() - Set the result of a completion if it does not already have an error,
 *                         then finish it.
 */
static inline void vdo_fail_completion(struct vdo_completion *completion, int result)
{
	vdo_set_completion_result(completion, result);
	vdo_finish_completion(completion);
}

/**
 * vdo_assert_completion_type() - Assert that a completion is of the correct type.
 *
 * Return: VDO_SUCCESS or an error
 */
static inline int vdo_assert_completion_type(struct vdo_completion *completion,
					     enum vdo_completion_type expected)
{
	return VDO_ASSERT(expected == completion->type,
			  "completion type should be %u, not %u", expected,
			  completion->type);
}

static inline void vdo_set_completion_callback(struct vdo_completion *completion,
					       vdo_action_fn callback,
					       thread_id_t callback_thread_id)
{
	completion->callback = callback;
	completion->callback_thread_id = callback_thread_id;
}

/**
 * vdo_launch_completion_callback() - Set the callback for a completion and launch it immediately.
 */
static inline void vdo_launch_completion_callback(struct vdo_completion *completion,
						  vdo_action_fn callback,
						  thread_id_t callback_thread_id)
{
	vdo_set_completion_callback(completion, callback, callback_thread_id);
	vdo_launch_completion(completion);
}

/**
 * vdo_prepare_completion() - Prepare a completion for launch.
 *
 * Resets the completion, and then sets its callback, error handler, callback thread, and parent.
 */
static inline void vdo_prepare_completion(struct vdo_completion *completion,
					  vdo_action_fn callback,
					  vdo_action_fn error_handler,
					  thread_id_t callback_thread_id, void *parent)
{
	vdo_reset_completion(completion);
	vdo_set_completion_callback(completion, callback, callback_thread_id);
	completion->error_handler = error_handler;
	completion->parent = parent;
}

/**
 * vdo_prepare_completion_for_requeue() - Prepare a completion for launch ensuring that it will
 *                                        always be requeued.
 *
 * Resets the completion, and then sets its callback, error handler, callback thread, and parent.
 */
static inline void vdo_prepare_completion_for_requeue(struct vdo_completion *completion,
						      vdo_action_fn callback,
						      vdo_action_fn error_handler,
						      thread_id_t callback_thread_id,
						      void *parent)
{
	vdo_prepare_completion(completion, callback, error_handler,
			       callback_thread_id, parent);
	completion->requeue = true;
}

void vdo_enqueue_completion(struct vdo_completion *completion,
			    enum vdo_completion_priority priority);


bool vdo_requeue_completion_if_needed(struct vdo_completion *completion,
				      thread_id_t callback_thread_id);

#endif /* VDO_COMPLETION_H */
