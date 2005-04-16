/******************************************************************************
 *
 * Module Name: tbxfroot - Find the root ACPI table (RSDT)
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
	 ACPI_MODULE_NAME    ("tbxfroot")


/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_find_table
 *
 * PARAMETERS:  Signature           - String with ACPI table signature
 *              oem_id              - String with the table OEM ID
 *              oem_table_id        - String with the OEM Table ID.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find an ACPI table (in the RSDT/XSDT) that matches the
 *              Signature, OEM ID and OEM Table ID.
 *
 ******************************************************************************/

acpi_status
acpi_tb_find_table (
	char                            *signature,
	char                            *oem_id,
	char                            *oem_table_id,
	struct acpi_table_header        **table_ptr)
{
	acpi_status                     status;
	struct acpi_table_header        *table;


	ACPI_FUNCTION_TRACE ("tb_find_table");


	/* Validate string lengths */

	if ((ACPI_STRLEN (signature)  > ACPI_NAME_SIZE) ||
		(ACPI_STRLEN (oem_id)     > sizeof (table->oem_id)) ||
		(ACPI_STRLEN (oem_table_id) > sizeof (table->oem_table_id))) {
		return_ACPI_STATUS (AE_AML_STRING_LIMIT);
	}

	if (!ACPI_STRNCMP (signature, DSDT_SIG, ACPI_NAME_SIZE)) {
		/*
		 * The DSDT pointer is contained in the FADT, not the RSDT.
		 * This code should suffice, because the only code that would perform
		 * a "find" on the DSDT is the data_table_region() AML opcode -- in
		 * which case, the DSDT is guaranteed to be already loaded.
		 * If this becomes insufficient, the FADT will have to be found first.
		 */
		if (!acpi_gbl_DSDT) {
			return_ACPI_STATUS (AE_NO_ACPI_TABLES);
		}

		table = acpi_gbl_DSDT;
	}
	else {
		/* Find the table */

		status = acpi_get_firmware_table (signature, 1,
				   ACPI_LOGICAL_ADDRESSING, &table);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Check oem_id and oem_table_id */

	if ((oem_id[0]     && ACPI_STRNCMP (
			 oem_id, table->oem_id, sizeof (table->oem_id))) ||
		(oem_table_id[0] && ACPI_STRNCMP (
				   oem_table_id, table->oem_table_id, sizeof (table->oem_table_id)))) {
		return_ACPI_STATUS (AE_AML_NAME_NOT_FOUND);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "Found table [%4.4s]\n", table->signature));
	*table_ptr = table;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_get_firmware_table
 *
 * PARAMETERS:  Signature       - Any ACPI table signature
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *              Flags           - Physical/Virtual support
 *              table_pointer   - Where a buffer containing the table is
 *                                returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get an ACPI table. A buffer is
 *              allocated for the table and returned in table_pointer.
 *              This table will be a complete table including the header.
 *
 ******************************************************************************/

acpi_status
acpi_get_firmware_table (
	acpi_string                     signature,
	u32                             instance,
	u32                             flags,
	struct acpi_table_header        **table_pointer)
{
	acpi_status                     status;
	struct acpi_pointer             address;
	struct acpi_table_header        *header = NULL;
	struct acpi_table_desc          *table_info = NULL;
	struct acpi_table_desc          *rsdt_info;
	u32                             table_count;
	u32                             i;
	u32                             j;


	ACPI_FUNCTION_TRACE ("acpi_get_firmware_table");


	/*
	 * Ensure that at least the table manager is initialized.  We don't
	 * require that the entire ACPI subsystem is up for this interface.
	 * If we have a buffer, we must have a length too
	 */
	if ((instance == 0)     ||
		(!signature)        ||
		(!table_pointer)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Ensure that we have a RSDP */

	if (!acpi_gbl_RSDP) {
		/* Get the RSDP */

		status = acpi_os_get_root_pointer (flags, &address);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "RSDP not found\n"));
			return_ACPI_STATUS (AE_NO_ACPI_TABLES);
		}

		/* Map and validate the RSDP */

