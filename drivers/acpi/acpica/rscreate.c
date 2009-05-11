/*******************************************************************************
 *
 * Module Name: rscreate - Create resource lists/tables
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
ACPI_MODULE_NAME("rscreate")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_create_resource_list
 *
 * PARAMETERS:  aml_buffer          - Pointer to the resource byte stream
 *              output_buffer       - Pointer to the user's buffer
 *
 * RETURN:      Status: AE_OK if okay, else a valid acpi_status code
 *              If output_buffer is not large enough, output_buffer_length
 *              indicates how large output_buffer should be, else it
 *              indicates how may u8 elements of output_buffer are valid.
 *
 * DESCRIPTION: Takes the byte stream returned from a _CRS, _PRS control method
 *              execution and parses the stream to create a linked list
 *              of device resources.
 *
 ******************************************************************************/
acpi_status
acpi_rs_create_resource_list(union acpi_operand_object *aml_buffer,
			     struct acpi_buffer *output_buffer)
{

	acpi_status status;
	u8 *aml_start;
	acpi_size list_size_needed = 0;
	u32 aml_buffer_length;
	void *resource;

	ACPI_FUNCTION_TRACE(rs_create_resource_list);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "AmlBuffer = %p\n", aml_buffer));

	/* Params already validated, so we don't re-validate here */

	aml_buffer_length = aml_buffer->buffer.length;
	aml_start = aml_buffer->buffer.pointer;

	/*
	 * Pass the aml_buffer into a module that can calculate
	 * the buffer size needed for the linked list
	 */
	status = acpi_rs_get_list_length(aml_start, aml_buffer_length,
					 &list_size_needed);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Status=%X ListSizeNeeded=%X\n",
			  status, (u32) list_size_needed));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(output_buffer, list_size_needed);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Do the conversion */

	resource = output_buffer->pointer;
	status = acpi_ut_walk_aml_resources(aml_start, aml_buffer_length,
					    acpi_rs_convert_aml_to_resources,
					    &resource);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "OutputBuffer %p Length %X\n",
			  output_buffer->pointer, (u32) output_buffer->length));
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_create_pci_routing_table
 *
 * PARAMETERS:  package_object          - Pointer to a union acpi_operand_object
 *                                        package
 *              output_buffer           - Pointer to the user's buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid acpi_status code.
 *              If the output_buffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and output_buffer->Length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Takes the union acpi_operand_object    package and creates a
 *              linked list of PCI interrupt descriptions
 *
 * NOTE: It is the caller's responsibility to ensure that the start of the
 * output buffer is aligned properly (if necessary).
 *
 ******************************************************************************/

acpi_status
acpi_rs_create_pci_routing_table(union acpi_operand_object *package_object,
				 struct acpi_buffer *output_buffer)
{
	u8 *buffer;
	union acpi_operand_object **top_object_list;
	union acpi_operand_object **sub_object_list;
	union acpi_operand_object *obj_desc;
	acpi_size buffer_size_needed = 0;
	u32 number_of_elements;
	u32 index;
	struct acpi_pci_routing_table *user_prt;
	struct acpi_namespace_node *node;
	acpi_status status;
	struct acpi_buffer path_buffer;

	ACPI_FUNCTION_TRACE(rs_create_pci_routing_table);

	/* Params already validated, so we don't re-validate here */

	/* Get the required buffer length */

