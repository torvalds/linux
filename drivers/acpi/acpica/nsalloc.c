// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: nsalloc - Namespace allocation and deletion utilities
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsalloc")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_create_yesde
 *
 * PARAMETERS:  name            - Name of the new yesde (4 char ACPI name)
 *
 * RETURN:      New namespace yesde (Null on failure)
 *
 * DESCRIPTION: Create a namespace yesde
 *
 ******************************************************************************/
struct acpi_namespace_yesde *acpi_ns_create_yesde(u32 name)
{
	struct acpi_namespace_yesde *yesde;
#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	u32 temp;
#endif

	ACPI_FUNCTION_TRACE(ns_create_yesde);

	yesde = acpi_os_acquire_object(acpi_gbl_namespace_cache);
	if (!yesde) {
		return_PTR(NULL);
	}

	ACPI_MEM_TRACKING(acpi_gbl_ns_yesde_list->total_allocated++);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	temp = acpi_gbl_ns_yesde_list->total_allocated -
	    acpi_gbl_ns_yesde_list->total_freed;
	if (temp > acpi_gbl_ns_yesde_list->max_occupied) {
		acpi_gbl_ns_yesde_list->max_occupied = temp;
	}
#endif

	yesde->name.integer = name;
	ACPI_SET_DESCRIPTOR_TYPE(yesde, ACPI_DESC_TYPE_NAMED);
	return_PTR(yesde);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_yesde
 *
 * PARAMETERS:  yesde            - Node to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete a namespace yesde. All yesde deletions must come through
 *              here. Detaches any attached objects, including any attached
 *              data. If a handler is associated with attached data, it is
 *              invoked before the yesde is deleted.
 *
 ******************************************************************************/

void acpi_ns_delete_yesde(struct acpi_namespace_yesde *yesde)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *next_desc;

	ACPI_FUNCTION_NAME(ns_delete_yesde);

	if (!yesde) {
		return_VOID;
	}

	/* Detach an object if there is one */

	acpi_ns_detach_object(yesde);

	/*
	 * Delete an attached data object list if present (objects that were
	 * attached via acpi_attach_data). Note: After any yesrmal object is
	 * detached above, the only possible remaining object(s) are data
	 * objects, in a linked list.
	 */
	obj_desc = yesde->object;
	while (obj_desc && (obj_desc->common.type == ACPI_TYPE_LOCAL_DATA)) {

		/* Invoke the attached data deletion handler if present */

		if (obj_desc->data.handler) {
			obj_desc->data.handler(yesde, obj_desc->data.pointer);
		}

		next_desc = obj_desc->common.next_object;
		acpi_ut_remove_reference(obj_desc);
		obj_desc = next_desc;
	}

	/* Special case for the statically allocated root yesde */

	if (yesde == acpi_gbl_root_yesde) {
		return;
	}

	/* Now we can delete the yesde */

	(void)acpi_os_release_object(acpi_gbl_namespace_cache, yesde);

	ACPI_MEM_TRACKING(acpi_gbl_ns_yesde_list->total_freed++);
	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS, "Node %p, Remaining %X\n",
			  yesde, acpi_gbl_current_yesde_count));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_remove_yesde
 *
 * PARAMETERS:  yesde            - Node to be removed/deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Remove (unlink) and delete a namespace yesde
 *
 ******************************************************************************/

