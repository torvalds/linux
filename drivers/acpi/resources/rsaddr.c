/*******************************************************************************
 *
 * Module Name: rsaddr - Address resource descriptors (16/32/64)
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
#include <acpi/acresrc.h>

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsaddr")

/*******************************************************************************
 *
 * acpi_rs_convert_address16 - All WORD (16-bit) address resources
 *
 ******************************************************************************/
struct acpi_rsconvert_info acpi_rs_convert_address16[5] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_ADDRESS16,
	 ACPI_RS_SIZE(struct acpi_resource_address16),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_address16)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_ADDRESS16,
	 sizeof(struct aml_resource_address16),
	 0},

	/* Resource Type, General Flags, and Type-Specific Flags */

	{ACPI_RSC_ADDRESS, 0, 0, 0},

	/*
	 * These fields are contiguous in both the source and destination:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.address16.granularity),
	 AML_OFFSET(address16.granularity),
	 5},

	/* Optional resource_source (Index and String) */

	{ACPI_RSC_SOURCE, ACPI_RS_OFFSET(data.address16.resource_source),
	 0,
	 sizeof(struct aml_resource_address16)}
};

/*******************************************************************************
 *
 * acpi_rs_convert_address32 - All DWORD (32-bit) address resources
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_address32[5] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_ADDRESS32,
	 ACPI_RS_SIZE(struct acpi_resource_address32),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_address32)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_ADDRESS32,
	 sizeof(struct aml_resource_address32),
	 0},

	/* Resource Type, General Flags, and Type-Specific Flags */

	{ACPI_RSC_ADDRESS, 0, 0, 0},

	/*
	 * These fields are contiguous in both the source and destination:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.address32.granularity),
	 AML_OFFSET(address32.granularity),
	 5},

	/* Optional resource_source (Index and String) */

	{ACPI_RSC_SOURCE, ACPI_RS_OFFSET(data.address32.resource_source),
	 0,
	 sizeof(struct aml_resource_address32)}
};

/*******************************************************************************
 *
 * acpi_rs_convert_address64 - All QWORD (64-bit) address resources
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_address64[5] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_ADDRESS64,
	 ACPI_RS_SIZE(struct acpi_resource_address64),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_address64)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_ADDRESS64,
	 sizeof(struct aml_resource_address64),
	 0},

	/* Resource Type, General Flags, and Type-Specific Flags */

	{ACPI_RSC_ADDRESS, 0, 0, 0},

	/*
	 * These fields are contiguous in both the source and destination:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	{ACPI_RSC_MOVE64, ACPI_RS_OFFSET(data.address64.granularity),
	 AML_OFFSET(address64.granularity),
	 5},

	/* Optional resource_source (Index and String) */

	{ACPI_RSC_SOURCE, ACPI_RS_OFFSET(data.address64.resource_source),
	 0,
	 sizeof(struct aml_resource_address64)}
};

/*******************************************************************************
 *
 * acpi_rs_convert_ext_address64 - All Extended (64-bit) address resources
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_ext_address64[5] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64,
	 ACPI_RS_SIZE(struct acpi_resource_extended_address64),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_ext_address64)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64,
	 sizeof(struct aml_resource_extended_address64),
	 0},

	/* Resource Type, General Flags, and Type-Specific Flags */

	{ACPI_RSC_ADDRESS, 0, 0, 0},

	/* Revision ID */

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.ext_address64.revision_iD),
	 AML_OFFSET(ext_address64.revision_iD),
	 1},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 * Type-Specific Attribute
	 */
	{ACPI_RSC_MOVE64, ACPI_RS_OFFSET(data.ext_address64.granularity),
	 AML_OFFSET(ext_address64.granularity),
	 6}
};

/*******************************************************************************
 *
 * acpi_rs_convert_general_flags - Flags common to all address descriptors
 *
 ******************************************************************************/

static struct acpi_rsconvert_info acpi_rs_convert_general_flags[6] = {
	{ACPI_RSC_FLAGINIT, 0, AML_OFFSET(address.flags),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_general_flags)},

	/* Resource Type (Memory, Io, bus_number, etc.) */

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.address.resource_type),
	 AML_OFFSET(address.resource_type),
	 1},

	/* General Flags - Consume, Decode, min_fixed, max_fixed */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.producer_consumer),
	 AML_OFFSET(address.flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.decode),
	 AML_OFFSET(address.flags),
	 1},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.min_address_fixed),
	 AML_OFFSET(address.flags),
	 2},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.max_address_fixed),
	 AML_OFFSET(address.flags),
	 3}
};

