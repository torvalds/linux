/******************************************************************************
 *
 * Module Name: tbinstal - ACPI table installation and removal
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

#include <acpi/acpi.h>
#include <acpi/actables.h>

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbinstal")

/* Local prototypes */
static acpi_status
acpi_tb_match_signature(char *signature,
			struct acpi_table_desc *table_info, u8 search_type);

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_match_signature
 *
 * PARAMETERS:  Signature           - Table signature to match
 *              table_info          - Return data
 *              search_type         - Table type to match (primary/secondary)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compare signature against the list of "ACPI-subsystem-owned"
 *              tables (DSDT/FADT/SSDT, etc.) Returns the table_type_iD on match.
 *
 ******************************************************************************/

static acpi_status
acpi_tb_match_signature(char *signature,
			struct acpi_table_desc *table_info, u8 search_type)
{
	acpi_native_uint i;

	ACPI_FUNCTION_TRACE("tb_match_signature");

	/* Search for a signature match among the known table types */

	for (i = 0; i < NUM_ACPI_TABLE_TYPES; i++) {
		if (!(acpi_gbl_table_data[i].flags & search_type)) {
			continue;
		}

		if (!ACPI_STRNCMP(signature, acpi_gbl_table_data[i].signature,
				  acpi_gbl_table_data[i].sig_length)) {

			/* Found a signature match, return index if requested */

			if (table_info) {
				table_info->type = (u8) i;
			}

			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Table [%4.4s] is an ACPI table consumed by the core subsystem\n",
					  (char *)acpi_gbl_table_data[i].
					  signature));

			return_ACPI_STATUS(AE_OK);
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Table [%4.4s] is not an ACPI table consumed by the core subsystem - ignored\n",
			  (char *)signature));

	return_ACPI_STATUS(AE_TABLE_NOT_SUPPORTED);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_table
 *
 * PARAMETERS:  table_info          - Return value from acpi_tb_get_table_body
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install the table into the global data structures.
 *
 ******************************************************************************/

acpi_status acpi_tb_install_table(struct acpi_table_desc *table_info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("tb_install_table");

	/* Lock tables while installing */

	status = acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not acquire table mutex"));
		return_ACPI_STATUS(status);
	}

	/*
	 * Ignore a table that is already installed. For example, some BIOS
	 * ASL code will repeatedly attempt to load the same SSDT.
	 */
	status = acpi_tb_is_table_installed(table_info);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Install the table into the global data structure */

	status = acpi_tb_init_table_descriptor(table_info->type, table_info);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not install table [%4.4s]",
				table_info->pointer->signature));
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "%s located at %p\n",
			  acpi_gbl_table_data[table_info->type].name,
			  table_info->pointer));

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_recognize_table
 *
 * PARAMETERS:  table_info          - Return value from acpi_tb_get_table_body
 *              search_type         - Table type to match (primary/secondary)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a table signature for a match against known table types
 *
 * NOTE:  All table pointers are validated as follows:
 *          1) Table pointer must point to valid physical memory
 *          2) Signature must be 4 ASCII chars, even if we don't recognize the
 *             name
 *          3) Table must be readable for length specified in the header
 *          4) Table checksum must be valid (with the exception of the FACS
 *             which has no checksum for some odd reason)
 *
 ******************************************************************************/

