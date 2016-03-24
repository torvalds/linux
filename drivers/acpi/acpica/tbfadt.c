/******************************************************************************
 *
 * Module Name: tbfadt   - FADT table utilities
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
#include "actables.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbfadt")

/* Local prototypes */
static void
acpi_tb_init_generic_address(struct acpi_generic_address *generic_address,
			     u8 space_id,
			     u8 byte_width,
			     u64 address, char *register_name, u8 flags);

static void acpi_tb_convert_fadt(void);

static void acpi_tb_setup_fadt_registers(void);

static u64
acpi_tb_select_address(char *register_name, u32 address32, u64 address64);

/* Table for conversion of FADT to common internal format and FADT validation */

typedef struct acpi_fadt_info {
	char *name;
	u16 address64;
	u16 address32;
	u16 length;
	u8 default_length;
	u8 flags;

} acpi_fadt_info;

#define ACPI_FADT_OPTIONAL          0
#define ACPI_FADT_REQUIRED          1
#define ACPI_FADT_SEPARATE_LENGTH   2
#define ACPI_FADT_GPE_REGISTER      4

static struct acpi_fadt_info fadt_info_table[] = {
	{"Pm1aEventBlock",
	 ACPI_FADT_OFFSET(xpm1a_event_block),
	 ACPI_FADT_OFFSET(pm1a_event_block),
	 ACPI_FADT_OFFSET(pm1_event_length),
	 ACPI_PM1_REGISTER_WIDTH * 2,	/* Enable + Status register */
	 ACPI_FADT_REQUIRED},

	{"Pm1bEventBlock",
	 ACPI_FADT_OFFSET(xpm1b_event_block),
	 ACPI_FADT_OFFSET(pm1b_event_block),
	 ACPI_FADT_OFFSET(pm1_event_length),
	 ACPI_PM1_REGISTER_WIDTH * 2,	/* Enable + Status register */
	 ACPI_FADT_OPTIONAL},

	{"Pm1aControlBlock",
	 ACPI_FADT_OFFSET(xpm1a_control_block),
	 ACPI_FADT_OFFSET(pm1a_control_block),
	 ACPI_FADT_OFFSET(pm1_control_length),
	 ACPI_PM1_REGISTER_WIDTH,
	 ACPI_FADT_REQUIRED},

	{"Pm1bControlBlock",
	 ACPI_FADT_OFFSET(xpm1b_control_block),
	 ACPI_FADT_OFFSET(pm1b_control_block),
	 ACPI_FADT_OFFSET(pm1_control_length),
	 ACPI_PM1_REGISTER_WIDTH,
	 ACPI_FADT_OPTIONAL},

	{"Pm2ControlBlock",
	 ACPI_FADT_OFFSET(xpm2_control_block),
	 ACPI_FADT_OFFSET(pm2_control_block),
	 ACPI_FADT_OFFSET(pm2_control_length),
	 ACPI_PM2_REGISTER_WIDTH,
	 ACPI_FADT_SEPARATE_LENGTH},

	{"PmTimerBlock",
	 ACPI_FADT_OFFSET(xpm_timer_block),
	 ACPI_FADT_OFFSET(pm_timer_block),
	 ACPI_FADT_OFFSET(pm_timer_length),
	 ACPI_PM_TIMER_WIDTH,
	 ACPI_FADT_SEPARATE_LENGTH},	/* ACPI 5.0A: Timer is optional */

	{"Gpe0Block",
	 ACPI_FADT_OFFSET(xgpe0_block),
	 ACPI_FADT_OFFSET(gpe0_block),
	 ACPI_FADT_OFFSET(gpe0_block_length),
	 0,
	 ACPI_FADT_SEPARATE_LENGTH | ACPI_FADT_GPE_REGISTER},

	{"Gpe1Block",
	 ACPI_FADT_OFFSET(xgpe1_block),
	 ACPI_FADT_OFFSET(gpe1_block),
	 ACPI_FADT_OFFSET(gpe1_block_length),
	 0,
	 ACPI_FADT_SEPARATE_LENGTH | ACPI_FADT_GPE_REGISTER}
};

