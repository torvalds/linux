/******************************************************************************
 *
 * Module Name: tbconvrt - ACPI Table conversion utilities
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
#include <acpi/actables.h>

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbconvrt")

/* Local prototypes */
static void
acpi_tb_init_generic_address(struct acpi_generic_address *new_gas_struct,
			     u8 register_bit_width,
			     acpi_physical_address address);

static void
acpi_tb_convert_fadt1(struct fadt_descriptor_rev2 *local_fadt,
		      struct fadt_descriptor_rev1 *original_fadt);

static void
acpi_tb_convert_fadt2(struct fadt_descriptor_rev2 *local_fadt,
		      struct fadt_descriptor_rev2 *original_fadt);

u8 acpi_fadt_is_v1;
EXPORT_SYMBOL(acpi_fadt_is_v1);

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table_count
 *
 * PARAMETERS:  RSDP            - Pointer to the RSDP
 *              RSDT            - Pointer to the RSDT/XSDT
 *
 * RETURN:      The number of tables pointed to by the RSDT or XSDT.
 *
 * DESCRIPTION: Calculate the number of tables.  Automatically handles either
 *              an RSDT or XSDT.
 *
 ******************************************************************************/

u32
acpi_tb_get_table_count(struct rsdp_descriptor *RSDP,
			struct acpi_table_header *RSDT)
{
	u32 pointer_size;

	ACPI_FUNCTION_ENTRY();

	/* RSDT pointers are 32 bits, XSDT pointers are 64 bits */

	if (acpi_gbl_root_table_type == ACPI_TABLE_TYPE_RSDT) {
		pointer_size = sizeof(u32);
	} else {
		pointer_size = sizeof(u64);
	}

