/******************************************************************************
 *
 * Module Name: evxfevnt - External Interfaces, ACPI event disable/enable
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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
#include "acevents.h"
#include "acnamesp.h"
#include "actables.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxfevnt")

/* Local prototypes */
static acpi_status
acpi_ev_get_gpe_device(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
		       struct acpi_gpe_block_info *gpe_block, void *context);

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
	acpi_status status;
	int retry;

	ACPI_FUNCTION_TRACE(acpi_enable);

	/* ACPI tables must be present */

	if (!acpi_tb_tables_loaded()) {
		return_ACPI_STATUS(AE_NO_ACPI_TABLES);
	}

	/* Check current mode */

	if (acpi_hw_get_mode() == ACPI_SYS_MODE_ACPI) {
		ACPI_DEBUG_PRINT((ACPI_DB_INIT,
				  "System is already in ACPI mode\n"));
		return_ACPI_STATUS(AE_OK);
	}

	/* Transition to ACPI mode */

	status = acpi_hw_set_mode(ACPI_SYS_MODE_ACPI);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO,
			    "Could not transition to ACPI mode"));
		return_ACPI_STATUS(status);
	}

	/* Sanity check that transition succeeded */

	for (retry = 0; retry < 30000; ++retry) {
		if (acpi_hw_get_mode() == ACPI_SYS_MODE_ACPI) {
			if (retry != 0)
				ACPI_WARNING((AE_INFO,
				"Platform took > %d00 usec to enter ACPI mode", retry));
			return_ACPI_STATUS(AE_OK);
		}
		acpi_os_stall(100);	/* 100 usec */
	}

	ACPI_ERROR((AE_INFO, "Hardware did not enter ACPI mode"));
	return_ACPI_STATUS(AE_NO_HARDWARE_RESPONSE);
}

ACPI_EXPORT_SYMBOL(acpi_enable)

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

	ACPI_FUNCTION_TRACE(acpi_disable);

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

ACPI_EXPORT_SYMBOL(acpi_disable)

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

	ACPI_FUNCTION_TRACE(acpi_enable_event);

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Enable the requested fixed event (by writing a one to the enable
	 * register bit)
	 */
	status =
	    acpi_write_bit_register(acpi_gbl_fixed_event_info[event].
				    enable_register_id, ACPI_ENABLE_EVENT);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Make sure that the hardware responded */

	status =
	    acpi_read_bit_register(acpi_gbl_fixed_event_info[event].
				   enable_register_id, &value);
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

ACPI_EXPORT_SYMBOL(acpi_enable_event)

/*******************************************************************************
 *
 * FUNCTION:    acpi_gpe_wakeup
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *              Action          - Enable or Disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set or clear the GPE's wakeup enable mask bit.
 *
 ******************************************************************************/
acpi_status acpi_gpe_wakeup(acpi_handle gpe_device, u32 gpe_number, u8 action)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_register_info *gpe_register_info;
	acpi_cpu_flags flags;
	u32 register_bit;

	ACPI_FUNCTION_TRACE(acpi_gpe_wakeup);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info || !(gpe_event_info->flags & ACPI_GPE_CAN_WAKE)) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	gpe_register_info = gpe_event_info->register_info;
	if (!gpe_register_info) {
		status = AE_NOT_EXIST;
		goto unlock_and_exit;
	}

	register_bit =
	    acpi_hw_get_gpe_register_bit(gpe_event_info, gpe_register_info);

	/* Perform the action */

	switch (action) {
	case ACPI_GPE_ENABLE:
		ACPI_SET_BIT(gpe_register_info->enable_for_wake,
			     (u8)register_bit);
		break;

	case ACPI_GPE_DISABLE:
		ACPI_CLEAR_BIT(gpe_register_info->enable_for_wake,
			       (u8)register_bit);
		break;

	default:
		ACPI_ERROR((AE_INFO, "%u, Invalid action", action));
		status = AE_BAD_PARAMETER;
		break;
	}

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_gpe_wakeup)

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a reference to a GPE. On the first reference, the GPE is
 *              hardware-enabled.
 *
 ******************************************************************************/
