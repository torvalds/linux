// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "action-manager.h"

#include "memory-alloc.h"
#include "permassert.h"

#include "admin-state.h"
#include "completion.h"
#include "status-codes.h"
#include "types.h"
#include "vdo.h"

/**
 * struct action - An action to be performed in each of a set of zones.
 * @in_use: Whether this structure is in use.
 * @operation: The admin operation associated with this action.
 * @preamble: The method to run on the initiator thread before the action is applied to each zone.
 * @zone_action: The action to be performed in each zone.
 * @conclusion: The method to run on the initiator thread after the action is applied to each zone.
 * @parent: The object to notify when the action is complete.
 * @context: The action specific context.
 * @next: The action to perform after this one.
 */
struct action {
	bool in_use;
	const struct admin_state_code *operation;
	vdo_action_preamble_fn preamble;
	vdo_zone_action_fn zone_action;
	vdo_action_conclusion_fn conclusion;
	struct vdo_completion *parent;
	void *context;
	struct action *next;
};

/**
 * struct action_manager - Definition of an action manager.
 * @completion: The completion for performing actions.
 * @state: The state of this action manager.
 * @actions: The two action slots.
 * @current_action: The current action slot.
 * @zones: The number of zones in which an action is to be applied.
 * @Scheduler: A function to schedule a default next action.
 * @get_zone_thread_id: A function to get the id of the thread on which to apply an action to a
 *                      zone.
 * @initiator_thread_id: The ID of the thread on which actions may be initiated.
 * @context: Opaque data associated with this action manager.
 * @acting_zone: The zone currently being acted upon.
 */
struct action_manager {
	struct vdo_completion completion;
	struct admin_state state;
	struct action actions[2];
	struct action *current_action;
	zone_count_t zones;
	vdo_action_scheduler_fn scheduler;
	vdo_zone_thread_getter_fn get_zone_thread_id;
	thread_id_t initiator_thread_id;
	void *context;
	zone_count_t acting_zone;
};

static inline struct action_manager *as_action_manager(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_ACTION_COMPLETION);
	return container_of(completion, struct action_manager, completion);
}

/* Implements vdo_action_scheduler_fn. */
static bool no_default_action(void *context __always_unused)
{
	return false;
}

/* Implements vdo_action_preamble_fn. */
static void no_preamble(void *context __always_unused, struct vdo_completion *completion)
{
	vdo_finish_completion(completion);
}

/* Implements vdo_action_conclusion_fn. */
static int no_conclusion(void *context __always_unused)
{
	return VDO_SUCCESS;
}

/**
 * vdo_make_action_manager() - Make an action manager.
 * @zones: The number of zones to which actions will be applied.
 * @get_zone_thread_id: A function to get the thread id associated with a zone.
 * @initiator_thread_id: The thread on which actions may initiated.
 * @context: The object which holds the per-zone context for the action.
 * @scheduler: A function to schedule a next action after an action concludes if there is no
 *             pending action (may be NULL).
 * @vdo: The vdo used to initialize completions.
 * @manager_ptr: A pointer to hold the new action manager.
 *
 * Return: VDO_SUCCESS or an error code.
 */
int vdo_make_action_manager(zone_count_t zones,
			    vdo_zone_thread_getter_fn get_zone_thread_id,
			    thread_id_t initiator_thread_id, void *context,
			    vdo_action_scheduler_fn scheduler, struct vdo *vdo,
			    struct action_manager **manager_ptr)
{
	struct action_manager *manager;
	int result = vdo_allocate(1, struct action_manager, __func__, &manager);

	if (result != VDO_SUCCESS)
		return result;

	*manager = (struct action_manager) {
		.zones = zones,
		.scheduler =
			((scheduler == NULL) ? no_default_action : scheduler),
		.get_zone_thread_id = get_zone_thread_id,
		.initiator_thread_id = initiator_thread_id,
		.context = context,
	};

