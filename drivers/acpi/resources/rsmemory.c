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
	 ACPI_MODULE_NAME    ("rsmemory")


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_memory24_resource
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
acpi_rs_memory24_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size)
{
	u8                              *buffer = byte_stream_buffer;
	struct acpi_resource            *output_struct = (void *) *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;
	acpi_size                       struct_size = ACPI_SIZEOF_RESOURCE (struct acpi_resource_mem24);


	ACPI_FUNCTION_TRACE ("rs_memory24_resource");


	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;

	ACPI_MOVE_16_TO_16 (&temp16, buffer);
	buffer += 2;
	*bytes_consumed = (acpi_size) temp16 + 3;
	output_struct->id = ACPI_RSTYPE_MEM24;

	/*
	 * Check Byte 3 the Read/Write bit
	 */
	temp8 = *buffer;
	buffer += 1;
	output_struct->data.memory24.read_write_attribute = temp8 & 0x01;

	/*
	 * Get min_base_address (Bytes 4-5)
	 */
	ACPI_MOVE_16_TO_16 (&temp16, buffer);
	buffer += 2;
	output_struct->data.memory24.min_base_address = temp16;

	/*
	 * Get max_base_address (Bytes 6-7)
	 */
	ACPI_MOVE_16_TO_16 (&temp16, buffer);
	buffer += 2;
	output_struct->data.memory24.max_base_address = temp16;

	/*
	 * Get Alignment (Bytes 8-9)
	 */
	ACPI_MOVE_16_TO_16 (&temp16, buffer);
	buffer += 2;
	output_struct->data.memory24.alignment = temp16;

	/*
	 * Get range_length (Bytes 10-11)
	 */
	ACPI_MOVE_16_TO_16 (&temp16, buffer);
	output_struct->data.memory24.range_length = temp16;

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_memory24_stream
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
acpi_rs_memory24_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed)
{
	u8                              *buffer = *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;


	ACPI_FUNCTION_TRACE ("rs_memory24_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x81;
	buffer += 1;

	/*
	 * The length field is static
	 */
	temp16 = 0x09;
	ACPI_MOVE_16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the Information Byte
	 */
	temp8 = (u8) (linked_list->data.memory24.read_write_attribute & 0x01);
	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the Range minimum base address
	 */
	ACPI_MOVE_32_TO_16 (buffer, &linked_list->data.memory24.min_base_address);
	buffer += 2;

	/*
	 * Set the Range maximum base address
	 */
	ACPI_MOVE_32_TO_16 (buffer, &linked_list->data.memory24.max_base_address);
	buffer += 2;

	/*
	 * Set the base alignment
	 */
	ACPI_MOVE_32_TO_16 (buffer, &linked_list->data.memory24.alignment);
	buffer += 2;

	/*
	 * Set the range length
	 */
	ACPI_MOVE_32_TO_16 (buffer, &linked_list->data.memory24.range_length);
	buffer += 2;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_memory32_range_resource
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
acpi_rs_memory32_range_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size)
{
	u8                              *buffer = byte_stream_buffer;
	struct acpi_resource            *output_struct = (void *) *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;
	acpi_size                       struct_size = ACPI_SIZEOF_RESOURCE (struct acpi_resource_mem32);


	ACPI_FUNCTION_TRACE ("rs_memory32_range_resource");


	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;

	ACPI_MOVE_16_TO_16 (&temp16, buffer);
	buffer += 2;
	*bytes_consumed = (acpi_size) temp16 + 3;

	output_struct->id = ACPI_RSTYPE_MEM32;

	/*
	 *  Point to the place in the output buffer where the data portion will
	 *  begin.
	 *  1. Set the RESOURCE_DATA * Data to point to its own address, then
	 *  2. Set the pointer to the next address.
	 *
	 *  NOTE: output_struct->Data is cast to u8, otherwise, this addition adds
	 *  4 * sizeof(RESOURCE_DATA) instead of 4 * sizeof(u8)
	 */

	/*
	 * Check Byte 3 the Read/Write bit
	 */
	temp8 = *buffer;
	buffer += 1;

	output_struct->data.memory32.read_write_attribute = temp8 & 0x01;

	/*
	 * Get min_base_address (Bytes 4-7)
	 */
	ACPI_MOVE_32_TO_32 (&output_struct->data.memory32.min_base_address, buffer);
	buffer += 4;

	/*
	 * Get max_base_address (Bytes 8-11)
	 */
	ACPI_MOVE_32_TO_32 (&output_struct->data.memory32.max_base_address, buffer);
	buffer += 4;

	/*
	 * Get Alignment (Bytes 12-15)
	 */
	ACPI_MOVE_32_TO_32 (&output_struct->data.memory32.alignment, buffer);
	buffer += 4;

	/*
	 * Get range_length (Bytes 16-19)
	 */
	ACPI_MOVE_32_TO_32 (&output_struct->data.memory32.range_length, buffer);

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_fixed_memory32_resource
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
acpi_rs_fixed_memory32_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size)
{
	u8                              *buffer = byte_stream_buffer;
	struct acpi_resource            *output_struct = (void *) *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;
	acpi_size                       struct_size = ACPI_SIZEOF_RESOURCE (struct acpi_resource_fixed_mem32);


	ACPI_FUNCTION_TRACE ("rs_fixed_memory32_resource");


	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;
	ACPI_MOVE_16_TO_16 (&temp16, buffer);

	buffer += 2;
	*bytes_consumed = (acpi_size) temp16 + 3;

	output_struct->id = ACPI_RSTYPE_FIXED_MEM32;

	/*
	 * Check Byte 3 the Read/Write bit
	 */
	temp8 = *buffer;
	buffer += 1;
	output_struct->data.fixed_memory32.read_write_attribute = temp8 & 0x01;

	/*
	 * Get range_base_address (Bytes 4-7)
	 */
	ACPI_MOVE_32_TO_32 (&output_struct->data.fixed_memory32.range_base_address, buffer);
	buffer += 4;

	/*
	 * Get range_length (Bytes 8-11)
	 */
	ACPI_MOVE_32_TO_32 (&output_struct->data.fixed_memory32.range_length, buffer);

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_memory32_range_stream
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
acpi_rs_memory32_range_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed)
{
	u8                              *buffer = *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;


	ACPI_FUNCTION_TRACE ("rs_memory32_range_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x85;
	buffer += 1;

	/*
	 * The length field is static
	 */
	temp16 = 0x11;

	ACPI_MOVE_16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the Information Byte
	 */
	temp8 = (u8) (linked_list->data.memory32.read_write_attribute & 0x01);
	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the Range minimum base address
	 */
	ACPI_MOVE_32_TO_32 (buffer, &linked_list->data.memory32.min_base_address);
	buffer += 4;

	/*
	 * Set the Range maximum base address
	 */
	ACPI_MOVE_32_TO_32 (buffer, &linked_list->data.memory32.max_base_address);
	buffer += 4;

	/*
	 * Set the base alignment
	 */
	ACPI_MOVE_32_TO_32 (buffer, &linked_list->data.memory32.alignment);
	buffer += 4;

	/*
	 * Set the range length
	 */
	ACPI_MOVE_32_TO_32 (buffer, &linked_list->data.memory32.range_length);
	buffer += 4;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_fixed_memory32_stream
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
acpi_rs_fixed_memory32_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed)
{
	u8                              *buffer = *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;


	ACPI_FUNCTION_TRACE ("rs_fixed_memory32_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x86;
	buffer += 1;

	/*
	 * The length field is static
	 */
	temp16 = 0x09;

	ACPI_MOVE_16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the Information Byte
	 */
	temp8 = (u8) (linked_list->data.fixed_memory32.read_write_attribute & 0x01);
	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the Range base address
	 */
	ACPI_MOVE_32_TO_32 (buffer,
			 &linked_list->data.fixed_memory32.range_base_address);
	buffer += 4;

	/*
	 * Set the range length
	 */
	ACPI_MOVE_32_TO_32 (buffer,
			 &linked_list->data.fixed_memory32.range_length);
	buffer += 4;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}

