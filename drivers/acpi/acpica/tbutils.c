// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: tbutils - ACPI Table utilities
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "actables.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbutils")

/* Local prototypes */
static acpi_physical_address
acpi_tb_get_root_table_entry(u8 *table_entry, u32 table_entry_size);

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_initialize_facs
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a permanent mapping for the FADT and save it in a global
 *              for accessing the Global Lock and Firmware Waking Vector
 *
 ******************************************************************************/

acpi_status acpi_tb_initialize_facs(void)
{
	struct acpi_table_facs *facs;

	/* If Hardware Reduced flag is set, there is no FACS */

	if (acpi_gbl_reduced_hardware) {
		acpi_gbl_FACS = NULL;
		return (AE_OK);
	} else if (acpi_gbl_FADT.Xfacs &&
		   (!acpi_gbl_FADT.facs
		    || !acpi_gbl_use32_bit_facs_addresses)) {
		(void)acpi_get_table_by_index(acpi_gbl_xfacs_index,
					      ACPI_CAST_INDIRECT_PTR(struct
								     acpi_table_header,
								     &facs));
		acpi_gbl_FACS = facs;
	} else if (acpi_gbl_FADT.facs) {
		(void)acpi_get_table_by_index(acpi_gbl_facs_index,
					      ACPI_CAST_INDIRECT_PTR(struct
								     acpi_table_header,
								     &facs));
		acpi_gbl_FACS = facs;
	}

	/* If there is no FACS, just continue. There was already an error msg */

	return (AE_OK);
}
#endif				/* !ACPI_REDUCED_HARDWARE */

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_check_dsdt_header
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Quick compare to check validity of the DSDT. This will detect
 *              if the DSDT has been replaced from outside the OS and/or if
 *              the DSDT header has been corrupted.
 *
 ******************************************************************************/