	status = acpi_rs_get_pci_routing_table_length(package_object,
						      &buffer_size_needed);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "BufferSizeNeeded = %X\n",
			  (u32) buffer_size_needed));

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(output_buffer, buffer_size_needed);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Loop through the ACPI_INTERNAL_OBJECTS - Each object should be a
	 * package that in turn contains an acpi_integer Address, a u8 Pin,
	 * a Name, and a u8 source_index.
	 */
	top_object_list = package_object->package.elements;
	number_of_elements = package_object->package.count;
	buffer = output_buffer->pointer;
	user_prt = ACPI_CAST_PTR(struct acpi_pci_routing_table, buffer);

	for (index = 0; index < number_of_elements; index++) {

		/*
		 * Point user_prt past this current structure
		 *
		 * NOTE: On the first iteration, user_prt->Length will
		 * be zero because we cleared the return buffer earlier
		 */
		buffer += user_prt->length;
		user_prt = ACPI_CAST_PTR(struct acpi_pci_routing_table, buffer);

		/*
		 * Fill in the Length field with the information we have at this point.
		 * The minus four is to subtract the size of the u8 Source[4] member
		 * because it is added below.
		 */
		user_prt->length = (sizeof(struct acpi_pci_routing_table) - 4);

		/* Each element of the top-level package must also be a package */

		if ((*top_object_list)->common.type != ACPI_TYPE_PACKAGE) {
			ACPI_ERROR((AE_INFO,
				    "(PRT[%X]) Need sub-package, found %s",
				    index,
				    acpi_ut_get_object_type_name
				    (*top_object_list)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Each sub-package must be of length 4 */

		if ((*top_object_list)->package.count != 4) {
			ACPI_ERROR((AE_INFO,
				    "(PRT[%X]) Need package of length 4, found length %d",
				    index, (*top_object_list)->package.count));
			return_ACPI_STATUS(AE_AML_PACKAGE_LIMIT);
		}

		/*
		 * Dereference the sub-package.
		 * The sub_object_list will now point to an array of the four IRQ
		 * elements: [Address, Pin, Source, source_index]
		 */
		sub_object_list = (*top_object_list)->package.elements;

		/* 1) First subobject: Dereference the PRT.Address */

		obj_desc = sub_object_list[0];
		if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
			ACPI_ERROR((AE_INFO,
				    "(PRT[%X].Address) Need Integer, found %s",
				    index,
				    acpi_ut_get_object_type_name(obj_desc)));
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		user_prt->address = obj_desc->integer.value;

		/* 2) Second subobject: Dereference the PRT.Pin */

		obj_desc = sub_object_list[1];
		if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
			ACPI_ERROR((AE_INFO,
				    "(PRT[%X].Pin) Need Integer, found %s",
				    index,
				    acpi_ut_get_object_type_name(obj_desc)));
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		user_prt->pin = (u32) obj_desc->integer.value;

		/*
		 * If the BIOS has erroneously reversed the _PRT source_name (index 2)
		 * and the source_index (index 3), fix it. _PRT is important enough to
		 * workaround this BIOS error. This also provides compatibility with
		 * other ACPI implementations.
		 */
		obj_desc = sub_object_list[3];
		if (!obj_desc || (obj_desc->common.type != ACPI_TYPE_INTEGER)) {
			sub_object_list[3] = sub_object_list[2];
			sub_object_list[2] = obj_desc;

			ACPI_WARNING((AE_INFO,
				      "(PRT[%X].Source) SourceName and SourceIndex are reversed, fixed",
				      index));
		}

		/*
		 * 3) Third subobject: Dereference the PRT.source_name
		 * The name may be unresolved (slack mode), so allow a null object
		 */
		obj_desc = sub_object_list[2];
		if (obj_desc) {
			switch (obj_desc->common.type) {
			case ACPI_TYPE_LOCAL_REFERENCE:

				if (obj_desc->reference.class !=
				    ACPI_REFCLASS_NAME) {
					ACPI_ERROR((AE_INFO,
						    "(PRT[%X].Source) Need name, found Reference Class %X",
						    index,
						    obj_desc->reference.class));
					return_ACPI_STATUS(AE_BAD_DATA);
				}

				node = obj_desc->reference.node;

				/* Use *remaining* length of the buffer as max for pathname */

				path_buffer.length = output_buffer->length -
				    (u32) ((u8 *) user_prt->source -
					   (u8 *) output_buffer->pointer);
				path_buffer.pointer = user_prt->source;

				status =
				    acpi_ns_handle_to_pathname((acpi_handle)
							       node,
							       &path_buffer);

				/* +1 to include null terminator */

				user_prt->length +=
				    (u32) ACPI_STRLEN(user_prt->source) + 1;
				break;

			case ACPI_TYPE_STRING:

				ACPI_STRCPY(user_prt->source,
					    obj_desc->string.pointer);

				/*
				 * Add to the Length field the length of the string
				 * (add 1 for terminator)
				 */
				user_prt->length += obj_desc->string.length + 1;
				break;

			case ACPI_TYPE_INTEGER:
				/*
				 * If this is a number, then the Source Name is NULL, since the
				 * entire buffer was zeroed out, we can leave this alone.
				 *
				 * Add to the Length field the length of the u32 NULL
				 */
				user_prt->length += sizeof(u32);
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "(PRT[%X].Source) Need Ref/String/Integer, found %s",
					    index,
					    acpi_ut_get_object_type_name
					    (obj_desc)));
				return_ACPI_STATUS(AE_BAD_DATA);
			}
		}

		/* Now align the current length */

		user_prt->length =
		    (u32) ACPI_ROUND_UP_TO_64BIT(user_prt->length);

		/* 4) Fourth subobject: Dereference the PRT.source_index */

		obj_desc = sub_object_list[3];
		if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
			ACPI_ERROR((AE_INFO,
				    "(PRT[%X].SourceIndex) Need Integer, found %s",
				    index,
				    acpi_ut_get_object_type_name(obj_desc)));
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		user_prt->source_index = (u32) obj_desc->integer.value;

		/* Point to the next union acpi_operand_object in the top level package */

		top_object_list++;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "OutputBuffer %p Length %X\n",
			  output_buffer->pointer, (u32) output_buffer->length));
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_create_aml_resources
 *
 * PARAMETERS:  linked_list_buffer      - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid acpi_status code.
 *              If the output_buffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and output_buffer->Length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Takes the linked list of device resources and
 *              creates a bytestream to be used as input for the
 *              _SRS control method.
 *
 ******************************************************************************/

acpi_status
acpi_rs_create_aml_resources(struct acpi_resource *linked_list_buffer,
			     struct acpi_buffer *output_buffer)
{
	acpi_status status;
	acpi_size aml_size_needed = 0;

	ACPI_FUNCTION_TRACE(rs_create_aml_resources);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "LinkedListBuffer = %p\n",
			  linked_list_buffer));

	/*
	 * Params already validated, so we don't re-validate here
	 *
	 * Pass the linked_list_buffer into a module that calculates
	 * the buffer size needed for the byte stream.
	 */
	status = acpi_rs_get_aml_length(linked_list_buffer, &aml_size_needed);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "AmlSizeNeeded=%X, %s\n",
			  (u32) aml_size_needed,
			  acpi_format_exception(status)));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(output_buffer, aml_size_needed);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Do the conversion */

	status =
	    acpi_rs_convert_resources_to_aml(linked_list_buffer,
					     aml_size_needed,
					     output_buffer->pointer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "OutputBuffer %p Length %X\n",
			  output_buffer->pointer, (u32) output_buffer->length));
	return_ACPI_STATUS(AE_OK);
}