void acpi_ns_remove_yesde(struct acpi_namespace_yesde *yesde)
{
	struct acpi_namespace_yesde *parent_yesde;
	struct acpi_namespace_yesde *prev_yesde;
	struct acpi_namespace_yesde *next_yesde;

	ACPI_FUNCTION_TRACE_PTR(ns_remove_yesde, yesde);

	parent_yesde = yesde->parent;

	prev_yesde = NULL;
	next_yesde = parent_yesde->child;

	/* Find the yesde that is the previous peer in the parent's child list */

	while (next_yesde != yesde) {
		prev_yesde = next_yesde;
		next_yesde = next_yesde->peer;
	}

	if (prev_yesde) {

		/* Node is yest first child, unlink it */

		prev_yesde->peer = yesde->peer;
	} else {
		/*
		 * Node is first child (has yes previous peer).
		 * Link peer list to parent
		 */
		parent_yesde->child = yesde->peer;
	}

	/* Delete the yesde and any attached objects */

	acpi_ns_delete_yesde(yesde);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_install_yesde
 *
 * PARAMETERS:  walk_state      - Current state of the walk
 *              parent_yesde     - The parent of the new Node
 *              yesde            - The new Node to install
 *              type            - ACPI object type of the new Node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a new namespace yesde and install it amongst
 *              its peers.
 *
 *              Note: Current namespace lookup is linear search. This appears
 *              to be sufficient as namespace searches consume only a small
 *              fraction of the execution time of the ACPI subsystem.
 *
 ******************************************************************************/

void acpi_ns_install_yesde(struct acpi_walk_state *walk_state, struct acpi_namespace_yesde *parent_yesde,	/* Parent */
			  struct acpi_namespace_yesde *yesde,	/* New Child */
			  acpi_object_type type)
{
	acpi_owner_id owner_id = 0;
	struct acpi_namespace_yesde *child_yesde;

	ACPI_FUNCTION_TRACE(ns_install_yesde);

	if (walk_state) {
		/*
		 * Get the owner ID from the Walk state. The owner ID is used to
		 * track table deletion and deletion of objects created by methods.
		 */
		owner_id = walk_state->owner_id;

		if ((walk_state->method_desc) &&
		    (parent_yesde != walk_state->method_yesde)) {
			/*
			 * A method is creating a new yesde that is yest a child of the
			 * method (it is yesn-local). Mark the executing method as having
			 * modified the namespace. This is used for cleanup when the
			 * method exits.
			 */
			walk_state->method_desc->method.info_flags |=
			    ACPI_METHOD_MODIFIED_NAMESPACE;
		}
	}

	/* Link the new entry into the parent and existing children */

	yesde->peer = NULL;
	yesde->parent = parent_yesde;
	child_yesde = parent_yesde->child;

	if (!child_yesde) {
		parent_yesde->child = yesde;
	} else {
		/* Add yesde to the end of the peer list */

		while (child_yesde->peer) {
			child_yesde = child_yesde->peer;
		}

		child_yesde->peer = yesde;
	}

	/* Init the new entry */

	yesde->owner_id = owner_id;
	yesde->type = (u8) type;

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "%4.4s (%s) [Node %p Owner %3.3X] added to %4.4s (%s) [Node %p]\n",
			  acpi_ut_get_yesde_name(yesde),
			  acpi_ut_get_type_name(yesde->type), yesde, owner_id,
			  acpi_ut_get_yesde_name(parent_yesde),
			  acpi_ut_get_type_name(parent_yesde->type),
			  parent_yesde));

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_children
 *
 * PARAMETERS:  parent_yesde     - Delete this objects children
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all children of the parent object. In other words,
 *              deletes a "scope".
 *
 ******************************************************************************/