	manager->actions[0].next = &manager->actions[1];
	manager->current_action = manager->actions[1].next =
		&manager->actions[0];
	vdo_set_admin_state_code(&manager->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
	vdo_initialize_completion(&manager->completion, vdo, VDO_ACTION_COMPLETION);
	*manager_ptr = manager;
	return VDO_SUCCESS;
}

const struct admin_state_code *vdo_get_current_manager_operation(struct action_manager *manager)
{
	return vdo_get_admin_state_code(&manager->state);
}

void *vdo_get_current_action_context(struct action_manager *manager)
{
	return manager->current_action->in_use ? manager->current_action->context : NULL;
}

static void finish_action_callback(struct vdo_completion *completion);
static void apply_to_zone(struct vdo_completion *completion);

static thread_id_t get_acting_zone_thread_id(struct action_manager *manager)
{
	return manager->get_zone_thread_id(manager->context, manager->acting_zone);
}

static void preserve_error(struct vdo_completion *completion)
{
	if (completion->parent != NULL)
		vdo_set_completion_result(completion->parent, completion->result);

	vdo_reset_completion(completion);
	vdo_run_completion(completion);
}

static void prepare_for_next_zone(struct action_manager *manager)
{
	vdo_prepare_completion_for_requeue(&manager->completion, apply_to_zone,
					   preserve_error,
					   get_acting_zone_thread_id(manager),
					   manager->current_action->parent);
}

static void prepare_for_conclusion(struct action_manager *manager)
{
	vdo_prepare_completion_for_requeue(&manager->completion, finish_action_callback,
					   preserve_error, manager->initiator_thread_id,
					   manager->current_action->parent);
}

static void apply_to_zone(struct vdo_completion *completion)
{
	zone_count_t zone;
	struct action_manager *manager = as_action_manager(completion);

	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == get_acting_zone_thread_id(manager)),
			    "%s() called on acting zones's thread", __func__);

	zone = manager->acting_zone++;
	if (manager->acting_zone == manager->zones) {
		/*
		 * We are about to apply to the last zone. Once that is finished, we're done, so go
		 * back to the initiator thread and finish up.
		 */
		prepare_for_conclusion(manager);
	} else {
		/* Prepare to come back on the next zone */
		prepare_for_next_zone(manager);
	}

	manager->current_action->zone_action(manager->context, zone, completion);
}

static void handle_preamble_error(struct vdo_completion *completion)
{
	/* Skip the zone actions since the preamble failed. */
	completion->callback = finish_action_callback;
	preserve_error(completion);
}

static void launch_current_action(struct action_manager *manager)
{
	struct action *action = manager->current_action;
	int result = vdo_start_operation(&manager->state, action->operation);

	if (result != VDO_SUCCESS) {
		if (action->parent != NULL)
			vdo_set_completion_result(action->parent, result);

		/* We aren't going to run the preamble, so don't run the conclusion */
		action->conclusion = no_conclusion;
		finish_action_callback(&manager->completion);
		return;
	}

	if (action->zone_action == NULL) {
		prepare_for_conclusion(manager);
	} else {
		manager->acting_zone = 0;
		vdo_prepare_completion_for_requeue(&manager->completion, apply_to_zone,
						   handle_preamble_error,
						   get_acting_zone_thread_id(manager),
						   manager->current_action->parent);
	}

	action->preamble(manager->context, &manager->completion);
}

/**
 * vdo_schedule_default_action() - Attempt to schedule the default action.
 * @manager: The action manager.
 *
 * If the manager is not operating normally, the action will not be scheduled.
 *
 * Return: true if an action was scheduled.
 */
bool vdo_schedule_default_action(struct action_manager *manager)
{
	/* Don't schedule a default action if we are operating or not in normal operation. */
	const struct admin_state_code *code = vdo_get_current_manager_operation(manager);

	return ((code == VDO_ADMIN_STATE_NORMAL_OPERATION) &&
		manager->scheduler(manager->context));
}

static void finish_action_callback(struct vdo_completion *completion)
{
	bool has_next_action;
	int result;
	struct action_manager *manager = as_action_manager(completion);
	struct action action = *(manager->current_action);

	manager->current_action->in_use = false;
	manager->current_action = manager->current_action->next;

	/*
	 * We need to check this now to avoid use-after-free issues if running the conclusion or
	 * notifying the parent results in the manager being freed.
	 */
	has_next_action =
		(manager->current_action->in_use || vdo_schedule_default_action(manager));
	result = action.conclusion(manager->context);
	vdo_finish_operation(&manager->state, VDO_SUCCESS);
	if (action.parent != NULL)
		vdo_continue_completion(action.parent, result);

	if (has_next_action)
		launch_current_action(manager);
}

