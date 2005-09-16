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

/* Dispatch table for convert-to-stream functions */
typedef
acpi_status(*ACPI_STREAM_HANDLER) (struct acpi_resource * resource,
				   u8 ** output_buffer,
				   acpi_size * bytes_consumed);

static ACPI_STREAM_HANDLER acpi_gbl_stream_dispatch[] = {
	acpi_rs_irq_stream,	/* ACPI_RSTYPE_IRQ */
	acpi_rs_dma_stream,	/* ACPI_RSTYPE_DMA */
	acpi_rs_start_depend_fns_stream,	/* ACPI_RSTYPE_START_DPF */
	acpi_rs_end_depend_fns_stream,	/* ACPI_RSTYPE_END_DPF */
	acpi_rs_io_stream,	/* ACPI_RSTYPE_IO */
	acpi_rs_fixed_io_stream,	/* ACPI_RSTYPE_FIXED_IO */
	acpi_rs_vendor_stream,	/* ACPI_RSTYPE_VENDOR */
	acpi_rs_end_tag_stream,	/* ACPI_RSTYPE_END_TAG */
	acpi_rs_memory24_stream,	/* ACPI_RSTYPE_MEM24 */
	acpi_rs_memory32_range_stream,	/* ACPI_RSTYPE_MEM32 */
	acpi_rs_fixed_memory32_stream,	/* ACPI_RSTYPE_FIXED_MEM32 */
	acpi_rs_address16_stream,	/* ACPI_RSTYPE_ADDRESS16 */
	acpi_rs_address32_stream,	/* ACPI_RSTYPE_ADDRESS32 */
	acpi_rs_address64_stream,	/* ACPI_RSTYPE_ADDRESS64 */
	acpi_rs_extended_irq_stream,	/* ACPI_RSTYPE_EXT_IRQ */
	acpi_rs_generic_register_stream	/* ACPI_RSTYPE_GENERIC_REG */
};

/* Dispatch tables for convert-to-resource functions */

typedef
acpi_status(*ACPI_RESOURCE_HANDLER) (u8 * byte_stream_buffer,
				     acpi_size * bytes_consumed,
				     u8 ** output_buffer,
				     acpi_size * structure_size);

static ACPI_RESOURCE_HANDLER acpi_gbl_sm_resource_dispatch[] = {
	NULL,			/* 0x00, Reserved */
	NULL,			/* 0x01, Reserved */
	NULL,			/* 0x02, Reserved */
	NULL,			/* 0x03, Reserved */
	acpi_rs_irq_resource,	/* ACPI_RDESC_TYPE_IRQ_FORMAT */
	acpi_rs_dma_resource,	/* ACPI_RDESC_TYPE_DMA_FORMAT */
	acpi_rs_start_depend_fns_resource,	/* ACPI_RDESC_TYPE_START_DEPENDENT */
	acpi_rs_end_depend_fns_resource,	/* ACPI_RDESC_TYPE_END_DEPENDENT */
	acpi_rs_io_resource,	/* ACPI_RDESC_TYPE_IO_PORT */
	acpi_rs_fixed_io_resource,	/* ACPI_RDESC_TYPE_FIXED_IO_PORT */
	NULL,			/* 0x0A, Reserved */
	NULL,			/* 0x0B, Reserved */
	NULL,			/* 0x0C, Reserved */
	NULL,			/* 0x0D, Reserved */
	acpi_rs_vendor_resource,	/* ACPI_RDESC_TYPE_SMALL_VENDOR */
	acpi_rs_end_tag_resource	/* ACPI_RDESC_TYPE_END_TAG */
};

static ACPI_RESOURCE_HANDLER acpi_gbl_lg_resource_dispatch[] = {
	NULL,			/* 0x00, Reserved */
	acpi_rs_memory24_resource,	/* ACPI_RDESC_TYPE_MEMORY_24 */
	acpi_rs_generic_register_resource,	/* ACPI_RDESC_TYPE_GENERIC_REGISTER */
	NULL,			/* 0x03, Reserved */
	acpi_rs_vendor_resource,	/* ACPI_RDESC_TYPE_LARGE_VENDOR */
	acpi_rs_memory32_range_resource,	/* ACPI_RDESC_TYPE_MEMORY_32 */
	acpi_rs_fixed_memory32_resource,	/* ACPI_RDESC_TYPE_FIXED_MEMORY_32 */
	acpi_rs_address32_resource,	/* ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE */
	acpi_rs_address16_resource,	/* ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE */
	acpi_rs_extended_irq_resource,	/* ACPI_RDESC_TYPE_EXTENDED_XRUPT */
	acpi_rs_address64_resource,	/* ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE */
	acpi_rs_address64_resource	/* ACPI_RDESC_TYPE_EXTENDED_ADDRESS_SPACE */
};

