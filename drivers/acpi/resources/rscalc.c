/*******************************************************************************
 *
 * Module Name: rscalc - Calculate stream and list lengths
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
#include <acpi/amlcode.h>
#include <acpi/amlresrc.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rscalc")

/*
 * Base sizes for external resource descriptors, indexed by internal type.
 * Includes size of the descriptor header (1 byte for small descriptors,
 * 3 bytes for large descriptors)
 */
static u8 acpi_gbl_stream_sizes[] = {
	4,			/* ACPI_RSTYPE_IRQ (Byte 3 is optional, but always created) */
	3,			/* ACPI_RSTYPE_DMA */
	2,			/* ACPI_RSTYPE_START_DPF (Byte 1 is optional, but always created) */
	1,			/* ACPI_RSTYPE_END_DPF */
	8,			/* ACPI_RSTYPE_IO */
	4,			/* ACPI_RSTYPE_FIXED_IO */
	1,			/* ACPI_RSTYPE_VENDOR */
	2,			/* ACPI_RSTYPE_END_TAG */
	12,			/* ACPI_RSTYPE_MEM24 */
	20,			/* ACPI_RSTYPE_MEM32 */
	12,			/* ACPI_RSTYPE_FIXED_MEM32 */
	16,			/* ACPI_RSTYPE_ADDRESS16 */
	26,			/* ACPI_RSTYPE_ADDRESS32 */
	46,			/* ACPI_RSTYPE_ADDRESS64 */
	9,			/* ACPI_RSTYPE_EXT_IRQ */
	15			/* ACPI_RSTYPE_GENERIC_REG */
};

/*
 * Base sizes of resource descriptors, both the actual AML stream length and
 * size of the internal struct representation.
 */
struct acpi_resource_sizes {
	u8 minimum_stream_size;
	u8 minimum_struct_size;
};

static struct acpi_resource_sizes acpi_gbl_sm_resource_sizes[] = {
	{0, 0},			/* 0x00, Reserved */
	{0, 0},			/* 0x01, Reserved */
	{0, 0},			/* 0x02, Reserved */
	{0, 0},			/* 0x03, Reserved */
	{3, ACPI_SIZEOF_RESOURCE(struct acpi_resource_irq)},	/* ACPI_RDESC_TYPE_IRQ_FORMAT */
	{3, ACPI_SIZEOF_RESOURCE(struct acpi_resource_dma)},	/* ACPI_RDESC_TYPE_DMA_FORMAT */
	{1, ACPI_SIZEOF_RESOURCE(struct acpi_resource_start_dpf)},	/* ACPI_RDESC_TYPE_START_DEPENDENT */
	{1, ACPI_RESOURCE_LENGTH},	/* ACPI_RDESC_TYPE_END_DEPENDENT */
	{8, ACPI_SIZEOF_RESOURCE(struct acpi_resource_io)},	/* ACPI_RDESC_TYPE_IO_PORT */
	{4, ACPI_SIZEOF_RESOURCE(struct acpi_resource_fixed_io)},	/* ACPI_RDESC_TYPE_FIXED_IO_PORT */
	{0, 0},			/* 0x0A, Reserved */
	{0, 0},			/* 0x0B, Reserved */
	{0, 0},			/* 0x0C, Reserved */
	{0, 0},			/* 0x0D, Reserved */
	{1, ACPI_SIZEOF_RESOURCE(struct acpi_resource_vendor)},	/* ACPI_RDESC_TYPE_SMALL_VENDOR */
	{2, ACPI_RESOURCE_LENGTH},	/* ACPI_RDESC_TYPE_END_TAG */
};

static struct acpi_resource_sizes acpi_gbl_lg_resource_sizes[] = {
	{0, 0},			/* 0x00, Reserved */
	{12, ACPI_SIZEOF_RESOURCE(struct acpi_resource_mem24)},	/* ACPI_RDESC_TYPE_MEMORY_24 */
	{15, ACPI_SIZEOF_RESOURCE(struct acpi_resource_generic_reg)},	/* ACPI_RDESC_TYPE_GENERIC_REGISTER */
	{0, 0},			/* 0x03, Reserved */
	{3, ACPI_SIZEOF_RESOURCE(struct acpi_resource_vendor)},	/* ACPI_RDESC_TYPE_LARGE_VENDOR */
	{20, ACPI_SIZEOF_RESOURCE(struct acpi_resource_mem32)},	/* ACPI_RDESC_TYPE_MEMORY_32 */
	{12, ACPI_SIZEOF_RESOURCE(struct acpi_resource_fixed_mem32)},	/* ACPI_RDESC_TYPE_FIXED_MEMORY_32 */
	{26, ACPI_SIZEOF_RESOURCE(struct acpi_resource_address32)},	/* ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE */
	{16, ACPI_SIZEOF_RESOURCE(struct acpi_resource_address16)},	/* ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE */
	{9, ACPI_SIZEOF_RESOURCE(struct acpi_resource_ext_irq)},	/* ACPI_RDESC_TYPE_EXTENDED_XRUPT */
	{46, ACPI_SIZEOF_RESOURCE(struct acpi_resource_address64)},	/* ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE */
	{56, ACPI_SIZEOF_RESOURCE(struct acpi_resource_address64)},	/* ACPI_RDESC_TYPE_EXTENDED_ADDRESS_SPACE */
};

