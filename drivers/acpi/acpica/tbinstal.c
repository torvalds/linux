/******************************************************************************
 *
 * Module Name: tbinstal - ACPI table installation and removal
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbinstal")

/* Local prototypes */
static acpi_status
acpi_tb_acquire_temporal_table(struct acpi_table_desc *table_desc,
			       acpi_physical_address address, u8 flags);

static void acpi_tb_release_temporal_table(struct acpi_table_desc *table_desc);

static acpi_status acpi_tb_acquire_root_table_entry(u32 *table_index);

static u8
acpi_tb_is_equivalent_table(struct acpi_table_desc *table_desc,
			    u32 table_index);

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
 * DESCRIPTION: Acquire a table. It can be used for tables not maintained in
 *              acpi_gbl_root_table_list.
 *
 ******************************************************************************/

acpi_status
acpi_tb_acquire_table(struct acpi_table_desc *table_desc,
		      struct acpi_table_header **table_ptr,
		      u32 *table_length, u8 *table_flags)
{
	struct acpi_table_header *table = NULL;

	switch (table_desc->flags & ACPI_TABLE_ORIGIN_MASK) {
	case ACPI_TABLE_ORIGIN_INTERN_PHYSICAL:

		table =
		    acpi_os_map_memory(table_desc->address, table_desc->length);
		break;

	case ACPI_TABLE_ORIGIN_INTERN_VIRTUAL:
	case ACPI_TABLE_ORIGIN_EXTERN_VIRTUAL:

		table =
		    ACPI_CAST_PTR(struct acpi_table_header,
				  table_desc->address);
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
 * DESCRIPTION: Release a table. The reversal of acpi_tb_acquire_table().
 *
 ******************************************************************************/

void
acpi_tb_release_table(struct acpi_table_header *table,
		      u32 table_length, u8 table_flags)
{
	switch (table_flags & ACPI_TABLE_ORIGIN_MASK) {
	case ACPI_TABLE_ORIGIN_INTERN_PHYSICAL:

		acpi_os_unmap_memory(table, table_length);
		break;

	case ACPI_TABLE_ORIGIN_INTERN_VIRTUAL:
	case ACPI_TABLE_ORIGIN_EXTERN_VIRTUAL:
	default:

		break;
	}
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
 * DESCRIPTION: Invalidate one internal ACPI table, this is reversal of
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
 * FUNCTION:    acpi_tb_verify_table
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
acpi_tb_verify_table(struct acpi_table_desc *table_desc, char *signature)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(tb_verify_table);

	/* Validate the table */

	status = acpi_tb_validate_table(table_desc);
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

	status =
	    acpi_tb_verify_checksum(table_desc->pointer, table_desc->length);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, AE_NO_MEMORY,
				"%4.4s " ACPI_PRINTF_UINT
				" Attempted table install failed",
				acpi_ut_valid_acpi_name(table_desc->signature.
							ascii) ? table_desc->
				signature.ascii : "????",
				ACPI_FORMAT_TO_UINT(table_desc->address)));
		goto invalidate_and_exit;
	}

	return_ACPI_STATUS(AE_OK);

