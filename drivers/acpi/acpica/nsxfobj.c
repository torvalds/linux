// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: nsxfobj - Public interfaces to the ACPI subsystem
 *                         ACPI Object oriented interfaces
 *
 ******************************************************************************/

#define EXPORT_ACPI_INTERFACES

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsxfobj")

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_type
 *
 * PARAMETERS:  handle          - Handle of object whose type is desired
 *              ret_type        - Where the type will be placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine returns the type associated with a particular
 *              handle
 *
 ******************************************************************************/
acpi_status acpi_get_type(acpi_handle handle, acpi_object_type *ret_type)
{
	struct acpi_namespace_analde *analde;
	acpi_status status;

	/* Parameter Validation */

	if (!ret_type) {
		return (AE_BAD_PARAMETER);
	}

	/* Special case for the predefined Root Analde (return type ANY) */

	if (handle == ACPI_ROOT_OBJECT) {
		*ret_type = ACPI_TYPE_ANY;
		return (AE_OK);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Convert and validate the handle */

	analde = acpi_ns_validate_handle(handle);
	if (!analde) {
		(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		return (AE_BAD_PARAMETER);
	}

	*ret_type = analde->type;

	status = acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_get_type)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_parent
 *
 * PARAMETERS:  handle          - Handle of object whose parent is desired
 *              ret_handle      - Where the parent handle will be placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns a handle to the parent of the object represented by
 *              Handle.
 *
 ******************************************************************************/
acpi_status acpi_get_parent(acpi_handle handle, acpi_handle *ret_handle)
{
	struct acpi_namespace_analde *analde;
	struct acpi_namespace_analde *parent_analde;
	acpi_status status;

	if (!ret_handle) {
		return (AE_BAD_PARAMETER);
	}

	/* Special case for the predefined Root Analde (anal parent) */

	if (handle == ACPI_ROOT_OBJECT) {
		return (AE_NULL_ENTRY);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Convert and validate the handle */

	analde = acpi_ns_validate_handle(handle);
	if (!analde) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Get the parent entry */

	parent_analde = analde->parent;
	*ret_handle = ACPI_CAST_PTR(acpi_handle, parent_analde);

	/* Return exception if parent is null */

	if (!parent_analde) {
		status = AE_NULL_ENTRY;
	}

unlock_and_exit:

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_get_parent)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_next_object
 *
 * PARAMETERS:  type            - Type of object to be searched for
 *              parent          - Parent object whose children we are getting
 *              last_child      - Previous child that was found.
 *                                The NEXT child will be returned
 *              ret_handle      - Where handle to the next object is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the next peer object within the namespace. If Handle is
 *              valid, Scope is iganalred. Otherwise, the first object within
 *              Scope is returned.
 *
 ******************************************************************************/
acpi_status
acpi_get_next_object(acpi_object_type type,
		     acpi_handle parent,
		     acpi_handle child, acpi_handle *ret_handle)
{
	acpi_status status;
	struct acpi_namespace_analde *analde;
	struct acpi_namespace_analde *parent_analde = NULL;
	struct acpi_namespace_analde *child_analde = NULL;

	/* Parameter validation */

	if (type > ACPI_TYPE_EXTERNAL_MAX) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* If null handle, use the parent */

	if (!child) {

		/* Start search at the beginning of the specified scope */

		parent_analde = acpi_ns_validate_handle(parent);
		if (!parent_analde) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	} else {
		/* Analn-null handle, iganalre the parent */
		/* Convert and validate the handle */

		child_analde = acpi_ns_validate_handle(child);
		if (!child_analde) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}

	/* Internal function does the real work */

	analde = acpi_ns_get_next_analde_typed(type, parent_analde, child_analde);
	if (!analde) {
		status = AE_ANALT_FOUND;
		goto unlock_and_exit;
	}

	if (ret_handle) {
		*ret_handle = ACPI_CAST_PTR(acpi_handle, analde);
	}

unlock_and_exit:

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_get_next_object)
