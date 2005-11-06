/******************************************************************************
 *
 * Module Name: tbxface - Public interfaces to the ACPI subsystem
 *                         ACPI table oriented interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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

#include <linux/module.h>

#include <acpi/acpi.h>
#include <acpi/acnamesp.h>
#include <acpi/actables.h>

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbxface")

/*******************************************************************************
 *
 * FUNCTION:    acpi_load_tables
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to load the ACPI tables from the
 *              provided RSDT
 *
 ******************************************************************************/
acpi_status acpi_load_tables(void)
{
	struct acpi_pointer rsdp_address;
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_load_tables");

	/* Get the RSDP */

	status = acpi_os_get_root_pointer(ACPI_LOGICAL_ADDRESSING,
					  &rsdp_address);
	if (ACPI_FAILURE(status)) {
		ACPI_REPORT_ERROR(("acpi_load_tables: Could not get RSDP, %s\n",
				   acpi_format_exception(status)));
		goto error_exit;
	}

	/* Map and validate the RSDP */

	acpi_gbl_table_flags = rsdp_address.pointer_type;

	status = acpi_tb_verify_rsdp(&rsdp_address);
	if (ACPI_FAILURE(status)) {
		ACPI_REPORT_ERROR(("acpi_load_tables: RSDP Failed validation: %s\n", acpi_format_exception(status)));
		goto error_exit;
	}

	/* Get the RSDT via the RSDP */

	status = acpi_tb_get_table_rsdt();
	if (ACPI_FAILURE(status)) {
		ACPI_REPORT_ERROR(("acpi_load_tables: Could not load RSDT: %s\n", acpi_format_exception(status)));
		goto error_exit;
	}

	/* Now get the tables needed by this subsystem (FADT, DSDT, etc.) */

	status = acpi_tb_get_required_tables();
	if (ACPI_FAILURE(status)) {
		ACPI_REPORT_ERROR(("acpi_load_tables: Error getting required tables (DSDT/FADT/FACS): %s\n", acpi_format_exception(status)));
		goto error_exit;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INIT, "ACPI Tables successfully acquired\n"));

	/* Load the namespace from the tables */

	status = acpi_ns_load_namespace();
	if (ACPI_FAILURE(status)) {
		ACPI_REPORT_ERROR(("acpi_load_tables: Could not load namespace: %s\n", acpi_format_exception(status)));
		goto error_exit;
	}

	return_ACPI_STATUS(AE_OK);

      error_exit:
	ACPI_REPORT_ERROR(("acpi_load_tables: Could not load tables: %s\n",
			   acpi_format_exception(status)));

	return_ACPI_STATUS(status);
}

#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_load_table
 *
 * PARAMETERS:  table_ptr       - pointer to a buffer containing the entire
 *                                table to be loaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to load a table from the caller's
 *              buffer.  The buffer must contain an entire ACPI Table including
 *              a valid header.  The header fields will be verified, and if it
 *              is determined that the table is invalid, the call will fail.
 *
 ******************************************************************************/