/* Local prototypes */

static ACPI_RESOURCE_HANDLER acpi_rs_get_resource_handler(u8 resource_type);

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_type
 *
 * PARAMETERS:  resource_type       - Byte 0 of a resource descriptor
 *
 * RETURN:      The Resource Type with no extraneous bits (except the large/
 *              small bit -- left alone)
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

u8 acpi_rs_get_resource_type(u8 resource_type)
{
	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (resource_type & ACPI_RDESC_TYPE_LARGE) {
		/* Large Resource Type -- bits 6:0 contain the name */

		return (resource_type);
	} else {
		/* Small Resource Type -- bits 6:3 contain the name */

		return ((u8) (resource_type & ACPI_RDESC_SMALL_MASK));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_handler
 *
 * PARAMETERS:  resource_type       - Byte 0 of a resource descriptor
 *
 * RETURN:      Pointer to the resource conversion handler
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

static ACPI_RESOURCE_HANDLER acpi_rs_get_resource_handler(u8 resource_type)
{
	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (resource_type & ACPI_RDESC_TYPE_LARGE) {
		/* Large Resource Type -- bits 6:0 contain the name */

		if (resource_type > ACPI_RDESC_LARGE_MAX) {
			return (NULL);
		}

		return (acpi_gbl_lg_resource_dispatch[(resource_type &
						       ACPI_RDESC_LARGE_MASK)]);
	} else {
		/* Small Resource Type -- bits 6:3 contain the name */

		return (acpi_gbl_sm_resource_dispatch[((resource_type &
							ACPI_RDESC_SMALL_MASK)
						       >> 3)]);
	}
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
	u8 *buffer = output_buffer;
	acpi_status status;
	acpi_size bytes_parsed = 0;
	acpi_size bytes_consumed = 0;
	acpi_size structure_size = 0;
	struct acpi_resource *resource;
	ACPI_RESOURCE_HANDLER handler;

	ACPI_FUNCTION_TRACE("rs_byte_stream_to_list");

	/* Loop until end-of-buffer or an end_tag is found */

	while (bytes_parsed < byte_stream_buffer_length) {
		/* Get the handler associated with this Descriptor Type */

		handler = acpi_rs_get_resource_handler(*byte_stream_buffer);
		if (handler) {
			/* Convert a byte stream resource to local resource struct */

			status = handler(byte_stream_buffer, &bytes_consumed,
					 &buffer, &structure_size);
		} else {
			/* Invalid resource type */

			status = AE_AML_INVALID_RESOURCE_TYPE;
		}

		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Set the aligned length of the new resource descriptor */

		resource = ACPI_CAST_PTR(struct acpi_resource, buffer);
		resource->length =
		    (u32) ACPI_ALIGN_RESOURCE_SIZE(resource->length);

		/* Normal exit on completion of an end_tag resource descriptor */

		if (acpi_rs_get_resource_type(*byte_stream_buffer) ==
		    ACPI_RDESC_TYPE_END_TAG) {
			return_ACPI_STATUS(AE_OK);
		}

		/* Update counter and point to the next input resource */

		bytes_parsed += bytes_consumed;
		byte_stream_buffer += bytes_consumed;

		/* Point to the next structure in the output buffer */

		buffer += ACPI_ALIGN_RESOURCE_SIZE(structure_size);
	}

	/* Completed buffer, but did not find an end_tag resource descriptor */

	return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_list_to_byte_stream
 *
 * PARAMETERS:  Resource                - Pointer to the resource linked list
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
acpi_rs_list_to_byte_stream(struct acpi_resource *resource,
			    acpi_size byte_stream_size_needed,
			    u8 * output_buffer)
{
	u8 *buffer = output_buffer;
	acpi_size bytes_consumed = 0;
	acpi_status status;

	ACPI_FUNCTION_TRACE("rs_list_to_byte_stream");

	/* Convert each resource descriptor in the list */

	while (1) {
		/* Validate Type before dispatch */

		if (resource->type > ACPI_RSTYPE_MAX) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Invalid descriptor type (%X) in resource list\n",
					  resource->type));
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		/* Perform the conversion, per resource type */

		status = acpi_gbl_stream_dispatch[resource->type] (resource,
								   &buffer,
								   &bytes_consumed);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Check for end-of-list */

		if (resource->type == ACPI_RSTYPE_END_TAG) {
			/* An End Tag indicates the end of the Resource Template */

			return_ACPI_STATUS(AE_OK);
		}

		/* Set the Buffer to point to the next (output) resource descriptor */

		buffer += bytes_consumed;

		/* Point to the next input resource object */

		resource = ACPI_PTR_ADD(struct acpi_resource,
					resource, resource->length);
	}
}
