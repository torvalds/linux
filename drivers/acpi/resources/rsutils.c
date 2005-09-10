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
acpi_rs_get_prt_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer)
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
	status = acpi_rs_create_byte_stream(in_buffer->pointer, &buffer);
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
