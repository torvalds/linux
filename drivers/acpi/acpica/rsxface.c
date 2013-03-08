/*******************************************************************************
 *
 * Module Name: rsxface - Public interfaces to the resource manager
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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

#include <linux/export.h>
#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"
#include "acnamesp.h"

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
	ACPI_COPY_FIELD(out, in, info);                      \
	ACPI_COPY_FIELD(out, in, granularity);               \
	ACPI_COPY_FIELD(out, in, minimum);                   \
	ACPI_COPY_FIELD(out, in, maximum);                   \
	ACPI_COPY_FIELD(out, in, translation_offset);        \
	ACPI_COPY_FIELD(out, in, address_length);            \
	ACPI_COPY_FIELD(out, in, resource_source);
/* Local prototypes */
static acpi_status
acpi_rs_match_vendor_resource(struct acpi_resource *resource, void *context);

static acpi_status
acpi_rs_validate_parameters(acpi_handle device_handle,
			    struct acpi_buffer *buffer,
			    struct acpi_namespace_node **return_node);

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_validate_parameters
 *
 * PARAMETERS:  device_handle   - Handle to a device
 *              buffer          - Pointer to a data buffer
 *              return_node     - Pointer to where the device node is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common parameter validation for resource interfaces
 *
 ******************************************************************************/

