/******************************************************************************
 *
 * Module Name: tbutils   - table utilities
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
ACPI_MODULE_NAME("tbutils")

/* Local prototypes */
static void acpi_tb_parse_fadt(struct acpi_table_fadt *fadt, u8 flags);

static void inline
acpi_tb_init_generic_address(struct acpi_generic_address *new_gas_struct,
			     u8 bit_width, acpi_physical_address address);

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_print_table_header
 *
 * PARAMETERS:  Address             - Table physical address
 *              Header              - Table header
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print an ACPI table header
 *
 ******************************************************************************/

void
acpi_tb_print_table_header(acpi_physical_address address,
			   struct acpi_table_header *header)
{

	ACPI_INFO((AE_INFO,
		   "%4.4s @ 0x%p Length 0x%04X (v%3.3d %6.6s %8.8s 0x%08X %4.4s 0x%08X)",
		   header->signature, ACPI_CAST_PTR(void, address),
		   header->length, header->revision, header->oem_id,
		   header->oem_table_id, header->oem_revision,
		   header->asl_compiler_id, header->asl_compiler_revision));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_init_generic_address
 *
 * PARAMETERS:  new_gas_struct      - GAS struct to be initialized
 *              bit_width           - Width of this register
 *              Address             - Address of the register
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a GAS structure.
 *
 ******************************************************************************/

static void inline
acpi_tb_init_generic_address(struct acpi_generic_address *new_gas_struct,
			     u8 bit_width, acpi_physical_address address)
{

	ACPI_STORE_ADDRESS(new_gas_struct->address, address);
	new_gas_struct->space_id = ACPI_ADR_SPACE_SYSTEM_IO;
	new_gas_struct->bit_width = bit_width;
	new_gas_struct->bit_offset = 0;
	new_gas_struct->access_width = 0;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_checksum
 *
 * PARAMETERS:  Buffer          - Pointer to memory region to be checked
 *              Length          - Length of this memory region
 *
 * RETURN:      Checksum (u8)
 *
 * DESCRIPTION: Calculates circular checksum of memory region.
 *
 ******************************************************************************/

u8 acpi_tb_checksum(u8 * buffer, acpi_native_uint length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end) {
		sum = (u8) (sum + *(buffer++));
	}

	return sum;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_fadt
 *
 * PARAMETERS:  Fadt                - FADT table to be converted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Converts a BIOS supplied ACPI 1.0 FADT to a local
 *              ACPI 2.0 FADT. If the BIOS supplied a 2.0 FADT then it is simply
 *              copied to the local FADT.  The ACPI CA software uses this
 *              local FADT. Thus a significant amount of special #ifdef
 *              type codeing is saved.
 *
 ******************************************************************************/

void acpi_tb_convert_fadt(struct acpi_table_fadt *fadt)
{

	/*
	 * Convert table pointers to 64-bit fields
	 */
	if (!acpi_gbl_FADT.Xfacs) {
		acpi_gbl_FADT.Xfacs = (u64) acpi_gbl_FADT.facs;
	}

	if (!acpi_gbl_FADT.Xdsdt) {
		acpi_gbl_FADT.Xdsdt = (u64) acpi_gbl_FADT.dsdt;
	}

	/*
	 * Convert the V1.0 block addresses to V2.0 GAS structures
	 */
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xpm1a_event_block,
				     acpi_gbl_FADT.pm1_event_length,
				     (acpi_physical_address) acpi_gbl_FADT.
				     pm1a_event_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xpm1b_event_block,
				     acpi_gbl_FADT.pm1_event_length,
				     (acpi_physical_address) acpi_gbl_FADT.
				     pm1b_event_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xpm1a_control_block,
				     acpi_gbl_FADT.pm1_control_length,
				     (acpi_physical_address) acpi_gbl_FADT.
				     pm1a_control_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xpm1b_control_block,
				     acpi_gbl_FADT.pm1_control_length,
				     (acpi_physical_address) acpi_gbl_FADT.
				     pm1b_control_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xpm2_control_block,
				     acpi_gbl_FADT.pm2_control_length,
				     (acpi_physical_address) acpi_gbl_FADT.
				     pm2_control_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xpm_timer_block,
				     acpi_gbl_FADT.pm_timer_length,
				     (acpi_physical_address) acpi_gbl_FADT.
				     pm_timer_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xgpe0_block, 0,
				     (acpi_physical_address) acpi_gbl_FADT.
				     gpe0_block);
	acpi_tb_init_generic_address(&acpi_gbl_FADT.xgpe1_block, 0,
				     (acpi_physical_address) acpi_gbl_FADT.
				     gpe1_block);

	/*
	 * Create separate GAS structs for the PM1 Enable registers
	 */
	acpi_tb_init_generic_address(&acpi_gbl_xpm1a_enable,
				     (u8) ACPI_DIV_2(acpi_gbl_FADT.
						     pm1_event_length),
				     (acpi_physical_address)
				     (acpi_gbl_FADT.xpm1a_event_block.address +
				      ACPI_DIV_2(acpi_gbl_FADT.
						 pm1_event_length)));

	/*
	 * PM1B is optional; leave null if not present
	 */
	if (acpi_gbl_FADT.xpm1b_event_block.address) {
		acpi_tb_init_generic_address(&acpi_gbl_xpm1b_enable,
					     (u8) ACPI_DIV_2(acpi_gbl_FADT.
							     pm1_event_length),
					     (acpi_physical_address)
					     (acpi_gbl_FADT.xpm1b_event_block.
					      address +
					      ACPI_DIV_2(acpi_gbl_FADT.
							 pm1_event_length)));
	}

	/* Global FADT is the new common V2.0 FADT  */

	acpi_gbl_FADT.header.length = sizeof(struct acpi_table_fadt);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_parse_fadt
 *
 * PARAMETERS:  Fadt                - Pointer to FADT table
 *              Flags               - Flags
 *
 * RETURN:      none
 *
 * DESCRIPTION: This function is called to initialise the FADT, DSDT and FACS
 *              tables (FADT contains the addresses of the DSDT and FACS)
 *
 ******************************************************************************/

static void acpi_tb_parse_fadt(struct acpi_table_fadt *fadt, u8 flags)
{
	acpi_physical_address dsdt_address =
	    (acpi_physical_address) fadt->Xdsdt;
	acpi_physical_address facs_address =
	    (acpi_physical_address) fadt->Xfacs;
	struct acpi_table_header *table;

	if (!dsdt_address) {
		goto no_dsdt;
	}

	table =
	    acpi_os_map_memory(dsdt_address, sizeof(struct acpi_table_header));
	if (!table) {
		goto no_dsdt;
	}

	/* Initialize the DSDT table */

	ACPI_MOVE_32_TO_32(&
			   (acpi_gbl_root_table_list.
			    tables[ACPI_TABLE_INDEX_DSDT].signature),
			   ACPI_SIG_DSDT);

	acpi_gbl_root_table_list.tables[ACPI_TABLE_INDEX_DSDT].address =
	    dsdt_address;
	acpi_gbl_root_table_list.tables[ACPI_TABLE_INDEX_DSDT].length =
	    table->length;
	acpi_gbl_root_table_list.tables[ACPI_TABLE_INDEX_DSDT].flags = flags;

	acpi_tb_print_table_header(dsdt_address, table);

	/* Global integer width is based upon revision of the DSDT */

	acpi_ut_set_integer_width(table->revision);
	acpi_os_unmap_memory(table, sizeof(struct acpi_table_header));

      no_dsdt:
	if (!facs_address) {
		return;
	}

	table =
	    acpi_os_map_memory(facs_address, sizeof(struct acpi_table_header));
	if (!table) {
		return;
	}

	/* Initialize the FACS table */

	ACPI_MOVE_32_TO_32(&
			   (acpi_gbl_root_table_list.
			    tables[ACPI_TABLE_INDEX_FACS].signature),
			   ACPI_SIG_FACS);

	acpi_gbl_root_table_list.tables[ACPI_TABLE_INDEX_FACS].address =
	    facs_address;
	acpi_gbl_root_table_list.tables[ACPI_TABLE_INDEX_FACS].length =
	    table->length;
	acpi_gbl_root_table_list.tables[ACPI_TABLE_INDEX_FACS].flags = flags;

	ACPI_INFO((AE_INFO, "%4.4s @ 0x%p",
		   table->signature, ACPI_CAST_PTR(void, facs_address)));

	acpi_os_unmap_memory(table, sizeof(struct acpi_table_header));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_parse_root_table
 *
 * PARAMETERS:  Rsdp                    - Pointer to the RSDP
 *              Flags                   - Flags
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

acpi_status acpi_tb_parse_root_table(struct acpi_table_rsdp *rsdp, u8 flags)
{
	struct acpi_table_header *table;
	acpi_physical_address address;
	u32 length;
	u8 *table_entry;
	acpi_native_uint i;
	acpi_native_uint pointer_size;
	u32 table_count;
	u8 checksum;
	acpi_status status;

	ACPI_FUNCTION_TRACE(tb_parse_root_table);

	/* Differentiate between RSDT and XSDT root tables */

	if (rsdp->revision > 1 && rsdp->xsdt_physical_address) {
		/*
		 * Root table is an XSDT (64-bit physical addresses). We must use the
		 * XSDT if the revision is > 1 and the XSDT pointer is present, as per
		 * the ACPI specification.
		 */
		address = (acpi_native_uint) rsdp->xsdt_physical_address;
		pointer_size = sizeof(u64);
	} else {
		/* Root table is an RSDT (32-bit physical addresses) */

		address = (acpi_native_uint) rsdp->rsdt_physical_address;
		pointer_size = sizeof(u32);
	}

	/* Map the table header to get the full table length */

	table = acpi_os_map_memory(address, sizeof(struct acpi_table_header));
	if (!table) {
		return (AE_NO_MEMORY);
	}

	/* Get the length of the full table, verify length and map entire table */

	length = table->length;
	acpi_os_unmap_memory(table, sizeof(struct acpi_table_header));

	if (length < sizeof(struct acpi_table_header)) {
		ACPI_ERROR((AE_INFO, "Invalid length 0x%X in RSDT/XSDT",
			    length));
		return (AE_INVALID_TABLE_LENGTH);
	}

	table = acpi_os_map_memory(address, length);
	if (!table) {
		return (AE_NO_MEMORY);
	}

	/* Validate the root table checksum */

	checksum = acpi_tb_checksum(ACPI_CAST_PTR(u8, table), length);
#if (ACPI_CHECKSUM_ABORT)

	if (checksum) {
		acpi_os_unmap_memory(table, length);
		return (AE_BAD_CHECKSUM);
	}
#endif

	acpi_tb_print_table_header(address, table);

	/* Calculate the number of tables described in the root table */

	table_count =
	    (table->length - sizeof(struct acpi_table_header)) / pointer_size;

	/* Setup loop */

	table_entry =
	    ACPI_CAST_PTR(u8, table) + sizeof(struct acpi_table_header);
	acpi_gbl_root_table_list.count = 2;

	/*
	 * Initialize the ACPI table entries
	 * First two entries in the table array are reserved for the DSDT and FACS
	 */
	for (i = 0; i < table_count; ++i, table_entry += pointer_size) {

		/* Ensure there is room for another table entry */

		if (acpi_gbl_root_table_list.count >=
		    acpi_gbl_root_table_list.size) {
			status = acpi_tb_resize_root_table_list();
			if (ACPI_FAILURE(status)) {
				ACPI_WARNING((AE_INFO,
					      "Truncating %u table entries!",
					      (unsigned)
					      (acpi_gbl_root_table_list.size -
					       acpi_gbl_root_table_list.
					       count)));
				break;
			}
		}

		/* Get the physical address (32-bit for RSDT, 64-bit for XSDT) */

		if (pointer_size == sizeof(u32)) {
			acpi_gbl_root_table_list.
			    tables[acpi_gbl_root_table_list.count].address =
			    (acpi_physical_address) (*ACPI_CAST_PTR
						     (u32, table_entry));
		} else {
			acpi_gbl_root_table_list.
			    tables[acpi_gbl_root_table_list.count].address =
			    (acpi_physical_address) (*ACPI_CAST_PTR
						     (u64, table_entry));
		}

		acpi_gbl_root_table_list.count++;
	}

	/*
	 * It is not possible to map more than one entry in some environments,
	 * so unmap the root table here before mapping other tables
	 */
	acpi_os_unmap_memory(table, length);

	/* Initialize all tables other than the DSDT and FACS */

	for (i = 2; i < acpi_gbl_root_table_list.count; i++) {
		address = acpi_gbl_root_table_list.tables[i].address;
		length = sizeof(struct acpi_table_header);

		table = acpi_os_map_memory(address, length);
		if (!table) {
			continue;
		}

		acpi_gbl_root_table_list.tables[i].length = table->length;
		acpi_gbl_root_table_list.tables[i].flags = flags;

		ACPI_MOVE_32_TO_32(&
				   (acpi_gbl_root_table_list.tables[i].
				    signature), table->signature);

		acpi_tb_print_table_header(address, table);

		/*
		 * Special case for the FADT because of multiple versions -
		 * get a local copy and convert to common format
		 */
		if (ACPI_COMPARE_NAME(table->signature, ACPI_SIG_FADT)) {
			acpi_os_unmap_memory(table, length);
			length = table->length;

			table = acpi_os_map_memory(address, length);
			if (!table) {
				continue;
			}

			/* Copy the entire FADT locally */

			ACPI_MEMCPY(&acpi_gbl_FADT, table,
				    ACPI_MIN(table->length,
					     sizeof(struct acpi_table_fadt)));

			/* Small table means old revision, convert to new */

			if (table->length < sizeof(struct acpi_table_fadt)) {
				acpi_tb_convert_fadt(ACPI_CAST_PTR
						     (struct acpi_table_fadt,
						      table));
			}

			/* Unmap original FADT */

			acpi_os_unmap_memory(table, length);
			acpi_tb_parse_fadt(&acpi_gbl_FADT, flags);
		} else {
			acpi_os_unmap_memory(table, length);
		}
	}

	return_ACPI_STATUS(AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_tb_map
 *
 * PARAMETERS:  Address             - Address to be mapped
 *              Length              - Length to be mapped
 *              Flags               - Logical or physical addressing mode
 *
 * RETURN:      Pointer to mapped region
 *
 * DESCRIPTION: Maps memory according to flag
 *
 *****************************************************************************/

void *acpi_tb_map(acpi_physical_address address, u32 length, u32 flags)
{

	if (flags == ACPI_TABLE_ORIGIN_MAPPED) {
		return (acpi_os_map_memory(address, length));
	} else {
		return (ACPI_CAST_PTR(void, address));
	}
}

/******************************************************************************
 *
 * FUNCTION:    acpi_tb_unmap
 *
 * PARAMETERS:  Pointer             - To mapped region
 *              Length              - Length to be unmapped
 *              Flags               - Logical or physical addressing mode
 *
 * RETURN:      None
 *
 * DESCRIPTION: Unmaps memory according to flag
 *
 *****************************************************************************/

void acpi_tb_unmap(void *pointer, u32 length, u32 flags)
{

	if (flags == ACPI_TABLE_ORIGIN_MAPPED) {
		acpi_os_unmap_memory(pointer, length);
	}
}