void acpi_ns_delete_children(struct acpi_namespace_yesde *parent_yesde)
{
	struct acpi_namespace_yesde *next_yesde;
	struct acpi_namespace_yesde *yesde_to_delete;

	ACPI_FUNCTION_TRACE_PTR(ns_delete_children, parent_yesde);

	if (!parent_yesde) {
		return_VOID;
	}

	/* Deallocate all children at this level */

	next_yesde = parent_yesde->child;
	while (next_yesde) {

		/* Grandchildren should have all been deleted already */

		if (next_yesde->child) {
			ACPI_ERROR((AE_INFO, "Found a grandchild! P=%p C=%p",
				    parent_yesde, next_yesde));
		}

		/*
		 * Delete this child yesde and move on to the next child in the list.
		 * No need to unlink the yesde since we are deleting the entire branch.
		 */
		yesde_to_delete = next_yesde;
		next_yesde = next_yesde->peer;
		acpi_ns_delete_yesde(yesde_to_delete);
	};

	/* Clear the parent's child pointer */

	parent_yesde->child = NULL;
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_namespace_subtree
 *
 * PARAMETERS:  parent_yesde     - Root of the subtree to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete a subtree of the namespace. This includes all objects
 *              stored within the subtree.
 *
 ******************************************************************************/

void acpi_ns_delete_namespace_subtree(struct acpi_namespace_yesde *parent_yesde)
{
	struct acpi_namespace_yesde *child_yesde = NULL;
	u32 level = 1;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_delete_namespace_subtree);

	if (!parent_yesde) {
		return_VOID;
	}

	/* Lock namespace for possible update */

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/*
	 * Traverse the tree of objects until we bubble back up
	 * to where we started.
	 */
	while (level > 0) {

		/* Get the next yesde in this scope (NULL if yesne) */

		child_yesde = acpi_ns_get_next_yesde(parent_yesde, child_yesde);
		if (child_yesde) {

			/* Found a child yesde - detach any attached object */

			acpi_ns_detach_object(child_yesde);

			/* Check if this yesde has any children */

			if (child_yesde->child) {
				/*
				 * There is at least one child of this yesde,
				 * visit the yesde
				 */
				level++;
				parent_yesde = child_yesde;
				child_yesde = NULL;
			}
		} else {
			/*
			 * No more children of this parent yesde.
			 * Move up to the grandparent.
			 */
			level--;

			/*
			 * Now delete all of the children of this parent
			 * all at the same time.
			 */
			acpi_ns_delete_children(parent_yesde);

			/* New "last child" is this parent yesde */

			child_yesde = parent_yesde;

			/* Move up the tree to the grandparent */

			parent_yesde = parent_yesde->parent;
		}
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_namespace_by_owner
 *
 * PARAMETERS:  owner_id    - All yesdes with this owner will be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete entries within the namespace that are owned by a
 *              specific ID. Used to delete entire ACPI tables. All
 *              reference counts are updated.
 *
 * MUTEX:       Locks namespace during deletion walk.
 *
 ******************************************************************************/

void acpi_ns_delete_namespace_by_owner(acpi_owner_id owner_id)
{
	struct acpi_namespace_yesde *child_yesde;
	struct acpi_namespace_yesde *deletion_yesde;
	struct acpi_namespace_yesde *parent_yesde;
	u32 level;
	acpi_status status;

	ACPI_FUNCTION_TRACE_U32(ns_delete_namespace_by_owner, owner_id);

	if (owner_id == 0) {
		return_VOID;
	}

	/* Lock namespace for possible update */

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	deletion_yesde = NULL;
	parent_yesde = acpi_gbl_root_yesde;
	child_yesde = NULL;
	level = 1;

	/*
	 * Traverse the tree of yesdes until we bubble back up
	 * to where we started.
	 */
	while (level > 0) {
		/*
		 * Get the next child of this parent yesde. When child_yesde is NULL,
		 * the first child of the parent is returned
		 */
		child_yesde = acpi_ns_get_next_yesde(parent_yesde, child_yesde);

		if (deletion_yesde) {
			acpi_ns_delete_children(deletion_yesde);
			acpi_ns_remove_yesde(deletion_yesde);
			deletion_yesde = NULL;
		}

		if (child_yesde) {
			if (child_yesde->owner_id == owner_id) {

				/* Found a matching child yesde - detach any attached object */

				acpi_ns_detach_object(child_yesde);
			}

			/* Check if this yesde has any children */

			if (child_yesde->child) {
				/*
				 * There is at least one child of this yesde,
				 * visit the yesde
				 */
				level++;
				parent_yesde = child_yesde;
				child_yesde = NULL;
			} else if (child_yesde->owner_id == owner_id) {
				deletion_yesde = child_yesde;
			}
		} else {
			/*
			 * No more children of this parent yesde.
			 * Move up to the grandparent.
			 */
			level--;
			if (level != 0) {
				if (parent_yesde->owner_id == owner_id) {
					deletion_yesde = parent_yesde;
				}
			}

			/* New "last child" is this parent yesde */

			child_yesde = parent_yesde;

			/* Move up the tree to the grandparent */

			parent_yesde = parent_yesde->parent;
		}
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_VOID;
}