static acpi_status
acpi_rs_validate_parameters(acpi_handle device_handle,
			    struct acpi_buffer *buffer,
			    struct acpi_namespace_node **return_node)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(rs_validate_parameters);

	/*
	 * Must have a valid handle to an ACPI device
	 */
	if (!device_handle) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	node = acpi_ns_validate_handle(device_handle);
	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (node->type != ACPI_TYPE_DEVICE) {
		return_ACPI_STATUS(AE_TYPE);
	}

	/*
	 * Validate the user buffer object
	 *
	 * if there is a non-zero buffer length we also need a valid pointer in
	 * the buffer. If it's a zero buffer length, we'll be returning the
	 * needed buffer size (later), so keep going.
	 */
	status = acpi_ut_validate_buffer(buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*return_node = node;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_irq_routing_table
 *
 * PARAMETERS:  device_handle   - Handle to the Bus device we are querying
 *              ret_buffer      - Pointer to a buffer to receive the
 *                                current resources for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the IRQ routing table for a
 *              specific bus. The caller must first acquire a handle for the
 *              desired bus. The routine table is placed in the buffer pointed
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
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_get_irq_routing_table);

	/* Validate parameters then dispatch to internal routine */

	status = acpi_rs_validate_parameters(device_handle, ret_buffer, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_prt_method_data(node, ret_buffer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_irq_routing_table)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_current_resources
 *
 * PARAMETERS:  device_handle   - Handle to the device object for the
 *                                device we are querying
 *              ret_buffer      - Pointer to a buffer to receive the
 *                                current resources for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the current resources for a
 *              specific device. The caller must first acquire a handle for
 *              the desired device. The resource data is placed in the buffer
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
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_get_current_resources);

	/* Validate parameters then dispatch to internal routine */

	status = acpi_rs_validate_parameters(device_handle, ret_buffer, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_crs_method_data(node, ret_buffer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_current_resources)
#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_get_possible_resources
 *
 * PARAMETERS:  device_handle   - Handle to the device object for the
 *                                device we are querying
 *              ret_buffer      - Pointer to a buffer to receive the
 *                                resources for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get a list of the possible resources
 *              for a specific device. The caller must first acquire a handle
 *              for the desired device. The resource data is placed in the
 *              buffer pointed to by the ret_buffer variable.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of ret_buffer is undefined.
 *
 ******************************************************************************/
acpi_status
acpi_get_possible_resources(acpi_handle device_handle,
			    struct acpi_buffer *ret_buffer)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_get_possible_resources);

	/* Validate parameters then dispatch to internal routine */

	status = acpi_rs_validate_parameters(device_handle, ret_buffer, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_prs_method_data(node, ret_buffer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_possible_resources)
#endif				/*  ACPI_FUTURE_USAGE  */
/*******************************************************************************
 *
 * FUNCTION:    acpi_set_current_resources
 *
 * PARAMETERS:  device_handle   - Handle to the device object for the
 *                                device we are setting resources
 *              in_buffer       - Pointer to a buffer containing the
 *                                resources to be set for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to set the current resources for a
 *              specific device. The caller must first acquire a handle for
 *              the desired device. The resource data is passed to the routine
 *              the buffer pointed to by the in_buffer variable.
 *
 ******************************************************************************/
acpi_status
acpi_set_current_resources(acpi_handle device_handle,
			   struct acpi_buffer *in_buffer)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_set_current_resources);

	/* Validate the buffer, don't allow zero length */

	if ((!in_buffer) || (!in_buffer->pointer) || (!in_buffer->length)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Validate parameters then dispatch to internal routine */

	status = acpi_rs_validate_parameters(device_handle, in_buffer, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_set_srs_method_data(node, in_buffer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_set_current_resources)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_event_resources
 *
 * PARAMETERS:  device_handle   - Handle to the device object for the
 *                                device we are getting resources
 *              in_buffer       - Pointer to a buffer containing the
 *                                resources to be set for the device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the event resources for a
 *              specific device. The caller must first acquire a handle for
 *              the desired device. The resource data is passed to the routine
 *              the buffer pointed to by the in_buffer variable. Uses the
 *              _AEI method.
 *
 ******************************************************************************/
acpi_status
acpi_get_event_resources(acpi_handle device_handle,
			 struct acpi_buffer *ret_buffer)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_get_event_resources);

	/* Validate parameters then dispatch to internal routine */

	status = acpi_rs_validate_parameters(device_handle, ret_buffer, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_rs_get_aei_method_data(node, ret_buffer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_event_resources)

/******************************************************************************
 *
 * FUNCTION:    acpi_resource_to_address64
 *
 * PARAMETERS:  resource        - Pointer to a resource
 *              out             - Pointer to the users's return buffer
 *                                (a struct acpi_resource_address64)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: If the resource is an address16, address32, or address64,
 *              copy it to the address64 return buffer. This saves the
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

	if (!resource || !out) {
		return (AE_BAD_PARAMETER);
	}

	/* Convert 16 or 32 address descriptor to 64 */

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_ADDRESS16:

		address16 =
		    ACPI_CAST_PTR(struct acpi_resource_address16,
				  &resource->data);
		ACPI_COPY_ADDRESS(out, address16);
		break;

	case ACPI_RESOURCE_TYPE_ADDRESS32:

		address32 =
		    ACPI_CAST_PTR(struct acpi_resource_address32,
				  &resource->data);
		ACPI_COPY_ADDRESS(out, address32);
		break;

	case ACPI_RESOURCE_TYPE_ADDRESS64:

		/* Simple copy for 64 bit source */

		ACPI_MEMCPY(out, &resource->data,
			    sizeof(struct acpi_resource_address64));
		break;

	default:
		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_resource_to_address64)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_vendor_resource
 *
 * PARAMETERS:  device_handle   - Handle for the parent device object
 *              name            - Method name for the parent resource
 *                                (METHOD_NAME__CRS or METHOD_NAME__PRS)
 *              uuid            - Pointer to the UUID to be matched.
 *                                includes both subtype and 16-byte UUID
 *              ret_buffer      - Where the vendor resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk a resource template for the specified device to find a
 *              vendor-defined resource that matches the supplied UUID and
 *              UUID subtype. Returns a struct acpi_resource of type Vendor.
 *
 ******************************************************************************/
acpi_status
acpi_get_vendor_resource(acpi_handle device_handle,
			 char *name,
			 struct acpi_vendor_uuid * uuid,
			 struct acpi_buffer * ret_buffer)
{
	struct acpi_vendor_walk_info info;
	acpi_status status;

	/* Other parameters are validated by acpi_walk_resources */

	if (!uuid || !ret_buffer) {
		return (AE_BAD_PARAMETER);
	}

	info.uuid = uuid;
	info.buffer = ret_buffer;
	info.status = AE_NOT_EXIST;

	/* Walk the _CRS or _PRS resource list for this device */

	status =
	    acpi_walk_resources(device_handle, name,
				acpi_rs_match_vendor_resource, &info);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	return (info.status);
}

ACPI_EXPORT_SYMBOL(acpi_get_vendor_resource)

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_match_vendor_resource
 *
 * PARAMETERS:  acpi_walk_resource_callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Match a vendor resource via the ACPI 3.0 UUID
 *
 ******************************************************************************/
static acpi_status
acpi_rs_match_vendor_resource(struct acpi_resource *resource, void *context)
{
	struct acpi_vendor_walk_info *info = context;
	struct acpi_resource_vendor_typed *vendor;
	struct acpi_buffer *buffer;
	acpi_status status;

	/* Ignore all descriptors except Vendor */

	if (resource->type != ACPI_RESOURCE_TYPE_VENDOR) {
		return (AE_OK);
	}

	vendor = &resource->data.vendor_typed;

	/*
	 * For a valid match, these conditions must hold:
	 *
	 * 1) Length of descriptor data must be at least as long as a UUID struct
	 * 2) The UUID subtypes must match
	 * 3) The UUID data must match
	 */
	if ((vendor->byte_length < (ACPI_UUID_LENGTH + 1)) ||
	    (vendor->uuid_subtype != info->uuid->subtype) ||
	    (ACPI_MEMCMP(vendor->uuid, info->uuid->data, ACPI_UUID_LENGTH))) {
		return (AE_OK);
	}

	/* Validate/Allocate/Clear caller buffer */

	buffer = info->buffer;
	status = acpi_ut_initialize_buffer(buffer, resource->length);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Found the correct resource, copy and return it */

	ACPI_MEMCPY(buffer->pointer, resource, resource->length);
	buffer->length = resource->length;

	/* Found the desired descriptor, terminate resource walk */

	info->status = AE_OK;
	return (AE_CTRL_TERMINATE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_walk_resource_buffer
 *
 * PARAMETERS:  buffer          - Formatted buffer returned by one of the
 *                                various Get*Resource functions
 *              user_function   - Called for each resource
 *              context         - Passed to user_function
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walks the input resource template. The user_function is called
 *              once for each resource in the list.
 *
 ******************************************************************************/

acpi_status
acpi_walk_resource_buffer(struct acpi_buffer * buffer,
			  acpi_walk_resource_callback user_function,
			  void *context)
{
	acpi_status status = AE_OK;
	struct acpi_resource *resource;
	struct acpi_resource *resource_end;

	ACPI_FUNCTION_TRACE(acpi_walk_resource_buffer);

	/* Parameter validation */

	if (!buffer || !buffer->pointer || !user_function) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Buffer contains the resource list and length */

	resource = ACPI_CAST_PTR(struct acpi_resource, buffer->pointer);
	resource_end =
	    ACPI_ADD_PTR(struct acpi_resource, buffer->pointer, buffer->length);

	/* Walk the resource list until the end_tag is found (or buffer end) */

	while (resource < resource_end) {

		/* Sanity check the resource type */

		if (resource->type > ACPI_RESOURCE_TYPE_MAX) {
			status = AE_AML_INVALID_RESOURCE_TYPE;
			break;
		}

		/* Sanity check the length. It must not be zero, or we loop forever */

		if (!resource->length) {
			return_ACPI_STATUS(AE_AML_BAD_RESOURCE_LENGTH);
		}

		/* Invoke the user function, abort on any error returned */

		status = user_function(resource, context);
		if (ACPI_FAILURE(status)) {
			if (status == AE_CTRL_TERMINATE) {

				/* This is an OK termination by the user function */

				status = AE_OK;
			}
			break;
		}

		/* end_tag indicates end-of-list */

		if (resource->type == ACPI_RESOURCE_TYPE_END_TAG) {
			break;
		}

		/* Get the next resource descriptor */

		resource = ACPI_NEXT_RESOURCE(resource);
	}

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_walk_resource_buffer)

/*******************************************************************************
 *
 * FUNCTION:    acpi_walk_resources
 *
 * PARAMETERS:  device_handle   - Handle to the device object for the
 *                                device we are querying
 *              name            - Method name of the resources we want.
 *                                (METHOD_NAME__CRS, METHOD_NAME__PRS, or
 *                                METHOD_NAME__AEI)
 *              user_function   - Called for each resource
 *              context         - Passed to user_function
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieves the current or possible resource list for the
 *              specified device. The user_function is called once for
 *              each resource in the list.
 *
 ******************************************************************************/
acpi_status
acpi_walk_resources(acpi_handle device_handle,
		    char *name,
		    acpi_walk_resource_callback user_function, void *context)
{
	acpi_status status;
	struct acpi_buffer buffer;

	ACPI_FUNCTION_TRACE(acpi_walk_resources);

	/* Parameter validation */

	if (!device_handle || !user_function || !name ||
	    (!ACPI_COMPARE_NAME(name, METHOD_NAME__CRS) &&
	     !ACPI_COMPARE_NAME(name, METHOD_NAME__PRS) &&
	     !ACPI_COMPARE_NAME(name, METHOD_NAME__AEI))) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the _CRS/_PRS/_AEI resource list */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_rs_get_method_data(device_handle, name, &buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Walk the resource list and cleanup */

	status = acpi_walk_resource_buffer(&buffer, user_function, context);
	ACPI_FREE(buffer.pointer);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_walk_resources)