acpi_status acpi_enable_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_enable_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {
		status = acpi_raw_enable_gpe(gpe_event_info);
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_enable_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_disable_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a reference to a GPE. When the last reference is
 *              removed, only then is the GPE disabled (for runtime GPEs), or
 *              the GPE mask bit disabled (for wake GPEs)
 *
 ******************************************************************************/
acpi_status acpi_disable_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_disable_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {
		status = acpi_raw_disable_gpe(gpe_event_info) ;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_disable_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_gpe_can_wake
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set the ACPI_GPE_CAN_WAKE flag for the given GPE.  If the GPE
 *              has a corresponding method and is currently enabled, disable it
 *              (GPEs with corresponding methods are enabled unconditionally
 *              during initialization, but GPEs that can wake up are expected
 *              to be initially disabled).
 *
 ******************************************************************************/
acpi_status acpi_gpe_can_wake(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_gpe_can_wake);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	if (gpe_event_info->flags & ACPI_GPE_CAN_WAKE) {
		goto unlock_and_exit;
	}

	gpe_event_info->flags |= ACPI_GPE_CAN_WAKE;
	if (gpe_event_info->flags & ACPI_GPE_DISPATCH_METHOD) {
		(void)acpi_raw_disable_gpe(gpe_event_info);
	}

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_gpe_can_wake)

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

	ACPI_FUNCTION_TRACE(acpi_disable_event);

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Disable the requested fixed event (by writing a zero to the enable
	 * register bit)
	 */
	status =
	    acpi_write_bit_register(acpi_gbl_fixed_event_info[event].
				    enable_register_id, ACPI_DISABLE_EVENT);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status =
	    acpi_read_bit_register(acpi_gbl_fixed_event_info[event].
				   enable_register_id, &value);
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

ACPI_EXPORT_SYMBOL(acpi_disable_event)

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

	ACPI_FUNCTION_TRACE(acpi_clear_event);

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Clear the requested fixed event (By writing a one to the status
	 * register bit)
	 */
	status =
	    acpi_write_bit_register(acpi_gbl_fixed_event_info[event].
				    status_register_id, ACPI_CLEAR_STATUS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_clear_event)

/*******************************************************************************
 *
 * FUNCTION:    acpi_clear_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear an ACPI event (general purpose)
 *
 ******************************************************************************/
acpi_status acpi_clear_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_clear_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_hw_clear_gpe(gpe_event_info);

      unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_clear_gpe)
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
	u32 value;

	ACPI_FUNCTION_TRACE(acpi_get_event_status);

	if (!event_status) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the status of the requested fixed event */

	status =
	    acpi_read_bit_register(acpi_gbl_fixed_event_info[event].
			      enable_register_id, &value);
	if (ACPI_FAILURE(status))
		return_ACPI_STATUS(status);

	*event_status = value;

	status =
	    acpi_read_bit_register(acpi_gbl_fixed_event_info[event].
			      status_register_id, &value);
	if (ACPI_FAILURE(status))
		return_ACPI_STATUS(status);

	if (value)
		*event_status |= ACPI_EVENT_FLAG_SET;

	if (acpi_gbl_fixed_event_handlers[event].handler)
		*event_status |= ACPI_EVENT_FLAG_HANDLE;

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_event_status)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_gpe_status
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
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
		    u32 gpe_number, acpi_event_status *event_status)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_get_gpe_status);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Obtain status on the requested GPE number */

	status = acpi_hw_get_gpe_status(gpe_event_info, event_status);

	if (gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK)
		*event_status |= ACPI_EVENT_FLAG_HANDLE;

      unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_gpe_status)
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

	ACPI_FUNCTION_TRACE(acpi_install_gpe_block);

	if ((!gpe_device) || (!gpe_block_address) || (!register_count)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_validate_handle(gpe_device);
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

	/* Install block in the device_object attached to the node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {

		/*
		 * No object, create a new one (Device nodes do not always have
		 * an attached object)
		 */
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

	/* Now install the GPE block in the device_object */

	obj_desc->device.gpe_block = gpe_block;

	/* Enable the runtime GPEs in the new block */

	status = acpi_ev_initialize_gpe_block(node, gpe_block);

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_gpe_block)

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

	ACPI_FUNCTION_TRACE(acpi_remove_gpe_block);

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_validate_handle(gpe_device);
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

ACPI_EXPORT_SYMBOL(acpi_remove_gpe_block)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_gpe_device
 *
 * PARAMETERS:  Index               - System GPE index (0-current_gpe_count)
 *              gpe_device          - Where the parent GPE Device is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain the GPE device associated with the input index. A NULL
 *              gpe device indicates that the gpe number is contained in one of
 *              the FADT-defined gpe blocks. Otherwise, the GPE block device.
 *
 ******************************************************************************/
acpi_status
acpi_get_gpe_device(u32 index, acpi_handle *gpe_device)
{
	struct acpi_gpe_device_info info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_gpe_device);

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (index >= acpi_current_gpe_count) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Setup and walk the GPE list */

	info.index = index;
	info.status = AE_NOT_EXIST;
	info.gpe_device = NULL;
	info.next_block_base_index = 0;

	status = acpi_ev_walk_gpe_list(acpi_ev_get_gpe_device, &info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*gpe_device = info.gpe_device;
	return_ACPI_STATUS(info.status);
}

ACPI_EXPORT_SYMBOL(acpi_get_gpe_device)

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_device
 *
 * PARAMETERS:  GPE_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Matches the input GPE index (0-current_gpe_count) with a GPE
 *              block device. NULL if the GPE is one of the FADT-defined GPEs.
 *
 ******************************************************************************/
static acpi_status
acpi_ev_get_gpe_device(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
		       struct acpi_gpe_block_info *gpe_block, void *context)
{
	struct acpi_gpe_device_info *info = context;

	/* Increment Index by the number of GPEs in this block */

	info->next_block_base_index += gpe_block->gpe_count;

	if (info->index < info->next_block_base_index) {
		/*
		 * The GPE index is within this block, get the node. Leave the node
		 * NULL for the FADT-defined GPEs
		 */
		if ((gpe_block->node)->type == ACPI_TYPE_DEVICE) {
			info->gpe_device = gpe_block->node;
		}

		info->status = AE_OK;
		return (AE_CTRL_END);
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_disable_all_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable and clear all GPEs in all GPE blocks
 *
 ******************************************************************************/

acpi_status acpi_disable_all_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_disable_all_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_disable_all_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_enable_all_runtime_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "runtime" GPEs, in all GPE blocks
 *
 ******************************************************************************/

acpi_status acpi_enable_all_runtime_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enable_all_runtime_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_enable_all_runtime_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}