		if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
			status = acpi_os_map_memory (address.pointer.physical, sizeof (struct rsdp_descriptor),
					  (void *) &acpi_gbl_RSDP);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}
		else {
			acpi_gbl_RSDP = address.pointer.logical;
		}

		/* The signature and checksum must both be correct */

		if (ACPI_STRNCMP ((char *) acpi_gbl_RSDP, RSDP_SIG, sizeof (RSDP_SIG)-1) != 0) {
			/* Nope, BAD Signature */

			return_ACPI_STATUS (AE_BAD_SIGNATURE);
		}

		if (acpi_tb_checksum (acpi_gbl_RSDP, ACPI_RSDP_CHECKSUM_LENGTH) != 0) {
			/* Nope, BAD Checksum */

			return_ACPI_STATUS (AE_BAD_CHECKSUM);
		}
	}

	/* Get the RSDT address via the RSDP */

	acpi_tb_get_rsdt_address (&address);
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"RSDP located at %p, RSDT physical=%8.8X%8.8X \n",
		acpi_gbl_RSDP,
		ACPI_FORMAT_UINT64 (address.pointer.value)));

	/* Insert processor_mode flags */

	address.pointer_type |= flags;

	/* Get and validate the RSDT */

	rsdt_info = ACPI_MEM_CALLOCATE (sizeof (struct acpi_table_desc));
	if (!rsdt_info) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_tb_get_table (&address, rsdt_info);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	status = acpi_tb_validate_rsdt (rsdt_info->pointer);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Allocate a scratch table header and table descriptor */

	header = ACPI_MEM_ALLOCATE (sizeof (struct acpi_table_header));
	if (!header) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	table_info = ACPI_MEM_ALLOCATE (sizeof (struct acpi_table_desc));
	if (!table_info) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Get the number of table pointers within the RSDT */

	table_count = acpi_tb_get_table_count (acpi_gbl_RSDP, rsdt_info->pointer);
	address.pointer_type = acpi_gbl_table_flags | flags;

	/*
	 * Search the RSDT/XSDT for the correct instance of the
	 * requested table
	 */
	for (i = 0, j = 0; i < table_count; i++) {
		/* Get the next table pointer, handle RSDT vs. XSDT */

		if (acpi_gbl_RSDP->revision < 2) {
			address.pointer.value = (ACPI_CAST_PTR (
				RSDT_DESCRIPTOR, rsdt_info->pointer))->table_offset_entry[i];
		}
		else {
			address.pointer.value = (ACPI_CAST_PTR (
				XSDT_DESCRIPTOR, rsdt_info->pointer))->table_offset_entry[i];
		}

		/* Get the table header */

		status = acpi_tb_get_table_header (&address, header);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* Compare table signatures and table instance */

		if (!ACPI_STRNCMP (header->signature, signature, ACPI_NAME_SIZE)) {
			/* An instance of the table was found */

			j++;
			if (j >= instance) {
				/* Found the correct instance, get the entire table */

				status = acpi_tb_get_table_body (&address, header, table_info);
				if (ACPI_FAILURE (status)) {
					goto cleanup;
				}

				*table_pointer = table_info->pointer;
				goto cleanup;
			}
		}
	}

	/* Did not find the table */

	status = AE_NOT_EXIST;


cleanup:
	acpi_os_unmap_memory (rsdt_info->pointer, (acpi_size) rsdt_info->pointer->length);
	ACPI_MEM_FREE (rsdt_info);

	if (header) {
		ACPI_MEM_FREE (header);
	}
	if (table_info) {
		ACPI_MEM_FREE (table_info);
	}
	return_ACPI_STATUS (status);
}
EXPORT_SYMBOL(acpi_get_firmware_table);


/* TBD: Move to a new file */

#if ACPI_MACHINE_WIDTH != 16

/*******************************************************************************
 *
 * FUNCTION:    acpi_find_root_pointer
 *
 * PARAMETERS:  **rsdp_address          - Where to place the RSDP address
 *              Flags                   - Logical/Physical addressing
 *
 * RETURN:      Status, Physical address of the RSDP
 *
 * DESCRIPTION: Find the RSDP
 *
 ******************************************************************************/