#define ACPI_FADT_INFO_ENTRIES \
			(sizeof (fadt_info_table) / sizeof (struct acpi_fadt_info))

/* Table used to split Event Blocks into separate status/enable registers */

typedef struct acpi_fadt_pm_info {
	struct acpi_generic_address *target;
	u16 source;
	u8 register_num;

} acpi_fadt_pm_info;

static struct acpi_fadt_pm_info fadt_pm_info_table[] = {
	{&acpi_gbl_xpm1a_status,
	 ACPI_FADT_OFFSET(xpm1a_event_block),
	 0},

	{&acpi_gbl_xpm1a_enable,
	 ACPI_FADT_OFFSET(xpm1a_event_block),
	 1},

	{&acpi_gbl_xpm1b_status,
	 ACPI_FADT_OFFSET(xpm1b_event_block),
	 0},

	{&acpi_gbl_xpm1b_enable,
	 ACPI_FADT_OFFSET(xpm1b_event_block),
	 1}
};

#define ACPI_FADT_PM_INFO_ENTRIES \
			(sizeof (fadt_pm_info_table) / sizeof (struct acpi_fadt_pm_info))

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_init_generic_address
 *
 * PARAMETERS:  generic_address     - GAS struct to be initialized
 *              space_id            - ACPI Space ID for this register
 *              byte_width          - Width of this register
 *              address             - Address of the register
 *              register_name       - ASCII name of the ACPI register
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a Generic Address Structure (GAS)
 *              See the ACPI specification for a full description and
 *              definition of this structure.
 *
 ******************************************************************************/

