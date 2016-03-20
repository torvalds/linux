/******************************************************************************
 *
 * Module Name: tbxfload - Table load/unload external interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
#include "acnamesp.h"
#include "actables.h"
#include "acevents.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbxfload")

/*******************************************************************************
 *
 * FUNCTION:    acpi_load_tables
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the ACPI tables from the RSDT/XSDT
 *
 ******************************************************************************/
acpi_status __init acpi_load_tables(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_load_tables);

	/*
	 * Install the default operation region handlers. These are the
	 * handlers that are defined by the ACPI specification to be
	 * "always accessible" -- namely, system_memory, system_IO, and
	 * PCI_Config. This also means that no _REG methods need to be
	 * run for these address spaces. We need to have these handlers
	 * installed before any AML code can be executed, especially any
	 * module-level code (11/2015).
	 * Note that we allow OSPMs to install their own region handlers
	 * between acpi_initialize_subsystem() and acpi_load_tables() to use
	 * their customized default region handlers.
	 */
	status = acpi_ev_install_region_handlers();
	if (ACPI_FAILURE(status) && status != AE_ALREADY_EXISTS) {
		ACPI_EXCEPTION((AE_INFO, status,
				"During Region initialization"));
		return_ACPI_STATUS(status);
	}

	/* Load the namespace from the tables */

	status = acpi_tb_load_namespace();

	/* Don't let single failures abort the load */

	if (status == AE_CTRL_TERMINATE) {
		status = AE_OK;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While loading namespace from ACPI tables"));
	}

	if (!acpi_gbl_group_module_level_code) {
		/*
		 * Initialize the objects that remain uninitialized. This
		 * runs the executable AML that may be part of the
		 * declaration of these objects:
		 * operation_regions, buffer_fields, Buffers, and Packages.
		 */
		status = acpi_ns_initialize_objects();
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	acpi_gbl_namespace_initialized = TRUE;
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL_INIT(acpi_load_tables)

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_load_namespace
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the namespace from the DSDT and all SSDTs/PSDTs found in
 *              the RSDT/XSDT.
 *
 ******************************************************************************/
acpi_status acpi_tb_load_namespace(void)
{
	acpi_status status;
	u32 i;
	struct acpi_table_header *new_dsdt;
	struct acpi_table_desc *table;
	u32 tables_loaded = 0;
	u32 tables_failed = 0;

	ACPI_FUNCTION_TRACE(tb_load_namespace);

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

	/*
	 * Load the namespace. The DSDT is required, but any SSDT and
	 * PSDT tables are optional. Verify the DSDT.
	 */
	table = &acpi_gbl_root_table_list.tables[acpi_gbl_dsdt_index];

	if (!acpi_gbl_root_table_list.current_table_count ||
	    !ACPI_COMPARE_NAME(table->signature.ascii, ACPI_SIG_DSDT) ||
	    ACPI_FAILURE(acpi_tb_validate_table(table))) {
		status = AE_NO_ACPI_TABLES;
		goto unlock_and_exit;
	}

	/*
	 * Save the DSDT pointer for simple access. This is the mapped memory
	 * address. We must take care here because the address of the .Tables
	 * array can change dynamically as tables are loaded at run-time. Note:
	 * .Pointer field is not validated until after call to acpi_tb_validate_table.
	 */
	acpi_gbl_DSDT = table->pointer;

	/*
	 * Optionally copy the entire DSDT to local memory (instead of simply
	 * mapping it.) There are some BIOSs that corrupt or replace the original
	 * DSDT, creating the need for this option. Default is FALSE, do not copy
	 * the DSDT.
	 */
	if (acpi_gbl_copy_dsdt_locally) {
		new_dsdt = acpi_tb_copy_dsdt(acpi_gbl_dsdt_index);
		if (new_dsdt) {
			acpi_gbl_DSDT = new_dsdt;
		}
	}

	/*
	 * Save the original DSDT header for detection of table corruption
	 * and/or replacement of the DSDT from outside the OS.
	 */
	memcpy(&acpi_gbl_original_dsdt_header, acpi_gbl_DSDT,
	       sizeof(struct acpi_table_header));

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);

	/* Load and parse tables */

	status = acpi_ns_load_table(acpi_gbl_dsdt_index, acpi_gbl_root_node);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "[DSDT] table load failed"));
		tables_failed++;
	} else {
		tables_loaded++;
	}

	/* Load any SSDT or PSDT tables. Note: Loop leaves tables locked */

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	for (i = 0; i < acpi_gbl_root_table_list.current_table_count; ++i) {
		table = &acpi_gbl_root_table_list.tables[i];

		if (!acpi_gbl_root_table_list.tables[i].address ||
		    (!ACPI_COMPARE_NAME(table->signature.ascii, ACPI_SIG_SSDT)
		     && !ACPI_COMPARE_NAME(table->signature.ascii,
					   ACPI_SIG_PSDT)
		     && !ACPI_COMPARE_NAME(table->signature.ascii,
					   ACPI_SIG_OSDT))
		    || ACPI_FAILURE(acpi_tb_validate_table(table))) {
			continue;
		}

		/* Ignore errors while loading tables, get as many as possible */

		(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
		status = acpi_ns_load_table(i, acpi_gbl_root_node);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"(%4.4s:%8.8s) while loading table",
					table->signature.ascii,
					table->pointer->oem_table_id));

			tables_failed++;

			ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
					      "Table [%4.4s:%8.8s] (id FF) - Table namespace load failed\n\n",
					      table->signature.ascii,
					      table->pointer->oem_table_id));
		} else {
			tables_loaded++;
		}

		(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	}

	if (!tables_failed) {
		ACPI_INFO(("%u ACPI AML tables successfully acquired and loaded\n", tables_loaded));
	} else {
		ACPI_ERROR((AE_INFO,
			    "%u table load failures, %u successful",
			    tables_failed, tables_loaded));

		/* Indicate at least one failure */

		status = AE_CTRL_TERMINATE;
	}

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_table
 *
 * PARAMETERS:  address             - Address of the ACPI table to be installed.
 *              physical            - Whether the address is a physical table
 *                                    address or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dynamically install an ACPI table.
 *              Note: This function should only be invoked after
 *                    acpi_initialize_tables() and before acpi_load_tables().
 *
 ******************************************************************************/

