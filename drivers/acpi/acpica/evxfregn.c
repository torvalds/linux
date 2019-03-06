// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evxfregn - External Interfaces, ACPI Operation Regions and
 *                         Address Spaces.
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#define EXPORT_ACPI_INTERFACES

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxfregn")

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_address_space_handler
 *
 * PARAMETERS:  device          - Handle for the device
 *              space_id        - The address space ID
 *              handler         - Address of the handler
 *              setup           - Address of the setup function
 *              context         - Value passed to the handler on each access
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for all op_regions of a given space_id.
 *
 * NOTE: This function should only be called after acpi_enable_subsystem has
 * been called. This is because any _REG methods associated with the Space ID
 * are executed here, and these methods can only be safely executed after
 * the default handlers have been installed and the hardware has been
 * initialized (via acpi_enable_subsystem.)
 *
 ******************************************************************************/
acpi_status
acpi_install_address_space_handler(acpi_handle device,
				   acpi_adr_space_type space_id,
				   acpi_adr_space_handler handler,
				   acpi_adr_space_setup setup, void *context)
{
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_address_space_handler);

	/* Parameter validation */

	if (!device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Convert and validate the device handle */

	node = acpi_ns_validate_handle(device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Install the handler for all Regions for this Space ID */

	status =
	    acpi_ev_install_space_handler(node, space_id, handler, setup,
					  context);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Run all _REG methods for this address space */

	acpi_ev_execute_reg_methods(node, space_id, ACPI_REG_CONNECT);

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_address_space_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_address_space_handler
 *
 * PARAMETERS:  device          - Handle for the device
 *              space_id        - The address space ID
 *              handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a previously installed handler.
 *
 ******************************************************************************/
acpi_status
acpi_remove_address_space_handler(acpi_handle device,
				  acpi_adr_space_type space_id,
				  acpi_adr_space_handler handler)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;
	union acpi_operand_object *region_obj;
	union acpi_operand_object **last_obj_ptr;
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_remove_address_space_handler);

	/* Parameter validation */

	if (!device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Convert and validate the device handle */

	node = acpi_ns_validate_handle(device);
	if (!node ||
	    ((node->type != ACPI_TYPE_DEVICE) &&
	     (node->type != ACPI_TYPE_PROCESSOR) &&
	     (node->type != ACPI_TYPE_THERMAL) &&
	     (node != acpi_gbl_root_node))) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Make sure the internal object exists */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		status = AE_NOT_EXIST;
		goto unlock_and_exit;
	}

	/* Find the address handler the user requested */

	handler_obj = obj_desc->common_notify.handler;
	last_obj_ptr = &obj_desc->common_notify.handler;
	while (handler_obj) {

		/* We have a handler, see if user requested this one */

		if (handler_obj->address_space.space_id == space_id) {

			/* Handler must be the same as the installed handler */

			if (handler_obj->address_space.handler != handler) {
				status = AE_BAD_PARAMETER;
				goto unlock_and_exit;
			}

			/* Matched space_id, first dereference this in the Regions */

			ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
					  "Removing address handler %p(%p) for region %s "
					  "on Device %p(%p)\n",
					  handler_obj, handler,
					  acpi_ut_get_region_name(space_id),
					  node, obj_desc));

			region_obj = handler_obj->address_space.region_list;

			/* Walk the handler's region list */

			while (region_obj) {
				/*
				 * First disassociate the handler from the region.
				 *
				 * NOTE: this doesn't mean that the region goes away
				 * The region is just inaccessible as indicated to
				 * the _REG method
				 */
				acpi_ev_detach_region(region_obj, TRUE);

				/*
				 * Walk the list: Just grab the head because the
				 * detach_region removed the previous head.
				 */
				region_obj =
				    handler_obj->address_space.region_list;
			}

			/* Remove this Handler object from the list */

			*last_obj_ptr = handler_obj->address_space.next;

			/* Now we can delete the handler object */

			acpi_ut_remove_reference(handler_obj);
			goto unlock_and_exit;
		}

		/* Walk the linked list of handlers */

		last_obj_ptr = &handler_obj->address_space.next;
		handler_obj = handler_obj->address_space.next;
	}

	/* The handler does not exist */

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Unable to remove address handler %p for %s(%X), DevNode %p, obj %p\n",
			  handler, acpi_ut_get_region_name(space_id), space_id,
			  node, obj_desc));

	status = AE_NOT_EXIST;

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_address_space_handler)
