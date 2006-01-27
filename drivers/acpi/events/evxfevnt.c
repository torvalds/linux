/******************************************************************************
 *
 * Module Name: evxfevnt - External Interfaces, ACPI event disable/enable
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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

#include <linux/module.h>

#include <acpi/acpi.h>
#include <acpi/acevents.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxfevnt")

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfers the system into ACPI mode.
 *
 ******************************************************************************/
acpi_status acpi_enable(void)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_enable");

	/* Make sure we have the FADT */

	if (!acpi_gbl_FADT) {
		ACPI_WARNING((AE_INFO, "No FADT information present!"));
		return_ACPI_STATUS(AE_NO_ACPI_TABLES);
	}

	if (acpi_hw_get_mode() == ACPI_SYS_MODE_ACPI) {
		ACPI_DEBUG_PRINT((ACPI_DB_INIT,
				  "System is already in ACPI mode\n"));
	} else {
		/* Transition to ACPI mode */

		status = acpi_hw_set_mode(ACPI_SYS_MODE_ACPI);
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Could not transition to ACPI mode"));
			return_ACPI_STATUS(status);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INIT,
				  "Transition to ACPI mode successful\n"));
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_disable
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfers the system into LEGACY (non-ACPI) mode.
 *
 ******************************************************************************/

acpi_status acpi_disable(void)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_disable");

	if (!acpi_gbl_FADT) {
		ACPI_WARNING((AE_INFO, "No FADT information present!"));
		return_ACPI_STATUS(AE_NO_ACPI_TABLES);
	}

	if (acpi_hw_get_mode() == ACPI_SYS_MODE_LEGACY) {
		ACPI_DEBUG_PRINT((ACPI_DB_INIT,
				  "System is already in legacy (non-ACPI) mode\n"));
	} else {
		/* Transition to LEGACY mode */

		status = acpi_hw_set_mode(ACPI_SYS_MODE_LEGACY);

		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Could not exit ACPI mode to legacy mode"));
			return_ACPI_STATUS(status);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INIT, "ACPI mode disabled\n"));
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_event
 *
 * PARAMETERS:  Event           - The fixed eventto be enabled
 *              Flags           - Reserved
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable an ACPI event (fixed)
 *
 ******************************************************************************/

acpi_status acpi_enable_event(u32 event, u32 flags)
{
	acpi_status status = AE_OK;
	u32 value;

	ACPI_FUNCTION_TRACE("acpi_enable_event");

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Enable the requested fixed event (by writing a one to the
	 * enable register bit)
	 */
	status =
	    acpi_set_register(acpi_gbl_fixed_event_info[event].
			      enable_register_id, 1, ACPI_MTX_LOCK);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Make sure that the hardware responded */

	status =
	    acpi_get_register(acpi_gbl_fixed_event_info[event].
			      enable_register_id, &value, ACPI_MTX_LOCK);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (value != 1) {
		ACPI_ERROR((AE_INFO,
			    "Could not enable %s event",
			    acpi_ut_get_event_name(event)));
		return_ACPI_STATUS(AE_NO_HARDWARE_RESPONSE);
	}

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_enable_event);

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_gpe_type
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device
 *              gpe_number      - GPE level within the GPE block
 *              Type            - New GPE type
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set the type of an individual GPE
 *
 ******************************************************************************/

acpi_status acpi_set_gpe_type(acpi_handle gpe_device, u32 gpe_number, u8 type)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;

	ACPI_FUNCTION_TRACE("acpi_set_gpe_type");

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	if ((gpe_event_info->flags & ACPI_GPE_TYPE_MASK) == type) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Set the new type (will disable GPE if currently enabled) */

	status = acpi_ev_set_gpe_type(gpe_event_info, type);

      unlock_and_exit:
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_set_gpe_type);

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device
 *              gpe_number      - GPE level within the GPE block
 *              Flags           - Just enable, or also wake enable?
 *                                Called from ISR or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable an ACPI event (general purpose)
 *
 ******************************************************************************/

acpi_status acpi_enable_gpe(acpi_handle gpe_device, u32 gpe_number, u32 flags)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;

	ACPI_FUNCTION_TRACE("acpi_enable_gpe");

	/* Use semaphore lock if not executing at interrupt level */

	if (flags & ACPI_NOT_ISR) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Perform the enable */

	status = acpi_ev_enable_gpe(gpe_event_info, TRUE);

      unlock_and_exit:
	if (flags & ACPI_NOT_ISR) {
		(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	}
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_enable_gpe);

/*******************************************************************************
 *
 * FUNCTION:    acpi_disable_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device
 *              gpe_number      - GPE level within the GPE block
 *              Flags           - Just disable, or also wake disable?
 *                                Called from ISR or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable an ACPI event (general purpose)
 *
 ******************************************************************************/

