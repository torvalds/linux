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
 * FUNCTION:    acpi_rs_end_tag_resource
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the byte_stream_buffer is
 *                                        returned
 *              output_buffer           - Pointer to the return data buffer
 *              structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/
acpi_status
acpi_rs_end_tag_resource(u8 * byte_stream_buffer,
			 acpi_size * bytes_consumed,
			 u8 ** output_buffer, acpi_size * structure_size)
{
	struct acpi_resource *output_struct = (void *)*output_buffer;
	acpi_size struct_size = ACPI_RESOURCE_LENGTH;

	ACPI_FUNCTION_TRACE("rs_end_tag_resource");

	/* The number of bytes consumed is static */

	*bytes_consumed = 2;

	/*  Fill out the structure */

	output_struct->id = ACPI_RSTYPE_END_TAG;

	/* Set the Length parameter */

	output_struct->length = 0;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_end_tag_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's return buffer
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_tag_stream(struct acpi_resource *linked_list,
		       u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u8 temp8 = 0;

	ACPI_FUNCTION_TRACE("rs_end_tag_stream");

	/* The descriptor field is static */

	*buffer = 0x79;
	buffer += 1;

	/*
	 * Set the Checksum - zero means that the resource data is treated as if
	 * the checksum operation succeeded (ACPI Spec 1.0b Section 6.4.2.8)
	 */
	temp8 = 0;

	*buffer = temp8;
	buffer += 1;

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_vendor_resource
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the byte_stream_buffer is
 *                                        returned
 *              output_buffer           - Pointer to the return data buffer
 *              structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_vendor_resource(u8 * byte_stream_buffer,
			acpi_size * bytes_consumed,
			u8 ** output_buffer, acpi_size * structure_size)
{
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;
	u8 index;
	acpi_size struct_size =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_vendor);

	ACPI_FUNCTION_TRACE("rs_vendor_resource");

	/* Dereference the Descriptor to find if this is a large or small item. */

	temp8 = *buffer;

	if (temp8 & 0x80) {
		/* Large Item, point to the length field */

		buffer += 1;

		/* Dereference */

		ACPI_MOVE_16_TO_16(&temp16, buffer);

		/* Calculate bytes consumed */

		*bytes_consumed = (acpi_size) temp16 + 3;

		/* Point to the first vendor byte */

		buffer += 2;
	} else {
		/* Small Item, dereference the size */

		temp16 = (u8) (*buffer & 0x07);

		/* Calculate bytes consumed */

		*bytes_consumed = (acpi_size) temp16 + 1;

		/* Point to the first vendor byte */

		buffer += 1;
	}

	output_struct->id = ACPI_RSTYPE_VENDOR;
	output_struct->data.vendor_specific.length = temp16;

	for (index = 0; index < temp16; index++) {
		output_struct->data.vendor_specific.reserved[index] = *buffer;
		buffer += 1;
	}

	/*
	 * In order for the struct_size to fall on a 32-bit boundary,
	 * calculate the length of the vendor string and expand the
	 * struct_size to the next 32-bit boundary.
	 */
	struct_size += ACPI_ROUND_UP_to_32_bITS(temp16);

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_vendor_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's return buffer
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_vendor_stream(struct acpi_resource *linked_list,
		      u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;
	u8 index;

	ACPI_FUNCTION_TRACE("rs_vendor_stream");

	/* Dereference the length to find if this is a large or small item. */

	if (linked_list->data.vendor_specific.length > 7) {
		/* Large Item, Set the descriptor field and length bytes */

		*buffer = 0x84;
		buffer += 1;

		temp16 = (u16) linked_list->data.vendor_specific.length;

		ACPI_MOVE_16_TO_16(buffer, &temp16);
		buffer += 2;
	} else {
		/* Small Item, Set the descriptor field */

		temp8 = 0x70;
		temp8 |= (u8) linked_list->data.vendor_specific.length;

		*buffer = temp8;
		buffer += 1;
	}

	/* Loop through all of the Vendor Specific fields */

	for (index = 0; index < linked_list->data.vendor_specific.length;
	     index++) {
		temp8 = linked_list->data.vendor_specific.reserved[index];

		*buffer = temp8;
		buffer += 1;
	}

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_start_depend_fns_resource
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the byte_stream_buffer is
 *                                        returned
 *              output_buffer           - Pointer to the return data buffer
 *              structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_start_depend_fns_resource(u8 * byte_stream_buffer,
				  acpi_size * bytes_consumed,
				  u8 ** output_buffer,
				  acpi_size * structure_size)
{
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	u8 temp8 = 0;
	acpi_size struct_size =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_start_dpf);

	ACPI_FUNCTION_TRACE("rs_start_depend_fns_resource");

	/* The number of bytes consumed are found in the descriptor (Bits:0-1) */

	temp8 = *buffer;

	*bytes_consumed = (temp8 & 0x01) + 1;

	output_struct->id = ACPI_RSTYPE_START_DPF;

	/* Point to Byte 1 if it is used */

	if (2 == *bytes_consumed) {
		buffer += 1;
		temp8 = *buffer;

		/* Check Compatibility priority */

		output_struct->data.start_dpf.compatibility_priority =
		    temp8 & 0x03;

		if (3 == output_struct->data.start_dpf.compatibility_priority) {
			return_ACPI_STATUS(AE_AML_BAD_RESOURCE_VALUE);
		}

		/* Check Performance/Robustness preference */

		output_struct->data.start_dpf.performance_robustness =
		    (temp8 >> 2) & 0x03;

		if (3 == output_struct->data.start_dpf.performance_robustness) {
			return_ACPI_STATUS(AE_AML_BAD_RESOURCE_VALUE);
		}
	} else {
		output_struct->data.start_dpf.compatibility_priority =
		    ACPI_ACCEPTABLE_CONFIGURATION;

		output_struct->data.start_dpf.performance_robustness =
		    ACPI_ACCEPTABLE_CONFIGURATION;
	}

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_end_depend_fns_resource
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the byte_stream_buffer is
 *                                        returned
 *              output_buffer           - Pointer to the return data buffer
 *              structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_depend_fns_resource(u8 * byte_stream_buffer,
				acpi_size * bytes_consumed,
				u8 ** output_buffer, acpi_size * structure_size)
{
	struct acpi_resource *output_struct = (void *)*output_buffer;
	acpi_size struct_size = ACPI_RESOURCE_LENGTH;

	ACPI_FUNCTION_TRACE("rs_end_depend_fns_resource");

	/* The number of bytes consumed is static */

	*bytes_consumed = 1;

	/*  Fill out the structure */

	output_struct->id = ACPI_RSTYPE_END_DPF;

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_start_depend_fns_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's return buffer
 *              bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes of the
 *                                        output_buffer used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_start_depend_fns_stream(struct acpi_resource *linked_list,
				u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u8 temp8 = 0;

	ACPI_FUNCTION_TRACE("rs_start_depend_fns_stream");

	/*
	 * The descriptor field is set based upon whether a byte is needed
	 * to contain Priority data.
	 */
	if (ACPI_ACCEPTABLE_CONFIGURATION ==
	    linked_list->data.start_dpf.compatibility_priority &&
	    ACPI_ACCEPTABLE_CONFIGURATION ==
	    linked_list->data.start_dpf.performance_robustness) {
		*buffer = 0x30;
	} else {
		*buffer = 0x31;
		buffer += 1;

		/* Set the Priority Byte Definition */

		temp8 = 0;
		temp8 =
		    (u8) ((linked_list->data.start_dpf.
			   performance_robustness & 0x03) << 2);
		temp8 |=
		    (linked_list->data.start_dpf.compatibility_priority & 0x03);
		*buffer = temp8;
	}

	buffer += 1;

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_end_depend_fns_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's return buffer
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_end_depend_fns_stream(struct acpi_resource *linked_list,
			      u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;

	ACPI_FUNCTION_TRACE("rs_end_depend_fns_stream");

	/* The descriptor field is static */

	*buffer = 0x38;
	buffer += 1;

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}
