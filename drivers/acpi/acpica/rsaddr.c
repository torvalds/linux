// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: rsaddr - Address resource descriptors (16/32/64)
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"

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
	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.address16.address.granularity),
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
	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.address32.address.granularity),
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
	{ACPI_RSC_MOVE64, ACPI_RS_OFFSET(data.address64.address.granularity),
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

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.ext_address64.revision_ID),
	 AML_OFFSET(ext_address64.revision_ID),
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
	{ACPI_RSC_MOVE64,
	 ACPI_RS_OFFSET(data.ext_address64.address.granularity),
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

	/* General flags - Consume, Decode, min_fixed, max_fixed */

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
 * PARAMETERS:  resource            - Pointer to the internal resource struct
 *              aml                 - Pointer to the AML resource descriptor
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
	struct aml_resource_address address;

	ACPI_FUNCTION_ENTRY();

	/* Avoid undefined behavior: member access within misaligned address */

	memcpy(&address, aml, sizeof(address));

	/* Validate the Resource Type */

	if ((address.resource_type > 2) &&
	    (address.resource_type < 0xC0) && (address.resource_type != 0x0A)) {
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
		    address.specific_flags;
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_address_common
 *
 * PARAMETERS:  aml                 - Pointer to the AML resource descriptor
 *              resource            - Pointer to the internal resource struct
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
