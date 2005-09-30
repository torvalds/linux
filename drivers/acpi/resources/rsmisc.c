/*******************************************************************************
 *
 * Module Name: rsmisc - Miscellaneous resource descriptors
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
ACPI_MODULE_NAME("rsmisc")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_generic_reg
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
acpi_rs_get_generic_reg(union aml_resource *aml,
			u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_generic_reg");

	/*
	 * Get the following fields from the AML descriptor:
	 * Address Space ID
	 * Register Bit Width
	 * Register Bit Offset
	 * Access Size
	 * Register Address
	 */
	resource->data.generic_reg.space_id = aml->generic_reg.address_space_id;
	resource->data.generic_reg.bit_width = aml->generic_reg.bit_width;
	resource->data.generic_reg.bit_offset = aml->generic_reg.bit_offset;
	resource->data.generic_reg.access_size = aml->generic_reg.access_size;
	ACPI_MOVE_64_TO_64(&resource->data.generic_reg.address,
			   &aml->generic_reg.address);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_GENERIC_REGISTER;
	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_generic_register);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_generic_reg
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
acpi_rs_set_generic_reg(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_generic_reg");

	/*
	 * Set the following fields in the AML descriptor:
	 * Address Space ID
	 * Register Bit Width
	 * Register Bit Offset
	 * Access Size
	 * Register Address
	 */
	aml->generic_reg.address_space_id =
	    (u8) resource->data.generic_reg.space_id;
	aml->generic_reg.bit_width = (u8) resource->data.generic_reg.bit_width;
	aml->generic_reg.bit_offset =
	    (u8) resource->data.generic_reg.bit_offset;
	aml->generic_reg.access_size =
	    (u8) resource->data.generic_reg.access_size;
	ACPI_MOVE_64_TO_64(&aml->generic_reg.address,
			   &resource->data.generic_reg.address);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_GENERIC_REGISTER,
				    sizeof(struct
					   aml_resource_generic_register), aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_vendor
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
acpi_rs_get_vendor(union aml_resource *aml,
		   u16 aml_resource_length, struct acpi_resource *resource)
{
	u8 *aml_byte_data;

	ACPI_FUNCTION_TRACE("rs_get_vendor");

	/* Determine if this is a large or small vendor specific item */

	if (aml->large_header.descriptor_type & ACPI_RESOURCE_NAME_LARGE) {
		/* Large item, Point to the first vendor byte */

		aml_byte_data =
		    ((u8 *) aml) + sizeof(struct aml_resource_large_header);
	} else {
		/* Small item, Point to the first vendor byte */

		aml_byte_data =
		    ((u8 *) aml) + sizeof(struct aml_resource_small_header);
	}

	/* Copy the vendor-specific bytes */

	ACPI_MEMCPY(resource->data.vendor.byte_data,
		    aml_byte_data, aml_resource_length);
	resource->data.vendor.byte_length = aml_resource_length;

	/*
	 * In order for the struct_size to fall on a 32-bit boundary,
	 * calculate the length of the vendor string and expand the
	 * struct_size to the next 32-bit boundary.
	 */
	resource->type = ACPI_RESOURCE_TYPE_VENDOR;
	resource->length = ACPI_SIZEOF_RESOURCE(struct acpi_resource_vendor) +
	    ACPI_ROUND_UP_to_32_bITS(aml_resource_length);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_vendor
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
acpi_rs_set_vendor(struct acpi_resource *resource, union aml_resource *aml)
{
	u32 resource_length;
	u8 *source;
	u8 *destination;

	ACPI_FUNCTION_TRACE("rs_set_vendor");

	resource_length = resource->data.vendor.byte_length;
	source = ACPI_CAST_PTR(u8, resource->data.vendor.byte_data);

	/* Length determines if this is a large or small resource */

	if (resource_length > 7) {
		/* Large item, get pointer to the data part of the descriptor */

		destination =
		    ((u8 *) aml) + sizeof(struct aml_resource_large_header);

		/* Complete the AML descriptor header */

		acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_VENDOR_LARGE,
					    (u32) (resource_length +
						   sizeof(struct
							  aml_resource_large_header)),
					    aml);
	} else {
		/* Small item, get pointer to the data part of the descriptor */

		destination =
		    ((u8 *) aml) + sizeof(struct aml_resource_small_header);

		/* Complete the AML descriptor header */

		acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_VENDOR_SMALL,
					    (u32) (resource_length +
						   sizeof(struct
							  aml_resource_small_header)),
					    aml);
	}

	/* Copy the vendor-specific bytes */

	ACPI_MEMCPY(destination, source, resource_length);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_start_dpf
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
acpi_rs_get_start_dpf(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_start_dpf");

	/* Get the flags byte if present */

	if (aml_resource_length == 1) {
		/* Get the Compatibility priority */

		resource->data.start_dpf.compatibility_priority =
		    (aml->start_dpf.flags & 0x03);

		if (resource->data.start_dpf.compatibility_priority >= 3) {
			return_ACPI_STATUS(AE_AML_BAD_RESOURCE_VALUE);
		}

		/* Get the Performance/Robustness preference */

		resource->data.start_dpf.performance_robustness =
		    ((aml->start_dpf.flags >> 2) & 0x03);

		if (resource->data.start_dpf.performance_robustness >= 3) {
			return_ACPI_STATUS(AE_AML_BAD_RESOURCE_VALUE);
		}
	} else {
		/* start_dependent_no_pri(), no flags byte, set defaults */

		resource->data.start_dpf.compatibility_priority =
		    ACPI_ACCEPTABLE_CONFIGURATION;

		resource->data.start_dpf.performance_robustness =
		    ACPI_ACCEPTABLE_CONFIGURATION;
	}

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_START_DEPENDENT;
	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_start_dependent);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_start_dpf
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
acpi_rs_set_start_dpf(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_start_dpf");

	/*
	 * The descriptor type field is set based upon whether a byte is needed
	 * to contain Priority data.
	 */
	if (ACPI_ACCEPTABLE_CONFIGURATION ==
	    resource->data.start_dpf.compatibility_priority &&
	    ACPI_ACCEPTABLE_CONFIGURATION ==
	    resource->data.start_dpf.performance_robustness) {
		acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_START_DEPENDENT,
					    sizeof(struct
						   aml_resource_start_dependent_noprio),
					    aml);
	} else {
		acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_START_DEPENDENT,
					    sizeof(struct
						   aml_resource_start_dependent),
					    aml);

		/* Set the Flags byte */

		aml->start_dpf.flags = (u8)
		    (((resource->data.start_dpf.
		       performance_robustness & 0x03) << 2) | (resource->data.
							       start_dpf.
							       compatibility_priority
							       & 0x03));
	}
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_end_dpf
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
acpi_rs_get_end_dpf(union aml_resource *aml,
		    u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_end_dpf");

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_END_DEPENDENT;
	resource->length = (u32) ACPI_RESOURCE_LENGTH;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_end_dpf
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
acpi_rs_set_end_dpf(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_end_dpf");

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_END_DEPENDENT,
				    sizeof(struct aml_resource_end_dependent),
				    aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_end_tag
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
acpi_rs_get_end_tag(union aml_resource *aml,
		    u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_end_tag");

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_END_TAG;
	resource->length = ACPI_RESOURCE_LENGTH;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_end_tag
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
acpi_rs_set_end_tag(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_end_tag");

	/*
	 * Set the Checksum - zero means that the resource data is treated as if
	 * the checksum operation succeeded (ACPI Spec 1.0b Section 6.4.2.8)
	 */
	aml->end_tag.checksum = 0;

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_END_TAG,
				    sizeof(struct aml_resource_end_tag), aml);
	return_ACPI_STATUS(AE_OK);
}
