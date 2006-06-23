/******************************************************************************
 *
 * Module Name: tbget - ACPI Table get* routines
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
ACPI_MODULE_NAME("tbget")

/* Local prototypes */
static acpi_status
acpi_tb_get_this_table(struct acpi_pointer *address,
		       struct acpi_table_header *header,
		       struct acpi_table_desc *table_info);

static acpi_status
acpi_tb_table_override(struct acpi_table_header *header,
		       struct acpi_table_desc *table_info);

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              table_info          - Where table info is returned
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get entire table of unknown size.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table(struct acpi_pointer *address,
		  struct acpi_table_desc *table_info)
{
	acpi_status status;
	struct acpi_table_header header;

	ACPI_FUNCTION_TRACE(tb_get_table);

	/* Get the header in order to get signature and table size */

	status = acpi_tb_get_table_header(address, &header);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the entire table */

	status = acpi_tb_get_table_body(address, &header, table_info);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not get ACPI table (size %X)",
				header.length));
		return_ACPI_STATUS(status);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table_header
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              return_header       - Where the table header is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an ACPI table header.  Works in both physical or virtual
 *              addressing mode.  Works with both physical or logical pointers.
 *              Table is either copied or mapped, depending on the pointer
 *              type and mode of the processor.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_header(struct acpi_pointer *address,
			 struct acpi_table_header *return_header)
{
	acpi_status status = AE_OK;
	struct acpi_table_header *header = NULL;

	ACPI_FUNCTION_TRACE(tb_get_table_header);

