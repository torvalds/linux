/*******************************************************************************
 *
 * Module Name: rsaddr - Address resource descriptors (16/32/64)
 *
 ******************************************************************************/

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

#include <acpi/acpi.h>
#include <acpi/acresrc.h>

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsaddr")

/* Local prototypes */
static void
acpi_rs_decode_general_flags(union acpi_resource_data *resource, u8 flags);

static u8 acpi_rs_encode_general_flags(union acpi_resource_data *resource);

static void
acpi_rs_decode_specific_flags(union acpi_resource_data *resource, u8 flags);

static u8 acpi_rs_encode_specific_flags(union acpi_resource_data *resource);

static void
acpi_rs_set_address_common(union aml_resource *aml,
			   struct acpi_resource *resource);

static u8
acpi_rs_get_address_common(struct acpi_resource *resource,
			   union aml_resource *aml);

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_decode_general_flags
 *
 * PARAMETERS:  Resource            - Address resource data struct
 *              Flags               - Raw AML flag byte
 *
 * RETURN:      Decoded flag bits in resource struct
 *
 * DESCRIPTION: Decode a general flag byte to an address resource struct
 *
 ******************************************************************************/

static void
acpi_rs_decode_general_flags(union acpi_resource_data *resource, u8 flags)
{
	ACPI_FUNCTION_ENTRY();

	/* Producer / Consumer - flag bit[0] */

	resource->address.producer_consumer = (u32) (flags & 0x01);

	/* Decode (_DEC) - flag bit[1] */

	resource->address.decode = (u32) ((flags >> 1) & 0x01);

	/* Min Address Fixed (_MIF) - flag bit[2] */

	resource->address.min_address_fixed = (u32) ((flags >> 2) & 0x01);

	/* Max Address Fixed (_MAF) - flag bit[3] */

	resource->address.max_address_fixed = (u32) ((flags >> 3) & 0x01);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_encode_general_flags
 *
 * PARAMETERS:  Resource            - Address resource data struct
 *
 * RETURN:      Encoded general flag byte
 *
 * DESCRIPTION: Construct a general flag byte from an address resource struct
 *
 ******************************************************************************/

static u8 acpi_rs_encode_general_flags(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	return ((u8)

		/* Producer / Consumer - flag bit[0] */
		((resource->address.producer_consumer & 0x01) |
		 /* Decode (_DEC) - flag bit[1] */
		 ((resource->address.decode & 0x01) << 1) |
		 /* Min Address Fixed (_MIF) - flag bit[2] */
		 ((resource->address.min_address_fixed & 0x01) << 2) |
		 /* Max Address Fixed (_MAF) - flag bit[3] */
		 ((resource->address.max_address_fixed & 0x01) << 3))
	    );
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_decode_specific_flags
 *
 * PARAMETERS:  Resource            - Address resource data struct
 *              Flags               - Raw AML flag byte
 *
 * RETURN:      Decoded flag bits in attribute struct
 *
 * DESCRIPTION: Decode a type-specific flag byte to an attribute struct.
 *              Type-specific flags are only defined for the Memory and IO
 *              resource types.
 *
 ******************************************************************************/

static void
acpi_rs_decode_specific_flags(union acpi_resource_data *resource, u8 flags)
{
	ACPI_FUNCTION_ENTRY();

	if (resource->address.resource_type == ACPI_MEMORY_RANGE) {
		/* Write Status (_RW) - flag bit[0] */

		resource->address.attribute.memory.read_write_attribute =
		    (u16) (flags & 0x01);

		/* Memory Attributes (_MEM) - flag bits[2:1] */

		resource->address.attribute.memory.cache_attribute =
		    (u16) ((flags >> 1) & 0x03);
	} else if (resource->address.resource_type == ACPI_IO_RANGE) {
		/* Ranges (_RNG) - flag bits[1:0] */

		resource->address.attribute.io.range_attribute =
		    (u16) (flags & 0x03);

		/* Translations (_TTP and _TRS) - flag bits[5:4] */

		resource->address.attribute.io.translation_attribute =
		    (u16) ((flags >> 4) & 0x03);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_encode_specific_flags
 *
 * PARAMETERS:  Resource            - Address resource data struct
 *
 * RETURN:      Encoded type-specific flag byte
 *
 * DESCRIPTION: Construct a type-specific flag byte from an attribute struct.
 *              Type-specific flags are only defined for the Memory and IO
 *              resource types.
 *
 ******************************************************************************/

static u8 acpi_rs_encode_specific_flags(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	if (resource->address.resource_type == ACPI_MEMORY_RANGE) {
		return ((u8)

			/* Write Status (_RW) - flag bit[0] */
			((resource->address.attribute.memory.
			  read_write_attribute & 0x01) |
			 /* Memory Attributes (_MEM) - flag bits[2:1] */
			 ((resource->address.attribute.memory.
			   cache_attribute & 0x03) << 1)));
	} else if (resource->address.resource_type == ACPI_IO_RANGE) {
		return ((u8)

			/* Ranges (_RNG) - flag bits[1:0] */
			((resource->address.attribute.io.
			  range_attribute & 0x03) |
			 /* Translations (_TTP and _TRS) - flag bits[5:4] */
			 ((resource->address.attribute.io.
			   translation_attribute & 0x03) << 4)));
	}

	return (0);
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

static void
acpi_rs_set_address_common(union aml_resource *aml,
			   struct acpi_resource *resource)
{
	ACPI_FUNCTION_ENTRY();

	/* Set the Resource Type (Memory, Io, bus_number, etc.) */

	aml->address.resource_type = (u8) resource->data.address.resource_type;

	/* Set the general flags */

	aml->address.flags = acpi_rs_encode_general_flags(&resource->data);

	/* Set the type-specific flags */

	aml->address.specific_flags =
	    acpi_rs_encode_specific_flags(&resource->data);
}

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

static u8
acpi_rs_get_address_common(struct acpi_resource *resource,
			   union aml_resource *aml)
{
	ACPI_FUNCTION_ENTRY();

	/* Validate resource type */

	if ((aml->address.resource_type > 2)
	    && (aml->address.resource_type < 0xC0)) {
		return (FALSE);
	}

	/* Get the Resource Type (Memory, Io, bus_number, etc.) */

	resource->data.address.resource_type = aml->address.resource_type;

	/* Get the General Flags */

	acpi_rs_decode_general_flags(&resource->data, aml->address.flags);

	/* Get the Type-Specific Flags */

	acpi_rs_decode_specific_flags(&resource->data,
				      aml->address.specific_flags);
	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_address16
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              aml_resource_length - Length of the resource from the AML header
 *              Resource            - Where the internal resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML resource descriptor to the corresponding
 *              internal resource descriptor, simplifying bitflags and handling
 *              alignment and endian issues if necessary.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_address16(union aml_resource * aml,
		      u16 aml_resource_length, struct acpi_resource * resource)
{
	ACPI_FUNCTION_TRACE("rs_get_address16");

	/* Get the Resource Type, general flags, and type-specific flags */

	if (!acpi_rs_get_address_common(resource, aml)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	acpi_rs_move_data(&resource->data.address16.granularity,
			  &aml->address16.granularity, 5,
			  ACPI_MOVE_TYPE_16_TO_32);

	/* Get the optional resource_source (index and string) */

	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address16) +
	    acpi_rs_get_resource_source(aml_resource_length,
					sizeof(struct aml_resource_address16),
					&resource->data.address16.
					resource_source, aml, NULL);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_ADDRESS16;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_address16
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_address16(struct acpi_resource *resource, union aml_resource *aml)
{
	acpi_size descriptor_length;

	ACPI_FUNCTION_TRACE("rs_set_address16");

	/* Set the Resource Type, General Flags, and Type-Specific Flags */

	acpi_rs_set_address_common(aml, resource);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	acpi_rs_move_data(&aml->address16.granularity,
			  &resource->data.address16.granularity, 5,
			  ACPI_MOVE_TYPE_32_TO_16);

	/* Resource Source Index and Resource Source are optional */

	descriptor_length = acpi_rs_set_resource_source(aml,
							sizeof(struct
							       aml_resource_address16),
							&resource->data.
							address16.
							resource_source);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_ADDRESS16,
				    descriptor_length, aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_address32
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              aml_resource_length - Length of the resource from the AML header
 *              Resource            - Where the internal resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML resource descriptor to the corresponding
 *              internal resource descriptor, simplifying bitflags and handling
 *              alignment and endian issues if necessary.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_address32(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource)
{

	ACPI_FUNCTION_TRACE("rs_get_address32");

	/* Get the Resource Type, general flags, and type-specific flags */

	if (!acpi_rs_get_address_common(resource, (void *)aml)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	acpi_rs_move_data(&resource->data.address32.granularity,
			  &aml->address32.granularity, 5,
			  ACPI_MOVE_TYPE_32_TO_32);

	/* Get the optional resource_source (index and string) */

	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address32) +
	    acpi_rs_get_resource_source(aml_resource_length,
					sizeof(struct aml_resource_address32),
					&resource->data.address32.
					resource_source, aml, NULL);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_ADDRESS32;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_address32
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_address32(struct acpi_resource *resource, union aml_resource *aml)
{
	acpi_size descriptor_length;

	ACPI_FUNCTION_TRACE("rs_set_address32");

	/* Set the Resource Type, General Flags, and Type-Specific Flags */

	acpi_rs_set_address_common(aml, resource);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	acpi_rs_move_data(&aml->address32.granularity,
			  &resource->data.address32.granularity, 5,
			  ACPI_MOVE_TYPE_32_TO_32);

	/* Resource Source Index and Resource Source are optional */

	descriptor_length = acpi_rs_set_resource_source(aml,
							sizeof(struct
							       aml_resource_address32),
							&resource->data.
							address32.
							resource_source);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_ADDRESS32,
				    descriptor_length, aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_address64
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              aml_resource_length - Length of the resource from the AML header
 *              Resource            - Where the internal resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML resource descriptor to the corresponding
 *              internal resource descriptor, simplifying bitflags and handling
 *              alignment and endian issues if necessary.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_address64(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_address64");

	/* Get the Resource Type, general Flags, and type-specific Flags */

	if (!acpi_rs_get_address_common(resource, aml)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	acpi_rs_move_data(&resource->data.address64.granularity,
			  &aml->address64.granularity, 5,
			  ACPI_MOVE_TYPE_64_TO_64);

	/* Get the optional resource_source (index and string) */

	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address64) +
	    acpi_rs_get_resource_source(aml_resource_length,
					sizeof(struct aml_resource_address64),
					&resource->data.address64.
					resource_source, aml, NULL);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_ADDRESS64;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_address64
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_address64(struct acpi_resource *resource, union aml_resource *aml)
{
	acpi_size descriptor_length;

	ACPI_FUNCTION_TRACE("rs_set_address64");

	/* Set the Resource Type, General Flags, and Type-Specific Flags */

	acpi_rs_set_address_common(aml, resource);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 */
	acpi_rs_move_data(&aml->address64.granularity,
			  &resource->data.address64.granularity, 5,
			  ACPI_MOVE_TYPE_64_TO_64);

	/* Resource Source Index and Resource Source are optional */

	descriptor_length = acpi_rs_set_resource_source(aml,
							sizeof(struct
							       aml_resource_address64),
							&resource->data.
							address64.
							resource_source);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_ADDRESS64,
				    descriptor_length, aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_ext_address64
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              aml_resource_length - Length of the resource from the AML header
 *              Resource            - Where the internal resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML resource descriptor to the corresponding
 *              internal resource descriptor, simplifying bitflags and handling
 *              alignment and endian issues if necessary.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_ext_address64(union aml_resource *aml,
			  u16 aml_resource_length,
			  struct acpi_resource *resource)
{

	ACPI_FUNCTION_TRACE("rs_get_ext_address64");

	/* Get the Resource Type, general flags, and type-specific flags */

	if (!acpi_rs_get_address_common(resource, aml)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	/*
	 * Get and validate the Revision ID
	 * Note: Only one revision ID is currently supported
	 */
	resource->data.ext_address64.revision_iD =
	    aml->ext_address64.revision_iD;
	if (aml->ext_address64.revision_iD !=
	    AML_RESOURCE_EXTENDED_ADDRESS_REVISION) {
		return_ACPI_STATUS(AE_SUPPORT);
	}

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 * Type-Specific Attribute
	 */
	acpi_rs_move_data(&resource->data.ext_address64.granularity,
			  &aml->ext_address64.granularity, 6,
			  ACPI_MOVE_TYPE_64_TO_64);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64;
	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_extended_address64);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_ext_address64
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_ext_address64(struct acpi_resource *resource,
			  union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_ext_address64");

	/* Set the Resource Type, General Flags, and Type-Specific Flags */

	acpi_rs_set_address_common(aml, resource);

	/* Only one Revision ID is currently supported */

	aml->ext_address64.revision_iD = AML_RESOURCE_EXTENDED_ADDRESS_REVISION;
	aml->ext_address64.reserved = 0;

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Address Granularity
	 * Address Range Minimum
	 * Address Range Maximum
	 * Address Translation Offset
	 * Address Length
	 * Type-Specific Attribute
	 */
	acpi_rs_move_data(&aml->ext_address64.granularity,
			  &resource->data.address64.granularity, 6,
			  ACPI_MOVE_TYPE_64_TO_64);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64,
				    sizeof(struct
					   aml_resource_extended_address64),
				    aml);
	return_ACPI_STATUS(AE_OK);
}
