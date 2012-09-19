/******************************************************************************
 *
 * Module Name: tbxface - ACPI table oriented external interfaces
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
#include "acnamesp.h"
#include "actables.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbxface")

/*******************************************************************************
 *
 * FUNCTION:    acpi_allocate_root_table
 *
 * PARAMETERS:  initial_table_count - Size of initial_table_array, in number of
 *                                    struct acpi_table_desc structures
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate a root table array. Used by iASL compiler and
 *              acpi_initialize_tables.
 *
 ******************************************************************************/
acpi_status acpi_allocate_root_table(u32 initial_table_count)
{

	acpi_gbl_root_table_list.max_table_count = initial_table_count;
	acpi_gbl_root_table_list.flags = ACPI_ROOT_ALLOW_RESIZE;

	return (acpi_tb_resize_root_table_list());
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_tables
 *
 * PARAMETERS:  initial_table_array - Pointer to an array of pre-allocated
 *                                    struct acpi_table_desc structures. If NULL, the
 *                                    array is dynamically allocated.
 *              initial_table_count - Size of initial_table_array, in number of
 *                                    struct acpi_table_desc structures
 *              allow_realloc       - Flag to tell Table Manager if resize of
 *                                    pre-allocated array is allowed. Ignored
 *                                    if initial_table_array is NULL.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the table manager, get the RSDP and RSDT/XSDT.
 *
 * NOTE:        Allows static allocation of the initial table array in order
 *              to avoid the use of dynamic memory in confined environments
 *              such as the kernel boot sequence where it may not be available.
 *
 *              If the host OS memory managers are initialized, use NULL for
 *              initial_table_array, and the table will be dynamically allocated.
 *
 ******************************************************************************/

acpi_status __init
acpi_initialize_tables(struct acpi_table_desc * initial_table_array,
		       u32 initial_table_count, u8 allow_resize)
{
	acpi_physical_address rsdp_address;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_initialize_tables);

	/*
	 * Set up the Root Table Array
	 * Allocate the table array if requested
	 */
	if (!initial_table_array) {
		status = acpi_allocate_root_table(initial_table_count);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	} else {
		/* Root Table Array has been statically allocated by the host */

		ACPI_MEMSET(initial_table_array, 0,
			    (acpi_size) initial_table_count *
			    sizeof(struct acpi_table_desc));

		acpi_gbl_root_table_list.tables = initial_table_array;
		acpi_gbl_root_table_list.max_table_count = initial_table_count;
		acpi_gbl_root_table_list.flags = ACPI_ROOT_ORIGIN_UNKNOWN;
		if (allow_resize) {
			acpi_gbl_root_table_list.flags |=
			    ACPI_ROOT_ALLOW_RESIZE;
		}
	}

	/* Get the address of the RSDP */

	rsdp_address = acpi_os_get_root_pointer();
	if (!rsdp_address) {
		return_ACPI_STATUS(AE_NOT_FOUND);
	}

	/*
	 * Get the root table (RSDT or XSDT) and extract all entries to the local
	 * Root Table Array. This array contains the information of the RSDT/XSDT
	 * in a common, more useable format.
	 */
	status = acpi_tb_parse_root_table(rsdp_address);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_reallocate_root_table
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reallocate Root Table List into dynamic memory. Copies the
 *              root list from the previously provided scratch area. Should
 *              be called once dynamic memory allocation is available in the
 *              kernel
 *
 ******************************************************************************/
acpi_status acpi_reallocate_root_table(void)
{
	struct acpi_table_desc *tables;
	acpi_size new_size;
	acpi_size current_size;

	ACPI_FUNCTION_TRACE(acpi_reallocate_root_table);

	/*
	 * Only reallocate the root table if the host provided a static buffer
	 * for the table array in the call to acpi_initialize_tables.
	 */
	if (acpi_gbl_root_table_list.flags & ACPI_ROOT_ORIGIN_ALLOCATED) {
		return_ACPI_STATUS(AE_SUPPORT);
	}

	/*
	 * Get the current size of the root table and add the default
	 * increment to create the new table size.
	 */
	current_size = (acpi_size)
	    acpi_gbl_root_table_list.current_table_count *
	    sizeof(struct acpi_table_desc);

	new_size = current_size +
	    (ACPI_ROOT_TABLE_SIZE_INCREMENT * sizeof(struct acpi_table_desc));

	/* Create new array and copy the old array */

	tables = ACPI_ALLOCATE_ZEROED(new_size);
	if (!tables) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	ACPI_MEMCPY(tables, acpi_gbl_root_table_list.tables, current_size);

	/*
	 * Update the root table descriptor. The new size will be the current
	 * number of tables plus the increment, independent of the reserved
	 * size of the original table list.
	 */
	acpi_gbl_root_table_list.tables = tables;
	acpi_gbl_root_table_list.max_table_count =
	    acpi_gbl_root_table_list.current_table_count +
	    ACPI_ROOT_TABLE_SIZE_INCREMENT;
	acpi_gbl_root_table_list.flags =
	    ACPI_ROOT_ORIGIN_ALLOCATED | ACPI_ROOT_ALLOW_RESIZE;

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_table_header
 *
 * PARAMETERS:  signature           - ACPI signature of needed table
 *              instance            - Which instance (for SSDTs)
 *              out_table_header    - The pointer to the table header to fill
 *
 * RETURN:      Status and pointer to mapped table header
 *
 * DESCRIPTION: Finds an ACPI table header.
 *
 * NOTE:        Caller is responsible in unmapping the header with
 *              acpi_os_unmap_memory
 *
 ******************************************************************************/
acpi_status
acpi_get_table_header(char *signature,
		      u32 instance, struct acpi_table_header *out_table_header)
{
       u32 i;
       u32 j;
	struct acpi_table_header *header;

	/* Parameter validation */

	if (!signature || !out_table_header) {
		return (AE_BAD_PARAMETER);
	}

	/* Walk the root table list */

	for (i = 0, j = 0; i < acpi_gbl_root_table_list.current_table_count;
	     i++) {
		if (!ACPI_COMPARE_NAME
		    (&(acpi_gbl_root_table_list.tables[i].signature),
		     signature)) {
			continue;
		}

		if (++j < instance) {
			continue;
		}

		if (!acpi_gbl_root_table_list.tables[i].pointer) {
			if ((acpi_gbl_root_table_list.tables[i].flags &
			     ACPI_TABLE_ORIGIN_MASK) ==
			    ACPI_TABLE_ORIGIN_MAPPED) {
				header =
				    acpi_os_map_memory(acpi_gbl_root_table_list.
						       tables[i].address,
						       sizeof(struct
							      acpi_table_header));
				if (!header) {
					return AE_NO_MEMORY;
				}
				ACPI_MEMCPY(out_table_header, header,
					    sizeof(struct acpi_table_header));
				acpi_os_unmap_memory(header,
						     sizeof(struct
							    acpi_table_header));
			} else {
				return AE_NOT_FOUND;
			}
		} else {
			ACPI_MEMCPY(out_table_header,
				    acpi_gbl_root_table_list.tables[i].pointer,
				    sizeof(struct acpi_table_header));
		}
		return (AE_OK);
	}

	return (AE_NOT_FOUND);
}

ACPI_EXPORT_SYMBOL(acpi_get_table_header)

/*******************************************************************************
 *
 * FUNCTION:    acpi_unload_table_id
 *
 * PARAMETERS:  id            - Owner ID of the table to be removed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine is used to force the unload of a table (by id)
 *
 ******************************************************************************/
acpi_status acpi_unload_table_id(acpi_owner_id id)
{
	int i;
	acpi_status status = AE_NOT_EXIST;

	ACPI_FUNCTION_TRACE(acpi_unload_table_id);

	/* Find table in the global table list */
	for (i = 0; i < acpi_gbl_root_table_list.current_table_count; ++i) {
		if (id != acpi_gbl_root_table_list.tables[i].owner_id) {
			continue;
		}
		/*
		 * Delete all namespace objects owned by this table. Note that these
		 * objects can appear anywhere in the namespace by virtue of the AML
		 * "Scope" operator. Thus, we need to track ownership by an ID, not
		 * simply a position within the hierarchy
		 */
		acpi_tb_delete_namespace_by_owner(i);
		status = acpi_tb_release_owner_id(i);
		acpi_tb_set_table_loaded_flag(i, FALSE);
		break;
	}
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_unload_table_id)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_table_with_size
 *
 * PARAMETERS:  signature           - ACPI signature of needed table
 *              instance            - Which instance (for SSDTs)
 *              out_table           - Where the pointer to the table is returned
 *
 * RETURN:      Status and pointer to table
 *
 * DESCRIPTION: Finds and verifies an ACPI table.
 *
 ******************************************************************************/
acpi_status
acpi_get_table_with_size(char *signature,
	       u32 instance, struct acpi_table_header **out_table,
	       acpi_size *tbl_size)
{
       u32 i;
       u32 j;
	acpi_status status;

	/* Parameter validation */

	if (!signature || !out_table) {
		return (AE_BAD_PARAMETER);
	}

	/* Walk the root table list */

	for (i = 0, j = 0; i < acpi_gbl_root_table_list.current_table_count;
	     i++) {
		if (!ACPI_COMPARE_NAME
		    (&(acpi_gbl_root_table_list.tables[i].signature),
		     signature)) {
			continue;
		}

		if (++j < instance) {
			continue;
		}

		status =
		    acpi_tb_verify_table(&acpi_gbl_root_table_list.tables[i]);
		if (ACPI_SUCCESS(status)) {
			*out_table = acpi_gbl_root_table_list.tables[i].pointer;
			*tbl_size = acpi_gbl_root_table_list.tables[i].length;
		}

		if (!acpi_gbl_permanent_mmap) {
			acpi_gbl_root_table_list.tables[i].pointer = NULL;
		}

		return (status);
	}

	return (AE_NOT_FOUND);
}
ACPI_EXPORT_SYMBOL(acpi_get_table_with_size)

acpi_status
acpi_get_table(char *signature,
	       u32 instance, struct acpi_table_header **out_table)
{
	acpi_size tbl_size;

	return acpi_get_table_with_size(signature,
		       instance, out_table, &tbl_size);
}
ACPI_EXPORT_SYMBOL(acpi_get_table)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_table_by_index
 *
 * PARAMETERS:  table_index         - Table index
 *              table               - Where the pointer to the table is returned
 *
 * RETURN:      Status and pointer to the table
 *
 * DESCRIPTION: Obtain a table by an index into the global table list.
 *
 ******************************************************************************/
acpi_status
acpi_get_table_by_index(u32 table_index, struct acpi_table_header **table)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_table_by_index);

	/* Parameter validation */

	if (!table) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

	/* Validate index */

	if (table_index >= acpi_gbl_root_table_list.current_table_count) {
		(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!acpi_gbl_root_table_list.tables[table_index].pointer) {

		/* Table is not mapped, map it */

		status =
		    acpi_tb_verify_table(&acpi_gbl_root_table_list.
					 tables[table_index]);
		if (ACPI_FAILURE(status)) {
			(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
			return_ACPI_STATUS(status);
		}
	}

	*table = acpi_gbl_root_table_list.tables[table_index].pointer;
	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_get_table_by_index)


/*******************************************************************************
 *
 * FUNCTION:    acpi_install_table_handler
 *
 * PARAMETERS:  handler         - Table event handler
 *              context         - Value passed to the handler on each event
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install table event handler
 *
 ******************************************************************************/
acpi_status
acpi_install_table_handler(acpi_tbl_handler handler, void *context)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_table_handler);

	if (!handler) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Don't allow more than one handler */

	if (acpi_gbl_table_handler) {
		status = AE_ALREADY_EXISTS;
		goto cleanup;
	}

	/* Install the handler */

	acpi_gbl_table_handler = handler;
	acpi_gbl_table_handler_context = context;

      cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_table_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_table_handler
 *
 * PARAMETERS:  handler         - Table event handler that was installed
 *                                previously.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove table event handler
 *
 ******************************************************************************/
acpi_status acpi_remove_table_handler(acpi_tbl_handler handler)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_remove_table_handler);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Make sure that the installed handler is the same */

	if (!handler || handler != acpi_gbl_table_handler) {
		status = AE_BAD_PARAMETER;
		goto cleanup;
	}

	/* Remove the handler */

	acpi_gbl_table_handler = NULL;

      cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_table_handler)
