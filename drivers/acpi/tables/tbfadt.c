/******************************************************************************
 *
 * Module Name: tbfadt   - FADT table utilities
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2007, R. Byron Moore
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
ACPI_MODULE_NAME("tbfadt")

/* Local prototypes */
static void inline
acpi_tb_init_generic_address(struct acpi_generic_address *generic_address,
			     u8 bit_width, u64 address);

static void acpi_tb_convert_fadt(void);

static void acpi_tb_validate_fadt(void);

/* Table for conversion of FADT to common internal format and FADT validation */

typedef struct acpi_fadt_info {
	char *name;
	u8 target;
	u8 source;
	u8 length;
	u8 type;

} acpi_fadt_info;

#define ACPI_FADT_REQUIRED          1
#define ACPI_FADT_SEPARATE_LENGTH   2

static struct acpi_fadt_info fadt_info_table[] = {
	{"Pm1aEventBlock", ACPI_FADT_OFFSET(xpm1a_event_block),
	 ACPI_FADT_OFFSET(pm1a_event_block),
	 ACPI_FADT_OFFSET(pm1_event_length), ACPI_FADT_REQUIRED},

	{"Pm1bEventBlock", ACPI_FADT_OFFSET(xpm1b_event_block),
	 ACPI_FADT_OFFSET(pm1b_event_block),
	 ACPI_FADT_OFFSET(pm1_event_length), 0},

	{"Pm1aControlBlock", ACPI_FADT_OFFSET(xpm1a_control_block),
	 ACPI_FADT_OFFSET(pm1a_control_block),
	 ACPI_FADT_OFFSET(pm1_control_length), ACPI_FADT_REQUIRED},

	{"Pm1bControlBlock", ACPI_FADT_OFFSET(xpm1b_control_block),
	 ACPI_FADT_OFFSET(pm1b_control_block),
	 ACPI_FADT_OFFSET(pm1_control_length), 0},

	{"Pm2ControlBlock", ACPI_FADT_OFFSET(xpm2_control_block),
	 ACPI_FADT_OFFSET(pm2_control_block),
	 ACPI_FADT_OFFSET(pm2_control_length), ACPI_FADT_SEPARATE_LENGTH},

	{"PmTimerBlock", ACPI_FADT_OFFSET(xpm_timer_block),
	 ACPI_FADT_OFFSET(pm_timer_block),
	 ACPI_FADT_OFFSET(pm_timer_length), ACPI_FADT_REQUIRED},

	{"Gpe0Block", ACPI_FADT_OFFSET(xgpe0_block),
	 ACPI_FADT_OFFSET(gpe0_block),
	 ACPI_FADT_OFFSET(gpe0_block_length), ACPI_FADT_SEPARATE_LENGTH},

	{"Gpe1Block", ACPI_FADT_OFFSET(xgpe1_block),
	 ACPI_FADT_OFFSET(gpe1_block),
	 ACPI_FADT_OFFSET(gpe1_block_length), ACPI_FADT_SEPARATE_LENGTH}
};

#define ACPI_FADT_INFO_ENTRIES        (sizeof (fadt_info_table) / sizeof (struct acpi_fadt_info))

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_init_generic_address
 *
 * PARAMETERS:  generic_address     - GAS struct to be initialized
 *              bit_width           - Width of this register
 *              Address             - Address of the register
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a Generic Address Structure (GAS)
 *              See the ACPI specification for a full description and
 *              definition of this structure.
 *
 ******************************************************************************/

