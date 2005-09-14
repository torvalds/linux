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
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rscalc")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_byte_stream_length
 *
 * PARAMETERS:  linked_list         - Pointer to the resource linked list
 *              size_needed         - u32 pointer of the size buffer needed
 *                                    to properly return the parsed data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource byte stream and parses it once, calculating
 *              the size buffer needed to hold the linked list that conveys
 *              the resource data.
 *
 ******************************************************************************/
acpi_status
acpi_rs_get_byte_stream_length(struct acpi_resource *linked_list,
			       acpi_size * size_needed)
{
	acpi_size byte_stream_size_needed = 0;
	acpi_size segment_size;
	u8 done = FALSE;

	ACPI_FUNCTION_TRACE("rs_get_byte_stream_length");

	while (!done) {
		/* Init the variable that will hold the size to add to the total. */

		segment_size = 0;

		switch (linked_list->id) {
		case ACPI_RSTYPE_IRQ:
			/*
			 * IRQ Resource
			 * For an IRQ Resource, Byte 3, although optional, will always be
			 * created - it holds IRQ information.
			 */
			segment_size = 4;
			break;

		case ACPI_RSTYPE_DMA:
			/*
			 * DMA Resource
			 * For this resource the size is static
			 */
			segment_size = 3;
			break;

		case ACPI_RSTYPE_START_DPF:
			/*
			 * Start Dependent Functions Resource
			 * For a start_dependent_functions Resource, Byte 1, although
			 * optional, will always be created.
			 */
			segment_size = 2;
			break;

		case ACPI_RSTYPE_END_DPF:
			/*
			 * End Dependent Functions Resource
			 * For this resource the size is static
			 */
			segment_size = 1;
			break;

		case ACPI_RSTYPE_IO:
			/*
			 * IO Port Resource
			 * For this resource the size is static
			 */
			segment_size = 8;
			break;

		case ACPI_RSTYPE_FIXED_IO:
			/*
			 * Fixed IO Port Resource
			 * For this resource the size is static
			 */
			segment_size = 4;
			break;

		case ACPI_RSTYPE_VENDOR:
			/*
			 * Vendor Defined Resource
			 * For a Vendor Specific resource, if the Length is between 1 and 7
			 * it will be created as a Small Resource data type, otherwise it
			 * is a Large Resource data type.
			 */
			if (linked_list->data.vendor_specific.length > 7) {
				segment_size = 3;
			} else {
				segment_size = 1;
			}
			segment_size +=
			    linked_list->data.vendor_specific.length;
			break;

		case ACPI_RSTYPE_END_TAG:
			/*
			 * End Tag
			 * For this resource the size is static
			 */
			segment_size = 2;
			done = TRUE;
			break;

		case ACPI_RSTYPE_MEM24:
			/*
			 * 24-Bit Memory Resource
			 * For this resource the size is static
			 */
			segment_size = 12;
			break;

		case ACPI_RSTYPE_MEM32:
			/*
			 * 32-Bit Memory Range Resource
			 * For this resource the size is static
			 */
			segment_size = 20;
			break;

		case ACPI_RSTYPE_FIXED_MEM32:
			/*
			 * 32-Bit Fixed Memory Resource
			 * For this resource the size is static
			 */
			segment_size = 12;
			break;

		case ACPI_RSTYPE_ADDRESS16:
			/*
			 * 16-Bit Address Resource
			 * The base size of this byte stream is 16. If a Resource Source
			 * string is not NULL, add 1 for the Index + the length of the null
			 * terminated string Resource Source + 1 for the null.
			 */
			segment_size = 16;

			if (linked_list->data.address16.resource_source.
			    string_ptr) {
				segment_size +=
				    linked_list->data.address16.resource_source.
				    string_length;
				segment_size++;
			}
			break;

		case ACPI_RSTYPE_ADDRESS32:
			/*
			 * 32-Bit Address Resource
			 * The base size of this byte stream is 26. If a Resource
			 * Source string is not NULL, add 1 for the Index + the
			 * length of the null terminated string Resource Source +
			 * 1 for the null.
			 */
			segment_size = 26;

			if (linked_list->data.address32.resource_source.
			    string_ptr) {
				segment_size +=
				    linked_list->data.address32.resource_source.
				    string_length;
				segment_size++;
			}
			break;

		case ACPI_RSTYPE_ADDRESS64:
			/*
			 * 64-Bit Address Resource
			 * The base size of this byte stream is 46. If a resource_source
			 * string is not NULL, add 1 for the Index + the length of the null
			 * terminated string Resource Source + 1 for the null.
			 */
			segment_size = 46;

			if (linked_list->data.address64.resource_source.
			    string_ptr) {
				segment_size +=
				    linked_list->data.address64.resource_source.
				    string_length;
				segment_size++;
			}
			break;

		case ACPI_RSTYPE_EXT_IRQ:
			/*
			 * Extended IRQ Resource
			 * The base size of this byte stream is 9. This is for an Interrupt
			 * table length of 1.  For each additional interrupt, add 4.
			 * If a Resource Source string is not NULL, add 1 for the
			 * Index + the length of the null terminated string
			 * Resource Source + 1 for the null.
			 */
			segment_size = 9 + (((acpi_size)
					     linked_list->data.extended_irq.
					     number_of_interrupts - 1) * 4);

			if (linked_list->data.extended_irq.resource_source.
			    string_ptr) {
				segment_size +=
				    linked_list->data.extended_irq.
				    resource_source.string_length;
				segment_size++;
			}
			break;

		default:

			/* If we get here, everything is out of sync, exit with error */

			return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);

		}		/* switch (linked_list->Id) */

		/* Update the total */

		byte_stream_size_needed += segment_size;

		/* Point to the next object */

		linked_list = ACPI_PTR_ADD(struct acpi_resource,
					   linked_list, linked_list->length);
	}

	/* This is the data the caller needs */

	*size_needed = byte_stream_size_needed;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_list_length
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource byte stream
 *              byte_stream_buffer_length - Size of byte_stream_buffer
 *              size_needed             - u32 pointer of the size buffer
 *                                        needed to properly return the
 *                                        parsed data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource byte stream and parses it once, calculating
 *              the size buffer needed to hold the linked list that conveys
 *              the resource data.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_list_length(u8 * byte_stream_buffer,
			u32 byte_stream_buffer_length, acpi_size * size_needed)
{
	u32 buffer_size = 0;
	u32 bytes_parsed = 0;
	u8 number_of_interrupts = 0;
	u8 number_of_channels = 0;
	u8 resource_type;
	u32 structure_size;
	u32 bytes_consumed;
	u8 *buffer;
	u8 temp8;
	u16 temp16;
	u8 index;
	u8 additional_bytes;

	ACPI_FUNCTION_TRACE("rs_get_list_length");

	while (bytes_parsed < byte_stream_buffer_length) {
		/* The next byte in the stream is the resource type */

		resource_type = acpi_rs_get_resource_type(*byte_stream_buffer);

		switch (resource_type) {
		case ACPI_RDESC_TYPE_MEMORY_24:
			/*
			 * 24-Bit Memory Resource
			 */
			bytes_consumed = 12;

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_mem24);
			break;

		case ACPI_RDESC_TYPE_LARGE_VENDOR:
			/*
			 * Vendor Defined Resource
			 */
			buffer = byte_stream_buffer;
			++buffer;

			ACPI_MOVE_16_TO_16(&temp16, buffer);
			bytes_consumed = temp16 + 3;

			/* Ensure a 32-bit boundary for the structure */

			temp16 = (u16) ACPI_ROUND_UP_to_32_bITS(temp16);

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_vendor) +
			    (temp16 * sizeof(u8));
			break;

		case ACPI_RDESC_TYPE_MEMORY_32:
			/*
			 * 32-Bit Memory Range Resource
			 */
			bytes_consumed = 20;

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_mem32);
			break;

		case ACPI_RDESC_TYPE_FIXED_MEMORY_32:
			/*
			 * 32-Bit Fixed Memory Resource
			 */
			bytes_consumed = 12;

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct
						 acpi_resource_fixed_mem32);
			break;

		case ACPI_RDESC_TYPE_EXTENDED_ADDRESS_SPACE:
			/*
			 * 64-Bit Address Resource
			 */
			buffer = byte_stream_buffer;

			++buffer;
			ACPI_MOVE_16_TO_16(&temp16, buffer);

			bytes_consumed = temp16 + 3;
			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct
						 acpi_resource_address64);
			break;

		case ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE:
			/*
			 * 64-Bit Address Resource
			 */
			buffer = byte_stream_buffer;

			++buffer;
			ACPI_MOVE_16_TO_16(&temp16, buffer);

			bytes_consumed = temp16 + 3;

			/*
			 * Resource Source Index and Resource Source are optional elements.
			 * Check the length of the Bytestream.  If it is greater than 43,
			 * that means that an Index exists and is followed by a null
			 * terminated string.  Therefore, set the temp variable to the
			 * length minus the minimum byte stream length plus the byte for
			 * the Index to determine the size of the NULL terminated string.
			 */
			if (43 < temp16) {
				temp8 = (u8) (temp16 - 44);
			} else {
				temp8 = 0;
			}

			/* Ensure a 64-bit boundary for the structure */

			temp8 = (u8) ACPI_ROUND_UP_to_64_bITS(temp8);

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address64)
			    + (temp8 * sizeof(u8));
			break;

		case ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE:
			/*
			 * 32-Bit Address Resource
			 */
			buffer = byte_stream_buffer;

			++buffer;
			ACPI_MOVE_16_TO_16(&temp16, buffer);

			bytes_consumed = temp16 + 3;

			/*
			 * Resource Source Index and Resource Source are optional elements.
			 * Check the length of the Bytestream.  If it is greater than 23,
			 * that means that an Index exists and is followed by a null
			 * terminated string.  Therefore, set the temp variable to the
			 * length minus the minimum byte stream length plus the byte for
			 * the Index to determine the size of the NULL terminated string.
			 */
			if (23 < temp16) {
				temp8 = (u8) (temp16 - 24);
			} else {
				temp8 = 0;
			}

			/* Ensure a 32-bit boundary for the structure */

			temp8 = (u8) ACPI_ROUND_UP_to_32_bITS(temp8);

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address32)
			    + (temp8 * sizeof(u8));
			break;

		case ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE:
			/*
			 * 16-Bit Address Resource
			 */
			buffer = byte_stream_buffer;

			++buffer;
			ACPI_MOVE_16_TO_16(&temp16, buffer);

			bytes_consumed = temp16 + 3;

			/*
			 * Resource Source Index and Resource Source are optional elements.
			 * Check the length of the Bytestream.  If it is greater than 13,
			 * that means that an Index exists and is followed by a null
			 * terminated string.  Therefore, set the temp variable to the
			 * length minus the minimum byte stream length plus the byte for
			 * the Index to determine the size of the NULL terminated string.
			 */
			if (13 < temp16) {
				temp8 = (u8) (temp16 - 14);
			} else {
				temp8 = 0;
			}

			/* Ensure a 32-bit boundary for the structure */

			temp8 = (u8) ACPI_ROUND_UP_to_32_bITS(temp8);

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_address16)
			    + (temp8 * sizeof(u8));
			break;

		case ACPI_RDESC_TYPE_EXTENDED_XRUPT:
			/*
			 * Extended IRQ
			 */
			buffer = byte_stream_buffer;

			++buffer;
			ACPI_MOVE_16_TO_16(&temp16, buffer);

			bytes_consumed = temp16 + 3;

			/*
			 * Point past the length field and the Interrupt vector flags to
			 * save off the Interrupt table length to the Temp8 variable.
			 */
			buffer += 3;
			temp8 = *buffer;

			/*
			 * To compensate for multiple interrupt numbers, add 4 bytes for
			 * each additional interrupts greater than 1
			 */
			additional_bytes = (u8) ((temp8 - 1) * 4);

			/*
			 * Resource Source Index and Resource Source are optional elements.
			 * Check the length of the Bytestream.  If it is greater than 9,
			 * that means that an Index exists and is followed by a null
			 * terminated string.  Therefore, set the temp variable to the
			 * length minus the minimum byte stream length plus the byte for
			 * the Index to determine the size of the NULL terminated string.
			 */
			if (9 + additional_bytes < temp16) {
				temp8 = (u8) (temp16 - (9 + additional_bytes));
			} else {
				temp8 = 0;
			}

			/* Ensure a 32-bit boundary for the structure */

			temp8 = (u8) ACPI_ROUND_UP_to_32_bITS(temp8);

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_ext_irq) +
			    (additional_bytes * sizeof(u8)) +
			    (temp8 * sizeof(u8));
			break;

		case ACPI_RDESC_TYPE_IRQ_FORMAT:
			/*
			 * IRQ Resource.
			 * Determine if it there are two or three trailing bytes
			 */
			buffer = byte_stream_buffer;
			temp8 = *buffer;

			if (temp8 & 0x01) {
				bytes_consumed = 4;
			} else {
				bytes_consumed = 3;
			}

			/* Point past the descriptor */

			++buffer;

			/* Look at the number of bits set */

			ACPI_MOVE_16_TO_16(&temp16, buffer);

			for (index = 0; index < 16; index++) {
				if (temp16 & 0x1) {
					++number_of_interrupts;
				}

				temp16 >>= 1;
			}

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_io) +
			    (number_of_interrupts * sizeof(u32));
			break;

		case ACPI_RDESC_TYPE_DMA_FORMAT:
			/*
			 * DMA Resource
			 */
			buffer = byte_stream_buffer;
			bytes_consumed = 3;

			/* Point past the descriptor */

			++buffer;

			/* Look at the number of bits set */

			temp8 = *buffer;

			for (index = 0; index < 8; index++) {
				if (temp8 & 0x1) {
					++number_of_channels;
				}

				temp8 >>= 1;
			}

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_dma) +
			    (number_of_channels * sizeof(u32));
			break;

		case ACPI_RDESC_TYPE_START_DEPENDENT:
			/*
			 * Start Dependent Functions Resource
			 * Determine if it there are two or three trailing bytes
			 */
			buffer = byte_stream_buffer;
			temp8 = *buffer;

			if (temp8 & 0x01) {
				bytes_consumed = 2;
			} else {
				bytes_consumed = 1;
			}

			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct
						 acpi_resource_start_dpf);
			break;

		case ACPI_RDESC_TYPE_END_DEPENDENT:
			/*
			 * End Dependent Functions Resource
			 */
			bytes_consumed = 1;
			structure_size = ACPI_RESOURCE_LENGTH;
			break;

		case ACPI_RDESC_TYPE_IO_PORT:
			/*
			 * IO Port Resource
			 */
			bytes_consumed = 8;
			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_io);
			break;

		case ACPI_RDESC_TYPE_FIXED_IO_PORT:
			/*
			 * Fixed IO Port Resource
			 */
			bytes_consumed = 4;
			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_fixed_io);
			break;

		case ACPI_RDESC_TYPE_SMALL_VENDOR:
			/*
			 * Vendor Specific Resource
			 */
			buffer = byte_stream_buffer;

			temp8 = *buffer;
			temp8 = (u8) (temp8 & 0x7);
			bytes_consumed = temp8 + 1;

			/* Ensure a 32-bit boundary for the structure */

			temp8 = (u8) ACPI_ROUND_UP_to_32_bITS(temp8);
			structure_size =
			    ACPI_SIZEOF_RESOURCE(struct acpi_resource_vendor) +
			    (temp8 * sizeof(u8));
			break;

		case ACPI_RDESC_TYPE_END_TAG:
			/*
			 * End Tag
			 */
			bytes_consumed = 2;
			structure_size = ACPI_RESOURCE_LENGTH;
			byte_stream_buffer_length = bytes_parsed;
			break;

		default:
			/*
			 * If we get here, everything is out of sync,
			 * exit with an error
			 */
			return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
		}

		/* Update the return value and counter */

		buffer_size += (u32) ACPI_ALIGN_RESOURCE_SIZE(structure_size);
		bytes_parsed += bytes_consumed;

		/* Set the byte stream to point to the next resource */

		byte_stream_buffer += bytes_consumed;
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
				temp_size_needed += acpi_ns_get_pathname_length((*sub_object_list)->reference.node);
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
