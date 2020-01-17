// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: nswalk - Functions for walking the ACPI namespace
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nswalk")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_yesde
 *
 * PARAMETERS:  parent_yesde         - Parent yesde whose children we are
 *                                    getting
 *              child_yesde          - Previous child that was found.
 *                                    The NEXT child will be returned
 *
 * RETURN:      struct acpi_namespace_yesde - Pointer to the NEXT child or NULL if
 *                                    yesne is found.
 *
 * DESCRIPTION: Return the next peer yesde within the namespace. If Handle
 *              is valid, Scope is igyesred. Otherwise, the first yesde
 *              within Scope is returned.
 *
 ******************************************************************************/
struct acpi_namespace_yesde *acpi_ns_get_next_yesde(struct acpi_namespace_yesde
						  *parent_yesde,
						  struct acpi_namespace_yesde
						  *child_yesde)
{
	ACPI_FUNCTION_ENTRY();

	if (!child_yesde) {

		/* It's really the parent's _scope_ that we want */

		return (parent_yesde->child);
	}

	/* Otherwise just return the next peer */

	return (child_yesde->peer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_yesde_typed
 *
 * PARAMETERS:  type                - Type of yesde to be searched for
 *              parent_yesde         - Parent yesde whose children we are
 *                                    getting
 *              child_yesde          - Previous child that was found.
 *                                    The NEXT child will be returned
 *
 * RETURN:      struct acpi_namespace_yesde - Pointer to the NEXT child or NULL if
 *                                    yesne is found.
 *
 * DESCRIPTION: Return the next peer yesde within the namespace. If Handle
 *              is valid, Scope is igyesred. Otherwise, the first yesde
 *              within Scope is returned.
 *
 ******************************************************************************/

struct acpi_namespace_yesde *acpi_ns_get_next_yesde_typed(acpi_object_type type,
							struct
							acpi_namespace_yesde
							*parent_yesde,
							struct
							acpi_namespace_yesde
							*child_yesde)
{
	struct acpi_namespace_yesde *next_yesde = NULL;

	ACPI_FUNCTION_ENTRY();

	next_yesde = acpi_ns_get_next_yesde(parent_yesde, child_yesde);


	/* If any type is OK, we are done */

	if (type == ACPI_TYPE_ANY) {

		/* next_yesde is NULL if we are at the end-of-list */

		return (next_yesde);
	}

	/* Must search for the yesde -- but within this scope only */

	while (next_yesde) {

		/* If type matches, we are done */

		if (next_yesde->type == type) {
			return (next_yesde);
		}

		/* Otherwise, move on to the next peer yesde */

		next_yesde = next_yesde->peer;
	}

	/* Not found */

	return (NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_walk_namespace
 *
 * PARAMETERS:  type                - acpi_object_type to search for
 *              start_yesde          - Handle in namespace where search begins
 *              max_depth           - Depth to which search is to reach
 *              flags               - Whether to unlock the NS before invoking
 *                                    the callback routine
 *              descending_callback - Called during tree descent
 *                                    when an object of "Type" is found
 *              ascending_callback  - Called during tree ascent
 *                                    when an object of "Type" is found
 *              context             - Passed to user function(s) above
 *              return_value        - from the user_function if terminated
 *                                    early. Otherwise, returns NULL.
 * RETURNS:     Status
 *
 * DESCRIPTION: Performs a modified depth-first walk of the namespace tree,
 *              starting (and ending) at the yesde specified by start_handle.
 *              The callback function is called whenever a yesde that matches
 *              the type parameter is found. If the callback function returns
 *              a yesn-zero value, the search is terminated immediately and
 *              this value is returned to the caller.
 *
 *              The point of this procedure is to provide a generic namespace
 *              walk routine that can be called from multiple places to
 *              provide multiple services; the callback function(s) can be
 *              tailored to each task, whether it is a print function,
 *              a compare function, etc.
 *
 ******************************************************************************/

acpi_status
acpi_ns_walk_namespace(acpi_object_type type,
		       acpi_handle start_yesde,
		       u32 max_depth,
		       u32 flags,
		       acpi_walk_callback descending_callback,
		       acpi_walk_callback ascending_callback,
		       void *context, void **return_value)
{
	acpi_status status;
	acpi_status mutex_status;
	struct acpi_namespace_yesde *child_yesde;
	struct acpi_namespace_yesde *parent_yesde;
	acpi_object_type child_type;
	u32 level;
	u8 yesde_previously_visited = FALSE;

	ACPI_FUNCTION_TRACE(ns_walk_namespace);

	/* Special case for the namespace Root Node */

	if (start_yesde == ACPI_ROOT_OBJECT) {
		start_yesde = acpi_gbl_root_yesde;
	}

	/* Null child means "get first yesde" */

	parent_yesde = start_yesde;
	child_yesde = acpi_ns_get_next_yesde(parent_yesde, NULL);
	child_type = ACPI_TYPE_ANY;
	level = 1;

	/*
	 * Traverse the tree of yesdes until we bubble back up to where we
	 * started. When Level is zero, the loop is done because we have
	 * bubbled up to (and passed) the original parent handle (start_entry)
	 */
	while (level > 0 && child_yesde) {
		status = AE_OK;

		/* Found next child, get the type if we are yest searching for ANY */

		if (type != ACPI_TYPE_ANY) {
			child_type = child_yesde->type;
		}

		/*
		 * Igyesre all temporary namespace yesdes (created during control
		 * method execution) unless told otherwise. These temporary yesdes
		 * can cause a race condition because they can be deleted during
		 * the execution of the user function (if the namespace is
		 * unlocked before invocation of the user function.) Only the
		 * debugger namespace dump will examine the temporary yesdes.
		 */
		if ((child_yesde->flags & ANOBJ_TEMPORARY) &&
		    !(flags & ACPI_NS_WALK_TEMP_NODES)) {
			status = AE_CTRL_DEPTH;
		}

		/* Type must match requested type */

		else if (child_type == type) {
			/*
			 * Found a matching yesde, invoke the user callback function.
			 * Unlock the namespace if flag is set.
			 */
			if (flags & ACPI_NS_WALK_UNLOCK) {
				mutex_status =
				    acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
				if (ACPI_FAILURE(mutex_status)) {
					return_ACPI_STATUS(mutex_status);
				}
			}

			/*
			 * Invoke the user function, either descending, ascending,
			 * or both.
			 */
			if (!yesde_previously_visited) {
				if (descending_callback) {
					status =
					    descending_callback(child_yesde,
								level, context,
								return_value);
				}
			} else {
				if (ascending_callback) {
					status =
					    ascending_callback(child_yesde,
							       level, context,
							       return_value);
				}
			}

			if (flags & ACPI_NS_WALK_UNLOCK) {
				mutex_status =
				    acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
				if (ACPI_FAILURE(mutex_status)) {
					return_ACPI_STATUS(mutex_status);
				}
			}

			switch (status) {
			case AE_OK:
			case AE_CTRL_DEPTH:

				/* Just keep going */
				break;

			case AE_CTRL_TERMINATE:

				/* Exit yesw, with OK status */

				return_ACPI_STATUS(AE_OK);

			default:

				/* All others are valid exceptions */

				return_ACPI_STATUS(status);
			}
		}

		/*
		 * Depth first search: Attempt to go down ayesther level in the
		 * namespace if we are allowed to. Don't go any further if we have
		 * reached the caller specified maximum depth or if the user
		 * function has specified that the maximum depth has been reached.
		 */
		if (!yesde_previously_visited &&
		    (level < max_depth) && (status != AE_CTRL_DEPTH)) {
			if (child_yesde->child) {

				/* There is at least one child of this yesde, visit it */

				level++;
				parent_yesde = child_yesde;
				child_yesde =
				    acpi_ns_get_next_yesde(parent_yesde, NULL);
				continue;
			}
		}

		/* No more children, re-visit this yesde */

		if (!yesde_previously_visited) {
			yesde_previously_visited = TRUE;
			continue;
		}

		/* No more children, visit peers */

		child_yesde = acpi_ns_get_next_yesde(parent_yesde, child_yesde);
		if (child_yesde) {
			yesde_previously_visited = FALSE;
		}

		/* No peers, re-visit parent */

		else {
			/*
			 * No more children of this yesde (acpi_ns_get_next_yesde failed), go
			 * back upwards in the namespace tree to the yesde's parent.
			 */
			level--;
			child_yesde = parent_yesde;
			parent_yesde = parent_yesde->parent;

			yesde_previously_visited = TRUE;
		}
	}

	/* Complete walk, yest terminated by user function */

	return_ACPI_STATUS(AE_OK);
}
