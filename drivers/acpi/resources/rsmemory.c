/*******************************************************************************
 *
 * Module Name: rsmem24 - Memory resource descriptors
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
ACPI_MODULE_NAME("rsmemory")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_memory24
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
acpi_rs_get_memory24(union aml_resource * aml,
		     u16 aml_resource_length, struct acpi_resource * resource)
{
	ACPI_FUNCTION_TRACE("rs_get_memory24");

	/* Get the Read/Write bit */

	resource->data.memory24.read_write_attribute =
	    (aml->memory24.information & 0x01);

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Base Alignment
	 * Range Length
	 */
	acpi_rs_move_data(&resource->data.memory24.minimum,
			  &aml->memory24.minimum, 4, ACPI_MOVE_TYPE_16_TO_32);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_MEMORY24;
	resource->length = ACPI_SIZEOF_RESOURCE(struct acpi_resource_memory24);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_memory24
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
acpi_rs_set_memory24(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_memory24");

	/* Set the Information Byte */

	aml->memory24.information = (u8)
	    (resource->data.memory24.read_write_attribute & 0x01);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Base Alignment
	 * Range Length
	 */
	acpi_rs_move_data(&aml->memory24.minimum,
			  &resource->data.memory24.minimum, 4,
			  ACPI_MOVE_TYPE_32_TO_16);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_MEMORY24,
				    sizeof(struct aml_resource_memory24), aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_memory32
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
acpi_rs_get_memory32(union aml_resource *aml,
		     u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_memory32");

	/* Get the Read/Write bit */

	resource->data.memory32.read_write_attribute =
	    (aml->memory32.information & 0x01);

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Base Alignment
	 * Range Length
	 */
	acpi_rs_move_data(&resource->data.memory32.minimum,
			  &aml->memory32.minimum, 4, ACPI_MOVE_TYPE_32_TO_32);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_MEMORY32;
	resource->length = ACPI_SIZEOF_RESOURCE(struct acpi_resource_memory32);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_memory32
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
acpi_rs_set_memory32(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_memory32");

	/* Set the Information Byte */

	aml->memory32.information = (u8)
	    (resource->data.memory32.read_write_attribute & 0x01);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Base Alignment
	 * Range Length
	 */
	acpi_rs_move_data(&aml->memory32.minimum,
			  &resource->data.memory32.minimum, 4,
			  ACPI_MOVE_TYPE_32_TO_32);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_MEMORY32,
				    sizeof(struct aml_resource_memory32), aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_fixed_memory32
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
acpi_rs_get_fixed_memory32(union aml_resource *aml,
			   u16 aml_resource_length,
			   struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_fixed_memory32");

	/* Get the Read/Write bit */

	resource->data.fixed_memory32.read_write_attribute =
	    (aml->fixed_memory32.information & 0x01);

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Base Address
	 * Range Length
	 */
	ACPI_MOVE_32_TO_32(&resource->data.fixed_memory32.address,
			   &aml->fixed_memory32.address);
	ACPI_MOVE_32_TO_32(&resource->data.fixed_memory32.address_length,
			   &aml->fixed_memory32.address_length);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_FIXED_MEMORY32;
	resource->length =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_fixed_memory32);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_fixed_memory32
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
acpi_rs_set_fixed_memory32(struct acpi_resource *resource,
			   union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_fixed_memory32");

	/* Set the Information Byte */

	aml->fixed_memory32.information = (u8)
	    (resource->data.fixed_memory32.read_write_attribute & 0x01);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Base Address
	 * Range Length
	 */
	ACPI_MOVE_32_TO_32(&aml->fixed_memory32.address,
			   &resource->data.fixed_memory32.address);
	ACPI_MOVE_32_TO_32(&aml->fixed_memory32.address_length,
			   &resource->data.fixed_memory32.address_length);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_FIXED_MEMORY32,
				    sizeof(struct aml_resource_fixed_memory32),
				    aml);
	return_ACPI_STATUS(AE_OK);
}