/* Local prototypes */

static u8 acpi_rs_count_set_bits(u16 bit_field);

static struct acpi_resource_sizes *acpi_rs_get_resource_sizes(u8 resource_type);

static u16 acpi_rs_get_resource_length(u8 * resource);

static acpi_size
acpi_rs_struct_option_length(struct acpi_resource_source *resource_source);

static u32
acpi_rs_stream_option_length(u32 resource_length, u32 minimum_total_length);

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_count_set_bits
 *
 * PARAMETERS:  bit_field       - Field in which to count bits
 *
 * RETURN:      Number of bits set within the field
 *
 * DESCRIPTION: Count the number of bits set in a resource field. Used for
 *              (Short descriptor) interrupt and DMA lists.
 *
 ******************************************************************************/

static u8 acpi_rs_count_set_bits(u16 bit_field)
{
	u8 bits_set;

	ACPI_FUNCTION_ENTRY();

	for (bits_set = 0; bit_field; bits_set++) {
		/* Zero the least significant bit that is set */

		bit_field &= (bit_field - 1);
	}

	return (bits_set);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_sizes
 *
 * PARAMETERS:  resource_type       - Byte 0 of a resource descriptor
 *
 * RETURN:      Pointer to the resource conversion handler
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

static struct acpi_resource_sizes *acpi_rs_get_resource_sizes(u8 resource_type)
{
	struct acpi_resource_sizes *size_info;

	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (resource_type & ACPI_RDESC_TYPE_LARGE) {
		/* Large Resource Type -- bits 6:0 contain the name */

		if (resource_type > ACPI_RDESC_LARGE_MAX) {
			return (NULL);
		}

		size_info = &acpi_gbl_lg_resource_sizes[(resource_type &
							 ACPI_RDESC_LARGE_MASK)];
	} else {
		/* Small Resource Type -- bits 6:3 contain the name */

		size_info = &acpi_gbl_sm_resource_sizes[((resource_type &
							  ACPI_RDESC_SMALL_MASK)
							 >> 3)];
	}

	/* Zero entry indicates an invalid resource type */

	if (!size_info->minimum_stream_size) {
		return (NULL);
	}

	return (size_info);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_length
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *
 * RETURN:      Byte length of the (AML byte stream) descriptor. By definition,
 *              this does not include the size of the descriptor header and the
 *              length field itself.
 *
 * DESCRIPTION: Extract the length of a resource descriptor.
 *
 ******************************************************************************/

static u16 acpi_rs_get_resource_length(u8 * resource)
{
	u16 resource_length;

	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (*resource & ACPI_RDESC_TYPE_LARGE) {
		/* Large Resource type -- length is in bytes 1-2 */

		ACPI_MOVE_16_TO_16(&resource_length, (resource + 1));

	} else {
		/* Small Resource Type -- bits 2:0 of byte 0 contain the length */

		resource_length =
		    (u16) (*resource & ACPI_RDESC_SMALL_LENGTH_MASK);
	}

	return (resource_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_struct_option_length
 *
 * PARAMETERS:  resource_source     - Pointer to optional descriptor field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common code to handle optional resource_source_index and
 *              resource_source fields in some Large descriptors. Used during
 *              list-to-stream conversion
 *
 ******************************************************************************/

static acpi_size
acpi_rs_struct_option_length(struct acpi_resource_source *resource_source)
{
	ACPI_FUNCTION_ENTRY();

	/*
	 * If the resource_source string is valid, return the size of the string
	 * (string_length includes the NULL terminator) plus the size of the
	 * resource_source_index (1).
	 */
	if (resource_source->string_ptr) {
		return ((acpi_size) resource_source->string_length + 1);
	}

	return (0);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_stream_option_length
 *
 * PARAMETERS:  resource_length     - Length from the resource header
 *              minimum_total_length - Minimum length of this resource, before
 *                                    any optional fields. Includes header size
 *
 * RETURN:      Length of optional string (0 if no string present)
 *
 * DESCRIPTION: Common code to handle optional resource_source_index and
 *              resource_source fields in some Large descriptors. Used during
 *              stream-to-list conversion
 *
 ******************************************************************************/

static u32
acpi_rs_stream_option_length(u32 resource_length, u32 minimum_total_length)
{
	u32 string_length = 0;
	u32 minimum_resource_length;

	ACPI_FUNCTION_ENTRY();

	/*
	 * The resource_source_index and resource_source are optional elements of some
	 * Large-type resource descriptors.
	 */

	/* Compute minimum size of the data part of the resource descriptor */

	minimum_resource_length =
	    minimum_total_length - sizeof(struct asl_large_header);

	/*
	 * If the length of the actual resource descriptor is greater than the ACPI
	 * spec-defined minimum length, it means that a resource_source_index exists
	 * and is followed by a (required) null terminated string. The string length
	 * (including the null terminator) is the resource length minus the minimum
	 * length, minus one byte for the resource_source_index itself.
	 */
	if (resource_length > minimum_resource_length) {
		/* Compute the length of the optional string */

		string_length = resource_length - minimum_resource_length - 1;
	}

	/* Round up length to 32 bits for internal structure alignment */

	return (ACPI_ROUND_UP_to_32_bITS(string_length));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_byte_stream_length
 *
 * PARAMETERS:  Resource            - Pointer to the resource linked list
 *              size_needed         - Where the required size is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes a linked list of internal resource descriptors and
 *              calculates the size buffer needed to hold the corresponding
 *              external resource byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_byte_stream_length(struct acpi_resource * resource,
			       acpi_size * size_needed)
{
	acpi_size byte_stream_size_needed = 0;
	acpi_size segment_size;

	ACPI_FUNCTION_TRACE("rs_get_byte_stream_length");

	/* Traverse entire list of internal resource descriptors */

	while (resource) {
		/* Validate the descriptor type */

		if (resource->type > ACPI_RSTYPE_MAX) {
			return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
		}

		/* Get the base size of the (external stream) resource descriptor */

		segment_size = acpi_gbl_stream_sizes[resource->type];

		/*
		 * Augment the base size for descriptors with optional and/or
		 * variable-length fields
		 */
		switch (resource->type) {
		case ACPI_RSTYPE_VENDOR:
			/*
			 * Vendor Defined Resource:
			 * For a Vendor Specific resource, if the Length is between 1 and 7
			 * it will be created as a Small Resource data type, otherwise it
			 * is a Large Resource data type.
			 */
			if (resource->data.vendor_specific.length > 7) {
				/* Base size of a Large resource descriptor */

				segment_size = 3;
			}

			/* Add the size of the vendor-specific data */

			segment_size += resource->data.vendor_specific.length;
			break;

		case ACPI_RSTYPE_END_TAG:
			/*
			 * End Tag:
			 * We are done -- return the accumulated total size.
			 */
			*size_needed = byte_stream_size_needed + segment_size;

			/* Normal exit */

			return_ACPI_STATUS(AE_OK);

		case ACPI_RSTYPE_ADDRESS16:
			/*
			 * 16-Bit Address Resource:
			 * Add the size of the optional resource_source info
			 */
			segment_size +=
			    acpi_rs_struct_option_length(&resource->data.
							 address16.
							 resource_source);
			break;

		case ACPI_RSTYPE_ADDRESS32:
			/*
			 * 32-Bit Address Resource:
			 * Add the size of the optional resource_source info
			 */
			segment_size +=
			    acpi_rs_struct_option_length(&resource->data.
							 address32.
							 resource_source);
			break;

		case ACPI_RSTYPE_ADDRESS64:
			/*
			 * 64-Bit Address Resource:
			 * Add the size of the optional resource_source info
			 */
			segment_size +=
			    acpi_rs_struct_option_length(&resource->data.
							 address64.
							 resource_source);
			break;

		case ACPI_RSTYPE_EXT_IRQ:
			/*
			 * Extended IRQ Resource:
			 * Add the size of each additional optional interrupt beyond the
			 * required 1 (4 bytes for each u32 interrupt number)
			 */
			segment_size += (((acpi_size)
					  resource->data.extended_irq.
					  number_of_interrupts - 1) * 4);

			/* Add the size of the optional resource_source info */

			segment_size +=
			    acpi_rs_struct_option_length(&resource->data.
							 extended_irq.
							 resource_source);
			break;

		default:
			break;
		}

		/* Update the total */

		byte_stream_size_needed += segment_size;

		/* Point to the next object */

		resource = ACPI_PTR_ADD(struct acpi_resource,
					resource, resource->length);
	}

	/* Did not find an END_TAG descriptor */

	return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_list_length
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource byte stream
 *              byte_stream_buffer_length - Size of byte_stream_buffer
 *              size_needed             - Where the size needed is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes an external resource byte stream and calculates the size
 *              buffer needed to hold the corresponding internal resource
 *              descriptor linked list.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_list_length(u8 * byte_stream_buffer,
			u32 byte_stream_buffer_length, acpi_size * size_needed)
{
	u8 *buffer;
	struct acpi_resource_sizes *resource_info;
	u32 buffer_size = 0;
	u32 bytes_parsed = 0;
	u8 resource_type;
	u16 temp16;
	u16 resource_length;
	u16 header_length;
	u32 extra_struct_bytes;

	ACPI_FUNCTION_TRACE("rs_get_list_length");

	while (bytes_parsed < byte_stream_buffer_length) {
		/* The next byte in the stream is the resource descriptor type */

		resource_type = acpi_rs_get_resource_type(*byte_stream_buffer);

		/* Get the base stream size and structure sizes for the descriptor */

		resource_info = acpi_rs_get_resource_sizes(resource_type);
		if (!resource_info) {
			return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
		}

		/* Get the Length field from the input resource descriptor */

		resource_length =
		    acpi_rs_get_resource_length(byte_stream_buffer);

		/* Augment the size for descriptors with optional fields */

		extra_struct_bytes = 0;

		if (!(resource_type & ACPI_RDESC_TYPE_LARGE)) {
			/*
			 * Small resource descriptors
			 */
			header_length = 1;
			buffer = byte_stream_buffer + header_length;

			switch (resource_type) {
			case ACPI_RDESC_TYPE_IRQ_FORMAT:
				/*
				 * IRQ Resource:
				 * Get the number of bits set in the IRQ word
				 */
				ACPI_MOVE_16_TO_16(&temp16, buffer);

				extra_struct_bytes =
				    (acpi_rs_count_set_bits(temp16) *
				     sizeof(u32));
				break;

			case ACPI_RDESC_TYPE_DMA_FORMAT:
				/*
				 * DMA Resource:
				 * Get the number of bits set in the DMA channels byte
				 */
				extra_struct_bytes =
				    (acpi_rs_count_set_bits((u16) * buffer) *
				     sizeof(u32));
				break;

			case ACPI_RDESC_TYPE_SMALL_VENDOR:
				/*
				 * Vendor Specific Resource:
				 * Ensure a 32-bit boundary for the structure
				 */
				extra_struct_bytes =
				    ACPI_ROUND_UP_to_32_bITS(resource_length);
				break;

			case ACPI_RDESC_TYPE_END_TAG:
				/*
				 * End Tag:
				 * Terminate the loop now
				 */
				byte_stream_buffer_length = bytes_parsed;
				break;

			default:
				break;
			}
		} else {
			/*
			 * Large resource descriptors
			 */
			header_length = sizeof(struct asl_large_header);
			buffer = byte_stream_buffer + header_length;

			switch (resource_type) {
			case ACPI_RDESC_TYPE_LARGE_VENDOR:
				/*
				 * Vendor Defined Resource:
				 * Add vendor data and ensure a 32-bit boundary for the structure
				 */
				extra_struct_bytes =
				    ACPI_ROUND_UP_to_32_bITS(resource_length);
				break;

			case ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE:
			case ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE:
				/*
				 * 32-Bit or 16-bit Address Resource:
				 * Add the size of any optional data (resource_source)
				 */
				extra_struct_bytes =
				    acpi_rs_stream_option_length
				    (resource_length,
				     resource_info->minimum_stream_size);
				break;

			case ACPI_RDESC_TYPE_EXTENDED_XRUPT:
				/*
				 * Extended IRQ:
				 * Point past the interrupt_vector_flags to get the
				 * interrupt_table_length.
				 */
				buffer++;

				/*
				 * Add 4 bytes for each additional interrupt. Note: at least one
				 * interrupt is required and is included in the minimum
				 * descriptor size
				 */
				extra_struct_bytes =
				    ((*buffer - 1) * sizeof(u32));

				/* Add the size of any optional data (resource_source) */

				extra_struct_bytes +=
				    acpi_rs_stream_option_length(resource_length
								 -
								 extra_struct_bytes,
								 resource_info->
								 minimum_stream_size);
				break;

			case ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE:
				/*
				 * 64-Bit Address Resource:
				 * Add the size of any optional data (resource_source)
				 * Ensure a 64-bit boundary for the structure
				 */
				extra_struct_bytes =
				    ACPI_ROUND_UP_to_64_bITS
				    (acpi_rs_stream_option_length
				     (resource_length,
				      resource_info->minimum_stream_size));
				break;

			default:
				break;
			}
		}

		/* Update the required buffer size for the internal descriptor structs */

		temp16 =
		    (u16) (resource_info->minimum_struct_size +
			   extra_struct_bytes);
		buffer_size += (u32) ACPI_ALIGN_RESOURCE_SIZE(temp16);

		/*
		 * Update byte count and point to the next resource within the stream
		 * using the size of the header plus the length contained in the header
		 */
		temp16 = (u16) (header_length + resource_length);
		bytes_parsed += temp16;
		byte_stream_buffer += temp16;
	}

	/* This is the data the caller needs */

	*size_needed = buffer_size;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_pci_routing_table_length
 *
 * PARAMETERS:  package_object          - Pointer to the package object
 *              buffer_size_needed      - u32 pointer of the size buffer
 *                                        needed to properly return the
 *                                        parsed data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Given a package representing a PCI routing table, this
 *              calculates the size of the corresponding linked list of
 *              descriptions.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_pci_routing_table_length(union acpi_operand_object *package_object,
				     acpi_size * buffer_size_needed)
{
	u32 number_of_elements;
	acpi_size temp_size_needed = 0;
	union acpi_operand_object **top_object_list;
	u32 index;
	union acpi_operand_object *package_element;
	union acpi_operand_object **sub_object_list;
	u8 name_found;
	u32 table_index;

	ACPI_FUNCTION_TRACE("rs_get_pci_routing_table_length");

	number_of_elements = package_object->package.count;

	/*
	 * Calculate the size of the return buffer.
	 * The base size is the number of elements * the sizes of the
	 * structures.  Additional space for the strings is added below.
	 * The minus one is to subtract the size of the u8 Source[1]
	 * member because it is added below.
	 *
	 * But each PRT_ENTRY structure has a pointer to a string and
	 * the size of that string must be found.
	 */
	top_object_list = package_object->package.elements;

	for (index = 0; index < number_of_elements; index++) {
		/* Dereference the sub-package */

		package_element = *top_object_list;

		/*
		 * The sub_object_list will now point to an array of the
		 * four IRQ elements: Address, Pin, Source and source_index
		 */
		sub_object_list = package_element->package.elements;

		/* Scan the irq_table_elements for the Source Name String */

		name_found = FALSE;

		for (table_index = 0; table_index < 4 && !name_found;
		     table_index++) {
			if ((ACPI_TYPE_STRING ==
			     ACPI_GET_OBJECT_TYPE(*sub_object_list))
			    ||
			    ((ACPI_TYPE_LOCAL_REFERENCE ==
			      ACPI_GET_OBJECT_TYPE(*sub_object_list))
			     && ((*sub_object_list)->reference.opcode ==
				 AML_INT_NAMEPATH_OP))) {
				name_found = TRUE;
			} else {
				/* Look at the next element */

				sub_object_list++;
			}
		}

		temp_size_needed += (sizeof(struct acpi_pci_routing_table) - 4);

		/* Was a String type found? */

		if (name_found) {
			if (ACPI_GET_OBJECT_TYPE(*sub_object_list) ==
			    ACPI_TYPE_STRING) {
				/*
				 * The length String.Length field does not include the
				 * terminating NULL, add 1
				 */
				temp_size_needed += ((acpi_size)
						     (*sub_object_list)->string.
						     length + 1);
			} else {
				temp_size_needed +=
				    acpi_ns_get_pathname_length((*sub_object_list)->reference.node);
			}
		} else {
			/*
			 * If no name was found, then this is a NULL, which is
			 * translated as a u32 zero.
			 */
			temp_size_needed += sizeof(u32);
		}

		/* Round up the size since each element must be aligned */

		temp_size_needed = ACPI_ROUND_UP_to_64_bITS(temp_size_needed);

		/* Point to the next union acpi_operand_object */

		top_object_list++;
	}

	/*
	 * Adding an extra element to the end of the list, essentially a
	 * NULL terminator
	 */
	*buffer_size_needed =
	    temp_size_needed + sizeof(struct acpi_pci_routing_table);
	return_ACPI_STATUS(AE_OK);
}
