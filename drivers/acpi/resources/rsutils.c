/*******************************************************************************
 *
 * Module Name: rsutils - Utilities for the resource manager
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
#include <acpi/acnamesp.h>
#include <acpi/acresrc.h>

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsutils")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_move_data
 *
 * PARAMETERS:  Destination         - Pointer to the destination descriptor
 *              Source              - Pointer to the source descriptor
 *              item_count          - How many items to move
 *              move_type           - Byte width
 *
 * RETURN:      None
 *
 * DESCRIPTION: Move multiple data items from one descriptor to another. Handles
 *              alignment issues and endian issues if necessary, as configured
 *              via the ACPI_MOVE_* macros. (This is why a memcpy is not used)
 *
 ******************************************************************************/
void
acpi_rs_move_data(void *destination, void *source, u16 item_count, u8 move_type)
{
	acpi_native_uint i;

	/* One move per item */

	for (i = 0; i < item_count; i++) {
		switch (move_type) {
		case ACPI_MOVE_TYPE_16_TO_32:
			ACPI_MOVE_16_TO_32(&((u32 *) destination)[i],
					   &((u16 *) source)[i]);
			break;

		case ACPI_MOVE_TYPE_32_TO_16:
			ACPI_MOVE_32_TO_16(&((u16 *) destination)[i],
					   &((u32 *) source)[i]);
			break;

		case ACPI_MOVE_TYPE_32_TO_32:
			ACPI_MOVE_32_TO_32(&((u32 *) destination)[i],
					   &((u32 *) source)[i]);
			break;

		case ACPI_MOVE_TYPE_64_TO_64:
			ACPI_MOVE_64_TO_64(&((u64 *) destination)[i],
					   &((u64 *) source)[i]);
			break;

		default:
			return;
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_info
 *
 * PARAMETERS:  resource_type       - Byte 0 of a resource descriptor
 *
 * RETURN:      Pointer to the resource conversion handler
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

struct acpi_resource_info *acpi_rs_get_resource_info(u8 resource_type)
{
	struct acpi_resource_info *size_info;

	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (resource_type & ACPI_RESOURCE_NAME_LARGE) {
		/* Large Resource Type -- bits 6:0 contain the name */

		if (resource_type > ACPI_RESOURCE_NAME_LARGE_MAX) {
			return (NULL);
		}

		size_info = &acpi_gbl_lg_resource_info[(resource_type &
							ACPI_RESOURCE_NAME_LARGE_MASK)];
	} else {
		/* Small Resource Type -- bits 6:3 contain the name */

		size_info = &acpi_gbl_sm_resource_info[((resource_type &
							 ACPI_RESOURCE_NAME_SMALL_MASK)
							>> 3)];
	}

	/* Zero entry indicates an invalid resource type */

	if (!size_info->minimum_internal_struct_length) {
		return (NULL);
	}

	return (size_info);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_length
 *
 * PARAMETERS:  Aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Byte Length
 *
 * DESCRIPTION: Get the "Resource Length" of a raw AML descriptor. By
 *              definition, this does not include the size of the descriptor
 *              header or the length field itself.
 *
 ******************************************************************************/

u16 acpi_rs_get_resource_length(union aml_resource * aml)
{
	u16 resource_length;

	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (aml->large_header.descriptor_type & ACPI_RESOURCE_NAME_LARGE) {
		/* Large Resource type -- bytes 1-2 contain the 16-bit length */

		ACPI_MOVE_16_TO_16(&resource_length,
				   &aml->large_header.resource_length);

	} else {
		/* Small Resource type -- bits 2:0 of byte 0 contain the length */

		resource_length = (u16) (aml->small_header.descriptor_type &
					 ACPI_RESOURCE_NAME_SMALL_LENGTH_MASK);
	}

	return (resource_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_descriptor_length
 *
 * PARAMETERS:  Aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Byte length
 *
 * DESCRIPTION: Get the total byte length of a raw AML descriptor, including the
 *              length of the descriptor header and the length field itself.
 *              Used to walk descriptor lists.
 *
 ******************************************************************************/

u32 acpi_rs_get_descriptor_length(union aml_resource * aml)
{
	u32 descriptor_length;

	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (aml->large_header.descriptor_type & ACPI_RESOURCE_NAME_LARGE) {
		/* Large Resource type -- bytes 1-2 contain the 16-bit length */

		ACPI_MOVE_16_TO_32(&descriptor_length,
				   &aml->large_header.resource_length);
		descriptor_length += sizeof(struct aml_resource_large_header);

	} else {
		/* Small Resource type -- bits 2:0 of byte 0 contain the length */

		descriptor_length = (u32) (aml->small_header.descriptor_type &
					   ACPI_RESOURCE_NAME_SMALL_LENGTH_MASK);
		descriptor_length += sizeof(struct aml_resource_small_header);
	}

	return (descriptor_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_resource_header
 *
 * PARAMETERS:  descriptor_type     - Byte to be inserted as the type
 *              total_length        - Length of the AML descriptor, including
 *                                    the header and length fields.
 *              Aml                 - Pointer to the raw AML descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the descriptor_type and resource_length fields of an AML
 *              resource descriptor, both Large and Small descriptors are
 *              supported automatically
 *
 ******************************************************************************/

void
acpi_rs_set_resource_header(u8 descriptor_type,
			    acpi_size total_length, union aml_resource *aml)
{
	u16 resource_length;

	ACPI_FUNCTION_ENTRY();

	/* Set the descriptor type */

	aml->small_header.descriptor_type = descriptor_type;

	/* Determine if this is a small or large resource */

	if (aml->small_header.descriptor_type & ACPI_RESOURCE_NAME_LARGE) {
		/* Large Resource type -- bytes 1-2 contain the 16-bit length */

		resource_length =
		    (u16) (total_length -
			   sizeof(struct aml_resource_large_header));

		/* Insert length into the Large descriptor length field */

		ACPI_MOVE_16_TO_16(&aml->large_header.resource_length,
				   &resource_length);
	} else {
		/* Small Resource type -- bits 2:0 of byte 0 contain the length */

		resource_length =
		    (u16) (total_length -
			   sizeof(struct aml_resource_small_header));

		/* Insert length into the descriptor type byte */

		aml->small_header.descriptor_type |= (u8) resource_length;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_type
 *
 * PARAMETERS:  resource_type       - Byte 0 of a resource descriptor
 *
 * RETURN:      The Resource Type with no extraneous bits (except the
 *              Large/Small descriptor bit -- this is left alone)
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

u8 acpi_rs_get_resource_type(u8 resource_type)
{
	ACPI_FUNCTION_ENTRY();

	/* Determine if this is a small or large resource */

	if (resource_type & ACPI_RESOURCE_NAME_LARGE) {
		/* Large Resource Type -- bits 6:0 contain the name */

		return (resource_type);
	} else {
		/* Small Resource Type -- bits 6:3 contain the name */

		return ((u8) (resource_type & ACPI_RESOURCE_NAME_SMALL_MASK));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_strcpy
 *
 * PARAMETERS:  Destination         - Pointer to the destination string
 *              Source              - Pointer to the source string
 *
 * RETURN:      String length, including NULL terminator
 *
 * DESCRIPTION: Local string copy that returns the string length, saving a
 *              strcpy followed by a strlen.
 *
 ******************************************************************************/

static u16 acpi_rs_strcpy(char *destination, char *source)
{
	u16 i;

	ACPI_FUNCTION_ENTRY();

	for (i = 0; source[i]; i++) {
		destination[i] = source[i];
	}

	destination[i] = 0;

	/* Return string length including the NULL terminator */

	return ((u16) (i + 1));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_resource_source
 *
 * PARAMETERS:  resource_length     - Length field of the descriptor
 *              minimum_length      - Minimum length of the descriptor (minus
 *                                    any optional fields)
 *              resource_source     - Where the resource_source is returned
 *              Aml                 - Pointer to the raw AML descriptor
 *              string_ptr          - (optional) where to store the actual
 *                                    resource_source string
 *
 * RETURN:      Length of the string plus NULL terminator, rounded up to 32 bit
 *
 * DESCRIPTION: Copy the optional resource_source data from a raw AML descriptor
 *              to an internal resource descriptor
 *
 ******************************************************************************/

u16
acpi_rs_get_resource_source(u16 resource_length,
			    acpi_size minimum_length,
			    struct acpi_resource_source * resource_source,
			    union aml_resource * aml, char *string_ptr)
{
	acpi_size total_length;
	u8 *aml_resource_source;

	ACPI_FUNCTION_ENTRY();

	total_length =
	    resource_length + sizeof(struct aml_resource_large_header);
	aml_resource_source = ((u8 *) aml) + minimum_length;

	/*
	 * resource_source is present if the length of the descriptor is longer than
	 * the minimum length.
	 *
	 * Note: Some resource descriptors will have an additional null, so
	 * we add 1 to the minimum length.
	 */
	if (total_length > (minimum_length + 1)) {
		/* Get the resource_source_index */

		resource_source->index = aml_resource_source[0];

		resource_source->string_ptr = string_ptr;
		if (!string_ptr) {
			/*
			 * String destination pointer is not specified; Set the String
			 * pointer to the end of the current resource_source structure.
			 */
			resource_source->string_ptr = (char *)
			    ((u8 *) resource_source) +
			    sizeof(struct acpi_resource_source);
		}

		/* Copy the resource_source string to the destination */

		resource_source->string_length =
		    acpi_rs_strcpy(resource_source->string_ptr,
				   (char *)&aml_resource_source[1]);

		/*
		 * In order for the struct_size to fall on a 32-bit boundary,
		 * calculate the length of the string and expand the
		 * struct_size to the next 32-bit boundary.
		 */
		return ((u16)
			ACPI_ROUND_UP_to_32_bITS(resource_source->
						 string_length));
	} else {
		/* resource_source is not present */

		resource_source->index = 0;
		resource_source->string_length = 0;
		resource_source->string_ptr = NULL;
		return (0);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_resource_source
 *
 * PARAMETERS:  Aml                 - Pointer to the raw AML descriptor
 *              minimum_length      - Minimum length of the descriptor (minus
 *                                    any optional fields)
 *              resource_source     - Internal resource_source

 *
 * RETURN:      Total length of the AML descriptor
 *
 * DESCRIPTION: Convert an optoinal resource_source from internal format to a
 *              raw AML resource descriptor
 *
 ******************************************************************************/

acpi_size
acpi_rs_set_resource_source(union aml_resource * aml,
			    acpi_size minimum_length,
			    struct acpi_resource_source * resource_source)
{
	u8 *aml_resource_source;
	acpi_size descriptor_length;

	ACPI_FUNCTION_ENTRY();

	descriptor_length = minimum_length;

	/* Non-zero string length indicates presence of a resource_source */

	if (resource_source->string_length) {
		/* Point to the end of the AML descriptor */

		aml_resource_source = ((u8 *) aml) + minimum_length;

		/* Copy the resource_source_index */

		aml_resource_source[0] = (u8) resource_source->index;

		/* Copy the resource_source string */

		ACPI_STRCPY((char *)&aml_resource_source[1],
			    resource_source->string_ptr);

		/*
		 * Add the length of the string (+ 1 for null terminator) to the
		 * final descriptor length
		 */
		descriptor_length +=
		    ((acpi_size) resource_source->string_length + 1);
	}

	/* Return the new total length of the AML descriptor */

	return (descriptor_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_prt_method_data
 *
 * PARAMETERS:  Handle          - a handle to the containing object
 *              ret_buffer      - a pointer to a buffer structure for the
 *                                  results
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the _PRT value of an object
 *              contained in an object specified by the handle passed in
 *
 *              If the function fails an appropriate status will be returned
 *              and the contents of the callers buffer is undefined.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_prt_method_data(acpi_handle handle, struct acpi_buffer * ret_buffer)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("rs_get_prt_method_data");

	/* Parameters guaranteed valid by caller */

	/* Execute the method, no parameters */

	status = acpi_ut_evaluate_object(handle, METHOD_NAME__PRT,
					 ACPI_BTYPE_PACKAGE, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Create a resource linked list from the byte stream buffer that comes
	 * back from the _CRS method execution.
	 */
	status = acpi_rs_create_pci_routing_table(obj_desc, ret_buffer);

	/* On exit, we must delete the object returned by evaluate_object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_crs_method_data
 *
 * PARAMETERS:  Handle          - a handle to the containing object
 *              ret_buffer      - a pointer to a buffer structure for the
 *                                  results
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the _CRS value of an object
 *              contained in an object specified by the handle passed in
 *
 *              If the function fails an appropriate status will be returned
 *              and the contents of the callers buffer is undefined.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_crs_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("rs_get_crs_method_data");

	/* Parameters guaranteed valid by caller */

	/* Execute the method, no parameters */

	status = acpi_ut_evaluate_object(handle, METHOD_NAME__CRS,
					 ACPI_BTYPE_BUFFER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Make the call to create a resource linked list from the
	 * byte stream buffer that comes back from the _CRS method
	 * execution.
	 */
	status = acpi_rs_create_resource_list(obj_desc, ret_buffer);

	/* on exit, we must delete the object returned by evaluate_object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_prs_method_data
 *
 * PARAMETERS:  Handle          - a handle to the containing object
 *              ret_buffer      - a pointer to a buffer structure for the
 *                                  results
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the _PRS value of an object
 *              contained in an object specified by the handle passed in
 *
 *              If the function fails an appropriate status will be returned
 *              and the contents of the callers buffer is undefined.
 *
 ******************************************************************************/

#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_rs_get_prs_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("rs_get_prs_method_data");

	/* Parameters guaranteed valid by caller */

	/* Execute the method, no parameters */

	status = acpi_ut_evaluate_object(handle, METHOD_NAME__PRS,
					 ACPI_BTYPE_BUFFER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Make the call to create a resource linked list from the
	 * byte stream buffer that comes back from the _CRS method
	 * execution.
	 */
	status = acpi_rs_create_resource_list(obj_desc, ret_buffer);

	/* on exit, we must delete the object returned by evaluate_object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}
#endif				/*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_method_data
 *
 * PARAMETERS:  Handle          - a handle to the containing object
 *              Path            - Path to method, relative to Handle
 *              ret_buffer      - a pointer to a buffer structure for the
 *                                  results
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the _CRS or _PRS value of an
 *              object contained in an object specified by the handle passed in
 *
 *              If the function fails an appropriate status will be returned
 *              and the contents of the callers buffer is undefined.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_method_data(acpi_handle handle,
			char *path, struct acpi_buffer *ret_buffer)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("rs_get_method_data");

	/* Parameters guaranteed valid by caller */

	/* Execute the method, no parameters */

	status =
	    acpi_ut_evaluate_object(handle, path, ACPI_BTYPE_BUFFER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Make the call to create a resource linked list from the
	 * byte stream buffer that comes back from the method
	 * execution.
	 */
	status = acpi_rs_create_resource_list(obj_desc, ret_buffer);

	/* On exit, we must delete the object returned by evaluate_object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_srs_method_data
 *
 * PARAMETERS:  Handle          - a handle to the containing object
 *              in_buffer       - a pointer to a buffer structure of the
 *                                  parameter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to set the _SRS of an object contained
 *              in an object specified by the handle passed in
 *
 *              If the function fails an appropriate status will be returned
 *              and the contents of the callers buffer is undefined.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_srs_method_data(acpi_handle handle, struct acpi_buffer *in_buffer)
{
	struct acpi_parameter_info info;
	union acpi_operand_object *params[2];
	acpi_status status;
	struct acpi_buffer buffer;

	ACPI_FUNCTION_TRACE("rs_set_srs_method_data");

	/* Parameters guaranteed valid by caller */

	/*
	 * The in_buffer parameter will point to a linked list of
	 * resource parameters.  It needs to be formatted into a
	 * byte stream to be sent in as an input parameter to _SRS
	 *
	 * Convert the linked list into a byte stream
	 */
	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_rs_create_aml_resources(in_buffer->pointer, &buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Init the param object */

	params[0] = acpi_ut_create_internal_object(ACPI_TYPE_BUFFER);
	if (!params[0]) {
		acpi_os_free(buffer.pointer);
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Set up the parameter object */

	params[0]->buffer.length = (u32) buffer.length;
	params[0]->buffer.pointer = buffer.pointer;
	params[0]->common.flags = AOPOBJ_DATA_VALID;
	params[1] = NULL;

	info.node = handle;
	info.parameters = params;
	info.parameter_type = ACPI_PARAM_ARGS;

	/* Execute the method, no return value */

	status = acpi_ns_evaluate_relative(METHOD_NAME__SRS, &info);
	if (ACPI_SUCCESS(status)) {
		/* Delete any return object (especially if implicit_return is enabled) */

		if (info.return_object) {
			acpi_ut_remove_reference(info.return_object);
		}
	}

	/* Clean up and return the status from acpi_ns_evaluate_relative */

	acpi_ut_remove_reference(params[0]);
	return_ACPI_STATUS(status);
}
