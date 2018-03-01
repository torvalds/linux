/******************************************************************************
 *
 * Module Name: utxfinit - External interfaces for ACPICA initialization
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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
#include "acevents.h"
#include "acnamesp.h"
#include "acdebug.h"
#include "actables.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utxfinit")

/* For acpi_exec only */
void ae_do_object_overrides(void);

/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_subsystem
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes all global variables. This is the first function
 *              called, so any early initialization belongs here.
 *
 ******************************************************************************/

acpi_status ACPI_INIT_FUNCTION acpi_initialize_subsystem(void)
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

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL_INIT(acpi_initialize_subsystem)

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_subsystem
 *
 * PARAMETERS:  flags               - Init/enable Options
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Completes the subsystem initialization including hardware.
 *              Puts system into ACPI mode if it isn't already.
 *
 ******************************************************************************/
acpi_status ACPI_INIT_FUNCTION acpi_enable_subsystem(u32 flags)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(acpi_enable_subsystem);

	/*
	 * The early initialization phase is complete. The namespace is loaded,
	 * and we can now support address spaces other than Memory, I/O, and
	 * PCI_Config.
	 */
	acpi_gbl_early_initialization = FALSE;

#if (!ACPI_REDUCED_HARDWARE)

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
	if (!(flags & ACPI_NO_FACS_INIT)) {
		status = acpi_tb_initialize_facs();
		if (ACPI_FAILURE(status)) {
			ACPI_WARNING((AE_INFO, "Could not map the FACS table"));
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
	 * completely initialized (SCI and global_lock activated) and the various
	 * initialization control methods are run (_REG, _STA, _INI) on the
	 * entire namespace.
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
#endif				/* !ACPI_REDUCED_HARDWARE */

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL_INIT(acpi_enable_subsystem)

/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_objects
 *
 * PARAMETERS:  flags               - Init/enable Options
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Completes namespace initialization by initializing device
 *              objects and executing AML code for Regions, buffers, etc.
 *
 ******************************************************************************/
acpi_status ACPI_INIT_FUNCTION acpi_initialize_objects(u32 flags)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(acpi_initialize_objects);

#ifdef ACPI_EXEC_APP
	/*
	 * This call implements the "initialization file" option for acpi_exec.
	 * This is the precise point that we want to perform the overrides.
	 */
	ae_do_object_overrides();
#endif

	/*
	 * Execute any module-level code that was detected during the table load
	 * phase. Although illegal since ACPI 2.0, there are many machines that
	 * contain this type of code. Each block of detected executable AML code
	 * outside of any control method is wrapped with a temporary control
	 * method object and placed on a global list. The methods on this list
	 * are executed below.
	 *
	 * This case executes the module-level code for all tables only after
	 * all of the tables have been loaded. It is a legacy option and is
	 * not compatible with other ACPI implementations. See acpi_ns_load_table.
	 */
	if (!acpi_gbl_parse_table_as_term_list
	    && acpi_gbl_group_module_level_code) {
		acpi_ns_exec_module_code_list();

		/*
		 * Initialize the objects that remain uninitialized. This
		 * runs the executable AML that may be part of the
		 * declaration of these objects:
		 * operation_regions, buffer_fields, Buffers, and Packages.
		 */
		if (!(flags & ACPI_NO_OBJECT_INIT)) {
			status = acpi_ns_initialize_objects();
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}
	}

	/*
	 * Initialize all device/region objects in the namespace. This runs
	 * the device _STA and _INI methods and region _REG methods.
	 */
	if (!(flags & (ACPI_NO_DEVICE_INIT | ACPI_NO_ADDRESS_SPACE_INIT))) {
		status = acpi_ns_initialize_devices(flags);
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

ACPI_EXPORT_SYMBOL_INIT(acpi_initialize_objects)