/**
 * vdo_schedule_action() - Schedule an action to be applied to all zones.
 * @manager: The action manager to schedule the action on.
 * @preamble: A method to be invoked on the initiator thread once this action is started but before
 *            applying to each zone; may be NULL.
 * @action: The action to apply to each zone; may be NULL.
 * @conclusion: A method to be invoked back on the initiator thread once the action has been
 *              applied to all zones; may be NULL.
 * @parent: The object to notify once the action is complete or if the action can not be scheduled;
 *          may be NULL.
 *
 * The action will be launched immediately if there is no current action, or as soon as the current
 * action completes. If there is already a pending action, this action will not be scheduled, and,
 * if it has a parent, that parent will be notified. At least one of the preamble, action, or
 * conclusion must not be NULL.
 *
 * Return: true if the action was scheduled.
 */
bool vdo_schedule_action(struct action_manager *manager, vdo_action_preamble_fn preamble,
			 vdo_zone_action_fn action, vdo_action_conclusion_fn conclusion,
			 struct vdo_completion *parent)
{
	return vdo_schedule_operation(manager, VDO_ADMIN_STATE_OPERATING, preamble,
				      action, conclusion, parent);
}

/**
 * vdo_schedule_operation() - Schedule an operation to be applied to all zones.
 * @manager: The action manager to schedule the action on.
 * @operation: The operation this action will perform
 * @preamble: A method to be invoked on the initiator thread once this action is started but before
 *            applying to each zone; may be NULL.
 * @action: The action to apply to each zone; may be NULL.
 * @conclusion: A method to be invoked back on the initiator thread once the action has been
 *              applied to all zones; may be NULL.
 * @parent: The object to notify once the action is complete or if the action can not be scheduled;
 *          may be NULL.
 *
 * The operation's action will be launched immediately if there is no current action, or as soon as
 * the current action completes. If there is already a pending action, this operation will not be
 * scheduled, and, if it has a parent, that parent will be notified. At least one of the preamble,
 * action, or conclusion must not be NULL.
 *
 * Return: true if the action was scheduled.
 */
bool vdo_schedule_operation(struct action_manager *manager,
			    const struct admin_state_code *operation,
			    vdo_action_preamble_fn preamble, vdo_zone_action_fn action,
			    vdo_action_conclusion_fn conclusion,
			    struct vdo_completion *parent)
{
	return vdo_schedule_operation_with_context(manager, operation, preamble, action,
						   conclusion, NULL, parent);
}

/**
 * vdo_schedule_operation_with_context() - Schedule an operation on all zones.
 * @manager: The action manager to schedule the action on.
 * @operation: The operation this action will perform.
 * @preamble: A method to be invoked on the initiator thread once this action is started but before
 *            applying to each zone; may be NULL.
 * @action: The action to apply to each zone; may be NULL.
 * @conclusion: A method to be invoked back on the initiator thread once the action has been
 *              applied to all zones; may be NULL.
 * @context: An action-specific context which may be retrieved via
 *           vdo_get_current_action_context(); may be NULL.
 * @parent: The object to notify once the action is complete or if the action can not be scheduled;
 *          may be NULL.
 *
 * The operation's action will be launched immediately if there is no current action, or as soon as
 * the current action completes. If there is already a pending action, this operation will not be
 * scheduled, and, if it has a parent, that parent will be notified. At least one of the preamble,
 * action, or conclusion must not be NULL.
 *
 * Return: true if the action was scheduled
 */
bool vdo_schedule_operation_with_context(struct action_manager *manager,
					 const struct admin_state_code *operation,
					 vdo_action_preamble_fn preamble,
					 vdo_zone_action_fn action,
					 vdo_action_conclusion_fn conclusion,
					 void *context, struct vdo_completion *parent)
{
	struct action *current_action;

	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == manager->initiator_thread_id),
			    "action initiated from correct thread");
	if (!manager->current_action->in_use) {
		current_action = manager->current_action;
	} else if (!manager->current_action->next->in_use) {
		current_action = manager->current_action->next;
	} else {
		if (parent != NULL)
			vdo_continue_completion(parent, VDO_COMPONENT_BUSY);

		return false;
	}

	*current_action = (struct action) {
		.in_use = true,
		.operation = operation,
		.preamble = (preamble == NULL) ? no_preamble : preamble,
		.zone_action = action,
		.conclusion = (conclusion == NULL) ? no_conclusion : conclusion,
		.context = context,
		.parent = parent,
		.next = current_action->next,
	};

	if (current_action == manager->current_action)
		launch_current_action(manager);

	return true;
}