acpi_status
acpi_tb_recognize_table(struct acpi_table_desc *table_info, u8 search_type)
{
	struct acpi_table_header *table_header;
	acpi_status status;

	ACPI_FUNCTION_TRACE("tb_recognize_table");

	/* Ensure that we have a valid table pointer */

	table_header = (struct acpi_table_header *)table_info->pointer;
	if (!table_header) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * We only "recognize" a limited number of ACPI tables -- namely, the
	 * ones that are used by the subsystem (DSDT, FADT, etc.)
	 *
	 * An AE_TABLE_NOT_SUPPORTED means that the table was not recognized.
	 * This can be any one of many valid ACPI tables, it just isn't one of
	 * the tables that is consumed by the core subsystem
	 */
	status = acpi_tb_match_signature(table_header->signature,
					 table_info, search_type);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_tb_validate_table_header(table_header);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Return the table type and length via the info struct */

	table_info->length = (acpi_size) table_header->length;

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_init_table_descriptor
 *
 * PARAMETERS:  table_type          - The type of the table
 *              table_info          - A table info struct
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Install a table into the global data structs.
 *
 ******************************************************************************/

acpi_status
acpi_tb_init_table_descriptor(acpi_table_type table_type,
			      struct acpi_table_desc *table_info)
{
	struct acpi_table_list *list_head;
	struct acpi_table_desc *table_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE_U32("tb_init_table_descriptor", table_type);

	/* Allocate a descriptor for this table */

	table_desc = ACPI_MEM_CALLOCATE(sizeof(struct acpi_table_desc));
	if (!table_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Get a new owner ID for the table */

	status = acpi_ut_allocate_owner_id(&table_desc->owner_id);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Install the table into the global data structure */

	list_head = &acpi_gbl_table_lists[table_type];

	/*
	 * Two major types of tables:  1) Only one instance is allowed.  This
	 * includes most ACPI tables such as the DSDT.  2) Multiple instances of
	 * the table are allowed.  This includes SSDT and PSDTs.
	 */
	if (ACPI_IS_SINGLE_TABLE(acpi_gbl_table_data[table_type].flags)) {
		/*
		 * Only one table allowed, and a table has alread been installed
		 * at this location, so return an error.
		 */
		if (list_head->next) {
			ACPI_MEM_FREE(table_desc);
			return_ACPI_STATUS(AE_ALREADY_EXISTS);
		}

		table_desc->next = list_head->next;
		list_head->next = table_desc;

		if (table_desc->next) {
			table_desc->next->prev = table_desc;
		}

		list_head->count++;
	} else {
		/*
		 * Link the new table in to the list of tables of this type.
		 * Insert at the end of the list, order IS IMPORTANT.
		 *
		 * table_desc->Prev & Next are already NULL from calloc()
		 */
		list_head->count++;

		if (!list_head->next) {
			list_head->next = table_desc;
		} else {
			table_desc->next = list_head->next;

			while (table_desc->next->next) {
				table_desc->next = table_desc->next->next;
			}

			table_desc->next->next = table_desc;
			table_desc->prev = table_desc->next;
			table_desc->next = NULL;
		}
	}

	/* Finish initialization of the table descriptor */

	table_desc->type = (u8) table_type;
	table_desc->pointer = table_info->pointer;
	table_desc->length = table_info->length;
	table_desc->allocation = table_info->allocation;
	table_desc->aml_start = (u8 *) (table_desc->pointer + 1),
	    table_desc->aml_length = (u32) (table_desc->length -
					    (u32) sizeof(struct
							 acpi_table_header));
	table_desc->loaded_into_namespace = FALSE;

	/*
	 * Set the appropriate global pointer (if there is one) to point to the
	 * newly installed table
	 */
	if (acpi_gbl_table_data[table_type].global_ptr) {
		*(acpi_gbl_table_data[table_type].global_ptr) =
		    table_info->pointer;
	}

	/* Return Data */

	table_info->owner_id = table_desc->owner_id;
	table_info->installed_desc = table_desc;

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_delete_all_tables
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all internal ACPI tables
 *
 ******************************************************************************/

void acpi_tb_delete_all_tables(void)
{
	acpi_table_type type;

	/*
	 * Free memory allocated for ACPI tables
	 * Memory can either be mapped or allocated
	 */
	for (type = 0; type < NUM_ACPI_TABLE_TYPES; type++) {
		acpi_tb_delete_tables_by_type(type);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_delete_tables_by_type
 *
 * PARAMETERS:  Type                - The table type to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete an internal ACPI table
 *              Locks the ACPI table mutex
 *
 ******************************************************************************/

void acpi_tb_delete_tables_by_type(acpi_table_type type)
{
	struct acpi_table_desc *table_desc;
	u32 count;
	u32 i;

	ACPI_FUNCTION_TRACE_U32("tb_delete_tables_by_type", type);

	if (type > ACPI_TABLE_MAX) {
		return_VOID;
	}

	if (ACPI_FAILURE(acpi_ut_acquire_mutex(ACPI_MTX_TABLES))) {
		return;
	}

	/* Clear the appropriate "typed" global table pointer */

	switch (type) {
	case ACPI_TABLE_RSDP:
		acpi_gbl_RSDP = NULL;
		break;

	case ACPI_TABLE_DSDT:
		acpi_gbl_DSDT = NULL;
		break;

	case ACPI_TABLE_FADT:
		acpi_gbl_FADT = NULL;
		break;

	case ACPI_TABLE_FACS:
		acpi_gbl_FACS = NULL;
		break;

	case ACPI_TABLE_XSDT:
		acpi_gbl_XSDT = NULL;
		break;

	case ACPI_TABLE_SSDT:
	case ACPI_TABLE_PSDT:
	default:
		break;
	}

	/*
	 * Free the table
	 * 1) Get the head of the list
	 */
	table_desc = acpi_gbl_table_lists[type].next;
	count = acpi_gbl_table_lists[type].count;

	/*
	 * 2) Walk the entire list, deleting both the allocated tables
	 *    and the table descriptors
	 */
	for (i = 0; i < count; i++) {
		table_desc = acpi_tb_uninstall_table(table_desc);
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_delete_single_table
 *
 * PARAMETERS:  table_info          - A table info struct
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Low-level free for a single ACPI table.  Handles cases where
 *              the table was allocated a buffer or was mapped.
 *
 ******************************************************************************/

void acpi_tb_delete_single_table(struct acpi_table_desc *table_desc)
{

	/* Must have a valid table descriptor and pointer */

	if ((!table_desc) || (!table_desc->pointer)) {
		return;
	}

	/* Valid table, determine type of memory allocation */

	switch (table_desc->allocation) {
	case ACPI_MEM_NOT_ALLOCATED:
		break;

	case ACPI_MEM_ALLOCATED:

		ACPI_MEM_FREE(table_desc->pointer);
		break;

	case ACPI_MEM_MAPPED:

		acpi_os_unmap_memory(table_desc->pointer, table_desc->length);
		break;

	default:
		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_uninstall_table
 *
 * PARAMETERS:  table_info          - A table info struct
 *
 * RETURN:      Pointer to the next table in the list (of same type)
 *
 * DESCRIPTION: Free the memory associated with an internal ACPI table that
 *              is either installed or has never been installed.
 *              Table mutex should be locked.
 *
 ******************************************************************************/

struct acpi_table_desc *acpi_tb_uninstall_table(struct acpi_table_desc
						*table_desc)
{
	struct acpi_table_desc *next_desc;

	ACPI_FUNCTION_TRACE_PTR("tb_uninstall_table", table_desc);

	if (!table_desc) {
		return_PTR(NULL);
	}

	/* Unlink the descriptor from the doubly linked list */

	if (table_desc->prev) {
		table_desc->prev->next = table_desc->next;
	} else {
		/* Is first on list, update list head */

		acpi_gbl_table_lists[table_desc->type].next = table_desc->next;
	}

	if (table_desc->next) {
		table_desc->next->prev = table_desc->prev;
	}

	/* Free the memory allocated for the table itself */

	acpi_tb_delete_single_table(table_desc);

	/* Free the table descriptor */

	next_desc = table_desc->next;
	ACPI_MEM_FREE(table_desc);

	/* Return pointer to the next descriptor */

	return_PTR(next_desc);
}
