/******************************************************************************
 *
 * Module Name: utxface - External interfaces, miscellaneous utility functions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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

#include <linux/export.h>
#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"
#include "acdebug.h"
#include "actables.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utxface")

/*******************************************************************************
 *
 * FUNCTION:    acpi_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Shutdown the ACPICA subsystem and release all resources.
 *
 ******************************************************************************/
acpi_status acpi_terminate(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_terminate);

	/* Just exit if subsystem is already shutdown */

	if (acpi_gbl_shutdown) {
		ACPI_ERROR((AE_INFO, "ACPI Subsystem is already terminated"));
		return_ACPI_STATUS(AE_OK);
	}

	/* Subsystem appears active, go ahead and shut it down */

	acpi_gbl_shutdown = TRUE;
	acpi_gbl_startup_flags = 0;
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Shutting down ACPI Subsystem\n"));

	/* Terminate the AML Debugger if present */

	ACPI_DEBUGGER_EXEC(acpi_gbl_db_terminate_threads = TRUE);

	/* Shutdown and free all resources */

	acpi_ut_subsystem_shutdown();

	/* Free the mutex objects */

	acpi_ut_mutex_terminate();

#ifdef ACPI_DEBUGGER

	/* Shut down the debugger */

	acpi_db_terminate();
#endif

	/* Now we can shutdown the OS-dependent layer */

	status = acpi_os_terminate();
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_terminate)

#ifndef ACPI_ASL_COMPILER
#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_subsystem_status
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status of the ACPI subsystem
 *
 * DESCRIPTION: Other drivers that use the ACPI subsystem should call this
 *              before making any other calls, to ensure the subsystem
 *              initialized successfully.
 *
 ******************************************************************************/
acpi_status acpi_subsystem_status(void)
{

	if (acpi_gbl_startup_flags & ACPI_INITIALIZED_OK) {
		return (AE_OK);
	} else {
		return (AE_ERROR);
	}
}

ACPI_EXPORT_SYMBOL(acpi_subsystem_status)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_system_info
 *
 * PARAMETERS:  out_buffer      - A buffer to receive the resources for the
 *                                device
 *
 * RETURN:      status          - the status of the call
 *
 * DESCRIPTION: This function is called to get information about the current
 *              state of the ACPI subsystem.  It will return system information
 *              in the out_buffer.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of out_buffer is undefined.
 *
 ******************************************************************************/
acpi_status acpi_get_system_info(struct acpi_buffer * out_buffer)
{
	struct acpi_system_info *info_ptr;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_system_info);

	/* Parameter validation */

	status = acpi_ut_validate_buffer(out_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Validate/Allocate/Clear caller buffer */

	status =
	    acpi_ut_initialize_buffer(out_buffer,
				      sizeof(struct acpi_system_info));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Populate the return buffer
	 */
	info_ptr = (struct acpi_system_info *)out_buffer->pointer;

	info_ptr->acpi_ca_version = ACPI_CA_VERSION;

	/* System flags (ACPI capabilities) */

	info_ptr->flags = ACPI_SYS_MODE_ACPI;

	/* Timer resolution - 24 or 32 bits  */

	if (acpi_gbl_FADT.flags & ACPI_FADT_32BIT_TIMER) {
		info_ptr->timer_resolution = 24;
	} else {
		info_ptr->timer_resolution = 32;
	}

	/* Clear the reserved fields */

	info_ptr->reserved1 = 0;
	info_ptr->reserved2 = 0;

	/* Current debug levels */

	info_ptr->debug_layer = acpi_dbg_layer;
	info_ptr->debug_level = acpi_dbg_level;

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_get_system_info)

/*****************************************************************************
 *
 * FUNCTION:    acpi_install_initialization_handler
 *
 * PARAMETERS:  handler             - Callback procedure
 *              function            - Not (currently) used, see below
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install an initialization handler
 *
 * TBD: When a second function is added, must save the Function also.
 *
 ****************************************************************************/
acpi_status
acpi_install_initialization_handler(acpi_init_handler handler, u32 function)
{

	if (!handler) {
		return (AE_BAD_PARAMETER);
	}

	if (acpi_gbl_init_handler) {
		return (AE_ALREADY_EXISTS);
	}

	acpi_gbl_init_handler = handler;
	return AE_OK;
}

ACPI_EXPORT_SYMBOL(acpi_install_initialization_handler)
#endif				/*  ACPI_FUTURE_USAGE  */

