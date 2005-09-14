/*******************************************************************************
 *
 * Module Name: rsxface - Public interfaces to the resource manager
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

#include <linux/module.h>

#include <acpi/acpi.h>
#include <acpi/acresrc.h>

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsxface")

/* Local macros for 16,32-bit to 64-bit conversion */
#define ACPI_COPY_FIELD(out, in, field)  ((out)->field = (in)->field)
#define ACPI_COPY_ADDRESS(out, in)                      \
	ACPI_COPY_FIELD(out, in, resource_type);             \
	ACPI_COPY_FIELD(out, in, producer_consumer);         \
	ACPI_COPY_FIELD(out, in, decode);                    \
	ACPI_COPY_FIELD(out, in, min_address_fixed);         \
	ACPI_COPY_FIELD(out, in, max_address_fixed);         \
	ACPI_COPY_FIELD(out, in, attribute);                 \
	ACPI_COPY_FIELD(out, in, granularity);               \
	ACPI_COPY_FIELD(out, in, min_address_range);         \
	ACPI_COPY_FIELD(out, in, max_address_range);         \
	ACPI_COPY_FIELD(out, in, address_translation_offset); \
	ACPI_COPY_FIELD(out, in, address_length);            \
	ACPI_COPY_FIELD(out, in, resource_source);
/*******************************************************************************
 *
 * FUNCTION:    acpi_get_irq_routing_table
 *
 * PARAMETERS:  device_handle   - a handle to the Bus device we are querying
 *              ret_buffer      - a pointer to a buffer to receive the
 *                                current resources for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the IRQ routing table for a
 *              specific bus.  The caller must first acquire a handle for the
 *              desired bus.  The routine table is placed in the buffer pointed
 *              to by the ret_buffer variable parameter.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of ret_buffer is undefined.
 *
 *              This function attempts to execute the _PRT method contained in
 *              the object indicated by the passed device_handle.
 *
 ******************************************************************************/
acpi_status
acpi_get_irq_routing_table(acpi_handle device_handle,
			   struct acpi_buffer *ret_buffer)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_get_irq_routing_table ");

	/*
	 * Must have a valid handle and buffer, So we have to have a handle
	 * and a return buffer structure, and if there is a non-zero buffer length
	 * we also need a valid pointer in the buffer. If it's a zero buffer length,
	 * we'll be returning the needed buffer size, so keep going.
	 */
	if (!device_handle) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer(ret_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_prt_method_data(device_handle, ret_buffer);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_current_resources
 *
 * PARAMETERS:  device_handle   - a handle to the device object for the
 *                                device we are querying
 *              ret_buffer      - a pointer to a buffer to receive the
 *                                current resources for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the current resources for a
 *              specific device.  The caller must first acquire a handle for
 *              the desired device.  The resource data is placed in the buffer
 *              pointed to by the ret_buffer variable parameter.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of ret_buffer is undefined.
 *
 *              This function attempts to execute the _CRS method contained in
 *              the object indicated by the passed device_handle.
 *
 ******************************************************************************/

acpi_status
acpi_get_current_resources(acpi_handle device_handle,
			   struct acpi_buffer *ret_buffer)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_get_current_resources");

	/*
	 * Must have a valid handle and buffer, So we have to have a handle
	 * and a return buffer structure, and if there is a non-zero buffer length
	 * we also need a valid pointer in the buffer. If it's a zero buffer length,
	 * we'll be returning the needed buffer size, so keep going.
	 */
	if (!device_handle) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer(ret_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_crs_method_data(device_handle, ret_buffer);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_get_current_resources);

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_possible_resources
 *
 * PARAMETERS:  device_handle   - a handle to the device object for the
 *                                device we are querying
 *              ret_buffer      - a pointer to a buffer to receive the
 *                                resources for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get a list of the possible resources
 *              for a specific device.  The caller must first acquire a handle
 *              for the desired device.  The resource data is placed in the
 *              buffer pointed to by the ret_buffer variable.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of ret_buffer is undefined.
 *
 ******************************************************************************/

#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_get_possible_resources(acpi_handle device_handle,
			    struct acpi_buffer *ret_buffer)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_get_possible_resources");

	/*
	 * Must have a valid handle and buffer, So we have to have a handle
	 * and a return buffer structure, and if there is a non-zero buffer length
	 * we also need a valid pointer in the buffer. If it's a zero buffer length,
	 * we'll be returning the needed buffer size, so keep going.
	 */
	if (!device_handle) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer(ret_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_prs_method_data(device_handle, ret_buffer);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_get_possible_resources);
