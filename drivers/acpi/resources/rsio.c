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
 * FUNCTION:    acpi_rs_io_resource
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
acpi_rs_io_resource(u8 * byte_stream_buffer,
		    acpi_size * bytes_consumed,
		    u8 ** output_buffer, acpi_size * structure_size)
{
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;
	acpi_size struct_size = ACPI_SIZEOF_RESOURCE(struct acpi_resource_io);

	ACPI_FUNCTION_TRACE("rs_io_resource");

	/* The number of bytes consumed are Constant */

	*bytes_consumed = 8;

	output_struct->id = ACPI_RSTYPE_IO;

	/* Check Decode */

	buffer += 1;
	temp8 = *buffer;

	output_struct->data.io.io_decode = temp8 & 0x01;

	/* Check min_base Address */

	buffer += 1;
	ACPI_MOVE_16_TO_16(&temp16, buffer);

	output_struct->data.io.min_base_address = temp16;

	/* Check max_base Address */

	buffer += 2;
	ACPI_MOVE_16_TO_16(&temp16, buffer);

	output_struct->data.io.max_base_address = temp16;

	/* Check Base alignment */

	buffer += 2;
	temp8 = *buffer;

	output_struct->data.io.alignment = temp8;

	/* Check range_length */

	buffer += 1;
	temp8 = *buffer;

	output_struct->data.io.range_length = temp8;

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_fixed_io_resource
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
acpi_rs_fixed_io_resource(u8 * byte_stream_buffer,
			  acpi_size * bytes_consumed,
			  u8 ** output_buffer, acpi_size * structure_size)
{
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;
	acpi_size struct_size =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_fixed_io);

	ACPI_FUNCTION_TRACE("rs_fixed_io_resource");

	/* The number of bytes consumed are Constant */

	*bytes_consumed = 4;

	output_struct->id = ACPI_RSTYPE_FIXED_IO;

	/* Check Range Base Address */

	buffer += 1;
	ACPI_MOVE_16_TO_16(&temp16, buffer);

	output_struct->data.fixed_io.base_address = temp16;

	/* Check range_length */

	buffer += 2;
	temp8 = *buffer;

	output_struct->data.fixed_io.range_length = temp8;

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_io_stream
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
acpi_rs_io_stream(struct acpi_resource *linked_list,
		  u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;

	ACPI_FUNCTION_TRACE("rs_io_stream");

	/* The descriptor field is static */

	*buffer = 0x47;
	buffer += 1;

	/* Io Information Byte */

	temp8 = (u8) (linked_list->data.io.io_decode & 0x01);

	*buffer = temp8;
	buffer += 1;

	/* Set the Range minimum base address */

	temp16 = (u16) linked_list->data.io.min_base_address;

	ACPI_MOVE_16_TO_16(buffer, &temp16);
	buffer += 2;

	/* Set the Range maximum base address */

	temp16 = (u16) linked_list->data.io.max_base_address;

	ACPI_MOVE_16_TO_16(buffer, &temp16);
	buffer += 2;

	/* Set the base alignment */

	temp8 = (u8) linked_list->data.io.alignment;

	*buffer = temp8;
	buffer += 1;

	/* Set the range length */

	temp8 = (u8) linked_list->data.io.range_length;

	*buffer = temp8;
	buffer += 1;

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_fixed_io_stream
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
acpi_rs_fixed_io_stream(struct acpi_resource *linked_list,
			u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;

	ACPI_FUNCTION_TRACE("rs_fixed_io_stream");

	/* The descriptor field is static */

	*buffer = 0x4B;

	buffer += 1;

	/* Set the Range base address */

	temp16 = (u16) linked_list->data.fixed_io.base_address;

	ACPI_MOVE_16_TO_16(buffer, &temp16);
	buffer += 2;

	/* Set the range length */

	temp8 = (u8) linked_list->data.fixed_io.range_length;

	*buffer = temp8;
	buffer += 1;

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dma_resource
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
acpi_rs_dma_resource(u8 * byte_stream_buffer,
		     acpi_size * bytes_consumed,
		     u8 ** output_buffer, acpi_size * structure_size)
{
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	u8 temp8 = 0;
	u8 index;
	u8 i;
	acpi_size struct_size = ACPI_SIZEOF_RESOURCE(struct acpi_resource_dma);

	ACPI_FUNCTION_TRACE("rs_dma_resource");

	/* The number of bytes consumed are Constant */

	*bytes_consumed = 3;
	output_struct->id = ACPI_RSTYPE_DMA;

	/* Point to the 8-bits of Byte 1 */

	buffer += 1;
	temp8 = *buffer;

	/* Decode the DMA channel bits */

	for (i = 0, index = 0; index < 8; index++) {
		if ((temp8 >> index) & 0x01) {
			output_struct->data.dma.channels[i] = index;
			i++;
		}
	}

	/* Zero DMA channels is valid */

	output_struct->data.dma.number_of_channels = i;
	if (i > 0) {
		/* Calculate the structure size based upon the number of interrupts */

		struct_size += ((acpi_size) i - 1) * 4;
	}

	/* Point to Byte 2 */

	buffer += 1;
	temp8 = *buffer;

	/* Check for transfer preference (Bits[1:0]) */

	output_struct->data.dma.transfer = temp8 & 0x03;

	if (0x03 == output_struct->data.dma.transfer) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Invalid DMA.Transfer preference (3)\n"));
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	/* Get bus master preference (Bit[2]) */

	output_struct->data.dma.bus_master = (temp8 >> 2) & 0x01;

	/* Get channel speed support (Bits[6:5]) */

	output_struct->data.dma.type = (temp8 >> 5) & 0x03;

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dma_stream
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
acpi_rs_dma_stream(struct acpi_resource *linked_list,
		   u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u16 temp16 = 0;
	u8 temp8 = 0;
	u8 index;

	ACPI_FUNCTION_TRACE("rs_dma_stream");

	/* The descriptor field is static */

	*buffer = 0x2A;
	buffer += 1;
	temp8 = 0;

	/* Loop through all of the Channels and set the mask bits */

	for (index = 0;
	     index < linked_list->data.dma.number_of_channels; index++) {
		temp16 = (u16) linked_list->data.dma.channels[index];
		temp8 |= 0x1 << temp16;
	}

	*buffer = temp8;
	buffer += 1;

	/* Set the DMA Info */

	temp8 = (u8) ((linked_list->data.dma.type & 0x03) << 5);
	temp8 |= ((linked_list->data.dma.bus_master & 0x01) << 2);
	temp8 |= (linked_list->data.dma.transfer & 0x03);

	*buffer = temp8;
	buffer += 1;

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);
	return_ACPI_STATUS(AE_OK);
}