/*****************************************************************************
 *
 * FUNCTION:    acpi_purge_cached_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Empty all caches (delete the cached objects)
 *
 ****************************************************************************/
acpi_status acpi_purge_cached_objects(void)
{
	ACPI_FUNCTION_TRACE(acpi_purge_cached_objects);

	(void)acpi_os_purge_cache(acpi_gbl_state_cache);
	(void)acpi_os_purge_cache(acpi_gbl_operand_cache);
	(void)acpi_os_purge_cache(acpi_gbl_ps_node_cache);
	(void)acpi_os_purge_cache(acpi_gbl_ps_node_ext_cache);
	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_purge_cached_objects)

/*****************************************************************************
 *
 * FUNCTION:    acpi_install_interface
 *
 * PARAMETERS:  interface_name      - The interface to install
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install an _OSI interface to the global list
 *
 ****************************************************************************/
acpi_status acpi_install_interface(acpi_string interface_name)
{
	acpi_status status;
	struct acpi_interface_info *interface_info;

	/* Parameter validation */

	if (!interface_name || (ACPI_STRLEN(interface_name) == 0)) {
		return (AE_BAD_PARAMETER);
	}

	(void)acpi_os_acquire_mutex(acpi_gbl_osi_mutex, ACPI_WAIT_FOREVER);

	/* Check if the interface name is already in the global list */

	interface_info = acpi_ut_get_interface(interface_name);
	if (interface_info) {
		/*
		 * The interface already exists in the list. This is OK if the
		 * interface has been marked invalid -- just clear the bit.
		 */
		if (interface_info->flags & ACPI_OSI_INVALID) {
			interface_info->flags &= ~ACPI_OSI_INVALID;
			status = AE_OK;
		} else {
			status = AE_ALREADY_EXISTS;
		}
	} else {
		/* New interface name, install into the global list */

		status = acpi_ut_install_interface(interface_name);
	}

	acpi_os_release_mutex(acpi_gbl_osi_mutex);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_install_interface)

/*****************************************************************************
 *
 * FUNCTION:    acpi_remove_interface
 *
 * PARAMETERS:  interface_name      - The interface to remove
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove an _OSI interface from the global list
 *
 ****************************************************************************/
acpi_status acpi_remove_interface(acpi_string interface_name)
{
	acpi_status status;

	/* Parameter validation */

	if (!interface_name || (ACPI_STRLEN(interface_name) == 0)) {
		return (AE_BAD_PARAMETER);
	}

	(void)acpi_os_acquire_mutex(acpi_gbl_osi_mutex, ACPI_WAIT_FOREVER);

	status = acpi_ut_remove_interface(interface_name);

	acpi_os_release_mutex(acpi_gbl_osi_mutex);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_interface)

/*****************************************************************************
 *
 * FUNCTION:    acpi_install_interface_handler
 *
 * PARAMETERS:  handler             - The _OSI interface handler to install
 *                                    NULL means "remove existing handler"
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for the predefined _OSI ACPI method.
 *              invoked during execution of the internal implementation of
 *              _OSI. A NULL handler simply removes any existing handler.
 *
 ****************************************************************************/
acpi_status acpi_install_interface_handler(acpi_interface_handler handler)
{
	acpi_status status = AE_OK;

	(void)acpi_os_acquire_mutex(acpi_gbl_osi_mutex, ACPI_WAIT_FOREVER);

	if (handler && acpi_gbl_interface_handler) {
		status = AE_ALREADY_EXISTS;
	} else {
		acpi_gbl_interface_handler = handler;
	}

	acpi_os_release_mutex(acpi_gbl_osi_mutex);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_install_interface_handler)

/*****************************************************************************
 *
 * FUNCTION:    acpi_check_address_range
 *
 * PARAMETERS:  space_id            - Address space ID
 *              address             - Start address
 *              length              - Length
 *              warn                - TRUE if warning on overlap desired
 *
 * RETURN:      Count of the number of conflicts detected.
 *
 * DESCRIPTION: Check if the input address range overlaps any of the
 *              ASL operation region address ranges.
 *
 ****************************************************************************/
u32
acpi_check_address_range(acpi_adr_space_type space_id,
			 acpi_physical_address address,
			 acpi_size length, u8 warn)
{
	u32 overlaps;
	acpi_status status;

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (0);
	}

	overlaps = acpi_ut_check_address_range(space_id, address,
					       (u32)length, warn);

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return (overlaps);
}

