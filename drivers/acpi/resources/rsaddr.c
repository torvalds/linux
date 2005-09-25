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

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_decode_general_flags
 *
 * PARAMETERS:  Resource            - Address resource data struct
 *              Flags               - Actual flag byte
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
	u8 flags;

	ACPI_FUNCTION_ENTRY();

	/* Producer / Consumer - flag bit[0] */

	flags = (u8) (resource->address.producer_consumer & 0x01);

	/* Decode (_DEC) - flag bit[1] */

	flags |= (u8) ((resource->address.decode & 0x01) << 1);

	/* Min Address Fixed (_MIF) - flag bit[2] */

	flags |= (u8) ((resource->address.min_address_fixed & 0x01) << 2);

	/* Max Address Fixed (_MAF) - flag bit[3] */

	flags |= (u8) ((resource->address.max_address_fixed & 0x01) << 3);

	return (flags);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_decode_specific_flags
 *
 * PARAMETERS:  Resource            - Address resource data struct
 *              Flags               - Actual flag byte
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
	u8 flags = 0;

	ACPI_FUNCTION_ENTRY();

	if (resource->address.resource_type == ACPI_MEMORY_RANGE) {
		/* Write Status (_RW) - flag bit[0] */

		flags = (u8)
		    (resource->address.attribute.memory.
		     read_write_attribute & 0x01);

		/* Memory Attributes (_MEM) - flag bits[2:1] */

		flags |= (u8)
		    ((resource->address.attribute.memory.
		      cache_attribute & 0x03) << 1);
	} else if (resource->address.resource_type == ACPI_IO_RANGE) {
		/* Ranges (_RNG) - flag bits[1:0] */

		flags = (u8)
		    (resource->address.attribute.io.range_attribute & 0x03);

		/* Translations (_TTP and _TRS) - flag bits[5:4] */

		flags |= (u8)
		    ((resource->address.attribute.io.
		      translation_attribute & 0x03) << 4);
	}

	return (flags);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_address16_resource
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
acpi_rs_address16_resource(u8 * byte_stream_buffer,
			   acpi_size * bytes_consumed,
			   u8 ** output_buffer, acpi_size * structure_size)
{
	u32 index;
	u16 temp16;
	u8 temp8;
	u8 *temp_ptr;
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	acpi_size struct_size =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address16);

	ACPI_FUNCTION_TRACE("rs_address16_resource");

	/* Get the Descriptor Length field */

	buffer += 1;
	ACPI_MOVE_16_TO_16(&temp16, buffer);

	/* Validate minimum descriptor length */

	if (temp16 < 13) {
		return_ACPI_STATUS(AE_AML_BAD_RESOURCE_LENGTH);
	}

	*bytes_consumed = temp16 + 3;
	output_struct->id = ACPI_RSTYPE_ADDRESS16;

	/* Get the Resource Type (Byte3) */

	buffer += 2;
	temp8 = *buffer;

	/* Values 0-2 and 0xC0-0xFF are valid */

	if ((temp8 > 2) && (temp8 < 0xC0)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	output_struct->data.address16.resource_type = temp8;

	/* Get the General Flags (Byte4) */

	buffer += 1;
	acpi_rs_decode_general_flags(&output_struct->data, *buffer);

	/* Get the Type Specific Flags (Byte5) */

	buffer += 1;
	acpi_rs_decode_specific_flags(&output_struct->data, *buffer);

	/* Get Granularity (Bytes 6-7) */

	buffer += 1;
	ACPI_MOVE_16_TO_32(&output_struct->data.address16.granularity, buffer);

	/* Get min_address_range (Bytes 8-9) */

	buffer += 2;
	ACPI_MOVE_16_TO_32(&output_struct->data.address16.min_address_range,
			   buffer);

	/* Get max_address_range (Bytes 10-11) */

	buffer += 2;
	ACPI_MOVE_16_TO_32(&output_struct->data.address16.max_address_range,
			   buffer);

	/* Get address_translation_offset (Bytes 12-13) */

	buffer += 2;
	ACPI_MOVE_16_TO_32(&output_struct->data.address16.
			   address_translation_offset, buffer);

	/* Get address_length (Bytes 14-15) */

	buffer += 2;
	ACPI_MOVE_16_TO_32(&output_struct->data.address16.address_length,
			   buffer);

	/* Resource Source Index (if present) */

	buffer += 2;

	/*
	 * This will leave us pointing to the Resource Source Index
	 * If it is present, then save it off and calculate the
	 * pointer to where the null terminated string goes:
	 * Each Interrupt takes 32-bits + the 5 bytes of the
	 * stream that are default.
	 *
	 * Note: Some resource descriptors will have an additional null, so
	 * we add 1 to the length.
	 */
	if (*bytes_consumed > (16 + 1)) {
		/* Dereference the Index */

		output_struct->data.address16.resource_source.index =
		    (u32) * buffer;

		/* Point to the String */

		buffer += 1;

		/* Point the String pointer to the end of this structure */

		output_struct->data.address16.resource_source.string_ptr =
		    (char *)((u8 *) output_struct + struct_size);

		temp_ptr = (u8 *)
		    output_struct->data.address16.resource_source.string_ptr;

		/* Copy the resource_source string into the buffer */

		index = 0;
		while (*buffer) {
			*temp_ptr = *buffer;

			temp_ptr++;
			buffer++;
			index++;
		}

		/* Add the terminating null and set the string length */

		*temp_ptr = 0;
		output_struct->data.address16.resource_source.string_length =
		    index + 1;

		/*
		 * In order for the struct_size to fall on a 32-bit boundary,
		 * calculate the length of the string and expand the
		 * struct_size to the next 32-bit boundary.
		 */
		temp8 = (u8) (index + 1);
		struct_size += ACPI_ROUND_UP_to_32_bITS(temp8);
	} else {
		output_struct->data.address16.resource_source.index = 0;
		output_struct->data.address16.resource_source.string_length = 0;
		output_struct->data.address16.resource_source.string_ptr = NULL;
	}

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_address16_stream
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
acpi_rs_address16_stream(struct acpi_resource *linked_list,
			 u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer = *output_buffer;
	u8 *length_field;
	acpi_size actual_bytes;

	ACPI_FUNCTION_TRACE("rs_address16_stream");

	/* Set the Descriptor Type field */

	*buffer = ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE;
	buffer += 1;

	/* Save a pointer to the Length field - to be filled in later */

	length_field = buffer;
	buffer += 2;

	/* Set the Resource Type (Memory, Io, bus_number) */

	*buffer = (u8) (linked_list->data.address16.resource_type & 0x03);
	buffer += 1;

	/* Set the general flags */

	*buffer = acpi_rs_encode_general_flags(&linked_list->data);
	buffer += 1;

	/* Set the type specific flags */

	*buffer = acpi_rs_encode_specific_flags(&linked_list->data);
	buffer += 1;

	/* Set the address space granularity */

	ACPI_MOVE_32_TO_16(buffer, &linked_list->data.address16.granularity);
	buffer += 2;

	/* Set the address range minimum */

	ACPI_MOVE_32_TO_16(buffer,
			   &linked_list->data.address16.min_address_range);
	buffer += 2;

	/* Set the address range maximum */

	ACPI_MOVE_32_TO_16(buffer,
			   &linked_list->data.address16.max_address_range);
	buffer += 2;

	/* Set the address translation offset */

	ACPI_MOVE_32_TO_16(buffer,
			   &linked_list->data.address16.
			   address_translation_offset);
	buffer += 2;

	/* Set the address length */

	ACPI_MOVE_32_TO_16(buffer, &linked_list->data.address16.address_length);
	buffer += 2;

	/* Resource Source Index and Resource Source are optional */

	if (linked_list->data.address16.resource_source.string_length) {
		*buffer =
		    (u8) linked_list->data.address16.resource_source.index;
		buffer += 1;

		/* Copy the resource_source string */

		ACPI_STRCPY((char *)buffer,
			    linked_list->data.address16.resource_source.
			    string_ptr);

		/*
		 * Buffer needs to be set to the length of the string + one for the
		 * terminating null
		 */
		buffer +=
		    (acpi_size) (ACPI_STRLEN
				 (linked_list->data.address16.resource_source.
				  string_ptr) + 1);
	}

	/* Return the number of bytes consumed in this operation */

	actual_bytes = ACPI_PTR_DIFF(buffer, *output_buffer);
	*bytes_consumed = actual_bytes;

	/*
	 * Set the length field to the number of bytes consumed
	 * minus the header size (3 bytes)
	 */
	actual_bytes -= 3;
	ACPI_MOVE_SIZE_TO_16(length_field, &actual_bytes);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_address32_resource
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
acpi_rs_address32_resource(u8 * byte_stream_buffer,
			   acpi_size * bytes_consumed,
			   u8 ** output_buffer, acpi_size * structure_size)
{
	u16 temp16;
	u8 temp8;
	u8 *temp_ptr;
	u32 index;
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	acpi_size struct_size =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address32);

	ACPI_FUNCTION_TRACE("rs_address32_resource");

	/* Get the Descriptor Length field */

	buffer += 1;
	ACPI_MOVE_16_TO_16(&temp16, buffer);

	/* Validate minimum descriptor length */

	if (temp16 < 23) {
		return_ACPI_STATUS(AE_AML_BAD_RESOURCE_LENGTH);
	}

	*bytes_consumed = temp16 + 3;
	output_struct->id = ACPI_RSTYPE_ADDRESS32;

	/* Get the Resource Type (Byte3) */

	buffer += 2;
	temp8 = *buffer;

	/* Values 0-2 and 0xC0-0xFF are valid */

	if ((temp8 > 2) && (temp8 < 0xC0)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	output_struct->data.address32.resource_type = temp8;

	/* Get the General Flags (Byte4) */

	buffer += 1;
	acpi_rs_decode_general_flags(&output_struct->data, *buffer);

	/* Get the Type Specific Flags (Byte5) */

	buffer += 1;
	acpi_rs_decode_specific_flags(&output_struct->data, *buffer);

	/* Get Granularity (Bytes 6-9) */

	buffer += 1;
	ACPI_MOVE_32_TO_32(&output_struct->data.address32.granularity, buffer);

	/* Get min_address_range (Bytes 10-13) */

	buffer += 4;
	ACPI_MOVE_32_TO_32(&output_struct->data.address32.min_address_range,
			   buffer);

	/* Get max_address_range (Bytes 14-17) */

	buffer += 4;
	ACPI_MOVE_32_TO_32(&output_struct->data.address32.max_address_range,
			   buffer);

	/* Get address_translation_offset (Bytes 18-21) */

	buffer += 4;
	ACPI_MOVE_32_TO_32(&output_struct->data.address32.
			   address_translation_offset, buffer);

	/* Get address_length (Bytes 22-25) */

	buffer += 4;
	ACPI_MOVE_32_TO_32(&output_struct->data.address32.address_length,
			   buffer);

	/* Resource Source Index (if present) */

	buffer += 4;

	/*
	 * This will leave us pointing to the Resource Source Index
	 * If it is present, then save it off and calculate the
	 * pointer to where the null terminated string goes:
	 *
	 * Note: Some resource descriptors will have an additional null, so
	 * we add 1 to the length.
	 */
	if (*bytes_consumed > (26 + 1)) {
		/* Dereference the Index */

		output_struct->data.address32.resource_source.index =
		    (u32) * buffer;

		/* Point to the String */

		buffer += 1;

		/* Point the String pointer to the end of this structure */

		output_struct->data.address32.resource_source.string_ptr =
		    (char *)((u8 *) output_struct + struct_size);

		temp_ptr = (u8 *)
		    output_struct->data.address32.resource_source.string_ptr;

		/* Copy the resource_source string into the buffer */

		index = 0;
		while (*buffer) {
			*temp_ptr = *buffer;

			temp_ptr++;
			buffer++;
			index++;
		}

		/* Add the terminating null and set the string length */

		*temp_ptr = 0;
		output_struct->data.address32.resource_source.string_length =
		    index + 1;

		/*
		 * In order for the struct_size to fall on a 32-bit boundary,
		 * calculate the length of the string and expand the
		 * struct_size to the next 32-bit boundary.
		 */
		temp8 = (u8) (index + 1);
		struct_size += ACPI_ROUND_UP_to_32_bITS(temp8);
	} else {
		output_struct->data.address32.resource_source.index = 0;
		output_struct->data.address32.resource_source.string_length = 0;
		output_struct->data.address32.resource_source.string_ptr = NULL;
	}

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_address32_stream
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
acpi_rs_address32_stream(struct acpi_resource *linked_list,
			 u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer;
	u16 *length_field;

	ACPI_FUNCTION_TRACE("rs_address32_stream");

	buffer = *output_buffer;

	/* Set the Descriptor Type field */

	*buffer = ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE;
	buffer += 1;

	/* Save a pointer to the Length field - to be filled in later */

	length_field = ACPI_CAST_PTR(u16, buffer);
	buffer += 2;

	/* Set the Resource Type (Memory, Io, bus_number) */

	*buffer = (u8) (linked_list->data.address32.resource_type & 0x03);
	buffer += 1;

	/* Set the general flags */

	*buffer = acpi_rs_encode_general_flags(&linked_list->data);
	buffer += 1;

	/* Set the type specific flags */

	*buffer = acpi_rs_encode_specific_flags(&linked_list->data);
	buffer += 1;

	/* Set the address space granularity */

	ACPI_MOVE_32_TO_32(buffer, &linked_list->data.address32.granularity);
	buffer += 4;

	/* Set the address range minimum */

	ACPI_MOVE_32_TO_32(buffer,
			   &linked_list->data.address32.min_address_range);
	buffer += 4;

	/* Set the address range maximum */

	ACPI_MOVE_32_TO_32(buffer,
			   &linked_list->data.address32.max_address_range);
	buffer += 4;

	/* Set the address translation offset */

	ACPI_MOVE_32_TO_32(buffer,
			   &linked_list->data.address32.
			   address_translation_offset);
	buffer += 4;

	/* Set the address length */

	ACPI_MOVE_32_TO_32(buffer, &linked_list->data.address32.address_length);
	buffer += 4;

	/* Resource Source Index and Resource Source are optional */

	if (linked_list->data.address32.resource_source.string_length) {
		*buffer =
		    (u8) linked_list->data.address32.resource_source.index;
		buffer += 1;

		/* Copy the resource_source string */

		ACPI_STRCPY((char *)buffer,
			    linked_list->data.address32.resource_source.
			    string_ptr);

		/*
		 * Buffer needs to be set to the length of the string + one for the
		 *  terminating null
		 */
		buffer +=
		    (acpi_size) (ACPI_STRLEN
				 (linked_list->data.address32.resource_source.
				  string_ptr) + 1);
	}

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);

	/*
	 * Set the length field to the number of bytes consumed
	 * minus the header size (3 bytes)
	 */
	*length_field = (u16) (*bytes_consumed - 3);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_address64_resource
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
acpi_rs_address64_resource(u8 * byte_stream_buffer,
			   acpi_size * bytes_consumed,
			   u8 ** output_buffer, acpi_size * structure_size)
{
	u16 temp16;
	u8 temp8;
	u8 resource_type;
	u8 *temp_ptr;
	u32 index;
	u8 *buffer = byte_stream_buffer;
	struct acpi_resource *output_struct = (void *)*output_buffer;
	acpi_size struct_size =
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address64);

	ACPI_FUNCTION_TRACE("rs_address64_resource");

	/* Get the Descriptor Type */

	resource_type = *buffer;

	/* Get the Descriptor Length field */

	buffer += 1;
	ACPI_MOVE_16_TO_16(&temp16, buffer);

	/* Validate minimum descriptor length */

	if (temp16 < 43) {
		return_ACPI_STATUS(AE_AML_BAD_RESOURCE_LENGTH);
	}

	*bytes_consumed = temp16 + 3;
	output_struct->id = ACPI_RSTYPE_ADDRESS64;

	/* Get the Resource Type (Byte3) */

	buffer += 2;
	temp8 = *buffer;

	/* Values 0-2 and 0xC0-0xFF are valid */

	if ((temp8 > 2) && (temp8 < 0xC0)) {
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	output_struct->data.address64.resource_type = temp8;

	/* Get the General Flags (Byte4) */

	buffer += 1;
	acpi_rs_decode_general_flags(&output_struct->data, *buffer);

	/* Get the Type Specific Flags (Byte5) */

	buffer += 1;
	acpi_rs_decode_specific_flags(&output_struct->data, *buffer);

	if (resource_type == ACPI_RDESC_TYPE_EXTENDED_ADDRESS_SPACE) {
		/* Move past revision_id and Reserved byte */

		buffer += 2;
	}

	/* Get Granularity (Bytes 6-13) or (Bytes 8-15) */

	buffer += 1;
	ACPI_MOVE_64_TO_64(&output_struct->data.address64.granularity, buffer);

	/* Get min_address_range (Bytes 14-21) or (Bytes 16-23) */

	buffer += 8;
	ACPI_MOVE_64_TO_64(&output_struct->data.address64.min_address_range,
			   buffer);

	/* Get max_address_range (Bytes 22-29) or (Bytes 24-31) */

	buffer += 8;
	ACPI_MOVE_64_TO_64(&output_struct->data.address64.max_address_range,
			   buffer);

	/* Get address_translation_offset (Bytes 30-37) or (Bytes 32-39) */

	buffer += 8;
	ACPI_MOVE_64_TO_64(&output_struct->data.address64.
			   address_translation_offset, buffer);

	/* Get address_length (Bytes 38-45) or (Bytes 40-47) */

	buffer += 8;
	ACPI_MOVE_64_TO_64(&output_struct->data.address64.address_length,
			   buffer);

	output_struct->data.address64.resource_source.index = 0;
	output_struct->data.address64.resource_source.string_length = 0;
	output_struct->data.address64.resource_source.string_ptr = NULL;

	if (resource_type == ACPI_RDESC_TYPE_EXTENDED_ADDRESS_SPACE) {
		/* Get type_specific_attribute (Bytes 48-55) */

		buffer += 8;
		ACPI_MOVE_64_TO_64(&output_struct->data.address64.
				   type_specific_attributes, buffer);
	} else {
		output_struct->data.address64.type_specific_attributes = 0;

		/* Resource Source Index (if present) */

		buffer += 8;

		/*
		 * This will leave us pointing to the Resource Source Index
		 * If it is present, then save it off and calculate the
		 * pointer to where the null terminated string goes:
		 * Each Interrupt takes 32-bits + the 5 bytes of the
		 * stream that are default.
		 *
		 * Note: Some resource descriptors will have an additional null, so
		 * we add 1 to the length.
		 */
		if (*bytes_consumed > (46 + 1)) {
			/* Dereference the Index */

			output_struct->data.address64.resource_source.index =
			    (u32) * buffer;

			/* Point to the String */

			buffer += 1;

			/* Point the String pointer to the end of this structure */

			output_struct->data.address64.resource_source.
			    string_ptr =
			    (char *)((u8 *) output_struct + struct_size);

			temp_ptr = (u8 *)
			    output_struct->data.address64.resource_source.
			    string_ptr;

			/* Copy the resource_source string into the buffer */

			index = 0;
			while (*buffer) {
				*temp_ptr = *buffer;

				temp_ptr++;
				buffer++;
				index++;
			}

			/*
			 * Add the terminating null and set the string length
			 */
			*temp_ptr = 0;
			output_struct->data.address64.resource_source.
			    string_length = index + 1;

			/*
			 * In order for the struct_size to fall on a 32-bit boundary,
			 * calculate the length of the string and expand the
			 * struct_size to the next 32-bit boundary.
			 */
			temp8 = (u8) (index + 1);
			struct_size += ACPI_ROUND_UP_to_32_bITS(temp8);
		}
	}

	/* Set the Length parameter */

	output_struct->length = (u32) struct_size;

	/* Return the final size of the structure */

	*structure_size = struct_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_address64_stream
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
acpi_rs_address64_stream(struct acpi_resource *linked_list,
			 u8 ** output_buffer, acpi_size * bytes_consumed)
{
	u8 *buffer;
	u16 *length_field;

	ACPI_FUNCTION_TRACE("rs_address64_stream");

	buffer = *output_buffer;

	/* Set the Descriptor Type field */

	*buffer = ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE;
	buffer += 1;

	/* Save a pointer to the Length field - to be filled in later */

	length_field = ACPI_CAST_PTR(u16, buffer);
	buffer += 2;

	/* Set the Resource Type (Memory, Io, bus_number) */

	*buffer = (u8) (linked_list->data.address64.resource_type & 0x03);
	buffer += 1;

	/* Set the general flags */

	*buffer = acpi_rs_encode_general_flags(&linked_list->data);
	buffer += 1;

	/* Set the type specific flags */

	*buffer = acpi_rs_encode_specific_flags(&linked_list->data);
	buffer += 1;

	/* Set the address space granularity */

	ACPI_MOVE_64_TO_64(buffer, &linked_list->data.address64.granularity);
	buffer += 8;

	/* Set the address range minimum */

	ACPI_MOVE_64_TO_64(buffer,
			   &linked_list->data.address64.min_address_range);
	buffer += 8;

	/* Set the address range maximum */

	ACPI_MOVE_64_TO_64(buffer,
			   &linked_list->data.address64.max_address_range);
	buffer += 8;

	/* Set the address translation offset */

	ACPI_MOVE_64_TO_64(buffer,
			   &linked_list->data.address64.
			   address_translation_offset);
	buffer += 8;

	/* Set the address length */

	ACPI_MOVE_64_TO_64(buffer, &linked_list->data.address64.address_length);
	buffer += 8;

	/* Resource Source Index and Resource Source are optional */

	if (linked_list->data.address64.resource_source.string_length) {
		*buffer =
		    (u8) linked_list->data.address64.resource_source.index;
		buffer += 1;

		/* Copy the resource_source string */

		ACPI_STRCPY((char *)buffer,
			    linked_list->data.address64.resource_source.
			    string_ptr);

		/*
		 * Buffer needs to be set to the length of the string + one for the
		 * terminating null
		 */
		buffer +=
		    (acpi_size) (ACPI_STRLEN
				 (linked_list->data.address64.resource_source.
				  string_ptr) + 1);
	}

	/* Return the number of bytes consumed in this operation */

	*bytes_consumed = ACPI_PTR_DIFF(buffer, *output_buffer);

	/*
	 * Set the length field to the number of bytes consumed
	 * minus the header size (3 bytes)
	 */
	*length_field = (u16) (*bytes_consumed - 3);
	return_ACPI_STATUS(AE_OK);
}