/*******************************************************************************
 *
 * acpi_rs_convert_mem_flags - Flags common to Memory address descriptors
 *
 ******************************************************************************/

static struct acpi_rsconvert_info acpi_rs_convert_mem_flags[5] = {
	{ACPI_RSC_FLAGINIT, 0, AML_OFFSET(address.specific_flags),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_mem_flags)},

	/* Memory-specific flags */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.info.mem.write_protect),
	 AML_OFFSET(address.specific_flags),
	 0},

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.address.info.mem.caching),
	 AML_OFFSET(address.specific_flags),
	 1},

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.address.info.mem.range_type),
	 AML_OFFSET(address.specific_flags),
	 3},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.info.mem.translation),
	 AML_OFFSET(address.specific_flags),
	 5}
};

/*******************************************************************************
 *
 * acpi_rs_convert_io_flags - Flags common to I/O address descriptors
 *
 ******************************************************************************/

static struct acpi_rsconvert_info acpi_rs_convert_io_flags[4] = {
	{ACPI_RSC_FLAGINIT, 0, AML_OFFSET(address.specific_flags),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_io_flags)},

	/* I/O-specific flags */

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.address.info.io.range_type),
	 AML_OFFSET(address.specific_flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.address.info.io.translation),
	 AML_OFFSET(address.specific_flags),
	 4},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.address.info.io.translation_type),
	 AML_OFFSET(address.specific_flags),
	 5}
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_address_common
 *
 * PARAMETERS:  Resource            - Pointer to the internal resource struct
 *              Aml                 - Pointer to the AML resource descriptor
 *
 * RETURN:      TRUE if the resource_type field is OK, FALSE otherwise
 *
 * DESCRIPTION: Convert common flag fields from a raw AML resource descriptor
 *              to an internal resource descriptor
 *
 ******************************************************************************/

u8
acpi_rs_get_address_common(struct acpi_resource *resource,
			   union aml_resource *aml)
{
	ACPI_FUNCTION_ENTRY();

	/* Validate the Resource Type */

	if ((aml->address.resource_type > 2)
	    && (aml->address.resource_type < 0xC0)) {
		return (FALSE);
	}

	/* Get the Resource Type and General Flags */

	(void)acpi_rs_convert_aml_to_resource(resource, aml,
					      acpi_rs_convert_general_flags);

	/* Get the Type-Specific Flags (Memory and I/O descriptors only) */

	if (resource->data.address.resource_type == ACPI_MEMORY_RANGE) {
		(void)acpi_rs_convert_aml_to_resource(resource, aml,
						      acpi_rs_convert_mem_flags);
	} else if (resource->data.address.resource_type == ACPI_IO_RANGE) {
		(void)acpi_rs_convert_aml_to_resource(resource, aml,
						      acpi_rs_convert_io_flags);
	} else {
		/* Generic resource type, just grab the type_specific byte */

		resource->data.address.info.type_specific =
		    aml->address.specific_flags;
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_address_common
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              Resource            - Pointer to the internal resource struct
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert common flag fields from a resource descriptor to an
 *              AML descriptor
 *
 ******************************************************************************/

void
acpi_rs_set_address_common(union aml_resource *aml,
			   struct acpi_resource *resource)
{
	ACPI_FUNCTION_ENTRY();

	/* Set the Resource Type and General Flags */

	(void)acpi_rs_convert_resource_to_aml(resource, aml,
					      acpi_rs_convert_general_flags);

	/* Set the Type-Specific Flags (Memory and I/O descriptors only) */

	if (resource->data.address.resource_type == ACPI_MEMORY_RANGE) {
		(void)acpi_rs_convert_resource_to_aml(resource, aml,
						      acpi_rs_convert_mem_flags);
	} else if (resource->data.address.resource_type == ACPI_IO_RANGE) {
		(void)acpi_rs_convert_resource_to_aml(resource, aml,
						      acpi_rs_convert_io_flags);
	} else {
		/* Generic resource type, just copy the type_specific byte */

		aml->address.specific_flags =
		    resource->data.address.info.type_specific;
	}
}