acpi_status
acpi_find_root_pointer (
	u32                             flags,
	struct acpi_pointer             *rsdp_address)
{
	struct acpi_table_desc          table_info;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("acpi_find_root_pointer");


	/* Get the RSDP */

	status = acpi_tb_find_rsdp (&table_info, flags);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"RSDP structure not found, %s Flags=%X\n",
			acpi_format_exception (status), flags));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	rsdp_address->pointer_type = ACPI_PHYSICAL_POINTER;
	rsdp_address->pointer.physical = table_info.physical_address;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_scan_memory_for_rsdp
 *
 * PARAMETERS:  start_address       - Starting pointer for search
 *              Length              - Maximum length to search
 *
 * RETURN:      Pointer to the RSDP if found, otherwise NULL.
 *
 * DESCRIPTION: Search a block of memory for the RSDP signature
 *
 ******************************************************************************/

u8 *
acpi_tb_scan_memory_for_rsdp (
	u8                              *start_address,
	u32                             length)
{
	u8                              *mem_rover;
	u8                              *end_address;
	u8                              checksum;


	ACPI_FUNCTION_TRACE ("tb_scan_memory_for_rsdp");


	end_address = start_address + length;

	/* Search from given start address for the requested length */

	for (mem_rover = start_address; mem_rover < end_address;
		 mem_rover += ACPI_RSDP_SCAN_STEP) {
		/* The signature and checksum must both be correct */

		if (ACPI_STRNCMP ((char *) mem_rover, RSDP_SIG, sizeof (RSDP_SIG)-1) != 0) {
			/* No signature match, keep looking */

			continue;
		}

		/* Signature matches, check the appropriate checksum */

		if ((ACPI_CAST_PTR (struct rsdp_descriptor, mem_rover))->revision < 2) {
			/* ACPI version 1.0 */

			checksum = acpi_tb_checksum (mem_rover, ACPI_RSDP_CHECKSUM_LENGTH);
		}
		else {
			/* Post ACPI 1.0, use extended_checksum */

			checksum = acpi_tb_checksum (mem_rover, ACPI_RSDP_XCHECKSUM_LENGTH);
		}

		if (checksum == 0) {
			/* Checksum valid, we have found a valid RSDP */

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				"RSDP located at physical address %p\n", mem_rover));
			return_PTR (mem_rover);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Found an RSDP at physical address %p, but it has a bad checksum\n",
			mem_rover));
	}

	/* Searched entire block, no RSDP was found */

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Searched entire block, no valid RSDP was found.\n"));
	return_PTR (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_find_rsdp
 *
 * PARAMETERS:  *table_info             - Where the table info is returned
 *              Flags                   - Current memory mode (logical vs.
 *                                        physical addressing)
 *
 * RETURN:      Status, RSDP physical address
 *
 * DESCRIPTION: search lower 1_mbyte of memory for the root system descriptor
 *              pointer structure.  If it is found, set *RSDP to point to it.
 *
 *              NOTE1: The RSDp must be either in the first 1_k of the Extended
 *              BIOS Data Area or between E0000 and FFFFF (From ACPI Spec.)
 *              Only a 32-bit physical address is necessary.
 *
 *              NOTE2: This function is always available, regardless of the
 *              initialization state of the rest of ACPI.
 *
 ******************************************************************************/

acpi_status
acpi_tb_find_rsdp (
	struct acpi_table_desc          *table_info,
	u32                             flags)
{
	u8                              *table_ptr;
	u8                              *mem_rover;
	u32                             physical_address;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("tb_find_rsdp");


	/*
	 * Scan supports either 1) Logical addressing or 2) Physical addressing
	 */
	if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
		/*
		 * 1a) Get the location of the EBDA
		 */
		status = acpi_os_map_memory ((acpi_physical_address) ACPI_EBDA_PTR_LOCATION,
				  ACPI_EBDA_PTR_LENGTH,
				  (void *) &table_ptr);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not map memory at %8.8X for length %X\n",
				ACPI_EBDA_PTR_LOCATION, ACPI_EBDA_PTR_LENGTH));
			return_ACPI_STATUS (status);
		}

		ACPI_MOVE_16_TO_32 (&physical_address, table_ptr);
		physical_address <<= 4;                 /* Convert segment to physical address */
		acpi_os_unmap_memory (table_ptr, ACPI_EBDA_PTR_LENGTH);

		/* EBDA present? */

		if (physical_address > 0x400) {
			/*
			 * 1b) Search EBDA paragraphs (EBDa is required to be a minimum of 1_k length)
			 */
			status = acpi_os_map_memory ((acpi_physical_address) physical_address,
					  ACPI_EBDA_WINDOW_SIZE,
					  (void *) &table_ptr);
			if (ACPI_FAILURE (status)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Could not map memory at %8.8X for length %X\n",
					physical_address, ACPI_EBDA_WINDOW_SIZE));
				return_ACPI_STATUS (status);
			}

			mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr, ACPI_EBDA_WINDOW_SIZE);
			acpi_os_unmap_memory (table_ptr, ACPI_EBDA_WINDOW_SIZE);

			if (mem_rover) {
				/* Found it, return the physical address */

				physical_address += ACPI_PTR_DIFF (mem_rover, table_ptr);

				table_info->physical_address = (acpi_physical_address) physical_address;
				return_ACPI_STATUS (AE_OK);
			}
		}

		/*
		 * 2) Search upper memory: 16-byte boundaries in E0000h-FFFFFh
		 */
		status = acpi_os_map_memory ((acpi_physical_address) ACPI_HI_RSDP_WINDOW_BASE,
				  ACPI_HI_RSDP_WINDOW_SIZE,
				  (void *) &table_ptr);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not map memory at %8.8X for length %X\n",
				ACPI_HI_RSDP_WINDOW_BASE, ACPI_HI_RSDP_WINDOW_SIZE));
			return_ACPI_STATUS (status);
		}

		mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr, ACPI_HI_RSDP_WINDOW_SIZE);
		acpi_os_unmap_memory (table_ptr, ACPI_HI_RSDP_WINDOW_SIZE);

		if (mem_rover) {
			/* Found it, return the physical address */

			physical_address = ACPI_HI_RSDP_WINDOW_BASE + ACPI_PTR_DIFF (mem_rover, table_ptr);

			table_info->physical_address = (acpi_physical_address) physical_address;
			return_ACPI_STATUS (AE_OK);
		}
	}

	/*
	 * Physical addressing
	 */
	else {
		/*
		 * 1a) Get the location of the EBDA
		 */
		ACPI_MOVE_16_TO_32 (&physical_address, ACPI_EBDA_PTR_LOCATION);
		physical_address <<= 4;     /* Convert segment to physical address */

		/* EBDA present? */

		if (physical_address > 0x400) {
			/*
			 * 1b) Search EBDA paragraphs (EBDa is required to be a minimum of 1_k length)
			 */
			mem_rover = acpi_tb_scan_memory_for_rsdp (ACPI_PHYSADDR_TO_PTR (physical_address),
					  ACPI_EBDA_WINDOW_SIZE);
			if (mem_rover) {
				/* Found it, return the physical address */

				table_info->physical_address = ACPI_TO_INTEGER (mem_rover);
				return_ACPI_STATUS (AE_OK);
			}
		}

		/*
		 * 2) Search upper memory: 16-byte boundaries in E0000h-FFFFFh
		 */
		mem_rover = acpi_tb_scan_memory_for_rsdp (ACPI_PHYSADDR_TO_PTR (ACPI_HI_RSDP_WINDOW_BASE),
				  ACPI_HI_RSDP_WINDOW_SIZE);
		if (mem_rover) {
			/* Found it, return the physical address */

			table_info->physical_address = ACPI_TO_INTEGER (mem_rover);
			return_ACPI_STATUS (AE_OK);
		}
	}

	/* RSDP signature was not found */

	return_ACPI_STATUS (AE_NOT_FOUND);
}

#endif

