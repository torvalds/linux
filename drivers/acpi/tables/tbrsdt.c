/******************************************************************************
 *
 * Module Name: tbrsdt - ACPI RSDT table utilities
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
ACPI_MODULE_NAME("tbrsdt")

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_verify_rsdp
 *
 * PARAMETERS:  Address         - RSDP (Pointer to RSDT)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate the RSDP (ptr) and RSDT (table)
 *
 ******************************************************************************/
acpi_status acpi_tb_verify_rsdp(struct acpi_pointer *address)
{
	struct acpi_table_desc table_info;
	acpi_status status;
	struct rsdp_descriptor *rsdp;

	ACPI_FUNCTION_TRACE(tb_verify_rsdp);

	switch (address->pointer_type) {
	case ACPI_LOGICAL_POINTER:

		rsdp = address->pointer.logical;
		break;

	case ACPI_PHYSICAL_POINTER:
		/*
		 * Obtain access to the RSDP structure
		 */
		status = acpi_os_map_memory(address->pointer.physical,
					    sizeof(struct rsdp_descriptor),
					    (void *)&rsdp);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
		break;

	default:
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Verify RSDP signature and checksum */

	status = acpi_tb_validate_rsdp(rsdp);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* The RSDP supplied is OK */

	table_info.pointer = ACPI_CAST_PTR(struct acpi_table_header, rsdp);
	table_info.length = sizeof(struct rsdp_descriptor);
	table_info.allocation = ACPI_MEM_MAPPED;

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor(ACPI_TABLE_ID_RSDP, &table_info);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Save the RSDP in a global for easy access */

	acpi_gbl_RSDP =
	    ACPI_CAST_PTR(struct rsdp_descriptor, table_info.pointer);
	return_ACPI_STATUS(status);

	/* Error exit */
      cleanup:

	if (acpi_gbl_table_flags & ACPI_PHYSICAL_POINTER) {
		acpi_os_unmap_memory(rsdp, sizeof(struct rsdp_descriptor));
	}
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_rsdt_address
 *
 * PARAMETERS:  out_address         - Where the address is returned
 *
 * RETURN:      None, Address
 *
 * DESCRIPTION: Extract the address of either the RSDT or XSDT, depending on the
 *              version of the RSDP and whether the XSDT pointer is valid
 *
 ******************************************************************************/

void acpi_tb_get_rsdt_address(struct acpi_pointer *out_address)
{

	ACPI_FUNCTION_ENTRY();

	out_address->pointer_type =
	    acpi_gbl_table_flags | ACPI_LOGICAL_ADDRESSING;

	/* Use XSDT if it is present */

	if ((acpi_gbl_RSDP->revision >= 2) &&
	    acpi_gbl_RSDP->xsdt_physical_address) {
		out_address->pointer.value =
		    acpi_gbl_RSDP->xsdt_physical_address;
		acpi_gbl_root_table_type = ACPI_TABLE_TYPE_XSDT;
	} else {
		/* No XSDT, use the RSDT */

		out_address->pointer.value =
		    acpi_gbl_RSDP->rsdt_physical_address;
		acpi_gbl_root_table_type = ACPI_TABLE_TYPE_RSDT;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_validate_rsdt
 *
 * PARAMETERS:  table_ptr       - Addressable pointer to the RSDT.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate signature for the RSDT or XSDT
 *
 ******************************************************************************/

acpi_status acpi_tb_validate_rsdt(struct acpi_table_header *table_ptr)
{
	char *signature;

	ACPI_FUNCTION_ENTRY();

	/* Search for appropriate signature, RSDT or XSDT */

	if (acpi_gbl_root_table_type == ACPI_TABLE_TYPE_RSDT) {
		signature = RSDT_SIG;
	} else {
		signature = XSDT_SIG;
	}

	if (!ACPI_COMPARE_NAME(table_ptr->signature, signature)) {

		/* Invalid RSDT or XSDT signature */

		ACPI_ERROR((AE_INFO,
			    "Invalid signature where RSDP indicates RSDT/XSDT should be located. RSDP:"));

		ACPI_DUMP_BUFFER(acpi_gbl_RSDP, 20);

		ACPI_ERROR((AE_INFO,
			    "RSDT/XSDT signature at %X (%p) is invalid",
			    acpi_gbl_RSDP->rsdt_physical_address,
			    (void *)(acpi_native_uint) acpi_gbl_RSDP->
			    rsdt_physical_address));

		if (acpi_gbl_root_table_type == ACPI_TABLE_TYPE_RSDT) {
			ACPI_ERROR((AE_INFO, "Looking for RSDT"));
		} else {
			ACPI_ERROR((AE_INFO, "Looking for XSDT"));
		}

		ACPI_DUMP_BUFFER((char *)table_ptr, 48);
		return (AE_BAD_SIGNATURE);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_get_table_rsdt
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate the RSDP (ptr) and RSDT (table)
 *
 ******************************************************************************/

acpi_status acpi_tb_get_table_rsdt(void)
{
	struct acpi_table_desc table_info;
	acpi_status status;
	struct acpi_pointer address;

	ACPI_FUNCTION_TRACE(tb_get_table_rsdt);

	/* Get the RSDT/XSDT via the RSDP */

	acpi_tb_get_rsdt_address(&address);

	table_info.type = ACPI_TABLE_ID_XSDT;
	status = acpi_tb_get_table(&address, &table_info);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not get the RSDT/XSDT"));
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "RSDP located at %p, points to RSDT physical=%8.8X%8.8X\n",
			  acpi_gbl_RSDP,
			  ACPI_FORMAT_UINT64(address.pointer.value)));

	/* Check the RSDT or XSDT signature */

	status = acpi_tb_validate_rsdt(table_info.pointer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the number of tables defined in the RSDT or XSDT */

	acpi_gbl_rsdt_table_count = acpi_tb_get_table_count(acpi_gbl_RSDP,
							    table_info.pointer);

	/* Convert and/or copy to an XSDT structure */

	status = acpi_tb_convert_to_xsdt(&table_info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor(ACPI_TABLE_ID_XSDT, &table_info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_gbl_XSDT =
	    ACPI_CAST_PTR(struct xsdt_descriptor, table_info.pointer);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "XSDT located at %p\n", acpi_gbl_XSDT));
	return_ACPI_STATUS(status);
}
