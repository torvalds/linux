/******************************************************************************
 *
 * Module Name: utaddress - op_region address range check
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
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utaddress")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_add_address_range
 *
 * PARAMETERS:  space_id            - Address space ID
 *              address             - op_region start address
 *              length              - op_region length
 *              region_node         - op_region namespace node
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add the Operation Region address range to the global list.
 *              The only supported Space IDs are Memory and I/O. Called when
 *              the op_region address/length operands are fully evaluated.
 *
 * MUTEX:       Locks the namespace
 *
 * NOTE: Because this interface is only called when an op_region argument
 * list is evaluated, there cannot be any duplicate region_nodes.
 * Duplicate Address/Length values are allowed, however, so that multiple
 * address conflicts can be detected.
 *
 ******************************************************************************/
acpi_status
acpi_ut_add_address_range(acpi_adr_space_type space_id,
			  acpi_physical_address address,
			  u32 length, struct acpi_namespace_node *region_node)
{
	struct acpi_address_range *range_info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_add_address_range);

	if ((space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
	    (space_id != ACPI_ADR_SPACE_SYSTEM_IO)) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Allocate/init a new info block, add it to the appropriate list */

	range_info = ACPI_ALLOCATE(sizeof(struct acpi_address_range));
	if (!range_info) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	range_info->start_address = address;
	range_info->end_address = (address + length - 1);
	range_info->region_node = region_node;

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(range_info);
		return_ACPI_STATUS(status);
	}

	range_info->next = acpi_gbl_address_range_list[space_id];
	acpi_gbl_address_range_list[space_id] = range_info;

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "\nAdded [%4.4s] address range: 0x%p-0x%p\n",
			  acpi_ut_get_node_name(range_info->region_node),
			  ACPI_CAST_PTR(void, address),
			  ACPI_CAST_PTR(void, range_info->end_address)));

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_remove_address_range
 *
 * PARAMETERS:  space_id            - Address space ID
 *              region_node         - op_region namespace node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Remove the Operation Region from the global list. The only
 *              supported Space IDs are Memory and I/O. Called when an
 *              op_region is deleted.
 *
 * MUTEX:       Assumes the namespace is locked
 *
 ******************************************************************************/

void
acpi_ut_remove_address_range(acpi_adr_space_type space_id,
			     struct acpi_namespace_node *region_node)
{
	struct acpi_address_range *range_info;
	struct acpi_address_range *prev;

	ACPI_FUNCTION_TRACE(ut_remove_address_range);

	if ((space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
	    (space_id != ACPI_ADR_SPACE_SYSTEM_IO)) {
		return_VOID;
	}

	/* Get the appropriate list head and check the list */

	range_info = prev = acpi_gbl_address_range_list[space_id];
	while (range_info) {
		if (range_info->region_node == region_node) {
			if (range_info == prev) {	/* Found at list head */
				acpi_gbl_address_range_list[space_id] =
				    range_info->next;
			} else {
				prev->next = range_info->next;
			}

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "\nRemoved [%4.4s] address range: 0x%p-0x%p\n",
					  acpi_ut_get_node_name(range_info->
								region_node),
					  ACPI_CAST_PTR(void,
							range_info->
							start_address),
					  ACPI_CAST_PTR(void,
							range_info->
							end_address)));

			ACPI_FREE(range_info);
			return_VOID;
		}

		prev = range_info;
		range_info = range_info->next;
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_check_address_range
 *
 * PARAMETERS:  space_id            - Address space ID
 *              address             - Start address
 *              length              - Length of address range
 *              warn                - TRUE if warning on overlap desired
 *
 * RETURN:      Count of the number of conflicts detected. Zero is always
 *              returned for Space IDs other than Memory or I/O.
 *
 * DESCRIPTION: Check if the input address range overlaps any of the
 *              ASL operation region address ranges. The only supported
 *              Space IDs are Memory and I/O.
 *
 * MUTEX:       Assumes the namespace is locked.
 *
 ******************************************************************************/

u32
acpi_ut_check_address_range(acpi_adr_space_type space_id,
			    acpi_physical_address address, u32 length, u8 warn)
{
	struct acpi_address_range *range_info;
	acpi_physical_address end_address;
	char *pathname;
	u32 overlap_count = 0;

	ACPI_FUNCTION_TRACE(ut_check_address_range);

	if ((space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
	    (space_id != ACPI_ADR_SPACE_SYSTEM_IO)) {
		return_VALUE(0);
	}

	range_info = acpi_gbl_address_range_list[space_id];
	end_address = address + length - 1;

	/* Check entire list for all possible conflicts */

	while (range_info) {
		/*
		 * Check if the requested Address/Length overlaps this address_range.
		 * Four cases to consider:
		 *
		 * 1) Input address/length is contained completely in the address range
		 * 2) Input address/length overlaps range at the range start
		 * 3) Input address/length overlaps range at the range end
		 * 4) Input address/length completely encompasses the range
		 */
		if ((address <= range_info->end_address) &&
		    (end_address >= range_info->start_address)) {

			/* Found an address range overlap */

			overlap_count++;
			if (warn) {	/* Optional warning message */
				pathname =
				    acpi_ns_get_external_pathname(range_info->
								  region_node);

				ACPI_WARNING((AE_INFO,
					      "0x%p-0x%p %s conflicts with Region %s %d",
					      ACPI_CAST_PTR(void, address),
					      ACPI_CAST_PTR(void, end_address),
					      acpi_ut_get_region_name(space_id),
					      pathname, overlap_count));
				ACPI_FREE(pathname);
			}
		}

		range_info = range_info->next;
	}

	return_VALUE(overlap_count);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_address_lists
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete all global address range lists (called during
 *              subsystem shutdown).
 *
 ******************************************************************************/

void acpi_ut_delete_address_lists(void)
{
	struct acpi_address_range *next;
	struct acpi_address_range *range_info;
	int i;

	/* Delete all elements in all address range lists */

	for (i = 0; i < ACPI_ADDRESS_RANGE_MAX; i++) {
		next = acpi_gbl_address_range_list[i];

		while (next) {
			range_info = next;
			next = range_info->next;
			ACPI_FREE(range_info);
		}

		acpi_gbl_address_range_list[i] = NULL;
	}
}