acpi_status acpi_disable_gpe(acpi_handle gpe_device, u32 gpe_number, u32 flags)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;

	ACPI_FUNCTION_TRACE("acpi_disable_gpe");

	/* Use semaphore lock if not executing at interrupt level */

	if (flags & ACPI_NOT_ISR) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ev_disable_gpe(gpe_event_info);

      unlock_and_exit:
	if (flags & ACPI_NOT_ISR) {
		(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	}
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_disable_event
 *
 * PARAMETERS:  Event           - The fixed eventto be enabled
 *              Flags           - Reserved
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable an ACPI event (fixed)
 *
 ******************************************************************************/

acpi_status acpi_disable_event(u32 event, u32 flags)
{
	acpi_status status = AE_OK;
	u32 value;

	ACPI_FUNCTION_TRACE("acpi_disable_event");

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Disable the requested fixed event (by writing a zero to the
	 * enable register bit)
	 */
	status =
	    acpi_set_register(acpi_gbl_fixed_event_info[event].
			      enable_register_id, 0, ACPI_MTX_LOCK);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status =
	    acpi_get_register(acpi_gbl_fixed_event_info[event].
			      enable_register_id, &value, ACPI_MTX_LOCK);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (value != 0) {
		ACPI_ERROR((AE_INFO,
			    "Could not disable %s events",
			    acpi_ut_get_event_name(event)));
		return_ACPI_STATUS(AE_NO_HARDWARE_RESPONSE);
	}

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_disable_event);

/*******************************************************************************
 *
 * FUNCTION:    acpi_clear_event
 *
 * PARAMETERS:  Event           - The fixed event to be cleared
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear an ACPI event (fixed)
 *
 ******************************************************************************/

acpi_status acpi_clear_event(u32 event)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_clear_event");

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Clear the requested fixed event (By writing a one to the
	 * status register bit)
	 */
	status =
	    acpi_set_register(acpi_gbl_fixed_event_info[event].
			      status_register_id, 1, ACPI_MTX_LOCK);

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_clear_event);

/*******************************************************************************
 *
 * FUNCTION:    acpi_clear_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device
 *              gpe_number      - GPE level within the GPE block
 *              Flags           - Called from an ISR or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear an ACPI event (general purpose)
 *
 ******************************************************************************/

acpi_status acpi_clear_gpe(acpi_handle gpe_device, u32 gpe_number, u32 flags)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;

	ACPI_FUNCTION_TRACE("acpi_clear_gpe");

	/* Use semaphore lock if not executing at interrupt level */

	if (flags & ACPI_NOT_ISR) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_hw_clear_gpe(gpe_event_info);

      unlock_and_exit:
	if (flags & ACPI_NOT_ISR) {
		(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	}
	return_ACPI_STATUS(status);
}

#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_get_event_status
 *
 * PARAMETERS:  Event           - The fixed event
 *              event_status    - Where the current status of the event will
 *                                be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtains and returns the current status of the event
 *
 ******************************************************************************/

acpi_status acpi_get_event_status(u32 event, acpi_event_status * event_status)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_get_event_status");

	if (!event_status) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the status of the requested fixed event */

	status =
	    acpi_get_register(acpi_gbl_fixed_event_info[event].
			      status_register_id, event_status, ACPI_MTX_LOCK);

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_gpe_status
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device
 *              gpe_number      - GPE level within the GPE block
 *              Flags           - Called from an ISR or not
 *              event_status    - Where the current status of the event will
 *                                be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get status of an event (general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_get_gpe_status(acpi_handle gpe_device,
		    u32 gpe_number, u32 flags, acpi_event_status * event_status)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;

	ACPI_FUNCTION_TRACE("acpi_get_gpe_status");

	/* Use semaphore lock if not executing at interrupt level */

	if (flags & ACPI_NOT_ISR) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Obtain status on the requested GPE number */

	status = acpi_hw_get_gpe_status(gpe_event_info, event_status);

      unlock_and_exit:
	if (flags & ACPI_NOT_ISR) {
		(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	}
	return_ACPI_STATUS(status);
}
#endif				/*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_gpe_block
 *
 * PARAMETERS:  gpe_device          - Handle to the parent GPE Block Device
 *              gpe_block_address   - Address and space_iD
 *              register_count      - Number of GPE register pairs in the block
 *              interrupt_number    - H/W interrupt for the block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create and Install a block of GPE registers
 *
 ******************************************************************************/

acpi_status
acpi_install_gpe_block(acpi_handle gpe_device,
		       struct acpi_generic_address *gpe_block_address,
		       u32 register_count, u32 interrupt_number)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	struct acpi_gpe_block_info *gpe_block;

	ACPI_FUNCTION_TRACE("acpi_install_gpe_block");

	if ((!gpe_device) || (!gpe_block_address) || (!register_count)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node(gpe_device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * For user-installed GPE Block Devices, the gpe_block_base_number
	 * is always zero
	 */
	status =
	    acpi_ev_create_gpe_block(node, gpe_block_address, register_count, 0,
				     interrupt_number, &gpe_block);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Run the _PRW methods and enable the GPEs */

	status = acpi_ev_initialize_gpe_block(node, gpe_block);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Get the device_object attached to the node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		/* No object, create a new one */

		obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_DEVICE);
		if (!obj_desc) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		status =
		    acpi_ns_attach_object(node, obj_desc, ACPI_TYPE_DEVICE);

		/* Remove local reference to the object */

		acpi_ut_remove_reference(obj_desc);

		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	/* Install the GPE block in the device_object */

	obj_desc->device.gpe_block = gpe_block;

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_install_gpe_block);

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_gpe_block
 *
 * PARAMETERS:  gpe_device          - Handle to the parent GPE Block Device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a previously installed block of GPE registers
 *
 ******************************************************************************/

acpi_status acpi_remove_gpe_block(acpi_handle gpe_device)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE("acpi_remove_gpe_block");

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node(gpe_device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Get the device_object attached to the node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc || !obj_desc->device.gpe_block) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Delete the GPE block (but not the device_object) */

	status = acpi_ev_delete_gpe_block(obj_desc->device.gpe_block);
	if (ACPI_SUCCESS(status)) {
		obj_desc->device.gpe_block = NULL;
	}

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_remove_gpe_block);
