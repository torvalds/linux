/******************************************************************************
 *
 * Module Name: nswalk - Functions for walking the ACPI namespace
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nswalk")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_node
 *
 * PARAMETERS:  parent_node         - Parent node whose children we are
 *                                    getting
 *              child_node          - Previous child that was found.
 *                                    The NEXT child will be returned
 *
 * RETURN:      struct acpi_namespace_node - Pointer to the NEXT child or NULL if
 *                                    none is found.
 *
 * DESCRIPTION: Return the next peer node within the namespace. If Handle
 *              is valid, Scope is ignored. Otherwise, the first node
 *              within Scope is returned.
 *
 ******************************************************************************/
struct acpi_namespace_node *acpi_ns_get_next_node(struct acpi_namespace_node
						  *parent_node,
						  struct acpi_namespace_node
						  *child_node)
{
	ACPI_FUNCTION_ENTRY();

	if (!child_node) {

		/* It's really the parent's _scope_ that we want */

		return (parent_node->child);
	}

	/* Otherwise just return the next peer */

	return (child_node->peer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_node_typed
 *
 * PARAMETERS:  type                - Type of node to be searched for
 *              parent_node         - Parent node whose children we are
 *                                    getting
 *              child_node          - Previous child that was found.
 *                                    The NEXT child will be returned
 *
 * RETURN:      struct acpi_namespace_node - Pointer to the NEXT child or NULL if
 *                                    none is found.
 *
 * DESCRIPTION: Return the next peer node within the namespace. If Handle
 *              is valid, Scope is ignored. Otherwise, the first node
 *              within Scope is returned.
 *
 ******************************************************************************/

struct acpi_namespace_node *acpi_ns_get_next_node_typed(acpi_object_type type,
							struct
							acpi_namespace_node
							*parent_node,
							struct
							acpi_namespace_node
							*child_node)
{
	struct acpi_namespace_node *next_node = NULL;

	ACPI_FUNCTION_ENTRY();

	next_node = acpi_ns_get_next_node(parent_node, child_node);


	/* If any type is OK, we are done */

	if (type == ACPI_TYPE_ANY) {

		/* next_node is NULL if we are at the end-of-list */

		return (next_node);
	}

	/* Must search for the node -- but within this scope only */

	while (next_node) {

		/* If type matches, we are done */

		if (next_node->type == type) {
			return (next_node);
		}

		/* Otherwise, move on to the next peer node */

		next_node = next_node->peer;
	}

	/* Not found */

	return (NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_walk_namespace
 *
 * PARAMETERS:  type                - acpi_object_type to search for
 *              start_node          - Handle in namespace where search begins
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
 *              starting (and ending) at the node specified by start_handle.
 *              The callback function is called whenever a node that matches
 *              the type parameter is found. If the callback function returns
 *              a non-zero value, the search is terminated immediately and
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
		       acpi_handle start_node,
		       u32 max_depth,
		       u32 flags,
		       acpi_walk_callback descending_callback,
		       acpi_walk_callback ascending_callback,
		       void *context, void **return_value)
{
	acpi_status status;
	acpi_status mutex_status;
	struct acpi_namespace_node *child_node;
	struct acpi_namespace_node *parent_node;
	acpi_object_type child_type;
	u32 level;
	u8 node_previously_visited = FALSE;

	ACPI_FUNCTION_TRACE(ns_walk_namespace);

	/* Special case for the namespace Root Node */

	if (start_node == ACPI_ROOT_OBJECT) {
		start_node = acpi_gbl_root_node;
	}

	/* Null child means "get first node" */

	parent_node = start_node;
	child_node = acpi_ns_get_next_node(parent_node, NULL);
	child_type = ACPI_TYPE_ANY;
	level = 1;

	/*
	 * Traverse the tree of nodes until we bubble back up to where we
	 * started. When Level is zero, the loop is done because we have
	 * bubbled up to (and passed) the original parent handle (start_entry)
	 */
	while (level > 0 && child_node) {
		status = AE_OK;

		/* Found next child, get the type if we are not searching for ANY */

		if (type != ACPI_TYPE_ANY) {
			child_type = child_node->type;
		}

		/*
		 * Ignore all temporary namespace nodes (created during control
		 * method execution) unless told otherwise. These temporary nodes
		 * can cause a race condition because they can be deleted during
		 * the execution of the user function (if the namespace is
		 * unlocked before invocation of the user function.) Only the
		 * debugger namespace dump will examine the temporary nodes.
		 */
		if ((child_node->flags & ANOBJ_TEMPORARY) &&
		    !(flags & ACPI_NS_WALK_TEMP_NODES)) {
			status = AE_CTRL_DEPTH;
		}

		/* Type must match requested type */

		else if (child_type == type) {
			/*
			 * Found a matching node, invoke the user callback function.
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
			if (!node_previously_visited) {
				if (descending_callback) {
					status =
					    descending_callback(child_node,
								level, context,
								return_value);
				}
			} else {
				if (ascending_callback) {
					status =
					    ascending_callback(child_node,
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

				/* Exit now, with OK status */

				return_ACPI_STATUS(AE_OK);

			default:

				/* All others are valid exceptions */

				return_ACPI_STATUS(status);
			}
		}

		/*
		 * Depth first search: Attempt to go down another level in the
		 * namespace if we are allowed to. Don't go any further if we have
		 * reached the caller specified maximum depth or if the user
		 * function has specified that the maximum depth has been reached.
		 */
		if (!node_previously_visited &&
		    (level < max_depth) && (status != AE_CTRL_DEPTH)) {
			if (child_node->child) {

				/* There is at least one child of this node, visit it */

				level++;
				parent_node = child_node;
				child_node =
				    acpi_ns_get_next_node(parent_node, NULL);
				continue;
			}
		}

		/* No more children, re-visit this node */

		if (!node_previously_visited) {
			node_previously_visited = TRUE;
			continue;
		}

		/* No more children, visit peers */

		child_node = acpi_ns_get_next_node(parent_node, child_node);
		if (child_node) {
			node_previously_visited = FALSE;
		}

		/* No peers, re-visit parent */

		else {
			/*
			 * No more children of this node (acpi_ns_get_next_node failed), go
			 * back upwards in the namespace tree to the node's parent.
			 */
			level--;
			child_node = parent_node;
			parent_node = parent_node->parent;

			node_previously_visited = TRUE;
		}
	}

	/* Complete walk, not terminated by user function */

	return_ACPI_STATUS(AE_OK);
}
