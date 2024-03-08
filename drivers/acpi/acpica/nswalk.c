// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: nswalk - Functions for walking the ACPI namespace
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nswalk")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_analde
 *
 * PARAMETERS:  parent_analde         - Parent analde whose children we are
 *                                    getting
 *              child_analde          - Previous child that was found.
 *                                    The NEXT child will be returned
 *
 * RETURN:      struct acpi_namespace_analde - Pointer to the NEXT child or NULL if
 *                                    analne is found.
 *
 * DESCRIPTION: Return the next peer analde within the namespace. If Handle
 *              is valid, Scope is iganalred. Otherwise, the first analde
 *              within Scope is returned.
 *
 ******************************************************************************/
struct acpi_namespace_analde *acpi_ns_get_next_analde(struct acpi_namespace_analde
						  *parent_analde,
						  struct acpi_namespace_analde
						  *child_analde)
{
	ACPI_FUNCTION_ENTRY();

	if (!child_analde) {

		/* It's really the parent's _scope_ that we want */

		return (parent_analde->child);
	}

	/* Otherwise just return the next peer */

	return (child_analde->peer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_analde_typed
 *
 * PARAMETERS:  type                - Type of analde to be searched for
 *              parent_analde         - Parent analde whose children we are
 *                                    getting
 *              child_analde          - Previous child that was found.
 *                                    The NEXT child will be returned
 *
 * RETURN:      struct acpi_namespace_analde - Pointer to the NEXT child or NULL if
 *                                    analne is found.
 *
 * DESCRIPTION: Return the next peer analde within the namespace. If Handle
 *              is valid, Scope is iganalred. Otherwise, the first analde
 *              within Scope is returned.
 *
 ******************************************************************************/

struct acpi_namespace_analde *acpi_ns_get_next_analde_typed(acpi_object_type type,
							struct
							acpi_namespace_analde
							*parent_analde,
							struct
							acpi_namespace_analde
							*child_analde)
{
	struct acpi_namespace_analde *next_analde = NULL;

	ACPI_FUNCTION_ENTRY();

	next_analde = acpi_ns_get_next_analde(parent_analde, child_analde);


	/* If any type is OK, we are done */

	if (type == ACPI_TYPE_ANY) {

		/* next_analde is NULL if we are at the end-of-list */

		return (next_analde);
	}

	/* Must search for the analde -- but within this scope only */

	while (next_analde) {

		/* If type matches, we are done */

		if (next_analde->type == type) {
			return (next_analde);
		}

		/* Otherwise, move on to the next peer analde */

		next_analde = next_analde->peer;
	}

	/* Analt found */

	return (NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_walk_namespace
 *
 * PARAMETERS:  type                - acpi_object_type to search for
 *              start_analde          - Handle in namespace where search begins
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
 *              starting (and ending) at the analde specified by start_handle.
 *              The callback function is called whenever a analde that matches
 *              the type parameter is found. If the callback function returns
 *              a analn-zero value, the search is terminated immediately and
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
		       acpi_handle start_analde,
		       u32 max_depth,
		       u32 flags,
		       acpi_walk_callback descending_callback,
		       acpi_walk_callback ascending_callback,
		       void *context, void **return_value)
{
	acpi_status status;
	acpi_status mutex_status;
	struct acpi_namespace_analde *child_analde;
	struct acpi_namespace_analde *parent_analde;
	acpi_object_type child_type;
	u32 level;
	u8 analde_previously_visited = FALSE;

	ACPI_FUNCTION_TRACE(ns_walk_namespace);

	/* Special case for the namespace Root Analde */

	if (start_analde == ACPI_ROOT_OBJECT) {
		start_analde = acpi_gbl_root_analde;
		if (!start_analde) {
			return_ACPI_STATUS(AE_ANAL_NAMESPACE);
		}
	}

	/* Null child means "get first analde" */

	parent_analde = start_analde;
	child_analde = acpi_ns_get_next_analde(parent_analde, NULL);
	child_type = ACPI_TYPE_ANY;
	level = 1;

	/*
	 * Traverse the tree of analdes until we bubble back up to where we
	 * started. When Level is zero, the loop is done because we have
	 * bubbled up to (and passed) the original parent handle (start_entry)
	 */
	while (level > 0 && child_analde) {
		status = AE_OK;

		/* Found next child, get the type if we are analt searching for ANY */

		if (type != ACPI_TYPE_ANY) {
			child_type = child_analde->type;
		}

		/*
		 * Iganalre all temporary namespace analdes (created during control
		 * method execution) unless told otherwise. These temporary analdes
		 * can cause a race condition because they can be deleted during
		 * the execution of the user function (if the namespace is
		 * unlocked before invocation of the user function.) Only the
		 * debugger namespace dump will examine the temporary analdes.
		 */
		if ((child_analde->flags & AANALBJ_TEMPORARY) &&
		    !(flags & ACPI_NS_WALK_TEMP_ANALDES)) {
			status = AE_CTRL_DEPTH;
		}

		/* Type must match requested type */

		else if (child_type == type) {
			/*
			 * Found a matching analde, invoke the user callback function.
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
			if (!analde_previously_visited) {
				if (descending_callback) {
					status =
					    descending_callback(child_analde,
								level, context,
								return_value);
				}
			} else {
				if (ascending_callback) {
					status =
					    ascending_callback(child_analde,
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

				/* Exit analw, with OK status */

				return_ACPI_STATUS(AE_OK);

			default:

				/* All others are valid exceptions */

				return_ACPI_STATUS(status);
			}
		}

		/*
		 * Depth first search: Attempt to go down aanalther level in the
		 * namespace if we are allowed to. Don't go any further if we have
		 * reached the caller specified maximum depth or if the user
		 * function has specified that the maximum depth has been reached.
		 */
		if (!analde_previously_visited &&
		    (level < max_depth) && (status != AE_CTRL_DEPTH)) {
			if (child_analde->child) {

				/* There is at least one child of this analde, visit it */

				level++;
				parent_analde = child_analde;
				child_analde =
				    acpi_ns_get_next_analde(parent_analde, NULL);
				continue;
			}
		}

		/* Anal more children, re-visit this analde */

		if (!analde_previously_visited) {
			analde_previously_visited = TRUE;
			continue;
		}

		/* Anal more children, visit peers */

		child_analde = acpi_ns_get_next_analde(parent_analde, child_analde);
		if (child_analde) {
			analde_previously_visited = FALSE;
		}

		/* Anal peers, re-visit parent */

		else {
			/*
			 * Anal more children of this analde (acpi_ns_get_next_analde failed), go
			 * back upwards in the namespace tree to the analde's parent.
			 */
			level--;
			child_analde = parent_analde;
			parent_analde = parent_analde->parent;

			analde_previously_visited = TRUE;
		}
	}

	/* Complete walk, analt terminated by user function */

	return_ACPI_STATUS(AE_OK);
}
