/******************************************************************************
 *
 * Module Name: tbdata - Table manager data structure functions
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

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "actables.h"
#include "acevents.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbdata")

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_init_table_descriptor
 *
 * PARAMETERS:  table_desc              - Table descriptor
 *              address                 - Physical address of the table
 *              flags                   - Allocation flags of the table
 *              table                   - Pointer to the table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a new table descriptor
 *
 ******************************************************************************/
void
acpi_tb_init_table_descriptor(struct acpi_table_desc *table_desc,
			      acpi_physical_address address,
			      u8 flags, struct acpi_table_header *table)
{

	/*
	 * Initialize the table descriptor. Set the pointer to NULL, since the
	 * table is not fully mapped at this time.
	 */
	memset(table_desc, 0, sizeof(struct acpi_table_desc));
	table_desc->address = address;
	table_desc->length = table->length;
	table_desc->flags = flags;
	ACPI_MOVE_32_TO_32(table_desc->signature.ascii, table->signature);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_acquire_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *              table_ptr           - Where table is returned
 *              table_length        - Where table length is returned
 *              table_flags         - Where table allocation flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire an ACPI table. It can be used for tables not
 *              maintained in the acpi_gbl_root_table_list.
 *
 ******************************************************************************/

acpi_status
acpi_tb_acquire_table(struct acpi_table_desc *table_desc,
		      struct acpi_table_header **table_ptr,
		      u32 *table_length, u8 *table_flags)
{
	struct acpi_table_header *table = NULL;

	switch (table_desc->flags & ACPI_TABLE_ORIGIN_MASK) {
	case ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL:

		table =
		    acpi_os_map_memory(table_desc->address, table_desc->length);
		break;

	case ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL:
	case ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL:

		table = ACPI_CAST_PTR(struct acpi_table_header,
				      ACPI_PHYSADDR_TO_PTR(table_desc->
							   address));
		break;

	default:

		break;
	}

	/* Table is not valid yet */

	if (!table) {
		return (AE_NO_MEMORY);
	}

	/* Fill the return values */

	*table_ptr = table;
	*table_length = table_desc->length;
	*table_flags = table_desc->flags;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_release_table
 *
 * PARAMETERS:  table               - Pointer for the table
 *              table_length        - Length for the table
 *              table_flags         - Allocation flags for the table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Release a table. The inverse of acpi_tb_acquire_table().
 *
 ******************************************************************************/

void
acpi_tb_release_table(struct acpi_table_header *table,
		      u32 table_length, u8 table_flags)
{

	switch (table_flags & ACPI_TABLE_ORIGIN_MASK) {
	case ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL:

		acpi_os_unmap_memory(table, table_length);
		break;

	case ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL:
	case ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL:
	default:

		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_acquire_temp_table
 *
 * PARAMETERS:  table_desc          - Table descriptor to be acquired
 *              address             - Address of the table
 *              flags               - Allocation flags of the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function validates the table header to obtain the length
 *              of a table and fills the table descriptor to make its state as
 *              "INSTALLED". Such a table descriptor is only used for verified
 *              installation.
 *
 ******************************************************************************/

acpi_status
acpi_tb_acquire_temp_table(struct acpi_table_desc *table_desc,
			   acpi_physical_address address, u8 flags)
{
	struct acpi_table_header *table_header;

	switch (flags & ACPI_TABLE_ORIGIN_MASK) {
	case ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL:

		/* Get the length of the full table from the header */

		table_header =
		    acpi_os_map_memory(address,
				       sizeof(struct acpi_table_header));
		if (!table_header) {
			return (AE_NO_MEMORY);
		}

		acpi_tb_init_table_descriptor(table_desc, address, flags,
					      table_header);
		acpi_os_unmap_memory(table_header,
				     sizeof(struct acpi_table_header));
		return (AE_OK);

	case ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL:
	case ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL:

		table_header = ACPI_CAST_PTR(struct acpi_table_header,
					     ACPI_PHYSADDR_TO_PTR(address));
		if (!table_header) {
			return (AE_NO_MEMORY);
		}

		acpi_tb_init_table_descriptor(table_desc, address, flags,
					      table_header);
		return (AE_OK);

	default:

		break;
	}

	/* Table is not valid yet */

	return (AE_NO_MEMORY);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_release_temp_table
 *
 * PARAMETERS:  table_desc          - Table descriptor to be released
 *
 * RETURN:      Status
 *
 * DESCRIPTION: The inverse of acpi_tb_acquire_temp_table().
 *
 *****************************************************************************/

void acpi_tb_release_temp_table(struct acpi_table_desc *table_desc)
{

	/*
	 * Note that the .Address is maintained by the callers of
	 * acpi_tb_acquire_temp_table(), thus do not invoke acpi_tb_uninstall_table()
	 * where .Address will be freed.
	 */
	acpi_tb_invalidate_table(table_desc);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_tb_validate_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to validate the table, the returned
 *              table descriptor is in "VALIDATED" state.
 *
 *****************************************************************************/

acpi_status acpi_tb_validate_table(struct acpi_table_desc *table_desc)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(tb_validate_table);

	/* Validate the table if necessary */

	if (!table_desc->pointer) {
		status = acpi_tb_acquire_table(table_desc, &table_desc->pointer,
					       &table_desc->length,
					       &table_desc->flags);
		if (!table_desc->pointer) {
			status = AE_NO_MEMORY;
		}
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_invalidate_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Invalidate one internal ACPI table, this is the inverse of
 *              acpi_tb_validate_table().
 *
 ******************************************************************************/

void acpi_tb_invalidate_table(struct acpi_table_desc *table_desc)
{

	ACPI_FUNCTION_TRACE(tb_invalidate_table);

	/* Table must be validated */

	if (!table_desc->pointer) {
		return_VOID;
	}

	acpi_tb_release_table(table_desc->pointer, table_desc->length,
			      table_desc->flags);
	table_desc->pointer = NULL;

	return_VOID;
}

/******************************************************************************
 *
 * FUNCTION:    acpi_tb_validate_temp_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to validate the table, the returned
 *              table descriptor is in "VALIDATED" state.
 *
 *****************************************************************************/

acpi_status acpi_tb_validate_temp_table(struct acpi_table_desc *table_desc)
{

	if (!table_desc->pointer && !acpi_gbl_verify_table_checksum) {
		/*
		 * Only validates the header of the table.
		 * Note that Length contains the size of the mapping after invoking
		 * this work around, this value is required by
		 * acpi_tb_release_temp_table().
		 * We can do this because in acpi_init_table_descriptor(), the Length
		 * field of the installed descriptor is filled with the actual
		 * table length obtaining from the table header.
		 */
		table_desc->length = sizeof(struct acpi_table_header);
	}

	return (acpi_tb_validate_table(table_desc));
}

/******************************************************************************
 *
 * FUNCTION:    acpi_tb_verify_temp_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *              signature           - Table signature to verify
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to validate and verify the table, the
 *              returned table descriptor is in "VALIDATED" state.
 *
 *****************************************************************************/

acpi_status
acpi_tb_verify_temp_table(struct acpi_table_desc *table_desc, char *signature)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(tb_verify_temp_table);

	/* Validate the table */

	status = acpi_tb_validate_temp_table(table_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* If a particular signature is expected (DSDT/FACS), it must match */

	if (signature && !ACPI_COMPARE_NAME(&table_desc->signature, signature)) {
		ACPI_BIOS_ERROR((AE_INFO,
				 "Invalid signature 0x%X for ACPI table, expected [%s]",
				 table_desc->signature.integer, signature));
		status = AE_BAD_SIGNATURE;
		goto invalidate_and_exit;
	}

	/* Verify the checksum */

	if (acpi_gbl_verify_table_checksum) {
		status =
		    acpi_tb_verify_checksum(table_desc->pointer,
					    table_desc->length);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, AE_NO_MEMORY,
					"%4.4s 0x%8.8X%8.8X"
					" Attempted table install failed",
					acpi_ut_valid_nameseg(table_desc->
							      signature.
							      ascii) ?
					table_desc->signature.ascii : "????",
					ACPI_FORMAT_UINT64(table_desc->
							   address)));

			goto invalidate_and_exit;
		}
	}

	return_ACPI_STATUS(AE_OK);

invalidate_and_exit:
	acpi_tb_invalidate_table(table_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_resize_root_table_list
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Expand the size of global table array
 *
 ******************************************************************************/

acpi_status acpi_tb_resize_root_table_list(void)
{
	struct acpi_table_desc *tables;
	u32 table_count;

	ACPI_FUNCTION_TRACE(tb_resize_root_table_list);

	/* allow_resize flag is a parameter to acpi_initialize_tables */

	if (!(acpi_gbl_root_table_list.flags & ACPI_ROOT_ALLOW_RESIZE)) {
		ACPI_ERROR((AE_INFO,
			    "Resize of Root Table Array is not allowed"));
		return_ACPI_STATUS(AE_SUPPORT);
	}

	/* Increase the Table Array size */

	if (acpi_gbl_root_table_list.flags & ACPI_ROOT_ORIGIN_ALLOCATED) {
		table_count = acpi_gbl_root_table_list.max_table_count;
	} else {
		table_count = acpi_gbl_root_table_list.current_table_count;
	}

	tables = ACPI_ALLOCATE_ZEROED(((acpi_size)table_count +
				       ACPI_ROOT_TABLE_SIZE_INCREMENT) *
				      sizeof(struct acpi_table_desc));
	if (!tables) {
		ACPI_ERROR((AE_INFO,
			    "Could not allocate new root table array"));
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Copy and free the previous table array */

	if (acpi_gbl_root_table_list.tables) {
		memcpy(tables, acpi_gbl_root_table_list.tables,
		       (acpi_size)table_count * sizeof(struct acpi_table_desc));

		if (acpi_gbl_root_table_list.flags & ACPI_ROOT_ORIGIN_ALLOCATED) {
			ACPI_FREE(acpi_gbl_root_table_list.tables);
		}
	}

	acpi_gbl_root_table_list.tables = tables;
	acpi_gbl_root_table_list.max_table_count =
	    table_count + ACPI_ROOT_TABLE_SIZE_INCREMENT;
	acpi_gbl_root_table_list.flags |= ACPI_ROOT_ORIGIN_ALLOCATED;

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_next_table_descriptor
 *
 * PARAMETERS:  table_index         - Where table index is returned
 *              table_desc          - Where table descriptor is returned
 *
 * RETURN:      Status and table index/descriptor.
 *
 * DESCRIPTION: Allocate a new ACPI table entry to the global table list
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_next_table_descriptor(u32 *table_index,
				  struct acpi_table_desc **table_desc)
{
	acpi_status status;
	u32 i;

	/* Ensure that there is room for the table in the Root Table List */

	if (acpi_gbl_root_table_list.current_table_count >=
	    acpi_gbl_root_table_list.max_table_count) {
		status = acpi_tb_resize_root_table_list();
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	i = acpi_gbl_root_table_list.current_table_count;
	acpi_gbl_root_table_list.current_table_count++;

	if (table_index) {
		*table_index = i;
	}
	if (table_desc) {
		*table_desc = &acpi_gbl_root_table_list.tables[i];
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete all internal ACPI tables
 *
 ******************************************************************************/

void acpi_tb_terminate(void)
{
	u32 i;

	ACPI_FUNCTION_TRACE(tb_terminate);

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

	/* Delete the individual tables */

	for (i = 0; i < acpi_gbl_root_table_list.current_table_count; i++) {
		acpi_tb_uninstall_table(&acpi_gbl_root_table_list.tables[i]);
	}

	/*
	 * Delete the root table array if allocated locally. Array cannot be
	 * mapped, so we don't need to check for that flag.
	 */
	if (acpi_gbl_root_table_list.flags & ACPI_ROOT_ORIGIN_ALLOCATED) {
		ACPI_FREE(acpi_gbl_root_table_list.tables);
	}

	acpi_gbl_root_table_list.tables = NULL;
	acpi_gbl_root_table_list.flags = 0;
	acpi_gbl_root_table_list.current_table_count = 0;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "ACPI Tables freed\n"));

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_delete_namespace_by_owner
 *
 * PARAMETERS:  table_index         - Table index
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete all namespace objects created when this table was loaded.
 *
 ******************************************************************************/

acpi_status acpi_tb_delete_namespace_by_owner(u32 table_index)
{
	acpi_owner_id owner_id;
	acpi_status status;

	ACPI_FUNCTION_TRACE(tb_delete_namespace_by_owner);

	status = acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (table_index >= acpi_gbl_root_table_list.current_table_count) {

		/* The table index does not exist */

		(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Get the owner ID for this table, used to delete namespace nodes */

	owner_id = acpi_gbl_root_table_list.tables[table_index].owner_id;
	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);

	/*
	 * Need to acquire the namespace writer lock to prevent interference
	 * with any concurrent namespace walks. The interpreter must be
	 * released during the deletion since the acquisition of the deletion
	 * lock may block, and also since the execution of a namespace walk
	 * must be allowed to use the interpreter.
	 */
	status = acpi_ut_acquire_write_lock(&acpi_gbl_namespace_rw_lock);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}
	acpi_ns_delete_namespace_by_owner(owner_id);
	acpi_ut_release_write_lock(&acpi_gbl_namespace_rw_lock);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_allocate_owner_id
 *
 * PARAMETERS:  table_index         - Table index
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocates owner_id in table_desc
 *
 ******************************************************************************/

acpi_status acpi_tb_allocate_owner_id(u32 table_index)
{
	acpi_status status = AE_BAD_PARAMETER;

	ACPI_FUNCTION_TRACE(tb_allocate_owner_id);

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (table_index < acpi_gbl_root_table_list.current_table_count) {
		status =
		    acpi_ut_allocate_owner_id(&
					      (acpi_gbl_root_table_list.
					       tables[table_index].owner_id));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_release_owner_id
 *
 * PARAMETERS:  table_index         - Table index
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Releases owner_id in table_desc
 *
 ******************************************************************************/

acpi_status acpi_tb_release_owner_id(u32 table_index)
{
	acpi_status status = AE_BAD_PARAMETER;

	ACPI_FUNCTION_TRACE(tb_release_owner_id);

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (table_index < acpi_gbl_root_table_list.current_table_count) {
		acpi_ut_release_owner_id(&
					 (acpi_gbl_root_table_list.
					  tables[table_index].owner_id));
		status = AE_OK;
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_owner_id
 *
 * PARAMETERS:  table_index         - Table index
 *              owner_id            - Where the table owner_id is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: returns owner_id for the ACPI table
 *
 ******************************************************************************/

acpi_status acpi_tb_get_owner_id(u32 table_index, acpi_owner_id *owner_id)
{
	acpi_status status = AE_BAD_PARAMETER;

	ACPI_FUNCTION_TRACE(tb_get_owner_id);

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (table_index < acpi_gbl_root_table_list.current_table_count) {
		*owner_id =
		    acpi_gbl_root_table_list.tables[table_index].owner_id;
		status = AE_OK;
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_is_table_loaded
 *
 * PARAMETERS:  table_index         - Index into the root table
 *
 * RETURN:      Table Loaded Flag
 *
 ******************************************************************************/

u8 acpi_tb_is_table_loaded(u32 table_index)
{
	u8 is_loaded = FALSE;

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (table_index < acpi_gbl_root_table_list.current_table_count) {
		is_loaded = (u8)
		    (acpi_gbl_root_table_list.tables[table_index].flags &
		     ACPI_TABLE_IS_LOADED);
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return (is_loaded);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_set_table_loaded_flag
 *
 * PARAMETERS:  table_index         - Table index
 *              is_loaded           - TRUE if table is loaded, FALSE otherwise
 *
 * RETURN:      None
 *
 * DESCRIPTION: Sets the table loaded flag to either TRUE or FALSE.
 *
 ******************************************************************************/

void acpi_tb_set_table_loaded_flag(u32 table_index, u8 is_loaded)
{

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	if (table_index < acpi_gbl_root_table_list.current_table_count) {
		if (is_loaded) {
			acpi_gbl_root_table_list.tables[table_index].flags |=
			    ACPI_TABLE_IS_LOADED;
		} else {
			acpi_gbl_root_table_list.tables[table_index].flags &=
			    ~ACPI_TABLE_IS_LOADED;
		}
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_load_table
 *
 * PARAMETERS:  table_index             - Table index
 *              parent_node             - Where table index is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table
 *
 ******************************************************************************/

acpi_status
acpi_tb_load_table(u32 table_index, struct acpi_namespace_node *parent_node)
{
	struct acpi_table_header *table;
	acpi_status status;
	acpi_owner_id owner_id;

	ACPI_FUNCTION_TRACE(tb_load_table);

	/*
	 * Note: Now table is "INSTALLED", it must be validated before
	 * using.
	 */
	status = acpi_get_table_by_index(table_index, &table);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ns_load_table(table_index, parent_node);

	/* Execute any module-level code that was found in the table */

	if (!acpi_gbl_parse_table_as_term_list
	    && acpi_gbl_group_module_level_code) {
		acpi_ns_exec_module_code_list();
	}

	/*
	 * Update GPEs for any new _Lxx/_Exx methods. Ignore errors. The host is
	 * responsible for discovering any new wake GPEs by running _PRW methods
	 * that may have been loaded by this table.
	 */
	status = acpi_tb_get_owner_id(table_index, &owner_id);
	if (ACPI_SUCCESS(status)) {
		acpi_ev_update_gpes(owner_id);
	}

	/* Invoke table handler if present */

	if (acpi_gbl_table_handler) {
		(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_LOAD, table,
					     acpi_gbl_table_handler_context);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_and_load_table
 *
 * PARAMETERS:  address                 - Physical address of the table
 *              flags                   - Allocation flags of the table
 *              override                - Whether override should be performed
 *              table_index             - Where table index is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install and load an ACPI table
 *
 ******************************************************************************/

acpi_status
acpi_tb_install_and_load_table(acpi_physical_address address,
			       u8 flags, u8 override, u32 *table_index)
{
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE(tb_install_and_load_table);

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

	/* Install the table and load it into the namespace */

	status = acpi_tb_install_standard_table(address, flags, TRUE,
						override, &i);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	status = acpi_tb_load_table(i, acpi_gbl_root_node);
	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

unlock_and_exit:
	*table_index = i;
	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_unload_table
 *
 * PARAMETERS:  table_index             - Table index
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unload an ACPI table
 *
 ******************************************************************************/

acpi_status acpi_tb_unload_table(u32 table_index)
{
	acpi_status status = AE_OK;
	struct acpi_table_header *table;

	ACPI_FUNCTION_TRACE(tb_unload_table);

	/* Ensure the table is still loaded */

	if (!acpi_tb_is_table_loaded(table_index)) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Invoke table handler if present */

	if (acpi_gbl_table_handler) {
		status = acpi_get_table_by_index(table_index, &table);
		if (ACPI_SUCCESS(status)) {
			(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_UNLOAD,
						     table,
						     acpi_gbl_table_handler_context);
		}
	}

	/* Delete the portion of the namespace owned by this table */

	status = acpi_tb_delete_namespace_by_owner(table_index);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	(void)acpi_tb_release_owner_id(table_index);
	acpi_tb_set_table_loaded_flag(table_index, FALSE);
	return_ACPI_STATUS(status);
}