#endif				/*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_walk_resources
 *
 * PARAMETERS:  device_handle   - a handle to the device object for the
 *                                device we are querying
 *              Path            - method name of the resources we want
 *                                (METHOD_NAME__CRS or METHOD_NAME__PRS)
 *              user_function   - called for each resource
 *              Context         - passed to user_function
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieves the current or possible resource list for the
 *              specified device.  The user_function is called once for
 *              each resource in the list.
 *
 ******************************************************************************/

acpi_status
acpi_walk_resources(acpi_handle device_handle,
		    char *path,
		    ACPI_WALK_RESOURCE_CALLBACK user_function, void *context)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_resource *resource;
	struct acpi_resource *buffer_end;

	ACPI_FUNCTION_TRACE("acpi_walk_resources");

	if (!device_handle ||
	    (ACPI_STRNCMP(path, METHOD_NAME__CRS, sizeof(METHOD_NAME__CRS)) &&
	     ACPI_STRNCMP(path, METHOD_NAME__PRS, sizeof(METHOD_NAME__PRS)))) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_rs_get_method_data(device_handle, path, &buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Setup pointers */

	resource = (struct acpi_resource *)buffer.pointer;
	buffer_end = ACPI_CAST_PTR(struct acpi_resource,
				   ((u8 *) buffer.pointer + buffer.length));

	/* Walk the resource list */

	for (;;) {
		if (!resource || resource->id == ACPI_RSTYPE_END_TAG) {
			break;
		}

		status = user_function(resource, context);

		switch (status) {
		case AE_OK:
		case AE_CTRL_DEPTH:

			/* Just keep going */

			status = AE_OK;
			break;

		case AE_CTRL_TERMINATE:

			/* Exit now, with OK stats */

			status = AE_OK;
			goto cleanup;

		default:

			/* All others are valid exceptions */

			goto cleanup;
		}

		/* Get the next resource descriptor */

		resource = ACPI_NEXT_RESOURCE(resource);

		/* Check for end-of-buffer */

		if (resource >= buffer_end) {
			goto cleanup;
		}
	}

      cleanup:

	acpi_os_free(buffer.pointer);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_walk_resources);

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_current_resources
 *
 * PARAMETERS:  device_handle   - a handle to the device object for the
 *                                device we are changing the resources of
 *              in_buffer       - a pointer to a buffer containing the
 *                                resources to be set for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to set the current resources for a
 *              specific device.  The caller must first acquire a handle for
 *              the desired device.  The resource data is passed to the routine
 *              the buffer pointed to by the in_buffer variable.
 *
 ******************************************************************************/

acpi_status
acpi_set_current_resources(acpi_handle device_handle,
			   struct acpi_buffer *in_buffer)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_set_current_resources");

	/* Must have a valid handle and buffer */

	if ((!device_handle) ||
	    (!in_buffer) || (!in_buffer->pointer) || (!in_buffer->length)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_rs_set_srs_method_data(device_handle, in_buffer);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_set_current_resources);

/******************************************************************************
 *
 * FUNCTION:    acpi_resource_to_address64
 *
 * PARAMETERS:  resource                - Pointer to a resource
 *              out                     - Pointer to the users's return
 *                                        buffer (a struct
 *                                        struct acpi_resource_address64)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: If the resource is an address16, address32, or address64,
 *              copy it to the address64 return buffer.  This saves the
 *              caller from having to duplicate code for different-sized
 *              addresses.
 *
 ******************************************************************************/

acpi_status
acpi_resource_to_address64(struct acpi_resource *resource,
			   struct acpi_resource_address64 *out)
{
	struct acpi_resource_address16 *address16;
	struct acpi_resource_address32 *address32;

	switch (resource->id) {
	case ACPI_RSTYPE_ADDRESS16:

		address16 = (struct acpi_resource_address16 *)&resource->data;
		ACPI_COPY_ADDRESS(out, address16);
		break;

	case ACPI_RSTYPE_ADDRESS32:

		address32 = (struct acpi_resource_address32 *)&resource->data;
		ACPI_COPY_ADDRESS(out, address32);
		break;

	case ACPI_RSTYPE_ADDRESS64:

		/* Simple copy for 64 bit source */

		ACPI_MEMCPY(out, &resource->data,
			    sizeof(struct acpi_resource_address64));
		break;

	default:
		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}

EXPORT_SYMBOL(acpi_resource_to_address64);
