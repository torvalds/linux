/******************************************************************************
 *
 * Module Name: evxfevnt - External Interfaces, ACPI event disable/enable
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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

#define EXPORT_ACPI_INTERFACES

#include <acpi/acpi.h>
#include "accommon.h"
#include "actables.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxfevnt")

#if (!ACPI_REDUCED_HARDWARE)	/* Entire module */
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

	if (acpi_gbl_fadt_index == ACPI_INVALID_TABLE_INDEX) {
		return_ACPI_STATUS(AE_NO_ACPI_TABLES);
	}

	/* If the Hardware Reduced flag is set, machine is always in acpi mode */

	if (acpi_gbl_reduced_hardware) {
		return_ACPI_STATUS(AE_OK);
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

	/* If the Hardware Reduced flag is set, machine is always in acpi mode */

	if (acpi_gbl_reduced_hardware) {
		return_ACPI_STATUS(AE_OK);
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

ACPI_EXPORT_SYMBOL(acpi_disable)

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_event
 *
 * PARAMETERS:  event           - The fixed eventto be enabled
 *              flags           - Reserved
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

	/* If Hardware Reduced flag is set, there are no fixed events */

	if (acpi_gbl_reduced_hardware) {
		return_ACPI_STATUS(AE_OK);
	}

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
 * FUNCTION:    acpi_disable_event
 *
 * PARAMETERS:  event           - The fixed event to be disabled
 *              flags           - Reserved
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

	/* If Hardware Reduced flag is set, there are no fixed events */

	if (acpi_gbl_reduced_hardware) {
		return_ACPI_STATUS(AE_OK);
	}

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
 * PARAMETERS:  event           - The fixed event to be cleared
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

	/* If Hardware Reduced flag is set, there are no fixed events */

	if (acpi_gbl_reduced_hardware) {
		return_ACPI_STATUS(AE_OK);
	}

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
 * FUNCTION:    acpi_get_event_status
 *
 * PARAMETERS:  event           - The fixed event
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
	acpi_status status;
	acpi_event_status local_event_status = 0;
	u32 in_byte;

	ACPI_FUNCTION_TRACE(acpi_get_event_status);

	if (!event_status) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Decode the Fixed Event */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Fixed event currently can be dispatched? */

	if (acpi_gbl_fixed_event_handlers[event].handler) {
		local_event_status |= ACPI_EVENT_FLAG_HAS_HANDLER;
	}

	/* Fixed event currently enabled? */

	status =
	    acpi_read_bit_register(acpi_gbl_fixed_event_info[event].
				   enable_register_id, &in_byte);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (in_byte) {
		local_event_status |=
		    (ACPI_EVENT_FLAG_ENABLED | ACPI_EVENT_FLAG_ENABLE_SET);
	}

	/* Fixed event currently active? */

	status =
	    acpi_read_bit_register(acpi_gbl_fixed_event_info[event].
				   status_register_id, &in_byte);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (in_byte) {
		local_event_status |= ACPI_EVENT_FLAG_STATUS_SET;
	}

	(*event_status) = local_event_status;
	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_get_event_status)
#endif				/* !ACPI_REDUCED_HARDWARE */
