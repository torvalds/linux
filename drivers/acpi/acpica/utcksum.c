// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: utcksum - Support generating table checksums
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acutils.h"

/* This module used for application-level code only */

#define _COMPONENT          ACPI_CA_DISASSEMBLER
ACPI_MODULE_NAME("utcksum")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_verify_checksum
 *
 * PARAMETERS:  table               - ACPI table to verify
 *              length              - Length of entire table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Verifies that the table checksums to zero. Optionally returns
 *              exception on bad checksum.
 *              Note: We don't have to check for a CDAT here, since CDAT is
 *              not in the RSDT/XSDT, and the CDAT table is never installed
 *              via ACPICA.
 *
 ******************************************************************************/
acpi_status acpi_ut_verify_checksum(struct acpi_table_header *table, u32 length)
{
	u8 checksum;

	/*
	 * FACS/S3PT:
	 * They are the odd tables, have no standard ACPI header and no checksum
	 */
	if (ACPI_COMPARE_NAMESEG(table->signature, ACPI_SIG_S3PT) ||
	    ACPI_COMPARE_NAMESEG(table->signature, ACPI_SIG_FACS)) {
		return (AE_OK);
	}

	/* Compute the checksum on the table */

	length = table->length;
	checksum =
	    acpi_ut_generate_checksum(ACPI_CAST_PTR(u8, table), length,
				      table->checksum);

	/* Computed checksum matches table? */

	if (checksum != table->checksum) {
		ACPI_BIOS_WARNING((AE_INFO,
				   "Incorrect checksum in table [%4.4s] - 0x%2.2X, "
				   "should be 0x%2.2X",
				   table->signature, table->checksum,
				   table->checksum - checksum));

#if (ACPI_CHECKSUM_ABORT)
		return (AE_BAD_CHECKSUM);
#endif
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_verify_cdat_checksum
 *
 * PARAMETERS:  table               - CDAT ACPI table to verify
 *              length              - Length of entire table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Verifies that the CDAT table checksums to zero. Optionally
 *              returns an exception on bad checksum.
 *
 ******************************************************************************/

acpi_status
acpi_ut_verify_cdat_checksum(struct acpi_table_cdat *cdat_table, u32 length)
{
	u8 checksum;

	/* Compute the checksum on the table */

	checksum = acpi_ut_generate_checksum(ACPI_CAST_PTR(u8, cdat_table),
					     cdat_table->length,
					     cdat_table->checksum);

	/* Computed checksum matches table? */

	if (checksum != cdat_table->checksum) {
		ACPI_BIOS_WARNING((AE_INFO,
				   "Incorrect checksum in table [%4.4s] - 0x%2.2X, "
				   "should be 0x%2.2X",
				   acpi_gbl_CDAT, cdat_table->checksum,
				   checksum));

#if (ACPI_CHECKSUM_ABORT)
		return (AE_BAD_CHECKSUM);
#endif
	}

	cdat_table->checksum = checksum;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_generate_checksum
 *
 * PARAMETERS:  table               - Pointer to table to be checksummed
 *              length              - Length of the table
 *              original_checksum   - Value of the checksum field
 *
 * RETURN:      8 bit checksum of buffer
 *
 * DESCRIPTION: Computes an 8 bit checksum of the table.
 *
 ******************************************************************************/

u8 acpi_ut_generate_checksum(void *table, u32 length, u8 original_checksum)
{
	u8 checksum;

	/* Sum the entire table as-is */

	checksum = acpi_ut_checksum((u8 *)table, length);

	/* Subtract off the existing checksum value in the table */

	checksum = (u8)(checksum - original_checksum);

	/* Compute and return the final checksum */

	checksum = (u8)(0 - checksum);
	return (checksum);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_checksum
 *
 * PARAMETERS:  buffer          - Pointer to memory region to be checked
 *              length          - Length of this memory region
 *
 * RETURN:      Checksum (u8)
 *
 * DESCRIPTION: Calculates circular checksum of memory region.
 *
 ******************************************************************************/

u8 acpi_ut_checksum(u8 *buffer, u32 length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end) {
		sum = (u8)(sum + *(buffer++));
	}

	return (sum);
}