static void
acpi_tb_init_generic_address(struct acpi_generic_address *generic_address,
			     u8 space_id,
			     u8 byte_width,
			     u64 address, char *register_name, u8 flags)
{
	u8 bit_width;

	/*
	 * Bit width field in the GAS is only one byte long, 255 max.
	 * Check for bit_width overflow in GAS.
	 */
	bit_width = (u8)(byte_width * 8);
	if (byte_width > 31) {	/* (31*8)=248, (32*8)=256 */
		/*
		 * No error for GPE blocks, because we do not use the bit_width
		 * for GPEs, the legacy length (byte_width) is used instead to
		 * allow for a large number of GPEs.
		 */
		if (!(flags & ACPI_FADT_GPE_REGISTER)) {
			ACPI_ERROR((AE_INFO,
				    "%s - 32-bit FADT register is too long (%u bytes, %u bits) "
				    "to convert to GAS struct - 255 bits max, truncating",
				    register_name, byte_width,
				    (byte_width * 8)));
		}

		bit_width = 255;
	}

	/*
	 * The 64-bit Address field is non-aligned in the byte packed
	 * GAS struct.
	 */
	ACPI_MOVE_64_TO_64(&generic_address->address, &address);

	/* All other fields are byte-wide */

	generic_address->space_id = space_id;
	generic_address->bit_width = bit_width;
	generic_address->bit_offset = 0;
	generic_address->access_width = 0;	/* Access width ANY */
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_select_address
 *
 * PARAMETERS:  register_name       - ASCII name of the ACPI register
 *              address32           - 32-bit address of the register
 *              address64           - 64-bit address of the register
 *
 * RETURN:      The resolved 64-bit address
 *
 * DESCRIPTION: Select between 32-bit and 64-bit versions of addresses within
 *              the FADT. Used for the FACS and DSDT addresses.
 *
 * NOTES:
 *
 * Check for FACS and DSDT address mismatches. An address mismatch between
 * the 32-bit and 64-bit address fields (FIRMWARE_CTRL/X_FIRMWARE_CTRL and
 * DSDT/X_DSDT) could be a corrupted address field or it might indicate
 * the presence of two FACS or two DSDT tables.
 *
 * November 2013:
 * By default, as per the ACPICA specification, a valid 64-bit address is
 * used regardless of the value of the 32-bit address. However, this
 * behavior can be overridden via the acpi_gbl_use32_bit_fadt_addresses flag.
 *
 ******************************************************************************/

static u64
acpi_tb_select_address(char *register_name, u32 address32, u64 address64)
{

	if (!address64) {

		/* 64-bit address is zero, use 32-bit address */

		return ((u64)address32);
	}

	if (address32 && (address64 != (u64)address32)) {

		/* Address mismatch between 32-bit and 64-bit versions */

		ACPI_BIOS_WARNING((AE_INFO,
				   "32/64X %s address mismatch in FADT: "
				   "0x%8.8X/0x%8.8X%8.8X, using %u-bit address",
				   register_name, address32,
				   ACPI_FORMAT_UINT64(address64),
				   acpi_gbl_use32_bit_fadt_addresses ? 32 :
				   64));

		/* 32-bit address override */

		if (acpi_gbl_use32_bit_fadt_addresses) {
			return ((u64)address32);
		}
	}

	/* Default is to use the 64-bit address */

	return (address64);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_parse_fadt
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize the FADT, DSDT and FACS tables
 *              (FADT contains the addresses of the DSDT and FACS)
 *
 ******************************************************************************/

void acpi_tb_parse_fadt(void)
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
	length = acpi_gbl_root_table_list.tables[acpi_gbl_fadt_index].length;

	table =
	    acpi_os_map_memory(acpi_gbl_root_table_list.
			       tables[acpi_gbl_fadt_index].address, length);
	if (!table) {
		return;
	}

	/*
	 * Validate the FADT checksum before we copy the table. Ignore
	 * checksum error as we want to try to get the DSDT and FACS.
	 */
	(void)acpi_tb_verify_checksum(table, length);

	/* Create a local copy of the FADT in common ACPI 2.0+ format */

	acpi_tb_create_local_fadt(table, length);

	/* All done with the real FADT, unmap it */

	acpi_os_unmap_memory(table, length);

	/* Obtain the DSDT and FACS tables via their addresses within the FADT */

	acpi_tb_install_fixed_table((acpi_physical_address) acpi_gbl_FADT.Xdsdt,
				    ACPI_SIG_DSDT, &acpi_gbl_dsdt_index);

	/* If Hardware Reduced flag is set, there is no FACS */

	if (!acpi_gbl_reduced_hardware) {
		if (acpi_gbl_FADT.facs) {
			acpi_tb_install_fixed_table((acpi_physical_address)
						    acpi_gbl_FADT.facs,
						    ACPI_SIG_FACS,
						    &acpi_gbl_facs_index);
		}
		if (acpi_gbl_FADT.Xfacs) {
			acpi_tb_install_fixed_table((acpi_physical_address)
						    acpi_gbl_FADT.Xfacs,
						    ACPI_SIG_FACS,
						    &acpi_gbl_xfacs_index);
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_create_local_fadt
 *
 * PARAMETERS:  table               - Pointer to BIOS FADT
 *              length              - Length of the table
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
	 * (typically the current ACPI specification version). If so, truncate
	 * the table, and issue a warning.
	 */
	if (length > sizeof(struct acpi_table_fadt)) {
		ACPI_BIOS_WARNING((AE_INFO,
				   "FADT (revision %u) is longer than %s length, "
				   "truncating length %u to %u",
				   table->revision, ACPI_FADT_CONFORMANCE,
				   length,
				   (u32)sizeof(struct acpi_table_fadt)));
	}

	/* Clear the entire local FADT */

	memset(&acpi_gbl_FADT, 0, sizeof(struct acpi_table_fadt));

	/* Copy the original FADT, up to sizeof (struct acpi_table_fadt) */

	memcpy(&acpi_gbl_FADT, table,
	       ACPI_MIN(length, sizeof(struct acpi_table_fadt)));

	/* Take a copy of the Hardware Reduced flag */

	acpi_gbl_reduced_hardware = FALSE;
	if (acpi_gbl_FADT.flags & ACPI_FADT_HW_REDUCED) {
		acpi_gbl_reduced_hardware = TRUE;
	}

	/* Convert the local copy of the FADT to the common internal format */

	acpi_tb_convert_fadt();

	/* Initialize the global ACPI register structures */

	acpi_tb_setup_fadt_registers();
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_fadt
 *
 * PARAMETERS:  none - acpi_gbl_FADT is used.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Converts all versions of the FADT to a common internal format.
 *              Expand 32-bit addresses to 64-bit as necessary. Also validate
 *              important fields within the FADT.
 *
 * NOTE:        acpi_gbl_FADT must be of size (struct acpi_table_fadt), and must
 *              contain a copy of the actual BIOS-provided FADT.
 *
 * Notes on 64-bit register addresses:
 *
 * After this FADT conversion, later ACPICA code will only use the 64-bit "X"
 * fields of the FADT for all ACPI register addresses.
 *
 * The 64-bit X fields are optional extensions to the original 32-bit FADT
 * V1.0 fields. Even if they are present in the FADT, they are optional and
 * are unused if the BIOS sets them to zero. Therefore, we must copy/expand
 * 32-bit V1.0 fields to the 64-bit X fields if the the 64-bit X field is
 * originally zero.
 *
 * For ACPI 1.0 FADTs (that contain no 64-bit addresses), all 32-bit address
 * fields are expanded to the corresponding 64-bit X fields in the internal
 * common FADT.
 *
 * For ACPI 2.0+ FADTs, all valid (non-zero) 32-bit address fields are expanded
 * to the corresponding 64-bit X fields, if the 64-bit field is originally
 * zero. Adhering to the ACPI specification, we completely ignore the 32-bit
 * field if the 64-bit field is valid, regardless of whether the host OS is
 * 32-bit or 64-bit.
 *
 * Possible additional checks:
 *  (acpi_gbl_FADT.pm1_event_length >= 4)
 *  (acpi_gbl_FADT.pm1_control_length >= 2)
 *  (acpi_gbl_FADT.pm_timer_length >= 4)
 *  Gpe block lengths must be multiple of 2
 *
 ******************************************************************************/

static void acpi_tb_convert_fadt(void)
{
	char *name;
	struct acpi_generic_address *address64;
	u32 address32;
	u8 length;
	u8 flags;
	u32 i;

	/*
	 * For ACPI 1.0 FADTs (revision 1 or 2), ensure that reserved fields which
	 * should be zero are indeed zero. This will workaround BIOSs that
	 * inadvertently place values in these fields.
	 *
	 * The ACPI 1.0 reserved fields that will be zeroed are the bytes located
	 * at offset 45, 55, 95, and the word located at offset 109, 110.
	 *
	 * Note: The FADT revision value is unreliable. Only the length can be
	 * trusted.
	 */
	if (acpi_gbl_FADT.header.length <= ACPI_FADT_V2_SIZE) {
		acpi_gbl_FADT.preferred_profile = 0;
		acpi_gbl_FADT.pstate_control = 0;
		acpi_gbl_FADT.cst_control = 0;
		acpi_gbl_FADT.boot_flags = 0;
	}

	/*
	 * Now we can update the local FADT length to the length of the
	 * current FADT version as defined by the ACPI specification.
	 * Thus, we will have a common FADT internally.
	 */
	acpi_gbl_FADT.header.length = sizeof(struct acpi_table_fadt);

	/*
	 * Expand the 32-bit DSDT addresses to 64-bit as necessary.
	 * Later ACPICA code will always use the X 64-bit field.
	 */
	acpi_gbl_FADT.Xdsdt = acpi_tb_select_address("DSDT",
						     acpi_gbl_FADT.dsdt,
						     acpi_gbl_FADT.Xdsdt);

	/* If Hardware Reduced flag is set, we are all done */

	if (acpi_gbl_reduced_hardware) {
		return;
	}

	/* Examine all of the 64-bit extended address fields (X fields) */

	for (i = 0; i < ACPI_FADT_INFO_ENTRIES; i++) {
		/*
		 * Get the 32-bit and 64-bit addresses, as well as the register
		 * length and register name.
		 */
		address32 = *ACPI_ADD_PTR(u32,
					  &acpi_gbl_FADT,
					  fadt_info_table[i].address32);

		address64 = ACPI_ADD_PTR(struct acpi_generic_address,
					 &acpi_gbl_FADT,
					 fadt_info_table[i].address64);

		length = *ACPI_ADD_PTR(u8,
				       &acpi_gbl_FADT,
				       fadt_info_table[i].length);

		name = fadt_info_table[i].name;
		flags = fadt_info_table[i].flags;

		/*
		 * Expand the ACPI 1.0 32-bit addresses to the ACPI 2.0 64-bit "X"
		 * generic address structures as necessary. Later code will always use
		 * the 64-bit address structures.
		 *
		 * November 2013:
		 * Now always use the 64-bit address if it is valid (non-zero), in
		 * accordance with the ACPI specification which states that a 64-bit
		 * address supersedes the 32-bit version. This behavior can be
		 * overridden by the acpi_gbl_use32_bit_fadt_addresses flag.
		 *
		 * During 64-bit address construction and verification,
		 * these cases are handled:
		 *
		 * Address32 zero, Address64 [don't care]   - Use Address64
		 *
		 * Address32 non-zero, Address64 zero       - Copy/use Address32
		 * Address32 non-zero == Address64 non-zero - Use Address64
		 * Address32 non-zero != Address64 non-zero - Warning, use Address64
		 *
		 * Override: if acpi_gbl_use32_bit_fadt_addresses is TRUE, and:
		 * Address32 non-zero != Address64 non-zero - Warning, copy/use Address32
		 *
		 * Note: space_id is always I/O for 32-bit legacy address fields
		 */
		if (address32) {
			if (!address64->address) {

				/* 64-bit address is zero, use 32-bit address */

				acpi_tb_init_generic_address(address64,
							     ACPI_ADR_SPACE_SYSTEM_IO,
							     *ACPI_ADD_PTR(u8,
									   &acpi_gbl_FADT,
									   fadt_info_table
									   [i].
									   length),
							     (u64)address32,
							     name, flags);
			} else if (address64->address != (u64)address32) {

				/* Address mismatch */

				ACPI_BIOS_WARNING((AE_INFO,
						   "32/64X address mismatch in FADT/%s: "
						   "0x%8.8X/0x%8.8X%8.8X, using %u-bit address",
						   name, address32,
						   ACPI_FORMAT_UINT64
						   (address64->address),
						   acpi_gbl_use32_bit_fadt_addresses
						   ? 32 : 64));

				if (acpi_gbl_use32_bit_fadt_addresses) {

					/* 32-bit address override */

					acpi_tb_init_generic_address(address64,
								     ACPI_ADR_SPACE_SYSTEM_IO,
								     *ACPI_ADD_PTR
								     (u8,
								      &acpi_gbl_FADT,
								      fadt_info_table
								      [i].
								      length),
								     (u64)
								     address32,
								     name,
								     flags);
				}
			}
		}

		/*
		 * For each extended field, check for length mismatch between the
		 * legacy length field and the corresponding 64-bit X length field.
		 * Note: If the legacy length field is > 0xFF bits, ignore this
		 * check. (GPE registers can be larger than the 64-bit GAS structure
		 * can accomodate, 0xFF bits).
		 */
		if (address64->address &&
		    (ACPI_MUL_8(length) <= ACPI_UINT8_MAX) &&
		    (address64->bit_width != ACPI_MUL_8(length))) {
			ACPI_BIOS_WARNING((AE_INFO,
					   "32/64X length mismatch in FADT/%s: %u/%u",
					   name, ACPI_MUL_8(length),
					   address64->bit_width));
		}

		if (fadt_info_table[i].flags & ACPI_FADT_REQUIRED) {
			/*
			 * Field is required (Pm1a_event, Pm1a_control).
			 * Both the address and length must be non-zero.
			 */
			if (!address64->address || !length) {
				ACPI_BIOS_ERROR((AE_INFO,
						 "Required FADT field %s has zero address and/or length: "
						 "0x%8.8X%8.8X/0x%X",
						 name,
						 ACPI_FORMAT_UINT64(address64->
								    address),
						 length));
			}
		} else if (fadt_info_table[i].flags & ACPI_FADT_SEPARATE_LENGTH) {
			/*
			 * Field is optional (Pm2_control, GPE0, GPE1) AND has its own
			 * length field. If present, both the address and length must
			 * be valid.
			 */
			if ((address64->address && !length) ||
			    (!address64->address && length)) {
				ACPI_BIOS_WARNING((AE_INFO,
						   "Optional FADT field %s has valid %s but zero %s: "
						   "0x%8.8X%8.8X/0x%X", name,
						   (length ? "Length" :
						    "Address"),
						   (length ? "Address" :
						    "Length"),
						   ACPI_FORMAT_UINT64
						   (address64->address),
						   length));
			}
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_setup_fadt_registers
 *
 * PARAMETERS:  None, uses acpi_gbl_FADT.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize global ACPI PM1 register definitions. Optionally,
 *              force FADT register definitions to their default lengths.
 *
 ******************************************************************************/

static void acpi_tb_setup_fadt_registers(void)
{
	struct acpi_generic_address *target64;
	struct acpi_generic_address *source64;
	u8 pm1_register_byte_width;
	u32 i;

	/*
	 * Optionally check all register lengths against the default values and
	 * update them if they are incorrect.
	 */
	if (acpi_gbl_use_default_register_widths) {
		for (i = 0; i < ACPI_FADT_INFO_ENTRIES; i++) {
			target64 =
			    ACPI_ADD_PTR(struct acpi_generic_address,
					 &acpi_gbl_FADT,
					 fadt_info_table[i].address64);

			/*
			 * If a valid register (Address != 0) and the (default_length > 0)
			 * (Not a GPE register), then check the width against the default.
			 */
			if ((target64->address) &&
			    (fadt_info_table[i].default_length > 0) &&
			    (fadt_info_table[i].default_length !=
			     target64->bit_width)) {
				ACPI_BIOS_WARNING((AE_INFO,
						   "Invalid length for FADT/%s: %u, using default %u",
						   fadt_info_table[i].name,
						   target64->bit_width,
						   fadt_info_table[i].
						   default_length));

				/* Incorrect size, set width to the default */

				target64->bit_width =
				    fadt_info_table[i].default_length;
			}
		}
	}

	/*
	 * Get the length of the individual PM1 registers (enable and status).
	 * Each register is defined to be (event block length / 2). Extra divide
	 * by 8 converts bits to bytes.
	 */
	pm1_register_byte_width = (u8)
	    ACPI_DIV_16(acpi_gbl_FADT.xpm1a_event_block.bit_width);

	/*
	 * Calculate separate GAS structs for the PM1x (A/B) Status and Enable
	 * registers. These addresses do not appear (directly) in the FADT, so it
	 * is useful to pre-calculate them from the PM1 Event Block definitions.
	 *
	 * The PM event blocks are split into two register blocks, first is the
	 * PM Status Register block, followed immediately by the PM Enable
	 * Register block. Each is of length (pm1_event_length/2)
	 *
	 * Note: The PM1A event block is required by the ACPI specification.
	 * However, the PM1B event block is optional and is rarely, if ever,
	 * used.
	 */

	for (i = 0; i < ACPI_FADT_PM_INFO_ENTRIES; i++) {
		source64 =
		    ACPI_ADD_PTR(struct acpi_generic_address, &acpi_gbl_FADT,
				 fadt_pm_info_table[i].source);

		if (source64->address) {
			acpi_tb_init_generic_address(fadt_pm_info_table[i].
						     target, source64->space_id,
						     pm1_register_byte_width,
						     source64->address +
						     (fadt_pm_info_table[i].
						      register_num *
						      pm1_register_byte_width),
						     "PmRegisters", 0);
		}
	}
}