	/*
	 * Flags contains the current processor mode (Virtual or Physical
	 * addressing) The pointer_type is either Logical or Physical
	 */
	switch (address->pointer_type) {
	case ACPI_PHYSMODE_PHYSPTR:
	case ACPI_LOGMODE_LOGPTR:

		/* Pointer matches processor mode, copy the header */

		ACPI_MEMCPY(return_header, address->pointer.logical,
			    sizeof(struct acpi_table_header));
		break;

	case ACPI_LOGMODE_PHYSPTR:

		/* Create a logical address for the physical pointer */

		status = acpi_os_map_memory(address->pointer.physical,
					    sizeof(struct acpi_table_header),
					    (void *)&header);
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Could not map memory at %8.8X%8.8X for table header",
				    ACPI_FORMAT_UINT64(address->pointer.
						       physical)));
			return_ACPI_STATUS(status);
		}

		/* Copy header and delete mapping */

		ACPI_MEMCPY(return_header, header,
			    sizeof(struct acpi_table_header));
		acpi_os_unmap_memory(header, sizeof(struct acpi_table_header));
		break;

	default:

		ACPI_ERROR((AE_INFO, "Invalid address flags %X",
			    address->pointer_type));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_TABLES, "Table Signature: [%4.4s]\n",
			  return_header->signature));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table_body
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              Header              - Header of the table to retrieve
 *              table_info          - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an entire ACPI table with support to allow the host OS to
 *              replace the table with a newer version (table override.)
 *              Works in both physical or virtual
 *              addressing mode.  Works with both physical or logical pointers.
 *              Table is either copied or mapped, depending on the pointer
 *              type and mode of the processor.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_body(struct acpi_pointer *address,
		       struct acpi_table_header *header,
		       struct acpi_table_desc *table_info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(tb_get_table_body);

	if (!table_info || !address) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Attempt table override. */

	status = acpi_tb_table_override(header, table_info);
	if (ACPI_SUCCESS(status)) {

		/* Table was overridden by the host OS */

		return_ACPI_STATUS(status);
	}

	/* No override, get the original table */

	status = acpi_tb_get_this_table(address, header, table_info);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_table_override
 *
 * PARAMETERS:  Header              - Pointer to table header
 *              table_info          - Return info if table is overridden
 *
 * RETURN:      None
 *
 * DESCRIPTION: Attempts override of current table with a new one if provided
 *              by the host OS.
 *
 ******************************************************************************/

static acpi_status
acpi_tb_table_override(struct acpi_table_header *header,
		       struct acpi_table_desc *table_info)
{
	struct acpi_table_header *new_table;
	acpi_status status;
	struct acpi_pointer address;

	ACPI_FUNCTION_TRACE(tb_table_override);

	/*
	 * The OSL will examine the header and decide whether to override this
	 * table.  If it decides to override, a table will be returned in new_table,
	 * which we will then copy.
	 */
	status = acpi_os_table_override(header, &new_table);
	if (ACPI_FAILURE(status)) {

		/* Some severe error from the OSL, but we basically ignore it */

		ACPI_EXCEPTION((AE_INFO, status,
				"Could not override ACPI table"));
		return_ACPI_STATUS(status);
	}

	if (!new_table) {

		/* No table override */

		return_ACPI_STATUS(AE_NO_ACPI_TABLES);
	}

	/*
	 * We have a new table to override the old one.  Get a copy of
	 * the new one.  We know that the new table has a logical pointer.
	 */
	address.pointer_type = ACPI_LOGICAL_POINTER | ACPI_LOGICAL_ADDRESSING;
	address.pointer.logical = new_table;

	status = acpi_tb_get_this_table(&address, new_table, table_info);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Could not copy ACPI table"));
		return_ACPI_STATUS(status);
	}

	/* Copy the table info */

	ACPI_INFO((AE_INFO, "Table [%4.4s] replaced by host OS",
		   table_info->pointer->signature));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_this_table
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              Header              - Header of the table to retrieve
 *              table_info          - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an entire ACPI table.  Works in both physical or virtual
 *              addressing mode.  Works with both physical or logical pointers.
 *              Table is either copied or mapped, depending on the pointer
 *              type and mode of the processor.
 *
 ******************************************************************************/

static acpi_status
acpi_tb_get_this_table(struct acpi_pointer *address,
		       struct acpi_table_header *header,
		       struct acpi_table_desc *table_info)
{
	struct acpi_table_header *full_table = NULL;
	u8 allocation;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(tb_get_this_table);

	/*
	 * Flags contains the current processor mode (Virtual or Physical
	 * addressing) The pointer_type is either Logical or Physical
	 */
	switch (address->pointer_type) {
	case ACPI_PHYSMODE_PHYSPTR:
	case ACPI_LOGMODE_LOGPTR:

		/* Pointer matches processor mode, copy the table to a new buffer */

		full_table = ACPI_ALLOCATE(header->length);
		if (!full_table) {
			ACPI_ERROR((AE_INFO,
				    "Could not allocate table memory for [%4.4s] length %X",
				    header->signature, header->length));
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Copy the entire table (including header) to the local buffer */

		ACPI_MEMCPY(full_table, address->pointer.logical,
			    header->length);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
		break;

	case ACPI_LOGMODE_PHYSPTR:

		/*
		 * Just map the table's physical memory
		 * into our address space.
		 */
		status = acpi_os_map_memory(address->pointer.physical,
					    (acpi_size) header->length,
					    (void *)&full_table);
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Could not map memory for table [%4.4s] at %8.8X%8.8X for length %X",
				    header->signature,
				    ACPI_FORMAT_UINT64(address->pointer.
						       physical),
				    header->length));
			return (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
		break;

	default:

		ACPI_ERROR((AE_INFO, "Invalid address flags %X",
			    address->pointer_type));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Validate checksum for _most_ tables,
	 * even the ones whose signature we don't recognize
	 */
	if (table_info->type != ACPI_TABLE_ID_FACS) {
		status = acpi_tb_verify_table_checksum(full_table);

#if (!ACPI_CHECKSUM_ABORT)
		if (ACPI_FAILURE(status)) {

			/* Ignore the error if configuration says so */

			status = AE_OK;
		}
#endif
	}

	/* Return values */

	table_info->pointer = full_table;
	table_info->length = (acpi_size) header->length;
	table_info->allocation = allocation;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Found table [%4.4s] at %8.8X%8.8X, mapped/copied to %p\n",
			  full_table->signature,
			  ACPI_FORMAT_UINT64(address->pointer.physical),
			  full_table));

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table_ptr
 *
 * PARAMETERS:  table_type      - one of the defined table types
 *              Instance        - Which table of this type
 *              return_table    - pointer to location to place the pointer for
 *                                return
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the pointer to an ACPI table.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_ptr(acpi_table_type table_type,
		      u32 instance, struct acpi_table_header **return_table)
{
	struct acpi_table_desc *table_desc;
	u32 i;

	ACPI_FUNCTION_TRACE(tb_get_table_ptr);

	if (table_type > ACPI_TABLE_ID_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Check for instance out of range of the current table count */

	if (instance > acpi_gbl_table_lists[table_type].count) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/*
	 * Walk the list to get the desired table
	 * Note: Instance is one-based
	 */
	table_desc = acpi_gbl_table_lists[table_type].next;
	for (i = 1; i < instance; i++) {
		table_desc = table_desc->next;
	}

	/* We are now pointing to the requested table's descriptor */

	*return_table = table_desc->pointer;
	return_ACPI_STATUS(AE_OK);
}
