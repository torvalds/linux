/*******************************************************************************
 *
 * Module Name: nsnames - Name manipulation and search
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
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsnames")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_build_external_path
 *
 * PARAMETERS:  node            - NS node whose pathname is needed
 *              size            - Size of the pathname
 *              *name_buffer    - Where to return the pathname
 *
 * RETURN:      Status
 *              Places the pathname into the name_buffer, in external format
 *              (name segments separated by path separators)
 *
 * DESCRIPTION: Generate a full pathaname
 *
 ******************************************************************************/
acpi_status
acpi_ns_build_external_path(struct acpi_namespace_node *node,
			    acpi_size size, char *name_buffer)
{
	acpi_size index;
	struct acpi_namespace_node *parent_node;

	ACPI_FUNCTION_ENTRY();

	/* Special case for root */

	index = size - 1;
	if (index < ACPI_NAME_SIZE) {
		name_buffer[0] = AML_ROOT_PREFIX;
		name_buffer[1] = 0;
		return (AE_OK);
	}

	/* Store terminator byte, then build name backwards */

	parent_node = node;
	name_buffer[index] = 0;

	while ((index > ACPI_NAME_SIZE) && (parent_node != acpi_gbl_root_node)) {
		index -= ACPI_NAME_SIZE;

		/* Put the name into the buffer */

		ACPI_MOVE_32_TO_32((name_buffer + index), &parent_node->name);
		parent_node = parent_node->parent;

		/* Prefix name with the path separator */

		index--;
		name_buffer[index] = ACPI_PATH_SEPARATOR;
	}

	/* Overwrite final separator with the root prefix character */

	name_buffer[index] = AML_ROOT_PREFIX;

	if (index != 0) {
		ACPI_ERROR((AE_INFO,
			    "Could not construct external pathname; index=%u, size=%u, Path=%s",
			    (u32) index, (u32) size, &name_buffer[size]));

		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_external_pathname
 *
 * PARAMETERS:  node            - Namespace node whose pathname is needed
 *
 * RETURN:      Pointer to storage containing the fully qualified name of
 *              the node, In external format (name segments separated by path
 *              separators.)
 *
 * DESCRIPTION: Used for debug printing in acpi_ns_search_table().
 *
 ******************************************************************************/

char *acpi_ns_get_external_pathname(struct acpi_namespace_node *node)
{
	acpi_status status;
	char *name_buffer;
	acpi_size size;

	ACPI_FUNCTION_TRACE_PTR(ns_get_external_pathname, node);

	/* Calculate required buffer size based on depth below root */

	size = acpi_ns_get_pathname_length(node);
	if (!size) {
		return_PTR(NULL);
	}

	/* Allocate a buffer to be returned to caller */

	name_buffer = ACPI_ALLOCATE_ZEROED(size);
	if (!name_buffer) {
		ACPI_ERROR((AE_INFO, "Could not allocate %u bytes", (u32)size));
		return_PTR(NULL);
	}

	/* Build the path in the allocated buffer */

	status = acpi_ns_build_external_path(node, size, name_buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(name_buffer);
		return_PTR(NULL);
	}

	return_PTR(name_buffer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_pathname_length
 *
 * PARAMETERS:  node        - Namespace node
 *
 * RETURN:      Length of path, including prefix
 *
 * DESCRIPTION: Get the length of the pathname string for this node
 *
 ******************************************************************************/

acpi_size acpi_ns_get_pathname_length(struct acpi_namespace_node *node)
{
	acpi_size size;
	struct acpi_namespace_node *next_node;

	ACPI_FUNCTION_ENTRY();

	/*
	 * Compute length of pathname as 5 * number of name segments.
	 * Go back up the parent tree to the root
	 */
	size = 0;
	next_node = node;

	while (next_node && (next_node != acpi_gbl_root_node)) {
		if (ACPI_GET_DESCRIPTOR_TYPE(next_node) != ACPI_DESC_TYPE_NAMED) {
			ACPI_ERROR((AE_INFO,
				    "Invalid Namespace Node (%p) while traversing namespace",
				    next_node));
			return 0;
		}
		size += ACPI_PATH_SEGMENT_LENGTH;
		next_node = next_node->parent;
	}

	if (!size) {
		size = 1;	/* Root node case */
	}

	return (size + 1);	/* +1 for null string terminator */
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_handle_to_pathname
 *
 * PARAMETERS:  target_handle           - Handle of named object whose name is
 *                                        to be found
 *              buffer                  - Where the pathname is returned
 *
 * RETURN:      Status, Buffer is filled with pathname if status is AE_OK
 *
 * DESCRIPTION: Build and return a full namespace pathname
 *
 ******************************************************************************/

acpi_status
acpi_ns_handle_to_pathname(acpi_handle target_handle,
			   struct acpi_buffer * buffer)
{
	acpi_status status;
	struct acpi_namespace_node *node;
	acpi_size required_size;

	ACPI_FUNCTION_TRACE_PTR(ns_handle_to_pathname, target_handle);

	node = acpi_ns_validate_handle(target_handle);
	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Determine size required for the caller buffer */

	required_size = acpi_ns_get_pathname_length(node);
	if (!required_size) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(buffer, required_size);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Build the path in the caller buffer */

	status =
	    acpi_ns_build_external_path(node, required_size, buffer->pointer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%s [%X]\n",
			  (char *)buffer->pointer, (u32) required_size));
	return_ACPI_STATUS(AE_OK);
}