void acpi_tb_check_dsdt_header(void)
{

	/* Compare original length and checksum to current values */

	if (acpi_gbl_original_dsdt_header.length != acpi_gbl_DSDT->length ||
	    acpi_gbl_original_dsdt_header.checksum != acpi_gbl_DSDT->checksum) {
		ACPI_BIOS_ERROR((AE_INFO,
				 "The DSDT has been corrupted or replaced - "
				 "old, new headers below"));

		acpi_tb_print_table_header(0, &acpi_gbl_original_dsdt_header);
		acpi_tb_print_table_header(0, acpi_gbl_DSDT);

		ACPI_ERROR((AE_INFO,
			    "Please send DMI info to linux-acpi@vger.kernel.org\n"
			    "If system does not work as expected, please boot with acpi=copy_dsdt"));

		/* Disable further error messages */

		acpi_gbl_original_dsdt_header.length = acpi_gbl_DSDT->length;
		acpi_gbl_original_dsdt_header.checksum =
		    acpi_gbl_DSDT->checksum;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_copy_dsdt
 *
 * PARAMETERS:  table_index         - Index of installed table to copy
 *
 * RETURN:      The copied DSDT
 *
 * DESCRIPTION: Implements a subsystem option to copy the DSDT to local memory.
 *              Some very bad BIOSs are known to either corrupt the DSDT or
 *              install a new, bad DSDT. This copy works around the problem.
 *
 ******************************************************************************/

struct acpi_table_header *acpi_tb_copy_dsdt(u32 table_index)
{
	struct acpi_table_header *new_table;
	struct acpi_table_desc *table_desc;

	table_desc = &acpi_gbl_root_table_list.tables[table_index];

	new_table = ACPI_ALLOCATE(table_desc->length);
	if (!new_table) {
		ACPI_ERROR((AE_INFO, "Could not copy DSDT of length 0x%X",
			    table_desc->length));
		return (NULL);
	}

	memcpy(new_table, table_desc->pointer, table_desc->length);
	acpi_tb_uninstall_table(table_desc);

	acpi_tb_init_table_descriptor(&acpi_gbl_root_table_list.
				      tables[acpi_gbl_dsdt_index],
				      ACPI_PTR_TO_PHYSADDR(new_table),
				      ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL,
				      new_table);

	ACPI_INFO(("Forced DSDT copy: length 0x%05X copied locally, original unmapped", new_table->length));

	return (new_table);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_root_table_entry
 *
 * PARAMETERS:  table_entry         - Pointer to the RSDT/XSDT table entry
 *              table_entry_size    - sizeof 32 or 64 (RSDT or XSDT)
 *
 * RETURN:      Physical address extracted from the root table
 *
 * DESCRIPTION: Get one root table entry. Handles 32-bit and 64-bit cases on
 *              both 32-bit and 64-bit platforms
 *
 * NOTE:        acpi_physical_address is 32-bit on 32-bit platforms, 64-bit on
 *              64-bit platforms.
 *
 ******************************************************************************/

static acpi_physical_address
acpi_tb_get_root_table_entry(u8 *table_entry, u32 table_entry_size)
{
	u64 address64;

	/*
	 * Get the table physical address (32-bit for RSDT, 64-bit for XSDT):
	 * Note: Addresses are 32-bit aligned (not 64) in both RSDT and XSDT
	 */
	if (table_entry_size == ACPI_RSDT_ENTRY_SIZE) {
		/*
		 * 32-bit platform, RSDT: Return 32-bit table entry
		 * 64-bit platform, RSDT: Expand 32-bit to 64-bit and return
		 */
		return ((acpi_physical_address)
			(*ACPI_CAST_PTR(u32, table_entry)));
	} else {
		/*
		 * 32-bit platform, XSDT: Truncate 64-bit to 32-bit and return
		 * 64-bit platform, XSDT: Move (unaligned) 64-bit to local,
		 *  return 64-bit
		 */
		ACPI_MOVE_64_TO_64(&address64, table_entry);

#if ACPI_MACHINE_WIDTH == 32
		if (address64 > ACPI_UINT32_MAX) {

			/* Will truncate 64-bit address to 32 bits, issue warning */

			ACPI_BIOS_WARNING((AE_INFO,
					   "64-bit Physical Address in XSDT is too large (0x%8.8X%8.8X),"
					   " truncating",
					   ACPI_FORMAT_UINT64(address64)));
		}
#endif
		return ((acpi_physical_address)(address64));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_parse_root_table
 *
 * PARAMETERS:  rsdp_address        - Pointer to the RSDP
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to parse the Root System Description
 *              Table (RSDT or XSDT)
 *
 * NOTE:        Tables are mapped (not copied) for efficiency. The FACS must
 *              be mapped and cannot be copied because it contains the actual
 *              memory location of the ACPI Global Lock.
 *
 ******************************************************************************/

acpi_status ACPI_INIT_FUNCTION
acpi_tb_parse_root_table(acpi_physical_address rsdp_address)
{
	struct acpi_table_rsdp *rsdp;
	u32 table_entry_size;
	u32 i;
	u32 table_count;
	struct acpi_table_header *table;
	acpi_physical_address address;
	u32 length;
	u8 *table_entry;
	acpi_status status;
	u32 table_index;

	ACPI_FUNCTION_TRACE(tb_parse_root_table);

	/* Map the entire RSDP and extract the address of the RSDT or XSDT */

	rsdp = acpi_os_map_memory(rsdp_address, sizeof(struct acpi_table_rsdp));
	if (!rsdp) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	acpi_tb_print_table_header(rsdp_address,
				   ACPI_CAST_PTR(struct acpi_table_header,
						 rsdp));

	/* Use XSDT if present and not overridden. Otherwise, use RSDT */

	if ((rsdp->revision > 1) &&
	    rsdp->xsdt_physical_address && !acpi_gbl_do_not_use_xsdt) {
		/*
		 * RSDP contains an XSDT (64-bit physical addresses). We must use
		 * the XSDT if the revision is > 1 and the XSDT pointer is present,
		 * as per the ACPI specification.
		 */
		address = (acpi_physical_address)rsdp->xsdt_physical_address;
		table_entry_size = ACPI_XSDT_ENTRY_SIZE;
	} else {
		/* Root table is an RSDT (32-bit physical addresses) */

		address = (acpi_physical_address)rsdp->rsdt_physical_address;
		table_entry_size = ACPI_RSDT_ENTRY_SIZE;
	}

	/*
	 * It is not possible to map more than one entry in some environments,
	 * so unmap the RSDP here before mapping other tables
	 */
	acpi_os_unmap_memory(rsdp, sizeof(struct acpi_table_rsdp));

	/* Map the RSDT/XSDT table header to get the full table length */

	table = acpi_os_map_memory(address, sizeof(struct acpi_table_header));
	if (!table) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	acpi_tb_print_table_header(address, table);

	/*
	 * Validate length of the table, and map entire table.
	 * Minimum length table must contain at least one entry.
	 */
	length = table->length;
	acpi_os_unmap_memory(table, sizeof(struct acpi_table_header));

	if (length < (sizeof(struct acpi_table_header) + table_entry_size)) {
		ACPI_BIOS_ERROR((AE_INFO,
				 "Invalid table length 0x%X in RSDT/XSDT",
				 length));
		return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
	}

	table = acpi_os_map_memory(address, length);
	if (!table) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Validate the root table checksum */

	status = acpi_tb_verify_checksum(table, length);
	if (ACPI_FAILURE(status)) {
		acpi_os_unmap_memory(table, length);
		return_ACPI_STATUS(status);
	}

	/* Get the number of entries and pointer to first entry */

	table_count = (u32)((table->length - sizeof(struct acpi_table_header)) /
			    table_entry_size);
	table_entry = ACPI_ADD_PTR(u8, table, sizeof(struct acpi_table_header));

	/* Initialize the root table array from the RSDT/XSDT */

	for (i = 0; i < table_count; i++) {

		/* Get the table physical address (32-bit for RSDT, 64-bit for XSDT) */

		address =
		    acpi_tb_get_root_table_entry(table_entry, table_entry_size);

		/* Skip NULL entries in RSDT/XSDT */

		if (!address) {
			goto next_table;
		}

		status = acpi_tb_install_standard_table(address,
							ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL,
							FALSE, TRUE,
							&table_index);

		if (ACPI_SUCCESS(status) &&
		    ACPI_COMPARE_NAMESEG(&acpi_gbl_root_table_list.
					 tables[table_index].signature,
					 ACPI_SIG_FADT)) {
			acpi_gbl_fadt_index = table_index;
			acpi_tb_parse_fadt();
		}

next_table:

		table_entry += table_entry_size;
	}

	acpi_os_unmap_memory(table, length);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *              out_table           - Where the pointer to the table is returned
 *
 * RETURN:      Status and pointer to the requested table
 *
 * DESCRIPTION: Increase a reference to a table descriptor and return the
 *              validated table pointer.
 *              If the table descriptor is an entry of the root table list,
 *              this API must be invoked with ACPI_MTX_TABLES acquired.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table(struct acpi_table_desc *table_desc,
		  struct acpi_table_header **out_table)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_tb_get_table);

	if (table_desc->validation_count == 0) {

		/* Table need to be "VALIDATED" */

		status = acpi_tb_validate_table(table_desc);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	if (table_desc->validation_count < ACPI_MAX_TABLE_VALIDATIONS) {
		table_desc->validation_count++;

		/*
		 * Detect validation_count overflows to ensure that the warning
		 * message will only be printed once.
		 */
		if (table_desc->validation_count >= ACPI_MAX_TABLE_VALIDATIONS) {
			ACPI_WARNING((AE_INFO,
				      "Table %p, Validation count overflows\n",
				      table_desc));
		}
	}

	*out_table = table_desc->pointer;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_put_table
 *
 * PARAMETERS:  table_desc          - Table descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decrease a reference to a table descriptor and release the
 *              validated table pointer if no references.
 *              If the table descriptor is an entry of the root table list,
 *              this API must be invoked with ACPI_MTX_TABLES acquired.
 *
 ******************************************************************************/

void acpi_tb_put_table(struct acpi_table_desc *table_desc)
{

	ACPI_FUNCTION_TRACE(acpi_tb_put_table);

	if (table_desc->validation_count < ACPI_MAX_TABLE_VALIDATIONS) {
		table_desc->validation_count--;

		/*
		 * Detect validation_count underflows to ensure that the warning
		 * message will only be printed once.
		 */
		if (table_desc->validation_count >= ACPI_MAX_TABLE_VALIDATIONS) {
			ACPI_WARNING((AE_INFO,
				      "Table %p, Validation count underflows\n",
				      table_desc));
			return_VOID;
		}
	}

	if (table_desc->validation_count == 0) {

		/* Table need to be "INVALIDATED" */

		acpi_tb_invalidate_table(table_desc);
	}

	return_VOID;
}
