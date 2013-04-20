/******************************************************************************
 *
 * Module Name: tbxfroot - Find the root ACPI table (RSDT)
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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
ACPI_MODULE_NAME("tbxfroot")

/* Local prototypes */
static u8 *acpi_tb_scan_memory_for_rsdp(u8 * start_address, u32 length);

static acpi_status acpi_tb_validate_rsdp(struct acpi_table_rsdp *rsdp);

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_validate_rsdp
 *
 * PARAMETERS:  rsdp                - Pointer to unvalidated RSDP
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate the RSDP (ptr)
 *
 ******************************************************************************/

static acpi_status acpi_tb_validate_rsdp(struct acpi_table_rsdp *rsdp)
{

	/*
	 * The signature and checksum must both be correct
	 *
	 * Note: Sometimes there exists more than one RSDP in memory; the valid
	 * RSDP has a valid checksum, all others have an invalid checksum.
	 */
	if (ACPI_STRNCMP((char *)rsdp, ACPI_SIG_RSDP,
			 sizeof(ACPI_SIG_RSDP) - 1) != 0) {

		/* Nope, BAD Signature */

		return (AE_BAD_SIGNATURE);
	}

	/* Check the standard checksum */

	if (acpi_tb_checksum((u8 *) rsdp, ACPI_RSDP_CHECKSUM_LENGTH) != 0) {
		return (AE_BAD_CHECKSUM);
	}

	/* Check extended checksum if table version >= 2 */

	if ((rsdp->revision >= 2) &&
	    (acpi_tb_checksum((u8 *) rsdp, ACPI_RSDP_XCHECKSUM_LENGTH) != 0)) {
		return (AE_BAD_CHECKSUM);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_find_root_pointer
 *
 * PARAMETERS:  table_address           - Where the table pointer is returned
 *
 * RETURN:      Status, RSDP physical address
 *
 * DESCRIPTION: Search lower 1Mbyte of memory for the root system descriptor
 *              pointer structure. If it is found, set *RSDP to point to it.
 *
 * NOTE1:       The RSDP must be either in the first 1K of the Extended
 *              BIOS Data Area or between E0000 and FFFFF (From ACPI Spec.)
 *              Only a 32-bit physical address is necessary.
 *
 * NOTE2:       This function is always available, regardless of the
 *              initialization state of the rest of ACPI.
 *
 ******************************************************************************/

acpi_status acpi_find_root_pointer(acpi_size *table_address)
{
	u8 *table_ptr;
	u8 *mem_rover;
	u32 physical_address;

	ACPI_FUNCTION_TRACE(acpi_find_root_pointer);

	/* 1a) Get the location of the Extended BIOS Data Area (EBDA) */

	table_ptr = acpi_os_map_memory((acpi_physical_address)
				       ACPI_EBDA_PTR_LOCATION,
				       ACPI_EBDA_PTR_LENGTH);
	if (!table_ptr) {
		ACPI_ERROR((AE_INFO,
			    "Could not map memory at 0x%8.8X for length %u",
			    ACPI_EBDA_PTR_LOCATION, ACPI_EBDA_PTR_LENGTH));

		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	ACPI_MOVE_16_TO_32(&physical_address, table_ptr);

	/* Convert segment part to physical address */

	physical_address <<= 4;
	acpi_os_unmap_memory(table_ptr, ACPI_EBDA_PTR_LENGTH);

	/* EBDA present? */

	if (physical_address > 0x400) {
		/*
		 * 1b) Search EBDA paragraphs (EBDA is required to be a
		 *     minimum of 1K length)
		 */
		table_ptr = acpi_os_map_memory((acpi_physical_address)
					       physical_address,
					       ACPI_EBDA_WINDOW_SIZE);
		if (!table_ptr) {
			ACPI_ERROR((AE_INFO,
				    "Could not map memory at 0x%8.8X for length %u",
				    physical_address, ACPI_EBDA_WINDOW_SIZE));

			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		mem_rover =
		    acpi_tb_scan_memory_for_rsdp(table_ptr,
						 ACPI_EBDA_WINDOW_SIZE);
		acpi_os_unmap_memory(table_ptr, ACPI_EBDA_WINDOW_SIZE);

		if (mem_rover) {

			/* Return the physical address */

			physical_address +=
			    (u32) ACPI_PTR_DIFF(mem_rover, table_ptr);

			*table_address = physical_address;
			return_ACPI_STATUS(AE_OK);
		}
	}

	/*
	 * 2) Search upper memory: 16-byte boundaries in E0000h-FFFFFh
	 */
	table_ptr = acpi_os_map_memory((acpi_physical_address)
				       ACPI_HI_RSDP_WINDOW_BASE,
				       ACPI_HI_RSDP_WINDOW_SIZE);

	if (!table_ptr) {
		ACPI_ERROR((AE_INFO,
			    "Could not map memory at 0x%8.8X for length %u",
			    ACPI_HI_RSDP_WINDOW_BASE,
			    ACPI_HI_RSDP_WINDOW_SIZE));

		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	mem_rover =
	    acpi_tb_scan_memory_for_rsdp(table_ptr, ACPI_HI_RSDP_WINDOW_SIZE);
	acpi_os_unmap_memory(table_ptr, ACPI_HI_RSDP_WINDOW_SIZE);

	if (mem_rover) {

		/* Return the physical address */

		physical_address = (u32)
		    (ACPI_HI_RSDP_WINDOW_BASE +
		     ACPI_PTR_DIFF(mem_rover, table_ptr));

		*table_address = physical_address;
		return_ACPI_STATUS(AE_OK);
	}

	/* A valid RSDP was not found */

	ACPI_BIOS_ERROR((AE_INFO, "A valid RSDP was not found"));
	return_ACPI_STATUS(AE_NOT_FOUND);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_scan_memory_for_rsdp
 *
 * PARAMETERS:  start_address       - Starting pointer for search
 *              length              - Maximum length to search
 *
 * RETURN:      Pointer to the RSDP if found, otherwise NULL.
 *
 * DESCRIPTION: Search a block of memory for the RSDP signature
 *
 ******************************************************************************/
static u8 *acpi_tb_scan_memory_for_rsdp(u8 * start_address, u32 length)
{
	acpi_status status;
	u8 *mem_rover;
	u8 *end_address;

	ACPI_FUNCTION_TRACE(tb_scan_memory_for_rsdp);

	end_address = start_address + length;

	/* Search from given start address for the requested length */

	for (mem_rover = start_address; mem_rover < end_address;
	     mem_rover += ACPI_RSDP_SCAN_STEP) {

		/* The RSDP signature and checksum must both be correct */

		status =
		    acpi_tb_validate_rsdp(ACPI_CAST_PTR
					  (struct acpi_table_rsdp, mem_rover));
		if (ACPI_SUCCESS(status)) {

			/* Sig and checksum valid, we have found a real RSDP */

			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "RSDP located at physical address %p\n",
					  mem_rover));
			return_PTR(mem_rover);
		}

		/* No sig match or bad checksum, keep searching */
	}

	/* Searched entire block, no RSDP was found */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Searched entire block from %p, valid RSDP was not found\n",
			  start_address));
	return_PTR(NULL);
}