	/*
	 * Determine the number of tables pointed to by the RSDT/XSDT.
	 * This is defined by the ACPI Specification to be the number of
	 * pointers contained within the RSDT/XSDT.  The size of the pointers
	 * is architecture-dependent.
	 */
	return ((RSDT->length -
		 sizeof(struct acpi_table_header)) / pointer_size);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_to_xsdt
 *
 * PARAMETERS:  table_info      - Info about the RSDT
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an RSDT to an XSDT (internal common format)
 *
 ******************************************************************************/

acpi_status acpi_tb_convert_to_xsdt(struct acpi_table_desc *table_info)
{
	acpi_size table_size;
	u32 i;
	XSDT_DESCRIPTOR *new_table;

	ACPI_FUNCTION_ENTRY();

	/* Compute size of the converted XSDT */

	table_size = ((acpi_size) acpi_gbl_rsdt_table_count * sizeof(u64)) +
	    sizeof(struct acpi_table_header);

	/* Allocate an XSDT */

	new_table = ACPI_MEM_CALLOCATE(table_size);
	if (!new_table) {
		return (AE_NO_MEMORY);
	}

	/* Copy the header and set the length */

	ACPI_MEMCPY(new_table, table_info->pointer,
		    sizeof(struct acpi_table_header));
	new_table->length = (u32) table_size;

	/* Copy the table pointers */

	for (i = 0; i < acpi_gbl_rsdt_table_count; i++) {
		/* RSDT pointers are 32 bits, XSDT pointers are 64 bits */

		if (acpi_gbl_root_table_type == ACPI_TABLE_TYPE_RSDT) {
			ACPI_STORE_ADDRESS(new_table->table_offset_entry[i],
					   (ACPI_CAST_PTR
					    (struct rsdt_descriptor_rev1,
					     table_info->pointer))->
					   table_offset_entry[i]);
		} else {
			new_table->table_offset_entry[i] =
			    (ACPI_CAST_PTR(XSDT_DESCRIPTOR,
					   table_info->pointer))->
			    table_offset_entry[i];
		}
	}

	/* Delete the original table (either mapped or in a buffer) */

	acpi_tb_delete_single_table(table_info);

	/* Point the table descriptor to the new table */

	table_info->pointer =
	    ACPI_CAST_PTR(struct acpi_table_header, new_table);
	table_info->length = table_size;
	table_info->allocation = ACPI_MEM_ALLOCATED;

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_init_generic_address
 *
 * PARAMETERS:  new_gas_struct      - GAS struct to be initialized
 *              register_bit_width  - Width of this register
 *              Address             - Address of the register
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a GAS structure.
 *
 ******************************************************************************/

static void
acpi_tb_init_generic_address(struct acpi_generic_address *new_gas_struct,
			     u8 register_bit_width,
			     acpi_physical_address address)
{

	ACPI_STORE_ADDRESS(new_gas_struct->address, address);

	new_gas_struct->address_space_id = ACPI_ADR_SPACE_SYSTEM_IO;
	new_gas_struct->register_bit_width = register_bit_width;
	new_gas_struct->register_bit_offset = 0;
	new_gas_struct->access_width = 0;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_fadt1
 *
 * PARAMETERS:  local_fadt      - Pointer to new FADT
 *              original_fadt   - Pointer to old FADT
 *
 * RETURN:      None, populates local_fadt
 *
 * DESCRIPTION: Convert an ACPI 1.0 FADT to common internal format
 *
 ******************************************************************************/

static void
acpi_tb_convert_fadt1(struct fadt_descriptor_rev2 *local_fadt,
		      struct fadt_descriptor_rev1 *original_fadt)
{

	/* ACPI 1.0 FACS */
	/* The BIOS stored FADT should agree with Revision 1.0 */
	acpi_fadt_is_v1 = 1;

	/*
	 * Copy the table header and the common part of the tables.
	 *
	 * The 2.0 table is an extension of the 1.0 table, so the entire 1.0
	 * table can be copied first, then expand some fields to 64 bits.
	 */
	ACPI_MEMCPY(local_fadt, original_fadt,
		    sizeof(struct fadt_descriptor_rev1));

	/* Convert table pointers to 64-bit fields */

	ACPI_STORE_ADDRESS(local_fadt->xfirmware_ctrl,
			   local_fadt->V1_firmware_ctrl);
	ACPI_STORE_ADDRESS(local_fadt->Xdsdt, local_fadt->V1_dsdt);

	/*
	 * System Interrupt Model isn't used in ACPI 2.0
	 * (local_fadt->Reserved1 = 0;)
	 */

	/*
	 * This field is set by the OEM to convey the preferred power management
	 * profile to OSPM. It doesn't have any 1.0 equivalence.  Since we don't
	 * know what kind of 32-bit system this is, we will use "unspecified".
	 */
	local_fadt->prefer_PM_profile = PM_UNSPECIFIED;

	/*
	 * Processor Performance State Control. This is the value OSPM writes to
	 * the SMI_CMD register to assume processor performance state control
	 * responsibility. There isn't any equivalence in 1.0, but as many 1.x
	 * ACPI tables contain _PCT and _PSS we also keep this value, unless
	 * acpi_strict is set.
	 */
	if (acpi_strict)
		local_fadt->pstate_cnt = 0;

	/*
	 * Support for the _CST object and C States change notification.
	 * This data item hasn't any 1.0 equivalence so leave it zero.
	 */
	local_fadt->cst_cnt = 0;

	/*
	 * FADT Rev 2 was an interim FADT released between ACPI 1.0 and ACPI 2.0.
	 * It primarily adds the FADT reset mechanism.
	 */
	if ((original_fadt->revision == 2) &&
	    (original_fadt->length ==
	     sizeof(struct fadt_descriptor_rev2_minus))) {
		/*
		 * Grab the entire generic address struct, plus the 1-byte reset value
		 * that immediately follows.
		 */
		ACPI_MEMCPY(&local_fadt->reset_register,
			    &(ACPI_CAST_PTR(struct fadt_descriptor_rev2_minus,
					    original_fadt))->reset_register,
			    sizeof(struct acpi_generic_address) + 1);
	} else {
		/*
		 * Since there isn't any equivalence in 1.0 and since it is highly
		 * likely that a 1.0 system has legacy support.
		 */
		local_fadt->iapc_boot_arch = BAF_LEGACY_DEVICES;
	}

	/*
	 * Convert the V1.0 block addresses to V2.0 GAS structures
	 */
	acpi_tb_init_generic_address(&local_fadt->xpm1a_evt_blk,
				     local_fadt->pm1_evt_len,
				     (acpi_physical_address) local_fadt->
				     V1_pm1a_evt_blk);
	acpi_tb_init_generic_address(&local_fadt->xpm1b_evt_blk,
				     local_fadt->pm1_evt_len,
				     (acpi_physical_address) local_fadt->
				     V1_pm1b_evt_blk);
	acpi_tb_init_generic_address(&local_fadt->xpm1a_cnt_blk,
				     local_fadt->pm1_cnt_len,
				     (acpi_physical_address) local_fadt->
				     V1_pm1a_cnt_blk);
	acpi_tb_init_generic_address(&local_fadt->xpm1b_cnt_blk,
				     local_fadt->pm1_cnt_len,
				     (acpi_physical_address) local_fadt->
				     V1_pm1b_cnt_blk);
	acpi_tb_init_generic_address(&local_fadt->xpm2_cnt_blk,
				     local_fadt->pm2_cnt_len,
				     (acpi_physical_address) local_fadt->
				     V1_pm2_cnt_blk);
	acpi_tb_init_generic_address(&local_fadt->xpm_tmr_blk,
				     local_fadt->pm_tm_len,
				     (acpi_physical_address) local_fadt->
				     V1_pm_tmr_blk);
	acpi_tb_init_generic_address(&local_fadt->xgpe0_blk, 0,
				     (acpi_physical_address) local_fadt->
				     V1_gpe0_blk);
	acpi_tb_init_generic_address(&local_fadt->xgpe1_blk, 0,
				     (acpi_physical_address) local_fadt->
				     V1_gpe1_blk);

	/* Create separate GAS structs for the PM1 Enable registers */

	acpi_tb_init_generic_address(&acpi_gbl_xpm1a_enable,
				     (u8) ACPI_DIV_2(acpi_gbl_FADT->
						     pm1_evt_len),
				     (acpi_physical_address)
				     (local_fadt->xpm1a_evt_blk.address +
				      ACPI_DIV_2(acpi_gbl_FADT->pm1_evt_len)));

	/* PM1B is optional; leave null if not present */

	if (local_fadt->xpm1b_evt_blk.address) {
		acpi_tb_init_generic_address(&acpi_gbl_xpm1b_enable,
					     (u8) ACPI_DIV_2(acpi_gbl_FADT->
							     pm1_evt_len),
					     (acpi_physical_address)
					     (local_fadt->xpm1b_evt_blk.
					      address +
					      ACPI_DIV_2(acpi_gbl_FADT->
							 pm1_evt_len)));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_fadt2
 *
 * PARAMETERS:  local_fadt      - Pointer to new FADT
 *              original_fadt   - Pointer to old FADT
 *
 * RETURN:      None, populates local_fadt
 *
 * DESCRIPTION: Convert an ACPI 2.0 FADT to common internal format.
 *              Handles optional "X" fields.
 *
 ******************************************************************************/

static void
acpi_tb_convert_fadt2(struct fadt_descriptor_rev2 *local_fadt,
		      struct fadt_descriptor_rev2 *original_fadt)
{

	/* We have an ACPI 2.0 FADT but we must copy it to our local buffer */

	ACPI_MEMCPY(local_fadt, original_fadt,
		    sizeof(struct fadt_descriptor_rev2));

	/*
	 * "X" fields are optional extensions to the original V1.0 fields, so
	 * we must selectively expand V1.0 fields if the corresponding X field
	 * is zero.
	 */
	if (!(local_fadt->xfirmware_ctrl)) {
		ACPI_STORE_ADDRESS(local_fadt->xfirmware_ctrl,
				   local_fadt->V1_firmware_ctrl);
	}

	if (!(local_fadt->Xdsdt)) {
		ACPI_STORE_ADDRESS(local_fadt->Xdsdt, local_fadt->V1_dsdt);
	}

	if (!(local_fadt->xpm1a_evt_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xpm1a_evt_blk,
					     local_fadt->pm1_evt_len,
					     (acpi_physical_address)
					     local_fadt->V1_pm1a_evt_blk);
	}

	if (!(local_fadt->xpm1b_evt_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xpm1b_evt_blk,
					     local_fadt->pm1_evt_len,
					     (acpi_physical_address)
					     local_fadt->V1_pm1b_evt_blk);
	}

	if (!(local_fadt->xpm1a_cnt_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xpm1a_cnt_blk,
					     local_fadt->pm1_cnt_len,
					     (acpi_physical_address)
					     local_fadt->V1_pm1a_cnt_blk);
	}

	if (!(local_fadt->xpm1b_cnt_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xpm1b_cnt_blk,
					     local_fadt->pm1_cnt_len,
					     (acpi_physical_address)
					     local_fadt->V1_pm1b_cnt_blk);
	}

	if (!(local_fadt->xpm2_cnt_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xpm2_cnt_blk,
					     local_fadt->pm2_cnt_len,
					     (acpi_physical_address)
					     local_fadt->V1_pm2_cnt_blk);
	}

	if (!(local_fadt->xpm_tmr_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xpm_tmr_blk,
					     local_fadt->pm_tm_len,
					     (acpi_physical_address)
					     local_fadt->V1_pm_tmr_blk);
	}

	if (!(local_fadt->xgpe0_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xgpe0_blk,
					     0,
					     (acpi_physical_address)
					     local_fadt->V1_gpe0_blk);
	}

	if (!(local_fadt->xgpe1_blk.address)) {
		acpi_tb_init_generic_address(&local_fadt->xgpe1_blk,
					     0,
					     (acpi_physical_address)
					     local_fadt->V1_gpe1_blk);
	}

	/* Create separate GAS structs for the PM1 Enable registers */

	acpi_tb_init_generic_address(&acpi_gbl_xpm1a_enable,
				     (u8) ACPI_DIV_2(acpi_gbl_FADT->
						     pm1_evt_len),
				     (acpi_physical_address)
				     (local_fadt->xpm1a_evt_blk.address +
				      ACPI_DIV_2(acpi_gbl_FADT->pm1_evt_len)));

	acpi_gbl_xpm1a_enable.address_space_id =
	    local_fadt->xpm1a_evt_blk.address_space_id;

	/* PM1B is optional; leave null if not present */

	if (local_fadt->xpm1b_evt_blk.address) {
		acpi_tb_init_generic_address(&acpi_gbl_xpm1b_enable,
					     (u8) ACPI_DIV_2(acpi_gbl_FADT->
							     pm1_evt_len),
					     (acpi_physical_address)
					     (local_fadt->xpm1b_evt_blk.
					      address +
					      ACPI_DIV_2(acpi_gbl_FADT->
							 pm1_evt_len)));

		acpi_gbl_xpm1b_enable.address_space_id =
		    local_fadt->xpm1b_evt_blk.address_space_id;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_convert_table_fadt
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Converts a BIOS supplied ACPI 1.0 FADT to a local
 *              ACPI 2.0 FADT. If the BIOS supplied a 2.0 FADT then it is simply
 *              copied to the local FADT.  The ACPI CA software uses this
 *              local FADT. Thus a significant amount of special #ifdef
 *              type codeing is saved.
 *
 ******************************************************************************/

acpi_status acpi_tb_convert_table_fadt(void)
{
	struct fadt_descriptor_rev2 *local_fadt;
	struct acpi_table_desc *table_desc;

	ACPI_FUNCTION_TRACE("tb_convert_table_fadt");

	/*
	 * acpi_gbl_FADT is valid. Validate the FADT length. The table must be
	 * at least as long as the version 1.0 FADT
	 */
	if (acpi_gbl_FADT->length < sizeof(struct fadt_descriptor_rev1)) {
		ACPI_REPORT_ERROR(("FADT is invalid, too short: 0x%X\n",
				   acpi_gbl_FADT->length));
		return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
	}

	/* Allocate buffer for the ACPI 2.0(+) FADT */

	local_fadt = ACPI_MEM_CALLOCATE(sizeof(struct fadt_descriptor_rev2));
	if (!local_fadt) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	if (acpi_gbl_FADT->revision >= FADT2_REVISION_ID) {
		if (acpi_gbl_FADT->length < sizeof(struct fadt_descriptor_rev2)) {
			/* Length is too short to be a V2.0 table */

			ACPI_REPORT_WARNING(("Inconsistent FADT length (0x%X) and revision (0x%X), using FADT V1.0 portion of table\n", acpi_gbl_FADT->length, acpi_gbl_FADT->revision));

			acpi_tb_convert_fadt1(local_fadt,
					      (void *)acpi_gbl_FADT);
		} else {
			/* Valid V2.0 table */

			acpi_tb_convert_fadt2(local_fadt, acpi_gbl_FADT);
		}
	} else {
		/* Valid V1.0 table */

		acpi_tb_convert_fadt1(local_fadt, (void *)acpi_gbl_FADT);
	}

	/* Global FADT pointer will point to the new common V2.0 FADT */

	acpi_gbl_FADT = local_fadt;
	acpi_gbl_FADT->length = sizeof(FADT_DESCRIPTOR);

	/* Free the original table */

	table_desc = acpi_gbl_table_lists[ACPI_TABLE_FADT].next;
	acpi_tb_delete_single_table(table_desc);

	/* Install the new table */

	table_desc->pointer =
	    ACPI_CAST_PTR(struct acpi_table_header, acpi_gbl_FADT);
	table_desc->allocation = ACPI_MEM_ALLOCATED;
	table_desc->length = sizeof(struct fadt_descriptor_rev2);

	/* Dump the entire FADT */

	ACPI_DEBUG_PRINT((ACPI_DB_TABLES,
			  "Hex dump of common internal FADT, size %d (%X)\n",
			  acpi_gbl_FADT->length, acpi_gbl_FADT->length));
	ACPI_DUMP_BUFFER((u8 *) (acpi_gbl_FADT), acpi_gbl_FADT->length);

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_build_common_facs
 *
 * PARAMETERS:  table_info      - Info for currently installed FACS
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert ACPI 1.0 and ACPI 2.0 FACS to a common internal
 *              table format.
 *
 ******************************************************************************/

acpi_status acpi_tb_build_common_facs(struct acpi_table_desc *table_info)
{

	ACPI_FUNCTION_TRACE("tb_build_common_facs");

	/* Absolute minimum length is 24, but the ACPI spec says 64 */

	if (acpi_gbl_FACS->length < 24) {
		ACPI_REPORT_ERROR(("Invalid FACS table length: 0x%X\n",
				   acpi_gbl_FACS->length));
		return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
	}

	if (acpi_gbl_FACS->length < 64) {
		ACPI_REPORT_WARNING(("FACS is shorter than the ACPI specification allows: 0x%X, using anyway\n", acpi_gbl_FACS->length));
	}

	/* Copy fields to the new FACS */

	acpi_gbl_common_fACS.global_lock = &(acpi_gbl_FACS->global_lock);

	if ((acpi_gbl_RSDP->revision < 2) ||
	    (acpi_gbl_FACS->length < 32) ||
	    (!(acpi_gbl_FACS->xfirmware_waking_vector))) {
		/* ACPI 1.0 FACS or short table or optional X_ field is zero */

		acpi_gbl_common_fACS.firmware_waking_vector = ACPI_CAST_PTR(u64,
									    &
									    (acpi_gbl_FACS->
									     firmware_waking_vector));
		acpi_gbl_common_fACS.vector_width = 32;
	} else {
		/* ACPI 2.0 FACS with valid X_ field */

		acpi_gbl_common_fACS.firmware_waking_vector =
		    &acpi_gbl_FACS->xfirmware_waking_vector;
		acpi_gbl_common_fACS.vector_width = 64;
	}

	return_ACPI_STATUS(AE_OK);
}