acpi_status __init
acpi_install_table(acpi_physical_address address, u8 physical)
{
	acpi_status status;
	u8 flags;
	u32 table_index;

	ACPI_FUNCTION_TRACE(acpi_install_table);

	if (physical) {
		flags = ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL;
	} else {
		flags = ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL;
	}

	status = acpi_tb_install_standard_table(address, flags,
						FALSE, FALSE, &table_index);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL_INIT(acpi_install_table)

/*******************************************************************************
 *
 * FUNCTION:    acpi_load_table
 *
 * PARAMETERS:  table               - Pointer to a buffer containing the ACPI
 *                                    table to be loaded.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dynamically load an ACPI table from the caller's buffer. Must
 *              be a valid ACPI table with a valid ACPI table header.
 *              Note1: Mainly intended to support hotplug addition of SSDTs.
 *              Note2: Does not copy the incoming table. User is responsible
 *              to ensure that the table is not deleted or unmapped.
 *
 ******************************************************************************/
acpi_status acpi_load_table(struct acpi_table_header *table)
{
	acpi_status status;
	u32 table_index;

	ACPI_FUNCTION_TRACE(acpi_load_table);

	/* Parameter validation */

	if (!table) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Must acquire the interpreter lock during this operation */

	status = acpi_ut_acquire_mutex(ACPI_MTX_INTERPRETER);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Install the table and load it into the namespace */

	ACPI_INFO(("Host-directed Dynamic ACPI Table Load:"));
	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

	status = acpi_tb_install_standard_table(ACPI_PTR_TO_PHYSADDR(table),
						ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL,
						TRUE, FALSE, &table_index);

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/*
	 * Note: Now table is "INSTALLED", it must be validated before
	 * using.
	 */
	status =
	    acpi_tb_validate_table(&acpi_gbl_root_table_list.
				   tables[table_index]);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	status = acpi_ns_load_table(table_index, acpi_gbl_root_node);

	/* Invoke table handler if present */

	if (acpi_gbl_table_handler) {
		(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_LOAD, table,
					     acpi_gbl_table_handler_context);
	}

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_INTERPRETER);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_load_table)

/*******************************************************************************
 *
 * FUNCTION:    acpi_unload_parent_table
 *
 * PARAMETERS:  object              - Handle to any namespace object owned by
 *                                    the table to be unloaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Via any namespace object within an SSDT or OEMx table, unloads
 *              the table and deletes all namespace objects associated with
 *              that table. Unloading of the DSDT is not allowed.
 *              Note: Mainly intended to support hotplug removal of SSDTs.
 *
 ******************************************************************************/
acpi_status acpi_unload_parent_table(acpi_handle object)
{
	struct acpi_namespace_node *node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, object);
	acpi_status status = AE_NOT_EXIST;
	acpi_owner_id owner_id;
	u32 i;

	ACPI_FUNCTION_TRACE(acpi_unload_parent_table);

	/* Parameter validation */

	if (!object) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * The node owner_id is currently the same as the parent table ID.
	 * However, this could change in the future.
	 */
	owner_id = node->owner_id;
	if (!owner_id) {

		/* owner_id==0 means DSDT is the owner. DSDT cannot be unloaded */

		return_ACPI_STATUS(AE_TYPE);
	}

	/* Must acquire the interpreter lock during this operation */

	status = acpi_ut_acquire_mutex(ACPI_MTX_INTERPRETER);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Find the table in the global table list */

	for (i = 0; i < acpi_gbl_root_table_list.current_table_count; i++) {
		if (owner_id != acpi_gbl_root_table_list.tables[i].owner_id) {
			continue;
		}

		/*
		 * Allow unload of SSDT and OEMx tables only. Do not allow unload
		 * of the DSDT. No other types of tables should get here, since
		 * only these types can contain AML and thus are the only types
		 * that can create namespace objects.
		 */
		if (ACPI_COMPARE_NAME
		    (acpi_gbl_root_table_list.tables[i].signature.ascii,
		     ACPI_SIG_DSDT)) {
			status = AE_TYPE;
			break;
		}

		/* Ensure the table is actually loaded */

		if (!acpi_tb_is_table_loaded(i)) {
			status = AE_NOT_EXIST;
			break;
		}

		/* Invoke table handler if present */

		if (acpi_gbl_table_handler) {
			(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_UNLOAD,
						     acpi_gbl_root_table_list.
						     tables[i].pointer,
						     acpi_gbl_table_handler_context);
		}

		/*
		 * Delete all namespace objects owned by this table. Note that
		 * these objects can appear anywhere in the namespace by virtue
		 * of the AML "Scope" operator. Thus, we need to track ownership
		 * by an ID, not simply a position within the hierarchy.
		 */
		status = acpi_tb_delete_namespace_by_owner(i);
		if (ACPI_FAILURE(status)) {
			break;
		}

		status = acpi_tb_release_owner_id(i);
		acpi_tb_set_table_loaded_flag(i, FALSE);
		break;
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_INTERPRETER);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_unload_parent_table)