static void inline
acpi_tb_init_generic_address(struct acpi_generic_address *generic_address,
			     u8 bit_width, u64 address)
{

	/*
	 * The 64-bit Address field is non-aligned in the byte packed
	 * GAS struct.
	 */
	ACPI_MOVE_64_TO_64(&generic_address->address, &address);

	/* All other fields are byte-wide */

	generic_address->space_id = ACPI_ADR_SPACE_SYSTEM_IO;
	generic_address->bit_width = bit_width;
	generic_address->bit_offset = 0;
	generic_address->access_width = 0;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_parse_fadt
 *
 * PARAMETERS:  table_index         - Index for the FADT
 *              Flags               - Flags
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize the FADT, DSDT and FACS tables
 *              (FADT contains the addresses of the DSDT and FACS)
 *
 ******************************************************************************/

void acpi_tb_parse_fadt(acpi_native_uint table_index, u8 flags)
{
	u32 length;
	struct acpi_table_header *table;

	/*
	 * The FADT has multiple versions with different lengths,
	 * and it contains pointers to both the DSDT and FACS tables.
	 *
	 * Get a local copy of the FADT and convert it to a common format
	 * Map entire FADT, assumed to be smaller than one page.
	 */
	length = acpi_gbl_root_table_list.tables[table_index].length;

	table =
	    acpi_os_map_memory(acpi_gbl_root_table_list.tables[table_index].
			       address, length);
	if (!table) {
		return;
	}

	/*
	 * Validate the FADT checksum before we copy the table. Ignore
	 * checksum error as we want to try to get the DSDT and FACS.
	 */
	(void)acpi_tb_verify_checksum(table, length);

	/* Obtain a local copy of the FADT in common ACPI 2.0+ format */

	acpi_tb_create_local_fadt(table, length);

	/* All done with the real FADT, unmap it */

	acpi_os_unmap_memory(table, length);

	/* Obtain the DSDT and FACS tables via their addresses within the FADT */

	acpi_tb_install_table((acpi_physical_address) acpi_gbl_FADT.Xdsdt,
			      flags, ACPI_SIG_DSDT, ACPI_TABLE_INDEX_DSDT);

	acpi_tb_install_table((acpi_physical_address) acpi_gbl_FADT.Xfacs,
			      flags, ACPI_SIG_FACS, ACPI_TABLE_INDEX_FACS);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_create_local_fadt
 *
 * PARAMETERS:  Table               - Pointer to BIOS FADT
 *              Length              - Length of the table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get a local copy of the FADT and convert it to a common format.
 *              Performs validation on some important FADT fields.
 *
 * NOTE:        We create a local copy of the FADT regardless of the version.
 *
 ******************************************************************************/

void acpi_tb_create_local_fadt(struct acpi_table_header *table, u32 length)
{

	/*
	 * Check if the FADT is larger than the largest table that we expect
	 * (the ACPI 2.0/3.0 version). If so, truncate the table, and issue
	 * a warning.
	 */
	if (length > sizeof(struct acpi_table_fadt)) {
		ACPI_WARNING((AE_INFO,
			      "FADT (revision %u) is longer than ACPI 2.0 version, truncating length 0x%X to 0x%zX",
			      table->revision, (unsigned)length,
			      sizeof(struct acpi_table_fadt)));
	}

	/* Clear the entire local FADT */

	ACPI_MEMSET(&acpi_gbl_FADT, 0, sizeof(struct acpi_table_fadt));

	/* Copy the original FADT, up to sizeof (struct acpi_table_fadt) */

	ACPI_MEMCPY(&acpi_gbl_FADT, table,
		    ACPI_MIN(length, sizeof(struct acpi_table_fadt)));

	/*
	 * 1) Convert the local copy of the FADT to the common internal format
	 * 2) Validate some of the important values within the FADT
	 */
	acpi_tb_convert_fadt();
	acpi_tb_validate_fadt();
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_fadt
 *
 * PARAMETERS:  None, uses acpi_gbl_FADT
 *
 * RETURN:      None
 *
 * DESCRIPTION: Converts all versions of the FADT to a common internal format.
 *              Expand all 32-bit addresses to 64-bit.
 *
 * NOTE:        acpi_gbl_FADT must be of size (struct acpi_table_fadt),
 *              and must contain a copy of the actual FADT.
 *
 * ACPICA will use the "X" fields of the FADT for all addresses.
 *
 * "X" fields are optional extensions to the original V1.0 fields. Even if
 * they are present in the structure, they can be optionally not used by
 * setting them to zero. Therefore, we must selectively expand V1.0 fields
 * if the corresponding X field is zero.
 *
 * For ACPI 1.0 FADTs, all address fields are expanded to the corresponding
 * "X" fields.
 *
 * For ACPI 2.0 FADTs, any "X" fields that are NULL are filled in by
 * expanding the corresponding ACPI 1.0 field.
 *
 ******************************************************************************/

static void acpi_tb_convert_fadt(void)
{
	u8 pm1_register_length;
	struct acpi_generic_address *target;
	acpi_native_uint i;

	/* Update the local FADT table header length */

	acpi_gbl_FADT.header.length = sizeof(struct acpi_table_fadt);

	/* Expand the 32-bit FACS and DSDT addresses to 64-bit as necessary */

	if (!acpi_gbl_FADT.Xfacs) {
		acpi_gbl_FADT.Xfacs = (u64) acpi_gbl_FADT.facs;
	}

	if (!acpi_gbl_FADT.Xdsdt) {
		acpi_gbl_FADT.Xdsdt = (u64) acpi_gbl_FADT.dsdt;
	}

	/*
	 * For ACPI 1.0 FADTs (revision 1 or 2), ensure that reserved fields which
	 * should be zero are indeed zero. This will workaround BIOSs that
	 * inadvertently place values in these fields.
	 *
	 * The ACPI 1.0 reserved fields that will be zeroed are the bytes located at
	 * offset 45, 55, 95, and the word located at offset 109, 110.
	 */
	if (acpi_gbl_FADT.header.revision < 3) {
		acpi_gbl_FADT.preferred_profile = 0;
		acpi_gbl_FADT.pstate_control = 0;
		acpi_gbl_FADT.cst_control = 0;
		acpi_gbl_FADT.boot_flags = 0;
	}

	/*
	 * Expand the ACPI 1.0 32-bit V1.0 addresses to the ACPI 2.0 64-bit "X"
	 * generic address structures as necessary.
	 */
	for (i = 0; i < ACPI_FADT_INFO_ENTRIES; i++) {
		target =
		    ACPI_ADD_PTR(struct acpi_generic_address, &acpi_gbl_FADT,
				 fadt_info_table[i].target);

		/* Expand only if the X target is null */

		if (!target->address) {
			acpi_tb_init_generic_address(target,
						     *ACPI_ADD_PTR(u8,
								   &acpi_gbl_FADT,
								   fadt_info_table
								   [i].length),
						     (u64) * ACPI_ADD_PTR(u32,
									  &acpi_gbl_FADT,
									  fadt_info_table
									  [i].
									  source));
		}
	}

	/*
	 * Calculate separate GAS structs for the PM1 Enable registers.
	 * These addresses do not appear (directly) in the FADT, so it is
	 * useful to calculate them once, here.
	 *
	 * The PM event blocks are split into two register blocks, first is the
	 * PM Status Register block, followed immediately by the PM Enable Register
	 * block. Each is of length (pm1_event_length/2)
	 */
	pm1_register_length = (u8) ACPI_DIV_2(acpi_gbl_FADT.pm1_event_length);

	/* The PM1A register block is required */

	acpi_tb_init_generic_address(&acpi_gbl_xpm1a_enable,
				     pm1_register_length,
				     (acpi_gbl_FADT.xpm1a_event_block.address +
				      pm1_register_length));
	/* Don't forget to copy space_id of the GAS */
	acpi_gbl_xpm1a_enable.space_id =
	    acpi_gbl_FADT.xpm1a_event_block.space_id;

	/* The PM1B register block is optional, ignore if not present */

	if (acpi_gbl_FADT.xpm1b_event_block.address) {
		acpi_tb_init_generic_address(&acpi_gbl_xpm1b_enable,
					     pm1_register_length,
					     (acpi_gbl_FADT.xpm1b_event_block.
					      address + pm1_register_length));
		/* Don't forget to copy space_id of the GAS */
		acpi_gbl_xpm1b_enable.space_id =
		    acpi_gbl_FADT.xpm1a_event_block.space_id;

	}
}

/******************************************************************************
 *
 * FUNCTION:    acpi_tb_validate_fadt
 *
 * PARAMETERS:  Table           - Pointer to the FADT to be validated
 *
 * RETURN:      None
 *
 * DESCRIPTION: Validate various important fields within the FADT. If a problem
 *              is found, issue a message, but no status is returned.
 *              Used by both the table manager and the disassembler.
 *
 * Possible additional checks:
 * (acpi_gbl_FADT.pm1_event_length >= 4)
 * (acpi_gbl_FADT.pm1_control_length >= 2)
 * (acpi_gbl_FADT.pm_timer_length >= 4)
 * Gpe block lengths must be multiple of 2
 *
 ******************************************************************************/

static void acpi_tb_validate_fadt(void)
{
	u32 *address32;
	struct acpi_generic_address *address64;
	u8 length;
	acpi_native_uint i;

	/* Examine all of the 64-bit extended address fields (X fields) */

	for (i = 0; i < ACPI_FADT_INFO_ENTRIES; i++) {

		/* Generate pointers to the 32-bit and 64-bit addresses and get the length */

		address64 =
		    ACPI_ADD_PTR(struct acpi_generic_address, &acpi_gbl_FADT,
				 fadt_info_table[i].target);
		address32 =
		    ACPI_ADD_PTR(u32, &acpi_gbl_FADT,
				 fadt_info_table[i].source);
		length =
		    *ACPI_ADD_PTR(u8, &acpi_gbl_FADT,
				  fadt_info_table[i].length);

		if (fadt_info_table[i].type & ACPI_FADT_REQUIRED) {
			/*
			 * Field is required (Pm1a_event, Pm1a_control, pm_timer).
			 * Both the address and length must be non-zero.
			 */
			if (!address64->address || !length) {
				ACPI_ERROR((AE_INFO,
					    "Required field \"%s\" has zero address and/or length: %8.8X%8.8X/%X",
					    fadt_info_table[i].name,
					    ACPI_FORMAT_UINT64(address64->
							       address),
					    length));
			}
		} else if (fadt_info_table[i].type & ACPI_FADT_SEPARATE_LENGTH) {
			/*
			 * Field is optional (PM2Control, GPE0, GPE1) AND has its own
			 * length field. If present, both the address and length must be valid.
			 */
			if ((address64->address && !length)
			    || (!address64->address && length)) {
				ACPI_WARNING((AE_INFO,
					      "Optional field \"%s\" has zero address or length: %8.8X%8.8X/%X",
					      fadt_info_table[i].name,
					      ACPI_FORMAT_UINT64(address64->
								 address),
					      length));
			}
		}

		/* If both 32- and 64-bit addresses are valid (non-zero), they must match */

		if (address64->address && *address32 &&
		    (address64->address != (u64) * address32)) {
			ACPI_ERROR((AE_INFO,
				    "32/64X address mismatch in \"%s\": [%8.8X] [%8.8X%8.8X], using 64X",
				    fadt_info_table[i].name, *address32,
				    ACPI_FORMAT_UINT64(address64->address)));
		}
	}
}
