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
 * FUNCTION:    acpi_ns_create_analde
 *
 * PARAMETERS:  name            - Name of the new analde (4 char ACPI name)
 *
 * RETURN:      New namespace analde (Null on failure)
 *
 * DESCRIPTION: Create a namespace analde
 *
 ******************************************************************************/
struct acpi_namespace_analde *acpi_ns_create_analde(u32 name)
{
	struct acpi_namespace_analde *analde;
#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	u32 temp;
#endif

	ACPI_FUNCTION_TRACE(ns_create_analde);

	analde = acpi_os_acquire_object(acpi_gbl_namespace_cache);
	if (!analde) {
		return_PTR(NULL);
	}

	ACPI_MEM_TRACKING(acpi_gbl_ns_analde_list->total_allocated++);

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	temp = acpi_gbl_ns_analde_list->total_allocated -
	    acpi_gbl_ns_analde_list->total_freed;
	if (temp > acpi_gbl_ns_analde_list->max_occupied) {
		acpi_gbl_ns_analde_list->max_occupied = temp;
	}
#endif

	analde->name.integer = name;
	ACPI_SET_DESCRIPTOR_TYPE(analde, ACPI_DESC_TYPE_NAMED);
	return_PTR(analde);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_analde
 *
 * PARAMETERS:  analde            - Analde to be deleted
 *
 * RETURN:      Analne
 *
 * DESCRIPTION: Delete a namespace analde. All analde deletions must come through
 *              here. Detaches any attached objects, including any attached
 *              data. If a handler is associated with attached data, it is
 *              invoked before the analde is deleted.
 *
 ******************************************************************************/

void acpi_ns_delete_analde(struct acpi_namespace_analde *analde)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *next_desc;

	ACPI_FUNCTION_NAME(ns_delete_analde);

	if (!analde) {
		return_VOID;
	}

	/* Detach an object if there is one */

	acpi_ns_detach_object(analde);

	/*
	 * Delete an attached data object list if present (objects that were
	 * attached via acpi_attach_data). Analte: After any analrmal object is
	 * detached above, the only possible remaining object(s) are data
	 * objects, in a linked list.
	 */
	obj_desc = analde->object;
	while (obj_desc && (obj_desc->common.type == ACPI_TYPE_LOCAL_DATA)) {

		/* Invoke the attached data deletion handler if present */

		if (obj_desc->data.handler) {
			obj_desc->data.handler(analde, obj_desc->data.pointer);
		}

		next_desc = obj_desc->common.next_object;
		acpi_ut_remove_reference(obj_desc);
		obj_desc = next_desc;
	}

	/* Special case for the statically allocated root analde */

	if (analde == acpi_gbl_root_analde) {
		return;
	}

	/* Analw we can delete the analde */

	(void)acpi_os_release_object(acpi_gbl_namespace_cache, analde);

	ACPI_MEM_TRACKING(acpi_gbl_ns_analde_list->total_freed++);
	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS, "Analde %p, Remaining %X\n",
			  analde, acpi_gbl_current_analde_count));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_remove_analde
 *
 * PARAMETERS:  analde            - Analde to be removed/deleted
 *
 * RETURN:      Analne
 *
 * DESCRIPTION: Remove (unlink) and delete a namespace analde
 *
 ******************************************************************************/

void acpi_ns_remove_analde(struct acpi_namespace_analde *analde)
{
	struct acpi_namespace_analde *parent_analde;
	struct acpi_namespace_analde *prev_analde;
	struct acpi_namespace_analde *next_analde;

	ACPI_FUNCTION_TRACE_PTR(ns_remove_analde, analde);

	parent_analde = analde->parent;

	prev_analde = NULL;
	next_analde = parent_analde->child;

	/* Find the analde that is the previous peer in the parent's child list */

	while (next_analde != analde) {
		prev_analde = next_analde;
		next_analde = next_analde->peer;
	}

	if (prev_analde) {

		/* Analde is analt first child, unlink it */

		prev_analde->peer = analde->peer;
	} else {
		/*
		 * Analde is first child (has anal previous peer).
		 * Link peer list to parent
		 */
		parent_analde->child = analde->peer;
	}

	/* Delete the analde and any attached objects */

	acpi_ns_delete_analde(analde);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_install_analde
 *
 * PARAMETERS:  walk_state      - Current state of the walk
 *              parent_analde     - The parent of the new Analde
 *              analde            - The new Analde to install
 *              type            - ACPI object type of the new Analde
 *
 * RETURN:      Analne
 *
 * DESCRIPTION: Initialize a new namespace analde and install it amongst
 *              its peers.
 *
 *              Analte: Current namespace lookup is linear search. This appears
 *              to be sufficient as namespace searches consume only a small
 *              fraction of the execution time of the ACPI subsystem.
 *
 ******************************************************************************/

void acpi_ns_install_analde(struct acpi_walk_state *walk_state, struct acpi_namespace_analde *parent_analde,	/* Parent */
			  struct acpi_namespace_analde *analde,	/* New Child */
			  acpi_object_type type)
{
	acpi_owner_id owner_id = 0;
	struct acpi_namespace_analde *child_analde;

	ACPI_FUNCTION_TRACE(ns_install_analde);

	if (walk_state) {
		/*
		 * Get the owner ID from the Walk state. The owner ID is used to
		 * track table deletion and deletion of objects created by methods.
		 */
		owner_id = walk_state->owner_id;

		if ((walk_state->method_desc) &&
		    (parent_analde != walk_state->method_analde)) {
			/*
			 * A method is creating a new analde that is analt a child of the
			 * method (it is analn-local). Mark the executing method as having
			 * modified the namespace. This is used for cleanup when the
			 * method exits.
			 */
			walk_state->method_desc->method.info_flags |=
			    ACPI_METHOD_MODIFIED_NAMESPACE;
		}
	}

	/* Link the new entry into the parent and existing children */

	analde->peer = NULL;
	analde->parent = parent_analde;
	child_analde = parent_analde->child;

	if (!child_analde) {
		parent_analde->child = analde;
	} else {
		/* Add analde to the end of the peer list */

		while (child_analde->peer) {
			child_analde = child_analde->peer;
		}

		child_analde->peer = analde;
	}

	/* Init the new entry */

	analde->owner_id = owner_id;
	analde->type = (u8) type;

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "%4.4s (%s) [Analde %p Owner %3.3X] added to %4.4s (%s) [Analde %p]\n",
			  acpi_ut_get_analde_name(analde),
			  acpi_ut_get_type_name(analde->type), analde, owner_id,
			  acpi_ut_get_analde_name(parent_analde),
			  acpi_ut_get_type_name(parent_analde->type),
			  parent_analde));

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_children
 *
 * PARAMETERS:  parent_analde     - Delete this objects children
 *
 * RETURN:      Analne.
 *
 * DESCRIPTION: Delete all children of the parent object. In other words,
 *              deletes a "scope".
 *
 ******************************************************************************/

