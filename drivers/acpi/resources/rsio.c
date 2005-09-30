/*******************************************************************************
 *
 * Module Name: rsio - IO and DMA resource descriptors
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
ACPI_MODULE_NAME("rsio")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_io
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
acpi_rs_get_io(union aml_resource *aml,
	       u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_io");

	/* Get the Decode flag */

	resource->data.io.io_decode = aml->io.information & 0x01;

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Alignment
	 * Length
	 */
	ACPI_MOVE_16_TO_32(&resource->data.io.minimum, &aml->io.minimum);
	ACPI_MOVE_16_TO_32(&resource->data.io.maximum, &aml->io.maximum);
	resource->data.io.alignment = aml->io.alignment;
	resource->data.io.address_length = aml->io.address_length;

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_IO;
	resource->length = ACPI_SIZEOF_RESOURCE(struct acpi_resource_io);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_io
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
acpi_rs_set_io(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_io");

	/* I/O Information Byte */

	aml->io.information = (u8) (resource->data.io.io_decode & 0x01);

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Alignment
	 * Length
	 */
	ACPI_MOVE_32_TO_16(&aml->io.minimum, &resource->data.io.minimum);
	ACPI_MOVE_32_TO_16(&aml->io.maximum, &resource->data.io.maximum);
	aml->io.alignment = (u8) resource->data.io.alignment;
	aml->io.address_length = (u8) resource->data.io.address_length;

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_IO,
				    sizeof(struct aml_resource_io), aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_fixed_io
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
acpi_rs_get_fixed_io(union aml_resource *aml,
		     u16 aml_resource_length, struct acpi_resource *resource)
{
	ACPI_FUNCTION_TRACE("rs_get_fixed_io");

	/*
	 * Get the following contiguous fields from the AML descriptor:
	 * Base Address
	 * Length
	 */
	ACPI_MOVE_16_TO_32(&resource->data.fixed_io.address,
			   &aml->fixed_io.address);
	resource->data.fixed_io.address_length = aml->fixed_io.address_length;

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_FIXED_IO;
	resource->length = ACPI_SIZEOF_RESOURCE(struct acpi_resource_fixed_io);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_fixed_io
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
acpi_rs_set_fixed_io(struct acpi_resource *resource, union aml_resource *aml)
{
	ACPI_FUNCTION_TRACE("rs_set_fixed_io");

	/*
	 * Set the following contiguous fields in the AML descriptor:
	 * Base Address
	 * Length
	 */
	ACPI_MOVE_32_TO_16(&aml->fixed_io.address,
			   &resource->data.fixed_io.address);
	aml->fixed_io.address_length =
	    (u8) resource->data.fixed_io.address_length;

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_FIXED_IO,
				    sizeof(struct aml_resource_fixed_io), aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_dma
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
acpi_rs_get_dma(union aml_resource *aml,
		u16 aml_resource_length, struct acpi_resource *resource)
{
	u32 channel_count = 0;
	u32 i;
	u8 temp8;

	ACPI_FUNCTION_TRACE("rs_get_dma");

	/* Decode the DMA channel bits */

	for (i = 0; i < 8; i++) {
		if ((aml->dma.dma_channel_mask >> i) & 0x01) {
			resource->data.dma.channels[channel_count] = i;
			channel_count++;
		}
	}

	resource->length = 0;
	resource->data.dma.channel_count = channel_count;

	/*
	 * Calculate the structure size based upon the number of channels
	 * Note: Zero DMA channels is valid
	 */
	if (channel_count > 0) {
		resource->length = (u32) (channel_count - 1) * 4;
	}

	/* Get the flags: transfer preference, bus mastering, channel speed */

	temp8 = aml->dma.flags;
	resource->data.dma.transfer = temp8 & 0x03;
	resource->data.dma.bus_master = (temp8 >> 2) & 0x01;
	resource->data.dma.type = (temp8 >> 5) & 0x03;

	if (resource->data.dma.transfer == 0x03) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Invalid DMA.Transfer preference (3)\n"));
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_DMA;
	resource->length += ACPI_SIZEOF_RESOURCE(struct acpi_resource_dma);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_dma
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
acpi_rs_set_dma(struct acpi_resource *resource, union aml_resource *aml)
{
	u8 i;

	ACPI_FUNCTION_TRACE("rs_set_dma");

	/* Convert channel list to 8-bit DMA channel bitmask */

	aml->dma.dma_channel_mask = 0;
	for (i = 0; i < resource->data.dma.channel_count; i++) {
		aml->dma.dma_channel_mask |=
		    (1 << resource->data.dma.channels[i]);
	}

	/* Set the DMA Flag bits */

	aml->dma.flags = (u8)
	    (((resource->data.dma.type & 0x03) << 5) |
	     ((resource->data.dma.bus_master & 0x01) << 2) |
	     (resource->data.dma.transfer & 0x03));

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_DMA,
				    sizeof(struct aml_resource_dma), aml);
	return_ACPI_STATUS(AE_OK);
}