ACPI_EXPORT_SYMBOL(acpi_check_address_range)
#endif				/* !ACPI_ASL_COMPILER */
/*******************************************************************************
 *
 * FUNCTION:    acpi_decode_pld_buffer
 *
 * PARAMETERS:  in_buffer           - Buffer returned by _PLD method
 *              length              - Length of the in_buffer
 *              return_buffer       - Where the decode buffer is returned
 *
 * RETURN:      Status and the decoded _PLD buffer. User must deallocate
 *              the buffer via ACPI_FREE.
 *
 * DESCRIPTION: Decode the bit-packed buffer returned by the _PLD method into
 *              a local struct that is much more useful to an ACPI driver.
 *
 ******************************************************************************/
acpi_status
acpi_decode_pld_buffer(u8 *in_buffer,
		       acpi_size length, struct acpi_pld_info ** return_buffer)
{
	struct acpi_pld_info *pld_info;
	u32 *buffer = ACPI_CAST_PTR(u32, in_buffer);
	u32 dword;

	/* Parameter validation */

	if (!in_buffer || !return_buffer || (length < 16)) {
		return (AE_BAD_PARAMETER);
	}

	pld_info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_pld_info));
	if (!pld_info) {
		return (AE_NO_MEMORY);
	}

	/* First 32-bit DWord */

	ACPI_MOVE_32_TO_32(&dword, &buffer[0]);
	pld_info->revision = ACPI_PLD_GET_REVISION(&dword);
	pld_info->ignore_color = ACPI_PLD_GET_IGNORE_COLOR(&dword);
	pld_info->color = ACPI_PLD_GET_COLOR(&dword);

	/* Second 32-bit DWord */

	ACPI_MOVE_32_TO_32(&dword, &buffer[1]);
	pld_info->width = ACPI_PLD_GET_WIDTH(&dword);
	pld_info->height = ACPI_PLD_GET_HEIGHT(&dword);

	/* Third 32-bit DWord */

	ACPI_MOVE_32_TO_32(&dword, &buffer[2]);
	pld_info->user_visible = ACPI_PLD_GET_USER_VISIBLE(&dword);
	pld_info->dock = ACPI_PLD_GET_DOCK(&dword);
	pld_info->lid = ACPI_PLD_GET_LID(&dword);
	pld_info->panel = ACPI_PLD_GET_PANEL(&dword);
	pld_info->vertical_position = ACPI_PLD_GET_VERTICAL(&dword);
	pld_info->horizontal_position = ACPI_PLD_GET_HORIZONTAL(&dword);
	pld_info->shape = ACPI_PLD_GET_SHAPE(&dword);
	pld_info->group_orientation = ACPI_PLD_GET_ORIENTATION(&dword);
	pld_info->group_token = ACPI_PLD_GET_TOKEN(&dword);
	pld_info->group_position = ACPI_PLD_GET_POSITION(&dword);
	pld_info->bay = ACPI_PLD_GET_BAY(&dword);

	/* Fourth 32-bit DWord */

	ACPI_MOVE_32_TO_32(&dword, &buffer[3]);
	pld_info->ejectable = ACPI_PLD_GET_EJECTABLE(&dword);
	pld_info->ospm_eject_required = ACPI_PLD_GET_OSPM_EJECT(&dword);
	pld_info->cabinet_number = ACPI_PLD_GET_CABINET(&dword);
	pld_info->card_cage_number = ACPI_PLD_GET_CARD_CAGE(&dword);
	pld_info->reference = ACPI_PLD_GET_REFERENCE(&dword);
	pld_info->rotation = ACPI_PLD_GET_ROTATION(&dword);
	pld_info->order = ACPI_PLD_GET_ORDER(&dword);

	if (length >= ACPI_PLD_BUFFER_SIZE) {

		/* Fifth 32-bit DWord (Revision 2 of _PLD) */

		ACPI_MOVE_32_TO_32(&dword, &buffer[4]);
		pld_info->vertical_offset = ACPI_PLD_GET_VERT_OFFSET(&dword);
		pld_info->horizontal_offset = ACPI_PLD_GET_HORIZ_OFFSET(&dword);
	}

	*return_buffer = pld_info;
	return (AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_decode_pld_buffer)
