/******************************************************************************
 *
 * Module Name: tbinstal - ACPI table installation and removal
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "actables.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbinstal")

/* Local prototypes */
static u8
acpi_tb_compare_tables(struct acpi_table_desc *table_desc, u32 table_index);

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_compare_tables
 *
 * PARAMETERS:  table_desc          - Table 1 descriptor to be compared
 *              table_index         - Index of table 2 to be compared
 *
 * RETURN:      TRUE if both tables are identical.
 *
 * DESCRIPTION: This function compares a table with another table that has
 *              already been installed in the root table list.
 *
 ******************************************************************************/

static u8
acpi_tb_compare_tables(struct acpi_table_desc *table_desc, u32 table_index)
{
	acpi_status status = AE_OK;
	u8 is_identical;
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
	is_identical = (u8)((table_desc->length != table_length ||
			     ACPI_MEMCMP(table_desc->pointer, table,
					 table_length)) ? FALSE : TRUE);

	/* Release the acquired table */

	acpi_tb_release_table(table, table_length, table_flags);
	return (is_identical);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_table_with_override
 *
 * PARAMETERS:  table_index             - Index into root table array
 *              new_table_desc          - New table descriptor to install
 *              override                - Whether override should be performed
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
acpi_tb_install_table_with_override(u32 table_index,
				    struct acpi_table_desc *new_table_desc,
				    u8 override)
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
	if (override) {
		acpi_tb_override_table(new_table_desc);
	}

	acpi_tb_init_table_descriptor(&acpi_gbl_root_table_list.
				      tables[table_index],
				      new_table_desc->address,
				      new_table_desc->flags,
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

	status = acpi_tb_acquire_temp_table(&new_table_desc, address,
					    ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO,
			    "Could not acquire table length at %8.8X%8.8X",
			    ACPI_FORMAT_UINT64(address)));
		return_ACPI_STATUS(status);
	}

	/* Validate and verify a table before installation */

	status = acpi_tb_verify_temp_table(&new_table_desc, signature);
	if (ACPI_FAILURE(status)) {
		goto release_and_exit;
	}

	acpi_tb_install_table_with_override(table_index, &new_table_desc, TRUE);

release_and_exit:

	/* Release the temporary table descriptor */

	acpi_tb_release_temp_table(&new_table_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_standard_table
 *
 * PARAMETERS:  address             - Address of the table (might be a virtual
 *                                    address depending on the table_flags)
 *              flags               - Flags for the table
 *              reload              - Whether reload should be performed
 *              override            - Whether override should be performed
 *              table_index         - Where the table index is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to install an ACPI table that is
 *              neither DSDT nor FACS (a "standard" table.)
 *              When this function is called by "Load" or "LoadTable" opcodes,
 *              or by acpi_load_table() API, the "Reload" parameter is set.
 *              After sucessfully returning from this function, table is
 *              "INSTALLED" but not "VALIDATED".
 *
 ******************************************************************************/

acpi_status
acpi_tb_install_standard_table(acpi_physical_address address,
			       u8 flags,
			       u8 reload, u8 override, u32 *table_index)
{
	u32 i;
	acpi_status status = AE_OK;
	struct acpi_table_desc new_table_desc;

	ACPI_FUNCTION_TRACE(tb_install_standard_table);

	/* Acquire a temporary table descriptor for validation */

	status = acpi_tb_acquire_temp_table(&new_table_desc, address, flags);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO,
			    "Could not acquire table length at %8.8X%8.8X",
			    ACPI_FORMAT_UINT64(address)));
		return_ACPI_STATUS(status);
	}

	/*
	 * Optionally do not load any SSDTs from the RSDT/XSDT. This can
	 * be useful for debugging ACPI problems on some machines.
	 */
	if (!reload &&
	    acpi_gbl_disable_ssdt_table_install &&
	    ACPI_COMPARE_NAME(&new_table_desc.signature, ACPI_SIG_SSDT)) {
		ACPI_INFO((AE_INFO,
			   "Ignoring installation of %4.4s at %8.8X%8.8X",
			   new_table_desc.signature.ascii,
			   ACPI_FORMAT_UINT64(address)));
		goto release_and_exit;
	}

	/* Validate and verify a table before installation */

	status = acpi_tb_verify_temp_table(&new_table_desc, NULL);
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
			if (!acpi_tb_compare_tables(&new_table_desc, i)) {
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
				 * acpi_tb_install_table_with_override() can be called again to
				 * indicate the re-installation.
				 */
				acpi_tb_uninstall_table(&new_table_desc);
				*table_index = i;
				return_ACPI_STATUS(AE_OK);
			}
		}
	}

	/* Add the table to the global root table list */

	status = acpi_tb_get_next_table_descriptor(&i, NULL);
	if (ACPI_FAILURE(status)) {
		goto release_and_exit;
	}

	*table_index = i;
	acpi_tb_install_table_with_override(i, &new_table_desc, override);

release_and_exit:

	/* Release the temporary table descriptor */

	acpi_tb_release_temp_table(&new_table_desc);
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
		acpi_tb_acquire_temp_table(&new_table_desc,
					   ACPI_PTR_TO_PHYSADDR(table),
					   ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL);
		override_type = "Logical";
		goto finish_override;
	}

	/* (2) Attempt physical override (returns a physical address) */

	status = acpi_os_physical_table_override(old_table_desc->pointer,
						 &address, &length);
	if (ACPI_SUCCESS(status) && address && length) {
		acpi_tb_acquire_temp_table(&new_table_desc, address,
					   ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL);
		override_type = "Physical";
		goto finish_override;
	}

	return;			/* There was no override */

finish_override:

	/* Validate and verify a table before overriding */

	status = acpi_tb_verify_temp_table(&new_table_desc, NULL);
	if (ACPI_FAILURE(status)) {
		return;
	}

	ACPI_INFO((AE_INFO, "%4.4s 0x%8.8X%8.8X"
		   " %s table override, new table: 0x%8.8X%8.8X",
		   old_table_desc->signature.ascii,
		   ACPI_FORMAT_UINT64(old_table_desc->address),
		   override_type, ACPI_FORMAT_UINT64(new_table_desc.address)));

	/* We can now uninstall the original table */

	acpi_tb_uninstall_table(old_table_desc);

	/*
	 * Replace the original table descriptor and keep its state as
	 * "VALIDATED".
	 */
	acpi_tb_init_table_descriptor(old_table_desc, new_table_desc.address,
				      new_table_desc.flags,
				      new_table_desc.pointer);
	acpi_tb_validate_temp_table(old_table_desc);

	/* Release the temporary table descriptor */

	acpi_tb_release_temp_table(&new_table_desc);
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
	    ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL) {
		ACPI_FREE(ACPI_PHYSADDR_TO_PTR(table_desc->address));
	}

	table_desc->address = ACPI_PTR_TO_PHYSADDR(NULL);
	return_VOID;
}
