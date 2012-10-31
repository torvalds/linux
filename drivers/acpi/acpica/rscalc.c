/*******************************************************************************
 *
 * Module Name: rscalc - Calculate stream and list lengths
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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
#include "accommon.h"
#include "acresrc.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rscalc")

/* Local prototypes */
static u8 acpi_rs_count_set_bits(u16 bit_field);

static acpi_rs_length
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

		bit_field &= (u16) (bit_field - 1);
	}

	return bits_set;
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

static acpi_rs_length
acpi_rs_struct_option_length(struct acpi_resource_source *resource_source)
{
	ACPI_FUNCTION_ENTRY();

	/*
	 * If the resource_source string is valid, return the size of the string
	 * (string_length includes the NULL terminator) plus the size of the
	 * resource_source_index (1).
	 */
	if (resource_source->string_ptr) {
		return ((acpi_rs_length) (resource_source->string_length + 1));
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
acpi_rs_stream_option_length(u32 resource_length,
			     u32 minimum_aml_resource_length)
{
	u32 string_length = 0;

	ACPI_FUNCTION_ENTRY();

	/*
	 * The resource_source_index and resource_source are optional elements of some
	 * Large-type resource descriptors.
	 */

	/*
	 * If the length of the actual resource descriptor is greater than the ACPI
	 * spec-defined minimum length, it means that a resource_source_index exists
	 * and is followed by a (required) null terminated string. The string length
	 * (including the null terminator) is the resource length minus the minimum
	 * length, minus one byte for the resource_source_index itself.
	 */
	if (resource_length > minimum_aml_resource_length) {

		/* Compute the length of the optional string */

		string_length =
		    resource_length - minimum_aml_resource_length - 1;
	}

	/*
	 * Round the length up to a multiple of the native word in order to
	 * guarantee that the entire resource descriptor is native word aligned
	 */
	return ((u32) ACPI_ROUND_UP_TO_NATIVE_WORD(string_length));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_aml_length
 *
 * PARAMETERS:  resource            - Pointer to the resource linked list
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
acpi_rs_get_aml_length(struct acpi_resource * resource, acpi_size * size_needed)
{
	acpi_size aml_size_needed = 0;
	acpi_rs_length total_size;

	ACPI_FUNCTION_TRACE(rs_get_aml_length);

	/* Traverse entire list of internal resource descriptors */

	while (resource) {

		/* Validate the descriptor type */

		if (resource->type > ACPI_RESOURCE_TYPE_MAX) {
			return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
		}

		/* Get the base size of the (external stream) resource descriptor */

		total_size = acpi_gbl_aml_resource_sizes[resource->type];

		/*
		 * Augment the base size for descriptors with optional and/or
		 * variable-length fields
		 */
		switch (resource->type) {
		case ACPI_RESOURCE_TYPE_IRQ:

			/* Length can be 3 or 2 */

			if (resource->data.irq.descriptor_length == 2) {
				total_size--;
			}
			break;

		case ACPI_RESOURCE_TYPE_START_DEPENDENT:

			/* Length can be 1 or 0 */

			if (resource->data.irq.descriptor_length == 0) {
				total_size--;
			}
			break;

		case ACPI_RESOURCE_TYPE_VENDOR:
			/*
			 * Vendor Defined Resource:
			 * For a Vendor Specific resource, if the Length is between 1 and 7
			 * it will be created as a Small Resource data type, otherwise it
			 * is a Large Resource data type.
			 */
			if (resource->data.vendor.byte_length > 7) {

				/* Base size of a Large resource descriptor */

				total_size =
				    sizeof(struct aml_resource_large_header);
			}

			/* Add the size of the vendor-specific data */

			total_size = (acpi_rs_length)
			    (total_size + resource->data.vendor.byte_length);
			break;

		case ACPI_RESOURCE_TYPE_END_TAG:
			/*
			 * End Tag:
			 * We are done -- return the accumulated total size.
			 */
			*size_needed = aml_size_needed + total_size;

			/* Normal exit */

			return_ACPI_STATUS(AE_OK);

		case ACPI_RESOURCE_TYPE_ADDRESS16:
			/*
			 * 16-Bit Address Resource:
			 * Add the size of the optional resource_source info
			 */
			total_size = (acpi_rs_length)
			    (total_size +
			     acpi_rs_struct_option_length(&resource->data.
							  address16.
							  resource_source));
			break;

		case ACPI_RESOURCE_TYPE_ADDRESS32:
			/*
			 * 32-Bit Address Resource:
			 * Add the size of the optional resource_source info
			 */
			total_size = (acpi_rs_length)
			    (total_size +
			     acpi_rs_struct_option_length(&resource->data.
							  address32.
							  resource_source));
			break;

		case ACPI_RESOURCE_TYPE_ADDRESS64:
			/*
			 * 64-Bit Address Resource:
			 * Add the size of the optional resource_source info
			 */
			total_size = (acpi_rs_length)
			    (total_size +
			     acpi_rs_struct_option_length(&resource->data.
							  address64.
							  resource_source));
			break;

		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			/*
			 * Extended IRQ Resource:
			 * Add the size of each additional optional interrupt beyond the
			 * required 1 (4 bytes for each u32 interrupt number)
			 */
			total_size = (acpi_rs_length)
			    (total_size +
			     ((resource->data.extended_irq.interrupt_count -
			       1) * 4) +
			     /* Add the size of the optional resource_source info */
			     acpi_rs_struct_option_length(&resource->data.
							  extended_irq.
							  resource_source));
			break;

		case ACPI_RESOURCE_TYPE_GPIO:

			total_size =
			    (acpi_rs_length) (total_size +
					      (resource->data.gpio.
					       pin_table_length * 2) +
					      resource->data.gpio.
					      resource_source.string_length +
					      resource->data.gpio.
					      vendor_length);

			break;

		case ACPI_RESOURCE_TYPE_SERIAL_BUS:

			total_size =
			    acpi_gbl_aml_resource_serial_bus_sizes[resource->
								   data.
								   common_serial_bus.
								   type];

			total_size = (acpi_rs_length) (total_size +
						       resource->data.
						       i2c_serial_bus.
						       resource_source.
						       string_length +
						       resource->data.
						       i2c_serial_bus.
						       vendor_length);

			break;

		default:
			break;
		}

		/* Update the total */

		aml_size_needed += total_size;

		/* Point to the next object */

		resource =
		    ACPI_ADD_PTR(struct acpi_resource, resource,
				 resource->length);
	}

	/* Did not find an end_tag resource descriptor */

	return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_list_length
 *
 * PARAMETERS:  aml_buffer          - Pointer to the resource byte stream
 *              aml_buffer_length   - Size of aml_buffer
 *              size_needed         - Where the size needed is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes an external resource byte stream and calculates the size
 *              buffer needed to hold the corresponding internal resource
 *              descriptor linked list.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_list_length(u8 * aml_buffer,
			u32 aml_buffer_length, acpi_size * size_needed)
{
	acpi_status status;
	u8 *end_aml;
	u8 *buffer;
	u32 buffer_size;
	u16 temp16;
	u16 resource_length;
	u32 extra_struct_bytes;
	u8 resource_index;
	u8 minimum_aml_resource_length;
	union aml_resource *aml_resource;

	ACPI_FUNCTION_TRACE(rs_get_list_length);

	*size_needed = ACPI_RS_SIZE_MIN;	/* Minimum size is one end_tag */
	end_aml = aml_buffer + aml_buffer_length;

	/* Walk the list of AML resource descriptors */

	while (aml_buffer < end_aml) {

		/* Validate the Resource Type and Resource Length */

		status = acpi_ut_validate_resource(aml_buffer, &resource_index);
		if (ACPI_FAILURE(status)) {
			/*
			 * Exit on failure. Cannot continue because the descriptor length
			 * may be bogus also.
			 */
			return_ACPI_STATUS(status);
		}

		aml_resource = (void *)aml_buffer;

		/* Get the resource length and base (minimum) AML size */

		resource_length = acpi_ut_get_resource_length(aml_buffer);
		minimum_aml_resource_length =
		    acpi_gbl_resource_aml_sizes[resource_index];

		/*
		 * Augment the size for descriptors with optional
		 * and/or variable length fields
		 */
		extra_struct_bytes = 0;
		buffer =
		    aml_buffer + acpi_ut_get_resource_header_length(aml_buffer);

		switch (acpi_ut_get_resource_type(aml_buffer)) {
		case ACPI_RESOURCE_NAME_IRQ:
			/*
			 * IRQ Resource:
			 * Get the number of bits set in the 16-bit IRQ mask
			 */
			ACPI_MOVE_16_TO_16(&temp16, buffer);
			extra_struct_bytes = acpi_rs_count_set_bits(temp16);
			break;

		case ACPI_RESOURCE_NAME_DMA:
			/*
			 * DMA Resource:
			 * Get the number of bits set in the 8-bit DMA mask
			 */
			extra_struct_bytes = acpi_rs_count_set_bits(*buffer);
			break;

		case ACPI_RESOURCE_NAME_VENDOR_SMALL:
		case ACPI_RESOURCE_NAME_VENDOR_LARGE:
			/*
			 * Vendor Resource:
			 * Get the number of vendor data bytes
			 */
			extra_struct_bytes = resource_length;
			break;

		case ACPI_RESOURCE_NAME_END_TAG:
			/*
			 * End Tag: This is the normal exit
			 */
			return_ACPI_STATUS(AE_OK);

		case ACPI_RESOURCE_NAME_ADDRESS32:
		case ACPI_RESOURCE_NAME_ADDRESS16:
		case ACPI_RESOURCE_NAME_ADDRESS64:
			/*
			 * Address Resource:
			 * Add the size of the optional resource_source
			 */
			extra_struct_bytes =
			    acpi_rs_stream_option_length(resource_length,
							 minimum_aml_resource_length);
			break;

		case ACPI_RESOURCE_NAME_EXTENDED_IRQ:
			/*
			 * Extended IRQ Resource:
			 * Using the interrupt_table_length, add 4 bytes for each additional
			 * interrupt. Note: at least one interrupt is required and is
			 * included in the minimum descriptor size (reason for the -1)
			 */
			extra_struct_bytes = (buffer[1] - 1) * sizeof(u32);

			/* Add the size of the optional resource_source */

			extra_struct_bytes +=
			    acpi_rs_stream_option_length(resource_length -
							 extra_struct_bytes,
							 minimum_aml_resource_length);
			break;

		case ACPI_RESOURCE_NAME_GPIO:

			/* Vendor data is optional */

			if (aml_resource->gpio.vendor_length) {
				extra_struct_bytes +=
				    aml_resource->gpio.vendor_offset -
				    aml_resource->gpio.pin_table_offset +
				    aml_resource->gpio.vendor_length;
			} else {
				extra_struct_bytes +=
				    aml_resource->large_header.resource_length +
				    sizeof(struct aml_resource_large_header) -
				    aml_resource->gpio.pin_table_offset;
			}
			break;

		case ACPI_RESOURCE_NAME_SERIAL_BUS:

			minimum_aml_resource_length =
			    acpi_gbl_resource_aml_serial_bus_sizes
			    [aml_resource->common_serial_bus.type];
			extra_struct_bytes +=
			    aml_resource->common_serial_bus.resource_length -
			    minimum_aml_resource_length;
			break;

		default:
			break;
		}

		/*
		 * Update the required buffer size for the internal descriptor structs
		 *
		 * Important: Round the size up for the appropriate alignment. This
		 * is a requirement on IA64.
		 */
		if (acpi_ut_get_resource_type(aml_buffer) ==
		    ACPI_RESOURCE_NAME_SERIAL_BUS) {
			buffer_size =
			    acpi_gbl_resource_struct_serial_bus_sizes
			    [aml_resource->common_serial_bus.type] +
			    extra_struct_bytes;
		} else {
			buffer_size =
			    acpi_gbl_resource_struct_sizes[resource_index] +
			    extra_struct_bytes;
		}
		buffer_size = (u32)ACPI_ROUND_UP_TO_NATIVE_WORD(buffer_size);

		*size_needed += buffer_size;

		ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES,
				  "Type %.2X, AmlLength %.2X InternalLength %.2X\n",
				  acpi_ut_get_resource_type(aml_buffer),
				  acpi_ut_get_descriptor_length(aml_buffer),
				  buffer_size));

		/*
		 * Point to the next resource within the AML stream using the length
		 * contained in the resource descriptor header
		 */
		aml_buffer += acpi_ut_get_descriptor_length(aml_buffer);
	}

	/* Did not find an end_tag resource descriptor */

	return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
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

	ACPI_FUNCTION_TRACE(rs_get_pci_routing_table_length);

	number_of_elements = package_object->package.count;

	/*
	 * Calculate the size of the return buffer.
	 * The base size is the number of elements * the sizes of the
	 * structures. Additional space for the strings is added below.
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

		/* We must have a valid Package object */

		if (!package_element ||
		    (package_element->common.type != ACPI_TYPE_PACKAGE)) {
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/*
		 * The sub_object_list will now point to an array of the
		 * four IRQ elements: Address, Pin, Source and source_index
		 */
		sub_object_list = package_element->package.elements;

		/* Scan the irq_table_elements for the Source Name String */

		name_found = FALSE;

		for (table_index = 0; table_index < 4 && !name_found;
		     table_index++) {
			if (*sub_object_list &&	/* Null object allowed */
			    ((ACPI_TYPE_STRING ==
			      (*sub_object_list)->common.type) ||
			     ((ACPI_TYPE_LOCAL_REFERENCE ==
			       (*sub_object_list)->common.type) &&
			      ((*sub_object_list)->reference.class ==
			       ACPI_REFCLASS_NAME)))) {
				name_found = TRUE;
			} else {
				/* Look at the next element */

				sub_object_list++;
			}
		}

		temp_size_needed += (sizeof(struct acpi_pci_routing_table) - 4);

		/* Was a String type found? */

		if (name_found) {
			if ((*sub_object_list)->common.type == ACPI_TYPE_STRING) {
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

		temp_size_needed = ACPI_ROUND_UP_TO_64BIT(temp_size_needed);

		/* Point to the next union acpi_operand_object */

		top_object_list++;
	}

	/*
	 * Add an extra element to the end of the list, essentially a
	 * NULL terminator
	 */
	*buffer_size_needed =
	    temp_size_needed + sizeof(struct acpi_pci_routing_table);
	return_ACPI_STATUS(AE_OK);
}
