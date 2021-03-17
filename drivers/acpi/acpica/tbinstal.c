// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: tbinstal - ACPI table installation and removal
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "actables.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbinstal")

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_install_table_with_override
 *
 * PARAMETERS:  new_table_desc          - New table descriptor to install
 *              override                - Whether override should be performed
 *              table_index             - Where the table index is returned
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
acpi_tb_install_table_with_override(struct acpi_table_desc *new_table_desc,
				    u8 override, u32 *table_index)
{
	u32 i;
	acpi_status status;

	status = acpi_tb_get_next_table_descriptor(&i, NULL);
	if (ACPI_FAILURE(status)) {
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

	acpi_tb_init_table_descriptor(&acpi_gbl_root_table_list.tables[i],
				      new_table_desc->address,
				      new_table_desc->flags,
				      new_table_desc->pointer);

	acpi_tb_print_table_header(new_table_desc->address,
				   new_table_desc->pointer);

	/* This synchronizes acpi_gbl_dsdt_index */

	*table_index = i;

	/* Set the global integer width (based upon revision of the DSDT) */

	if (i == acpi_gbl_dsdt_index) {
		acpi_ut_set_integer_width(new_table_desc->pointer->revision);
	}
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
 * DESCRIPTION: This function is called to verify and install an ACPI table.
 *              When this function is called by "Load" or "LoadTable" opcodes,
 *              or by acpi_load_table() API, the "Reload" parameter is set.
 *              After successfully returning from this function, table is
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
	    ACPI_COMPARE_NAMESEG(&new_table_desc.signature, ACPI_SIG_SSDT)) {
		ACPI_INFO(("Ignoring installation of %4.4s at %8.8X%8.8X",
			   new_table_desc.signature.ascii,
			   ACPI_FORMAT_UINT64(address)));
		goto release_and_exit;
	}

	/* Acquire the table lock */

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

	/* Validate and verify a table before installation */

	status = acpi_tb_verify_temp_table(&new_table_desc, NULL, &i);
	if (ACPI_FAILURE(status)) {
		if (status == AE_CTRL_TERMINATE) {
			/*
			 * Table was unloaded, allow it to be reloaded.
			 * As we are going to return AE_OK to the caller, we should
			 * take the responsibility of freeing the input descriptor.
			 * Refill the input descriptor to ensure
			 * acpi_tb_install_table_with_override() can be called again to
			 * indicate the re-installation.
			 */
			acpi_tb_uninstall_table(&new_table_desc);
			(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
			*table_index = i;
			return_ACPI_STATUS(AE_OK);
		}
		goto unlock_and_exit;
	}

	/* Add the table to the global root table list */

	acpi_tb_install_table_with_override(&new_table_desc, override,
					    table_index);

	/* Invoke table handler */

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	acpi_tb_notify_table(ACPI_TABLE_EVENT_INSTALL, new_table_desc.pointer);
	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);

unlock_and_exit:

	/* Release the table lock */

	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);

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
	struct acpi_table_desc new_table_desc;
	struct acpi_table_header *table;
	acpi_physical_address address;
	u32 length;
	ACPI_ERROR_ONLY(char *override_type);

	/* (1) Attempt logical override (returns a logical address) */

	status = acpi_os_table_override(old_table_desc->pointer, &table);
	if (ACPI_SUCCESS(status) && table) {
		acpi_tb_acquire_temp_table(&new_table_desc,
					   ACPI_PTR_TO_PHYSADDR(table),
					   ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL);
		ACPI_ERROR_ONLY(override_type = "Logical");
		goto finish_override;
	}

	/* (2) Attempt physical override (returns a physical address) */

	status = acpi_os_physical_table_override(old_table_desc->pointer,
						 &address, &length);
	if (ACPI_SUCCESS(status) && address && length) {
		acpi_tb_acquire_temp_table(&new_table_desc, address,
					   ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL);
		ACPI_ERROR_ONLY(override_type = "Physical");
		goto finish_override;
	}

	return;			/* There was no override */

finish_override:

	/*
	 * Validate and verify a table before overriding, no nested table
	 * duplication check as it's too complicated and unnecessary.
	 */
	status = acpi_tb_verify_temp_table(&new_table_desc, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		return;
	}

	ACPI_INFO(("%4.4s 0x%8.8X%8.8X"
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
