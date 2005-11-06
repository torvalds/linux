/*******************************************************************************
 *
 * Module Name: rslist - Linked list utilities
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
ACPI_MODULE_NAME("rslist")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_type
 *
 * PARAMETERS:  resource_start_byte     - Byte 0 of a resource descriptor
 *
 * RETURN:      The Resource Type with no extraneous bits
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/
u8 acpi_rs_get_resource_type(u8 resource_start_byte)
{

	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	switch (resource_start_byte & ACPI_RDESC_TYPE_MASK) {
	case ACPI_RDESC_TYPE_SMALL:

		/* Small Resource Type -- Only bits 6:3 are valid */

		return ((u8) (resource_start_byte & ACPI_RDESC_SMALL_MASK));

	case ACPI_RDESC_TYPE_LARGE:

		/* Large Resource Type -- All bits are valid */

		return (resource_start_byte);

	default:
		/* Invalid type */
		break;
	}

	return (0xFF);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_byte_stream_to_list
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource byte stream
 *              byte_stream_buffer_length - Length of byte_stream_buffer
 *              output_buffer           - Pointer to the buffer that will
 *                                        contain the output structures
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource byte stream and parses it, creating a
 *              linked list of resources in the caller's output buffer
 *
 ******************************************************************************/

acpi_status
acpi_rs_byte_stream_to_list(u8 * byte_stream_buffer,
			    u32 byte_stream_buffer_length, u8 * output_buffer)
{
	acpi_status status;
	acpi_size bytes_parsed = 0;
	u8 resource_type = 0;
	acpi_size bytes_consumed = 0;
	u8 *buffer = output_buffer;
	acpi_size structure_size = 0;
	u8 end_tag_processed = FALSE;
	struct acpi_resource *resource;

	ACPI_FUNCTION_TRACE("rs_byte_stream_to_list");

	while (bytes_parsed < byte_stream_buffer_length && !end_tag_processed) {
		/* The next byte in the stream is the resource type */

		resource_type = acpi_rs_get_resource_type(*byte_stream_buffer);

		switch (resource_type) {
		case ACPI_RDESC_TYPE_MEMORY_24:
			/*
			 * 24-Bit Memory Resource
			 */
			status = acpi_rs_memory24_resource(byte_stream_buffer,
							   &bytes_consumed,
							   &buffer,
							   &structure_size);
			break;

		case ACPI_RDESC_TYPE_LARGE_VENDOR:
			/*
			 * Vendor Defined Resource
			 */
			status = acpi_rs_vendor_resource(byte_stream_buffer,
							 &bytes_consumed,
							 &buffer,
							 &structure_size);
			break;

		case ACPI_RDESC_TYPE_MEMORY_32:
			/*
			 * 32-Bit Memory Range Resource
			 */
			status =
			    acpi_rs_memory32_range_resource(byte_stream_buffer,
							    &bytes_consumed,
							    &buffer,
							    &structure_size);
			break;

		case ACPI_RDESC_TYPE_FIXED_MEMORY_32:
			/*
			 * 32-Bit Fixed Memory Resource
			 */
			status =
			    acpi_rs_fixed_memory32_resource(byte_stream_buffer,
							    &bytes_consumed,
							    &buffer,
							    &structure_size);
			break;

		case ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE:
		case ACPI_RDESC_TYPE_EXTENDED_ADDRESS_SPACE:
			/*
			 * 64-Bit Address Resource
			 */
			status = acpi_rs_address64_resource(byte_stream_buffer,
							    &bytes_consumed,
							    &buffer,
							    &structure_size);
			break;

		case ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE:
			/*
			 * 32-Bit Address Resource
			 */
			status = acpi_rs_address32_resource(byte_stream_buffer,
							    &bytes_consumed,
							    &buffer,
							    &structure_size);
			break;

		case ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE:
			/*
			 * 16-Bit Address Resource
			 */
			status = acpi_rs_address16_resource(byte_stream_buffer,
							    &bytes_consumed,
							    &buffer,
							    &structure_size);
			break;

		case ACPI_RDESC_TYPE_EXTENDED_XRUPT:
			/*
			 * Extended IRQ
			 */
			status =
			    acpi_rs_extended_irq_resource(byte_stream_buffer,
							  &bytes_consumed,
							  &buffer,
							  &structure_size);
			break;

		case ACPI_RDESC_TYPE_IRQ_FORMAT:
			/*
			 * IRQ Resource
			 */
			status = acpi_rs_irq_resource(byte_stream_buffer,
						      &bytes_consumed, &buffer,
						      &structure_size);
			break;

		case ACPI_RDESC_TYPE_DMA_FORMAT:
			/*
			 * DMA Resource
			 */
			status = acpi_rs_dma_resource(byte_stream_buffer,
						      &bytes_consumed, &buffer,
						      &structure_size);
			break;

		case ACPI_RDESC_TYPE_START_DEPENDENT:
			/*
			 * Start Dependent Functions Resource
			 */
			status =
			    acpi_rs_start_depend_fns_resource
			    (byte_stream_buffer, &bytes_consumed, &buffer,
			     &structure_size);
			break;

		case ACPI_RDESC_TYPE_END_DEPENDENT:
			/*
			 * End Dependent Functions Resource
			 */
			status =
			    acpi_rs_end_depend_fns_resource(byte_stream_buffer,
							    &bytes_consumed,
							    &buffer,
							    &structure_size);
			break;

		case ACPI_RDESC_TYPE_IO_PORT:
			/*
			 * IO Port Resource
			 */
			status = acpi_rs_io_resource(byte_stream_buffer,
						     &bytes_consumed, &buffer,
						     &structure_size);
			break;

		case ACPI_RDESC_TYPE_FIXED_IO_PORT:
			/*
			 * Fixed IO Port Resource
			 */
			status = acpi_rs_fixed_io_resource(byte_stream_buffer,
							   &bytes_consumed,
							   &buffer,
							   &structure_size);
			break;

		case ACPI_RDESC_TYPE_SMALL_VENDOR:
			/*
			 * Vendor Specific Resource
			 */
			status = acpi_rs_vendor_resource(byte_stream_buffer,
							 &bytes_consumed,
							 &buffer,
							 &structure_size);
			break;

		case ACPI_RDESC_TYPE_END_TAG:
			/*
			 * End Tag
			 */
			end_tag_processed = TRUE;
			status = acpi_rs_end_tag_resource(byte_stream_buffer,
							  &bytes_consumed,
							  &buffer,
							  &structure_size);
			break;

		default:
			/*
			 * Invalid/Unknown resource type
			 */
			status = AE_AML_INVALID_RESOURCE_TYPE;
			break;
		}

		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Update the return value and counter */

		bytes_parsed += bytes_consumed;

		/* Set the byte stream to point to the next resource */

		byte_stream_buffer += bytes_consumed;

		/* Set the Buffer to the next structure */

		resource = ACPI_CAST_PTR(struct acpi_resource, buffer);
		resource->length =
		    (u32) ACPI_ALIGN_RESOURCE_SIZE(resource->length);
		buffer += ACPI_ALIGN_RESOURCE_SIZE(structure_size);
	}

	/* Check the reason for exiting the while loop */

	if (!end_tag_processed) {
		return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_list_to_byte_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              byte_steam_size_needed  - Calculated size of the byte stream
 *                                        needed from calling
 *                                        acpi_rs_get_byte_stream_length()
 *                                        The size of the output_buffer is
 *                                        guaranteed to be >=
 *                                        byte_stream_size_needed
 *              output_buffer           - Pointer to the buffer that will
 *                                        contain the byte stream
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource linked list and parses it, creating a
 *              byte stream of resources in the caller's output buffer
 *
 ******************************************************************************/

acpi_status
acpi_rs_list_to_byte_stream(struct acpi_resource *linked_list,
			    acpi_size byte_stream_size_needed,
			    u8 * output_buffer)
{
	acpi_status status;
	u8 *buffer = output_buffer;
	acpi_size bytes_consumed = 0;
	u8 done = FALSE;

	ACPI_FUNCTION_TRACE("rs_list_to_byte_stream");

	while (!done) {
		switch (linked_list->id) {
		case ACPI_RSTYPE_IRQ:
			/*
			 * IRQ Resource
			 */
			status =
			    acpi_rs_irq_stream(linked_list, &buffer,
					       &bytes_consumed);
			break;

		case ACPI_RSTYPE_DMA:
			/*
			 * DMA Resource
			 */
			status =
			    acpi_rs_dma_stream(linked_list, &buffer,
					       &bytes_consumed);
			break;

		case ACPI_RSTYPE_START_DPF:
			/*
			 * Start Dependent Functions Resource
			 */
			status = acpi_rs_start_depend_fns_stream(linked_list,
								 &buffer,
								 &bytes_consumed);
			break;

		case ACPI_RSTYPE_END_DPF:
			/*
			 * End Dependent Functions Resource
			 */
			status = acpi_rs_end_depend_fns_stream(linked_list,
							       &buffer,
							       &bytes_consumed);
			break;

		case ACPI_RSTYPE_IO:
			/*
			 * IO Port Resource
			 */
			status =
			    acpi_rs_io_stream(linked_list, &buffer,
					      &bytes_consumed);
			break;

		case ACPI_RSTYPE_FIXED_IO:
			/*
			 * Fixed IO Port Resource
			 */
			status =
			    acpi_rs_fixed_io_stream(linked_list, &buffer,
						    &bytes_consumed);
			break;

		case ACPI_RSTYPE_VENDOR:
			/*
			 * Vendor Defined Resource
			 */
			status =
			    acpi_rs_vendor_stream(linked_list, &buffer,
						  &bytes_consumed);
			break;

		case ACPI_RSTYPE_END_TAG:
			/*
			 * End Tag
			 */
			status =
			    acpi_rs_end_tag_stream(linked_list, &buffer,
						   &bytes_consumed);

			/* An End Tag indicates the end of the Resource Template */

			done = TRUE;
			break;

		case ACPI_RSTYPE_MEM24:
			/*
			 * 24-Bit Memory Resource
			 */
			status =
			    acpi_rs_memory24_stream(linked_list, &buffer,
						    &bytes_consumed);
			break;

		case ACPI_RSTYPE_MEM32:
			/*
			 * 32-Bit Memory Range Resource
			 */
			status =
			    acpi_rs_memory32_range_stream(linked_list, &buffer,
							  &bytes_consumed);
			break;

		case ACPI_RSTYPE_FIXED_MEM32:
			/*
			 * 32-Bit Fixed Memory Resource
			 */
			status =
			    acpi_rs_fixed_memory32_stream(linked_list, &buffer,
							  &bytes_consumed);
			break;

		case ACPI_RSTYPE_ADDRESS16:
			/*
			 * 16-Bit Address Descriptor Resource
			 */
			status = acpi_rs_address16_stream(linked_list, &buffer,
							  &bytes_consumed);
			break;

		case ACPI_RSTYPE_ADDRESS32:
			/*
			 * 32-Bit Address Descriptor Resource
			 */
			status = acpi_rs_address32_stream(linked_list, &buffer,
							  &bytes_consumed);
			break;

		case ACPI_RSTYPE_ADDRESS64:
			/*
			 * 64-Bit Address Descriptor Resource
			 */
			status = acpi_rs_address64_stream(linked_list, &buffer,
							  &bytes_consumed);
			break;

		case ACPI_RSTYPE_EXT_IRQ:
			/*
			 * Extended IRQ Resource
			 */
			status =
			    acpi_rs_extended_irq_stream(linked_list, &buffer,
							&bytes_consumed);
			break;

		default:
			/*
			 * If we get here, everything is out of sync,
			 * so exit with an error
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Invalid descriptor type (%X) in resource list\n",
					  linked_list->id));
			status = AE_BAD_DATA;
			break;
		}

		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Set the Buffer to point to the open byte */

		buffer += bytes_consumed;

		/* Point to the next object */

		linked_list = ACPI_PTR_ADD(struct acpi_resource,
					   linked_list, linked_list->length);
	}

	return_ACPI_STATUS(AE_OK);
}