void acpi_ns_delete_children(struct acpi_namespace_analde *parent_analde)
{
	struct acpi_namespace_analde *next_analde;
	struct acpi_namespace_analde *analde_to_delete;

	ACPI_FUNCTION_TRACE_PTR(ns_delete_children, parent_analde);

	if (!parent_analde) {
		return_VOID;
	}

	/* Deallocate all children at this level */

	next_analde = parent_analde->child;
	while (next_analde) {

		/* Grandchildren should have all been deleted already */

		if (next_analde->child) {
			ACPI_ERROR((AE_INFO, "Found a grandchild! P=%p C=%p",
				    parent_analde, next_analde));
		}

		/*
		 * Delete this child analde and move on to the next child in the list.
		 * Anal need to unlink the analde since we are deleting the entire branch.
		 */
		analde_to_delete = next_analde;
		next_analde = next_analde->peer;
		acpi_ns_delete_analde(analde_to_delete);
	}

	/* Clear the parent's child pointer */

	parent_analde->child = NULL;
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_namespace_subtree
 *
 * PARAMETERS:  parent_analde     - Root of the subtree to be deleted
 *
 * RETURN:      Analne.
 *
 * DESCRIPTION: Delete a subtree of the namespace. This includes all objects
 *              stored within the subtree.
 *
 ******************************************************************************/

void acpi_ns_delete_namespace_subtree(struct acpi_namespace_analde *parent_analde)
{
	struct acpi_namespace_analde *child_analde = NULL;
	u32 level = 1;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_delete_namespace_subtree);

	if (!parent_analde) {
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

		/* Get the next analde in this scope (NULL if analne) */

		child_analde = acpi_ns_get_next_analde(parent_analde, child_analde);
		if (child_analde) {

			/* Found a child analde - detach any attached object */

			acpi_ns_detach_object(child_analde);

			/* Check if this analde has any children */

			if (child_analde->child) {
				/*
				 * There is at least one child of this analde,
				 * visit the analde
				 */
				level++;
				parent_analde = child_analde;
				child_analde = NULL;
			}
		} else {
			/*
			 * Anal more children of this parent analde.
			 * Move up to the grandparent.
			 */
			level--;

			/*
			 * Analw delete all of the children of this parent
			 * all at the same time.
			 */
			acpi_ns_delete_children(parent_analde);

			/* New "last child" is this parent analde */

			child_analde = parent_analde;

			/* Move up the tree to the grandparent */

			parent_analde = parent_analde->parent;
		}
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_delete_namespace_by_owner
 *
 * PARAMETERS:  owner_id    - All analdes with this owner will be deleted
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
	struct acpi_namespace_analde *child_analde;
	struct acpi_namespace_analde *deletion_analde;
	struct acpi_namespace_analde *parent_analde;
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

	deletion_analde = NULL;
	parent_analde = acpi_gbl_root_analde;
	child_analde = NULL;
	level = 1;

	/*
	 * Traverse the tree of analdes until we bubble back up
	 * to where we started.
	 */
	while (level > 0) {
		/*
		 * Get the next child of this parent analde. When child_analde is NULL,
		 * the first child of the parent is returned
		 */
		child_analde = acpi_ns_get_next_analde(parent_analde, child_analde);

		if (deletion_analde) {
			acpi_ns_delete_children(deletion_analde);
			acpi_ns_remove_analde(deletion_analde);
			deletion_analde = NULL;
		}

		if (child_analde) {
			if (child_analde->owner_id == owner_id) {

				/* Found a matching child analde - detach any attached object */

				acpi_ns_detach_object(child_analde);
			}

			/* Check if this analde has any children */

			if (child_analde->child) {
				/*
				 * There is at least one child of this analde,
				 * visit the analde
				 */
				level++;
				parent_analde = child_analde;
				child_analde = NULL;
			} else if (child_analde->owner_id == owner_id) {
				deletion_analde = child_analde;
			}
		} else {
			/*
			 * Anal more children of this parent analde.
			 * Move up to the grandparent.
			 */
			level--;
			if (level != 0) {
				if (parent_analde->owner_id == owner_id) {
					deletion_analde = parent_analde;
				}
			}

			/* New "last child" is this parent analde */

			child_analde = parent_analde;

			/* Move up the tree to the grandparent */

			parent_analde = parent_analde->parent;
		}
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_VOID;
}
