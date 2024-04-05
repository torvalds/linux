/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_ACTION_MANAGER_H
#define VDO_ACTION_MANAGER_H

#include "admin-state.h"
#include "types.h"

/*
 * An action_manager provides a generic mechanism for applying actions to multi-zone entities (such
 * as the block map or slab depot). Each action manager is tied to a specific context for which it
 * manages actions. The manager ensures that only one action is active on that context at a time,
 * and supports at most one pending action. Calls to schedule an action when there is already a
 * pending action will result in VDO_COMPONENT_BUSY errors. Actions may only be submitted to the
 * action manager from a single thread (which thread is determined when the action manager is
 * constructed).
 *
 * A scheduled action consists of four components:
 *
 *   preamble
 *     an optional method to be run on the initiator thread before applying the action to all zones
 *   zone_action
 *     an optional method to be applied to each of the zones
 *   conclusion
 *     an optional method to be run on the initiator thread once the per-zone method has been
 *     applied to all zones
 *   parent
 *     an optional completion to be finished once the conclusion is done
 *
 * At least one of the three methods must be provided.
 */

/*
 * A function which is to be applied asynchronously to a set of zones.
 * @context: The object which holds the per-zone context for the action
 * @zone_number: The number of zone to which the action is being applied
 * @parent: The object to notify when the action is complete
 */
typedef void (*vdo_zone_action_fn)(void *context, zone_count_t zone_number,
				   struct vdo_completion *parent);

/*
 * A function which is to be applied asynchronously on an action manager's initiator thread as the
 * preamble of an action.
 * @context: The object which holds the per-zone context for the action
 * @parent: The object to notify when the action is complete
 */
typedef void (*vdo_action_preamble_fn)(void *context, struct vdo_completion *parent);

/*
 * A function which will run on the action manager's initiator thread as the conclusion of an
 * action.
 * @context: The object which holds the per-zone context for the action
 *
 * Return: VDO_SUCCESS or an error
 */
typedef int (*vdo_action_conclusion_fn)(void *context);

/*
 * A function to schedule an action.
 * @context: The object which holds the per-zone context for the action
 *
 * Return: true if an action was scheduled
 */
typedef bool (*vdo_action_scheduler_fn)(void *context);

/*
 * A function to get the id of the thread associated with a given zone.
 * @context: The action context
 * @zone_number: The number of the zone for which the thread ID is desired
 */
typedef thread_id_t (*vdo_zone_thread_getter_fn)(void *context, zone_count_t zone_number);

struct action_manager;

int __must_check vdo_make_action_manager(zone_count_t zones,
					 vdo_zone_thread_getter_fn get_zone_thread_id,
					 thread_id_t initiator_thread_id, void *context,
					 vdo_action_scheduler_fn scheduler,
					 struct vdo *vdo,
					 struct action_manager **manager_ptr);

const struct admin_state_code *__must_check
vdo_get_current_manager_operation(struct action_manager *manager);

void * __must_check vdo_get_current_action_context(struct action_manager *manager);

bool vdo_schedule_default_action(struct action_manager *manager);

bool vdo_schedule_action(struct action_manager *manager, vdo_action_preamble_fn preamble,
			 vdo_zone_action_fn action, vdo_action_conclusion_fn conclusion,
			 struct vdo_completion *parent);

bool vdo_schedule_operation(struct action_manager *manager,
			    const struct admin_state_code *operation,
			    vdo_action_preamble_fn preamble, vdo_zone_action_fn action,
			    vdo_action_conclusion_fn conclusion,
			    struct vdo_completion *parent);

bool vdo_schedule_operation_with_context(struct action_manager *manager,
					 const struct admin_state_code *operation,
					 vdo_action_preamble_fn preamble,
					 vdo_zone_action_fn action,
					 vdo_action_conclusion_fn conclusion,
					 void *context, struct vdo_completion *parent);

#endif /* VDO_ACTION_MANAGER_H */
