/******************************************************************************
 *
 * Module Name: utinit - Common ACPI subsystem initialization
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

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acevents.h"
#include "actables.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utinit")

/* Local prototypes */
static void acpi_ut_terminate(void);

#if (!ACPI_REDUCED_HARDWARE)

static void acpi_ut_free_gpe_lists(void);

#else

#define acpi_ut_free_gpe_lists()
#endif				/* !ACPI_REDUCED_HARDWARE */

#if (!ACPI_REDUCED_HARDWARE)
/******************************************************************************
 *
 * FUNCTION:    acpi_ut_free_gpe_lists
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Free global GPE lists
 *
 ******************************************************************************/

static void acpi_ut_free_gpe_lists(void)
{
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_gpe_block_info *next_gpe_block;
	struct acpi_gpe_xrupt_info *gpe_xrupt_info;
	struct acpi_gpe_xrupt_info *next_gpe_xrupt_info;

	/* Free global GPE blocks and related info structures */

	gpe_xrupt_info = acpi_gbl_gpe_xrupt_list_head;
	while (gpe_xrupt_info) {
		gpe_block = gpe_xrupt_info->gpe_block_list_head;
		while (gpe_block) {
			next_gpe_block = gpe_block->next;
			ACPI_FREE(gpe_block->event_info);
			ACPI_FREE(gpe_block->register_info);
			ACPI_FREE(gpe_block);

			gpe_block = next_gpe_block;
		}
		next_gpe_xrupt_info = gpe_xrupt_info->next;
		ACPI_FREE(gpe_xrupt_info);
		gpe_xrupt_info = next_gpe_xrupt_info;
	}
}
#endif				/* !ACPI_REDUCED_HARDWARE */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_init_globals
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize ACPICA globals. All globals that require specific
 *              initialization should be initialized here. This allows for
 *              a warm restart.
 *
 ******************************************************************************/

acpi_status acpi_ut_init_globals(void)
{
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE(ut_init_globals);

	/* Create all memory caches */

	status = acpi_ut_create_caches();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Address Range lists */

	for (i = 0; i < ACPI_ADDRESS_RANGE_MAX; i++) {
		acpi_gbl_address_range_list[i] = NULL;
	}

	/* Mutex locked flags */

	for (i = 0; i < ACPI_NUM_MUTEX; i++) {
		acpi_gbl_mutex_info[i].mutex = NULL;
		acpi_gbl_mutex_info[i].thread_id = ACPI_MUTEX_NOT_ACQUIRED;
		acpi_gbl_mutex_info[i].use_count = 0;
	}

	for (i = 0; i < ACPI_NUM_OWNERID_MASKS; i++) {
		acpi_gbl_owner_id_mask[i] = 0;
	}

	/* Last owner_ID is never valid */

	acpi_gbl_owner_id_mask[ACPI_NUM_OWNERID_MASKS - 1] = 0x80000000;

	/* Event counters */

	acpi_method_count = 0;
	acpi_sci_count = 0;
	acpi_gpe_count = 0;

	for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
		acpi_fixed_event_count[i] = 0;
	}

#if (!ACPI_REDUCED_HARDWARE)

	/* GPE/SCI support */

	acpi_gbl_all_gpes_initialized = FALSE;
	acpi_gbl_gpe_xrupt_list_head = NULL;
	acpi_gbl_gpe_fadt_blocks[0] = NULL;
	acpi_gbl_gpe_fadt_blocks[1] = NULL;
	acpi_current_gpe_count = 0;

	acpi_gbl_global_event_handler = NULL;
	acpi_gbl_sci_handler_list = NULL;