acpi_status acpi_load_table(struct acpi_table_header *table_ptr)
{
	acpi_status status;
	struct acpi_table_desc table_info;
	struct acpi_pointer address;

	ACPI_FUNCTION_TRACE("acpi_load_table");

	if (!table_ptr) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Copy the table to a local buffer */

	address.pointer_type = ACPI_LOGICAL_POINTER | ACPI_LOGICAL_ADDRESSING;
	address.pointer.logical = table_ptr;

	status = acpi_tb_get_table_body(&address, table_ptr, &table_info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Check signature for a valid table type */

	status = acpi_tb_recognize_table(&table_info, ACPI_TABLE_ALL);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Install the new table into the local data structures */

	status = acpi_tb_install_table(&table_info);
	if (ACPI_FAILURE(status)) {
		if (status == AE_ALREADY_EXISTS) {
			/* Table already exists, no error */

			status = AE_OK;
		}

		/* Free table allocated by acpi_tb_get_table_body */

		acpi_tb_delete_single_table(&table_info);
		return_ACPI_STATUS(status);
	}

	/* Convert the table to common format if necessary */

	switch (table_info.type) {
	case ACPI_TABLE_FADT:

		status = acpi_tb_convert_table_fadt();
		break;

	case ACPI_TABLE_FACS:

		status = acpi_tb_build_common_facs(&table_info);
		break;

	default:
		/* Load table into namespace if it contains executable AML */

		status =
		    acpi_ns_load_table(table_info.installed_desc,
				       acpi_gbl_root_node);
		break;
	}

	if (ACPI_FAILURE(status)) {
		/* Uninstall table and free the buffer */

		(void)acpi_tb_uninstall_table(table_info.installed_desc);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_unload_table
 *
 * PARAMETERS:  table_type    - Type of table to be unloaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine is used to force the unload of a table
 *
 ******************************************************************************/

acpi_status acpi_unload_table(acpi_table_type table_type)
{
	struct acpi_table_desc *table_desc;

	ACPI_FUNCTION_TRACE("acpi_unload_table");

	/* Parameter validation */

	if (table_type > ACPI_TABLE_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Find all tables of the requested type */

	table_desc = acpi_gbl_table_lists[table_type].next;
	while (table_desc) {
		/*
		 * Delete all namespace entries owned by this table.  Note that these
		 * entries can appear anywhere in the namespace by virtue of the AML
		 * "Scope" operator.  Thus, we need to track ownership by an ID, not
		 * simply a position within the hierarchy
		 */
		acpi_ns_delete_namespace_by_owner(table_desc->owner_id);
		acpi_ut_release_owner_id(&table_desc->owner_id);
		table_desc = table_desc->next;
	}

	/* Delete (or unmap) all tables of this type */

	acpi_tb_delete_tables_by_type(table_type);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_table_header
 *
 * PARAMETERS:  table_type      - one of the defined table types
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *                                see acpi_gbl_acpi_table_flag
 *              out_table_header - pointer to the struct acpi_table_header if successful
 *
 * DESCRIPTION: This function is called to get an ACPI table header.  The caller
 *              supplies an pointer to a data area sufficient to contain an ACPI
 *              struct acpi_table_header structure.
 *
 *              The header contains a length field that can be used to determine
 *              the size of the buffer needed to contain the entire table.  This
 *              function is not valid for the RSD PTR table since it does not
 *              have a standard header and is fixed length.
 *
 ******************************************************************************/

acpi_status
acpi_get_table_header(acpi_table_type table_type,
		      u32 instance, struct acpi_table_header *out_table_header)
{
	struct acpi_table_header *tbl_ptr;
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_get_table_header");

	if ((instance == 0) ||
	    (table_type == ACPI_TABLE_RSDP) || (!out_table_header)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Check the table type and instance */

	if ((table_type > ACPI_TABLE_MAX) ||
	    (ACPI_IS_SINGLE_TABLE(acpi_gbl_table_data[table_type].flags) &&
	     instance > 1)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get a pointer to the entire table */

	status = acpi_tb_get_table_ptr(table_type, instance, &tbl_ptr);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* The function will return a NULL pointer if the table is not loaded */

	if (tbl_ptr == NULL) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Copy the header to the caller's buffer */

	ACPI_MEMCPY((void *)out_table_header, (void *)tbl_ptr,
		    sizeof(struct acpi_table_header));

	return_ACPI_STATUS(status);
}

#endif				/*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_table
 *
 * PARAMETERS:  table_type      - one of the defined table types
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *                                see acpi_gbl_acpi_table_flag
 *              ret_buffer      - pointer to a structure containing a buffer to
 *                                receive the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get an ACPI table.  The caller
 *              supplies an out_buffer large enough to contain the entire ACPI
 *              table.  The caller should call the acpi_get_table_header function
 *              first to determine the buffer size needed.  Upon completion
 *              the out_buffer->Length field will indicate the number of bytes
 *              copied into the out_buffer->buf_ptr buffer. This table will be
 *              a complete table including the header.
 *
 ******************************************************************************/

acpi_status
acpi_get_table(acpi_table_type table_type,
	       u32 instance, struct acpi_buffer *ret_buffer)
{
	struct acpi_table_header *tbl_ptr;
	acpi_status status;
	acpi_size table_length;

	ACPI_FUNCTION_TRACE("acpi_get_table");

	/* Parameter validation */

	if (instance == 0) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer(ret_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Check the table type and instance */

	if ((table_type > ACPI_TABLE_MAX) ||
	    (ACPI_IS_SINGLE_TABLE(acpi_gbl_table_data[table_type].flags) &&
	     instance > 1)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get a pointer to the entire table */

	status = acpi_tb_get_table_ptr(table_type, instance, &tbl_ptr);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * acpi_tb_get_table_ptr will return a NULL pointer if the
	 * table is not loaded.
	 */
	if (tbl_ptr == NULL) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Get the table length */

	if (table_type == ACPI_TABLE_RSDP) {
		/* RSD PTR is the only "table" without a header */

		table_length = sizeof(struct rsdp_descriptor);
	} else {
		table_length = (acpi_size) tbl_ptr->length;
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(ret_buffer, table_length);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Copy the table to the buffer */

	ACPI_MEMCPY((void *)ret_buffer->pointer, (void *)tbl_ptr, table_length);
	return_ACPI_STATUS(AE_OK);
}

EXPORT_SYMBOL(acpi_get_table);