invalidate_and_exit:
	acpi_tb_invalidate_table(table_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_table
 *
 * PARAMETERS:  table_desc              - Table descriptor
 *              address                 - Physical address of the table
 *              flags                   - Allocation flags of the table
 *              table                   - Pointer to the table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Install an ACPI table into the global data structure.
 *
 ******************************************************************************/

void
acpi_tb_install_table(struct acpi_table_desc *table_desc,
		      acpi_physical_address address,
		      u8 flags, struct acpi_table_header *table)
{
	/*
	 * Initialize the table entry. Set the pointer to NULL, since the
	 * table is not fully mapped at this time.
	 */
	ACPI_MEMSET(table_desc, 0, sizeof(struct acpi_table_desc));
	table_desc->address = address;
	table_desc->length = table->length;
	table_desc->flags = flags;
	ACPI_MOVE_32_TO_32(table_desc->signature.ascii, table->signature);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_acquire_temporal_table
 *
 * PARAMETERS:  table_desc          - Table descriptor to be acquired
 *              address             - Address of the table
 *              flags               - Allocation flags of the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function validates the table header to obtain the length
 *              of a table and fills the table descriptor to make its state as
 *              "INSTALLED".  Such table descriptor is only used for verified
 *              installation.
 *
 ******************************************************************************/

static acpi_status
acpi_tb_acquire_temporal_table(struct acpi_table_desc *table_desc,
			       acpi_physical_address address, u8 flags)
{
	struct acpi_table_header *table_header;

	switch (flags & ACPI_TABLE_ORIGIN_MASK) {
	case ACPI_TABLE_ORIGIN_INTERN_PHYSICAL:

		/* Try to obtain the length of the table */

		table_header =
		    acpi_os_map_memory(address,
				       sizeof(struct acpi_table_header));
		if (!table_header) {
			return (AE_NO_MEMORY);
		}
		acpi_tb_install_table(table_desc, address, flags, table_header);
		acpi_os_unmap_memory(table_header,
				     sizeof(struct acpi_table_header));
		return (AE_OK);

	case ACPI_TABLE_ORIGIN_INTERN_VIRTUAL:
	case ACPI_TABLE_ORIGIN_EXTERN_VIRTUAL:

		table_header = ACPI_CAST_PTR(struct acpi_table_header, address);
		if (!table_header) {
			return (AE_NO_MEMORY);
		}
		acpi_tb_install_table(table_desc, address, flags, table_header);
		return (AE_OK);

	default:

		break;
	}

	/* Table is not valid yet */

	return (AE_NO_MEMORY);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_release_temporal_table
 *
 * PARAMETERS:  table_desc          - Table descriptor to be released
 *
 * RETURN:      Status
 *
 * DESCRIPTION: The reversal of acpi_tb_acquire_temporal_table().
 *
 ******************************************************************************/

static void acpi_tb_release_temporal_table(struct acpi_table_desc *table_desc)
{
	/*
	 * Note that the .Address is maintained by the callers of
	 * acpi_tb_acquire_temporal_table(), thus do not invoke acpi_tb_uninstall_table()
	 * where .Address will be freed.
	 */
	acpi_tb_invalidate_table(table_desc);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_and_override_table
 *
 * PARAMETERS:  table_index             - Index into root table array
 *              new_table_desc          - New table descriptor to install
 *
 * RETURN:      None
 *
 * DESCRIPTION: Install an ACPI table into the global data structure. The
 *              table override mechanism is called to allow the host
 *              OS to replace any table before it is installed in the root
 *              table array.
 *
 ******************************************************************************/

void
acpi_tb_install_and_override_table(u32 table_index,
				   struct acpi_table_desc *new_table_desc)
{
	if (table_index >= acpi_gbl_root_table_list.current_table_count) {
		return;
	}

	/*
	 * ACPI Table Override:
	 *
	 * Before we install the table, let the host OS override it with a new
	 * one if desired. Any table within the RSDT/XSDT can be replaced,
	 * including the DSDT which is pointed to by the FADT.
	 */
	acpi_tb_override_table(new_table_desc);

	acpi_tb_install_table(&acpi_gbl_root_table_list.tables[table_index],
			      new_table_desc->address, new_table_desc->flags,
			      new_table_desc->pointer);

	acpi_tb_print_table_header(new_table_desc->address,
				   new_table_desc->pointer);

	/* Set the global integer width (based upon revision of the DSDT) */

	if (table_index == ACPI_TABLE_INDEX_DSDT) {
		acpi_ut_set_integer_width(new_table_desc->pointer->revision);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_fixed_table
 *
 * PARAMETERS:  address                 - Physical address of DSDT or FACS
 *              signature               - Table signature, NULL if no need to
 *                                        match
 *              table_index             - Index into root table array
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a fixed ACPI table (DSDT/FACS) into the global data
 *              structure.
 *
 ******************************************************************************/

acpi_status
acpi_tb_install_fixed_table(acpi_physical_address address,
			    char *signature, u32 table_index)
{
	struct acpi_table_desc new_table_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE(tb_install_fixed_table);

	if (!address) {
		ACPI_ERROR((AE_INFO,
			    "Null physical address for ACPI table [%s]",
			    signature));
		return (AE_NO_MEMORY);
	}

	/* Fill a table descriptor for validation */

	status = acpi_tb_acquire_temporal_table(&new_table_desc, address,
						ACPI_TABLE_ORIGIN_INTERN_PHYSICAL);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO, "Could not acquire table length at %p",
			    ACPI_CAST_PTR(void, address)));
		return_ACPI_STATUS(status);
	}

	/* Validate and verify a table before installation */

	status = acpi_tb_verify_table(&new_table_desc, signature);
	if (ACPI_FAILURE(status)) {
		goto release_and_exit;
	}

	acpi_tb_install_and_override_table(table_index, &new_table_desc);

release_and_exit:

	/* Release the temporal table descriptor */

	acpi_tb_release_temporal_table(&new_table_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_is_equivalent_table
 *
 * PARAMETERS:  table_desc          - Table 1 descriptor to be compared
 *              table_index         - Index of table 2 to be compared
 *
 * RETURN:      TRUE if 2 tables are equivalent
 *
 * DESCRIPTION: This function is called to compare a table with what have
 *              already been installed in the root table list.
 *
 ******************************************************************************/

static u8
acpi_tb_is_equivalent_table(struct acpi_table_desc *table_desc, u32 table_index)
{
	acpi_status status = AE_OK;
	u8 is_equivalent;
	struct acpi_table_header *table;
	u32 table_length;
	u8 table_flags;

	status =
	    acpi_tb_acquire_table(&acpi_gbl_root_table_list.tables[table_index],
				  &table, &table_length, &table_flags);
	if (ACPI_FAILURE(status)) {
		return (FALSE);
	}

	/*
	 * Check for a table match on the entire table length,
	 * not just the header.
	 */
	is_equivalent = (u8)((table_desc->length != table_length ||
			      ACPI_MEMCMP(table_desc->pointer, table,
					  table_length)) ? FALSE : TRUE);

	/* Release the acquired table */

	acpi_tb_release_table(table, table_length, table_flags);

	return (is_equivalent);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_non_fixed_table
 *
 * PARAMETERS:  address             - Address of the table (might be a virtual
 *                                    address depending on the table_flags)
 *              flags               - Flags for the table
 *              reload              - Whether reload should be performed
 *              table_index         - Where the table index is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to install an ACPI table that is
 *              neither DSDT nor FACS.
 *              When this function is called by "Load" or "LoadTable" opcodes,
 *              or by acpi_load_table() API, the "Reload" parameter is set.
 *              After sucessfully returning from this function, table is
 *              "INSTALLED" but not "VALIDATED".
 *
 ******************************************************************************/

acpi_status
acpi_tb_install_non_fixed_table(acpi_physical_address address,
				u8 flags, u8 reload, u32 *table_index)
{
	u32 i;
	acpi_status status = AE_OK;
	struct acpi_table_desc new_table_desc;

	ACPI_FUNCTION_TRACE(tb_install_non_fixed_table);

	/* Acquire a temporal table descriptor for validation */

	status =
	    acpi_tb_acquire_temporal_table(&new_table_desc, address, flags);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO, "Could not acquire table length at %p",
			    ACPI_CAST_PTR(void, address)));
		return_ACPI_STATUS(status);
	}

	/* Validate and verify a table before installation */

	status = acpi_tb_verify_table(&new_table_desc, NULL);
	if (ACPI_FAILURE(status)) {
		goto release_and_exit;
	}

	if (reload) {
		/*
		 * Validate the incoming table signature.
		 *
		 * 1) Originally, we checked the table signature for "SSDT" or "PSDT".
		 * 2) We added support for OEMx tables, signature "OEM".
		 * 3) Valid tables were encountered with a null signature, so we just
		 *    gave up on validating the signature, (05/2008).
		 * 4) We encountered non-AML tables such as the MADT, which caused
		 *    interpreter errors and kernel faults. So now, we once again allow
		 *    only "SSDT", "OEMx", and now, also a null signature. (05/2011).
		 */
		if ((new_table_desc.signature.ascii[0] != 0x00) &&
		    (!ACPI_COMPARE_NAME
		     (&new_table_desc.signature, ACPI_SIG_SSDT))
		    && (ACPI_STRNCMP(new_table_desc.signature.ascii, "OEM", 3)))
		{
			ACPI_BIOS_ERROR((AE_INFO,
					 "Table has invalid signature [%4.4s] (0x%8.8X), "
					 "must be SSDT or OEMx",
					 acpi_ut_valid_acpi_name(new_table_desc.
								 signature.
								 ascii) ?
					 new_table_desc.signature.
					 ascii : "????",
					 new_table_desc.signature.integer));

			status = AE_BAD_SIGNATURE;
			goto release_and_exit;
		}

		/* Check if table is already registered */

		for (i = 0; i < acpi_gbl_root_table_list.current_table_count;
		     ++i) {
			/*
			 * Check for a table match on the entire table length,
			 * not just the header.
			 */
			if (!acpi_tb_is_equivalent_table(&new_table_desc, i)) {
				continue;
			}

			/*
			 * Note: the current mechanism does not unregister a table if it is
			 * dynamically unloaded. The related namespace entries are deleted,
			 * but the table remains in the root table list.
			 *
			 * The assumption here is that the number of different tables that
			 * will be loaded is actually small, and there is minimal overhead
			 * in just keeping the table in case it is needed again.
			 *
			 * If this assumption changes in the future (perhaps on large
			 * machines with many table load/unload operations), tables will
			 * need to be unregistered when they are unloaded, and slots in the
			 * root table list should be reused when empty.
			 */
			if (acpi_gbl_root_table_list.tables[i].
			    flags & ACPI_TABLE_IS_LOADED) {

				/* Table is still loaded, this is an error */

				status = AE_ALREADY_EXISTS;
				goto release_and_exit;
			} else {
				/*
				 * Table was unloaded, allow it to be reloaded.
				 * As we are going to return AE_OK to the caller, we should
				 * take the responsibility of freeing the input descriptor.
				 * Refill the input descriptor to ensure
				 * acpi_tb_install_and_override_table() can be called again to
				 * indicate the re-installation.
				 */
				acpi_tb_uninstall_table(&new_table_desc);
				*table_index = i;
				(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
				return_ACPI_STATUS(AE_OK);
			}
		}
	}

	/* Add the table to the global root table list */

	status = acpi_tb_acquire_root_table_entry(&i);
	if (ACPI_FAILURE(status)) {
		goto release_and_exit;
	}
	*table_index = i;
	acpi_tb_install_and_override_table(i, &new_table_desc);

release_and_exit:

	/* Release the temporal table descriptor */

	acpi_tb_release_temporal_table(&new_table_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_override_table
 *
 * PARAMETERS:  old_table_desc      - Validated table descriptor to be
 *                                    overridden
 *
 * RETURN:      None
 *
 * DESCRIPTION: Attempt table override by calling the OSL override functions.
 *              Note: If the table is overridden, then the entire new table
 *              is acquired and returned by this function.
 *              Before/after invocation, the table descriptor is in a state
 *              that is "VALIDATED".
 *
 ******************************************************************************/

void acpi_tb_override_table(struct acpi_table_desc *old_table_desc)
{
	acpi_status status;
	char *override_type;
	struct acpi_table_desc new_table_desc;
	struct acpi_table_header *table;
	acpi_physical_address address;
	u32 length;

	/* (1) Attempt logical override (returns a logical address) */

	status = acpi_os_table_override(old_table_desc->pointer, &table);
	if (ACPI_SUCCESS(status) && table) {
		acpi_tb_acquire_temporal_table(&new_table_desc,
					       ACPI_PTR_TO_PHYSADDR(table),
					       ACPI_TABLE_ORIGIN_EXTERN_VIRTUAL);
		override_type = "Logical";
		goto finish_override;
	}

	/* (2) Attempt physical override (returns a physical address) */

	status = acpi_os_physical_table_override(old_table_desc->pointer,
						 &address, &length);
	if (ACPI_SUCCESS(status) && address && length) {
		acpi_tb_acquire_temporal_table(&new_table_desc, address,
					       ACPI_TABLE_ORIGIN_INTERN_PHYSICAL);
		override_type = "Physical";
		goto finish_override;
	}

	return;			/* There was no override */

finish_override:

	/* Validate and verify a table before overriding */

	status = acpi_tb_verify_table(&new_table_desc, NULL);
	if (ACPI_FAILURE(status)) {
		return;
	}

	ACPI_INFO((AE_INFO, "%4.4s " ACPI_PRINTF_UINT
		   " %s table override, new table: " ACPI_PRINTF_UINT,
		   old_table_desc->signature.ascii,
		   ACPI_FORMAT_TO_UINT(old_table_desc->address),
		   override_type, ACPI_FORMAT_TO_UINT(new_table_desc.address)));

	/* We can now uninstall the original table */

	acpi_tb_uninstall_table(old_table_desc);

	/*
	 * Replace the original table descriptor and keep its state as
	 * "VALIDATED".
	 */
	acpi_tb_install_table(old_table_desc, new_table_desc.address,
			      new_table_desc.flags, new_table_desc.pointer);
	acpi_tb_validate_table(old_table_desc);

	/* Release the temporal table descriptor */

	acpi_tb_release_temporal_table(&new_table_desc);
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

	tables = ACPI_ALLOCATE_ZEROED(((acpi_size) table_count +
				       ACPI_ROOT_TABLE_SIZE_INCREMENT) *
				      sizeof(struct acpi_table_desc));
	if (!tables) {
		ACPI_ERROR((AE_INFO,
			    "Could not allocate new root table array"));
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Copy and free the previous table array */

	if (acpi_gbl_root_table_list.tables) {
		ACPI_MEMCPY(tables, acpi_gbl_root_table_list.tables,
			    (acpi_size) table_count *
			    sizeof(struct acpi_table_desc));

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
 * FUNCTION:    acpi_tb_acquire_root_table_entry
 *
 * PARAMETERS:  table_index         - Where table index is returned
 *
 * RETURN:      Status and table index.
 *
 * DESCRIPTION: Allocate a new ACPI table entry to the global table list
 *
 ******************************************************************************/

static acpi_status acpi_tb_acquire_root_table_entry(u32 *table_index)
{
	acpi_status status;

	/* Ensure that there is room for the table in the Root Table List */

	if (acpi_gbl_root_table_list.current_table_count >=
	    acpi_gbl_root_table_list.max_table_count) {
		status = acpi_tb_resize_root_table_list();
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	*table_index = acpi_gbl_root_table_list.current_table_count;
	acpi_gbl_root_table_list.current_table_count++;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_store_table
 *
 * PARAMETERS:  address             - Table address
 *              table               - Table header
 *              length              - Table length
 *              flags               - flags
 *
 * RETURN:      Status and table index.
 *
 * DESCRIPTION: Add an ACPI table to the global table list
 *
 ******************************************************************************/

acpi_status
acpi_tb_store_table(acpi_physical_address address,
		    struct acpi_table_header * table,
		    u32 length, u8 flags, u32 *table_index)
{
	acpi_status status;
	struct acpi_table_desc *table_desc;

	status = acpi_tb_acquire_root_table_entry(table_index);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Initialize added table */

	table_desc = &acpi_gbl_root_table_list.tables[*table_index];
	acpi_tb_install_table(table_desc, address, flags, table);
	table_desc->pointer = table;

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_uninstall_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete one internal ACPI table
 *
 ******************************************************************************/

void acpi_tb_uninstall_table(struct acpi_table_desc *table_desc)
{

	ACPI_FUNCTION_TRACE(tb_uninstall_table);

	/* Table must be installed */

	if (!table_desc->address) {
		return_VOID;
	}

	acpi_tb_invalidate_table(table_desc);

	if ((table_desc->flags & ACPI_TABLE_ORIGIN_MASK) ==
	    ACPI_TABLE_ORIGIN_INTERN_VIRTUAL) {
		ACPI_FREE(ACPI_CAST_PTR(void, table_desc->address));
	}

	table_desc->address = ACPI_PTR_TO_PHYSADDR(NULL);

	return_VOID;
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
	(void)acpi_ut_release_mutex(ACPI_MTX_INTERPRETER);
	status = acpi_ut_acquire_write_lock(&acpi_gbl_namespace_rw_lock);

	acpi_ns_delete_namespace_by_owner(owner_id);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_ut_release_write_lock(&acpi_gbl_namespace_rw_lock);

	status = acpi_ut_acquire_mutex(ACPI_MTX_INTERPRETER);
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
		status = acpi_ut_allocate_owner_id
		    (&(acpi_gbl_root_table_list.tables[table_index].owner_id));
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

acpi_status acpi_tb_get_owner_id(u32 table_index, acpi_owner_id * owner_id)
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
 * PARAMETERS:  table_index         - Table index
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