#endif				/* !ACPI_REDUCED_HARDWARE */

	/* Global handlers */

	acpi_gbl_global_notify[0].handler = NULL;
	acpi_gbl_global_notify[1].handler = NULL;
	acpi_gbl_exception_handler = NULL;
	acpi_gbl_init_handler = NULL;
	acpi_gbl_table_handler = NULL;
	acpi_gbl_interface_handler = NULL;

	/* Global Lock support */

	acpi_gbl_global_lock_semaphore = NULL;
	acpi_gbl_global_lock_mutex = NULL;
	acpi_gbl_global_lock_acquired = FALSE;
	acpi_gbl_global_lock_handle = 0;
	acpi_gbl_global_lock_present = FALSE;

	/* Miscellaneous variables */

	acpi_gbl_DSDT = NULL;
	acpi_gbl_cm_single_step = FALSE;
	acpi_gbl_shutdown = FALSE;
	acpi_gbl_ns_lookup_count = 0;
	acpi_gbl_ps_find_count = 0;
	acpi_gbl_acpi_hardware_present = TRUE;
	acpi_gbl_last_owner_id_index = 0;
	acpi_gbl_next_owner_id_offset = 0;
	acpi_gbl_debugger_configuration = DEBUGGER_THREADING;
	acpi_gbl_osi_mutex = NULL;

	/* Hardware oriented */

	acpi_gbl_events_initialized = FALSE;
	acpi_gbl_system_awake_and_running = TRUE;

	/* Namespace */

	acpi_gbl_module_code_list = NULL;
	acpi_gbl_root_node = NULL;
	acpi_gbl_root_node_struct.name.integer = ACPI_ROOT_NAME;
	acpi_gbl_root_node_struct.descriptor_type = ACPI_DESC_TYPE_NAMED;
	acpi_gbl_root_node_struct.type = ACPI_TYPE_DEVICE;
	acpi_gbl_root_node_struct.parent = NULL;
	acpi_gbl_root_node_struct.child = NULL;
	acpi_gbl_root_node_struct.peer = NULL;
	acpi_gbl_root_node_struct.object = NULL;

#ifdef ACPI_DISASSEMBLER
	acpi_gbl_external_list = NULL;
	acpi_gbl_num_external_methods = 0;
	acpi_gbl_resolved_external_methods = 0;
#endif

#ifdef ACPI_DEBUG_OUTPUT
	acpi_gbl_lowest_stack_pointer = ACPI_CAST_PTR(acpi_size, ACPI_SIZE_MAX);
#endif

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	acpi_gbl_display_final_mem_stats = FALSE;
	acpi_gbl_disable_mem_tracking = FALSE;
#endif

	return_ACPI_STATUS(AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ut_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Free global memory
 *
 ******************************************************************************/

static void acpi_ut_terminate(void)
{
	ACPI_FUNCTION_TRACE(ut_terminate);

	acpi_ut_free_gpe_lists();
	acpi_ut_delete_address_lists();
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_subsystem_shutdown
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Shutdown the various components. Do not delete the mutex
 *              objects here, because the AML debugger may be still running.
 *
 ******************************************************************************/

void acpi_ut_subsystem_shutdown(void)
{
	ACPI_FUNCTION_TRACE(ut_subsystem_shutdown);

	/* Just exit if subsystem is already shutdown */

	if (acpi_gbl_shutdown) {
		ACPI_ERROR((AE_INFO, "ACPI Subsystem is already terminated"));
		return_VOID;
	}

	/* Subsystem appears active, go ahead and shut it down */

	acpi_gbl_shutdown = TRUE;
	acpi_gbl_startup_flags = 0;
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Shutting down ACPI Subsystem\n"));

#ifndef ACPI_ASL_COMPILER

	/* Close the acpi_event Handling */

	acpi_ev_terminate();

	/* Delete any dynamic _OSI interfaces */

	acpi_ut_interface_terminate();
#endif

	/* Close the Namespace */

	acpi_ns_terminate();

	/* Delete the ACPI tables */

	acpi_tb_terminate();

	/* Close the globals */

	acpi_ut_terminate();

	/* Purge the local caches */

	(void)acpi_ut_delete_caches();
	return_VOID;
}
