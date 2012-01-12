/******************************************************************************
 *
 * Module Name: utxface - External interfaces for "global" ACPI functions
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

#ifndef ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_subsystem
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes all global variables.  This is the first function
 *              called, so any early initialization belongs here.
 *
 ******************************************************************************/
acpi_status __init acpi_initialize_subsystem(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_initialize_subsystem);

	acpi_gbl_startup_flags = ACPI_SUBSYSTEM_INITIALIZE;
	ACPI_DEBUG_EXEC(acpi_ut_init_stack_ptr_trace());

	/* Initialize the OS-Dependent layer */

	status = acpi_os_initialize();
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "During OSL initialization"));
		return_ACPI_STATUS(status);
	}

	/* Initialize all globals used by the subsystem */

	status = acpi_ut_init_globals();
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"During initialization of globals"));
		return_ACPI_STATUS(status);
	}

	/* Create the default mutex objects */

	status = acpi_ut_mutex_initialize();
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"During Global Mutex creation"));
		return_ACPI_STATUS(status);
	}

	/*
	 * Initialize the namespace manager and
	 * the root of the namespace tree
	 */
	status = acpi_ns_root_initialize();
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"During Namespace initialization"));
		return_ACPI_STATUS(status);
	}

	/* Initialize the global OSI interfaces list with the static names */

	status = acpi_ut_initialize_interfaces();
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"During OSI interfaces initialization"));
		return_ACPI_STATUS(status);
	}

	/* If configured, initialize the AML debugger */

	ACPI_DEBUGGER_EXEC(status = acpi_db_initialize());
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_subsystem
 *
 * PARAMETERS:  Flags           - Init/enable Options
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Completes the subsystem initialization including hardware.
 *              Puts system into ACPI mode if it isn't already.
 *
 ******************************************************************************/
acpi_status acpi_enable_subsystem(u32 flags)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(acpi_enable_subsystem);

	/* Enable ACPI mode */

	if (!(flags & ACPI_NO_ACPI_ENABLE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Going into ACPI mode\n"));

		acpi_gbl_original_mode = acpi_hw_get_mode();

		status = acpi_enable();
		if (ACPI_FAILURE(status)) {
			ACPI_WARNING((AE_INFO, "AcpiEnable failed"));
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Obtain a permanent mapping for the FACS. This is required for the
	 * Global Lock and the Firmware Waking Vector
	 */
	status = acpi_tb_initialize_facs();
	if (ACPI_FAILURE(status)) {
		ACPI_WARNING((AE_INFO, "Could not map the FACS table"));
		return_ACPI_STATUS(status);
	}

	/*
	 * Install the default op_region handlers. These are installed unless
	 * other handlers have already been installed via the
	 * install_address_space_handler interface.
	 */
	if (!(flags & ACPI_NO_ADDRESS_SPACE_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Installing default address space handlers\n"));

		status = acpi_ev_install_region_handlers();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Initialize ACPI Event handling (Fixed and General Purpose)
	 *
	 * Note1: We must have the hardware and events initialized before we can
	 * execute any control methods safely. Any control method can require
	 * ACPI hardware support, so the hardware must be fully initialized before
	 * any method execution!
	 *
	 * Note2: Fixed events are initialized and enabled here. GPEs are
	 * initialized, but cannot be enabled until after the hardware is
	 * completely initialized (SCI and global_lock activated)
	 */
	if (!(flags & ACPI_NO_EVENT_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Initializing ACPI events\n"));

		status = acpi_ev_initialize_events();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Install the SCI handler and Global Lock handler. This completes the
	 * hardware initialization.
	 */
	if (!(flags & ACPI_NO_HANDLER_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Installing SCI/GL handlers\n"));

		status = acpi_ev_install_xrupt_handlers();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_enable_subsystem)

/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_objects
 *
 * PARAMETERS:  Flags           - Init/enable Options
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Completes namespace initialization by initializing device
 *              objects and executing AML code for Regions, buffers, etc.
 *
 ******************************************************************************/
acpi_status acpi_initialize_objects(u32 flags)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(acpi_initialize_objects);

	/*
	 * Run all _REG methods
	 *
	 * Note: Any objects accessed by the _REG methods will be automatically
	 * initialized, even if they contain executable AML (see the call to
	 * acpi_ns_initialize_objects below).
	 */
	if (!(flags & ACPI_NO_ADDRESS_SPACE_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Executing _REG OpRegion methods\n"));

		status = acpi_ev_initialize_op_regions();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Execute any module-level code that was detected during the table load
	 * phase. Although illegal since ACPI 2.0, there are many machines that
	 * contain this type of code. Each block of detected executable AML code
	 * outside of any control method is wrapped with a temporary control
	 * method object and placed on a global list. The methods on this list
	 * are executed below.
	 */
	acpi_ns_exec_module_code_list();

	/*
	 * Initialize the objects that remain uninitialized. This runs the
	 * executable AML that may be part of the declaration of these objects:
	 * operation_regions, buffer_fields, Buffers, and Packages.
	 */
	if (!(flags & ACPI_NO_OBJECT_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Completing Initialization of ACPI Objects\n"));

		status = acpi_ns_initialize_objects();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Initialize all device objects in the namespace. This runs the device
	 * _STA and _INI methods.
	 */
	if (!(flags & ACPI_NO_DEVICE_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Initializing ACPI Devices\n"));

		status = acpi_ns_initialize_devices();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Empty the caches (delete the cached objects) on the assumption that
	 * the table load filled them up more than they will be at runtime --
	 * thus wasting non-paged memory.
	 */
	status = acpi_purge_cached_objects();

	acpi_gbl_startup_flags |= ACPI_INITIALIZED_OK;
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_initialize_objects)

#endif
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
 * RETURN:      Status          - the status of the call
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
 * PARAMETERS:  Handler             - Callback procedure
 *              Function            - Not (currently) used, see below
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
 * PARAMETERS:  Handler             - The _OSI interface handler to install
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
 *              Address             - Start address
 *              Length              - Length
 *              Warn                - TRUE if warning on overlap desired
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
