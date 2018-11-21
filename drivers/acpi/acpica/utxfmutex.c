// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: utxfmutex - external AML mutex access functions
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utxfmutex")

/* Local prototypes */
static acpi_status
acpi_ut_get_mutex_object(acpi_handle handle,
			 acpi_string pathname,
			 union acpi_operand_object **ret_obj);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_mutex_object
 *
 * PARAMETERS:  handle              - Mutex or prefix handle (optional)
 *              pathname            - Mutex pathname (optional)
 *              ret_obj             - Where the mutex object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an AML mutex object. The mutex node is pointed to by
 *              Handle:Pathname. Either Handle or Pathname can be NULL, but
 *              not both.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_get_mutex_object(acpi_handle handle,
			 acpi_string pathname,
			 union acpi_operand_object **ret_obj)
{
	struct acpi_namespace_node *mutex_node;
	union acpi_operand_object *mutex_obj;
	acpi_status status;

	/* Parameter validation */

	if (!ret_obj || (!handle && !pathname)) {
		return (AE_BAD_PARAMETER);
	}

	/* Get a the namespace node for the mutex */

	mutex_node = handle;
	if (pathname != NULL) {
		status =
		    acpi_get_handle(handle, pathname,
				    ACPI_CAST_PTR(acpi_handle, &mutex_node));
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	/* Ensure that we actually have a Mutex object */

	if (!mutex_node || (mutex_node->type != ACPI_TYPE_MUTEX)) {
		return (AE_TYPE);
	}

	/* Get the low-level mutex object */

	mutex_obj = acpi_ns_get_attached_object(mutex_node);
	if (!mutex_obj) {
		return (AE_NULL_OBJECT);
	}

	*ret_obj = mutex_obj;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_acquire_mutex
 *
 * PARAMETERS:  handle              - Mutex or prefix handle (optional)
 *              pathname            - Mutex pathname (optional)
 *              timeout             - Max time to wait for the lock (millisec)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire an AML mutex. This is a device driver interface to
 *              AML mutex objects, and allows for transaction locking between
 *              drivers and AML code. The mutex node is pointed to by
 *              Handle:Pathname. Either Handle or Pathname can be NULL, but
 *              not both.
 *
 ******************************************************************************/

acpi_status
acpi_acquire_mutex(acpi_handle handle, acpi_string pathname, u16 timeout)
{
	acpi_status status;
	union acpi_operand_object *mutex_obj;

	/* Get the low-level mutex associated with Handle:Pathname */

	status = acpi_ut_get_mutex_object(handle, pathname, &mutex_obj);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Acquire the OS mutex */

	status = acpi_os_acquire_mutex(mutex_obj->mutex.os_mutex, timeout);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_acquire_mutex)

/*******************************************************************************
 *
 * FUNCTION:    acpi_release_mutex
 *
 * PARAMETERS:  handle              - Mutex or prefix handle (optional)
 *              pathname            - Mutex pathname (optional)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release an AML mutex. This is a device driver interface to
 *              AML mutex objects, and allows for transaction locking between
 *              drivers and AML code. The mutex node is pointed to by
 *              Handle:Pathname. Either Handle or Pathname can be NULL, but
 *              not both.
 *
 ******************************************************************************/
acpi_status acpi_release_mutex(acpi_handle handle, acpi_string pathname)
{
	acpi_status status;
	union acpi_operand_object *mutex_obj;

	/* Get the low-level mutex associated with Handle:Pathname */

	status = acpi_ut_get_mutex_object(handle, pathname, &mutex_obj);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Release the OS mutex */

	acpi_os_release_mutex(mutex_obj->mutex.os_mutex);
	return (AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_release_mutex)
