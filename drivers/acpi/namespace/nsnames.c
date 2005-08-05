/*******************************************************************************
 *
 * Module Name: nsnames - Name manipulation and search
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
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsnames")

/* Local prototypes */
static void
acpi_ns_build_external_path(struct acpi_namespace_node *node,
			    acpi_size size, char *name_buffer);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_build_external_path
 *
 * PARAMETERS:  Node            - NS node whose pathname is needed
 *              Size            - Size of the pathname
 *              *name_buffer    - Where to return the pathname
 *
 * RETURN:      Places the pathname into the name_buffer, in external format
 *              (name segments separated by path separators)
 *
 * DESCRIPTION: Generate a full pathaname
 *
 ******************************************************************************/

static void
acpi_ns_build_external_path(struct acpi_namespace_node *node,
			    acpi_size size, char *name_buffer)
{
	acpi_size index;
	struct acpi_namespace_node *parent_node;

	ACPI_FUNCTION_NAME("ns_build_external_path");

	/* Special case for root */

	index = size - 1;
	if (index < ACPI_NAME_SIZE) {
		name_buffer[0] = AML_ROOT_PREFIX;
		name_buffer[1] = 0;
		return;
	}

	/* Store terminator byte, then build name backwards */

	parent_node = node;
	name_buffer[index] = 0;

	while ((index > ACPI_NAME_SIZE) && (parent_node != acpi_gbl_root_node)) {
		index -= ACPI_NAME_SIZE;

		/* Put the name into the buffer */

		ACPI_MOVE_32_TO_32((name_buffer + index), &parent_node->name);
		parent_node = acpi_ns_get_parent_node(parent_node);

		/* Prefix name with the path separator */

		index--;
		name_buffer[index] = ACPI_PATH_SEPARATOR;
	}

	/* Overwrite final separator with the root prefix character */

	name_buffer[index] = AML_ROOT_PREFIX;

	if (index != 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Could not construct pathname; index=%X, size=%X, Path=%s\n",
				  (u32) index, (u32) size, &name_buffer[size]));
	}

	return;
}

#ifdef ACPI_DEBUG_OUTPUT
/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_external_pathname
 *
 * PARAMETERS:  Node            - Namespace node whose pathname is needed
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
	char *name_buffer;
	acpi_size size;

	ACPI_FUNCTION_TRACE_PTR("ns_get_external_pathname", node);

	/* Calculate required buffer size based on depth below root */

	size = acpi_ns_get_pathname_length(node);

	/* Allocate a buffer to be returned to caller */

	name_buffer = ACPI_MEM_CALLOCATE(size);
	if (!name_buffer) {
		ACPI_REPORT_ERROR(("ns_get_table_pathname: allocation failure\n"));
		return_PTR(NULL);
	}

	/* Build the path in the allocated buffer */

	acpi_ns_build_external_path(node, size, name_buffer);
	return_PTR(name_buffer);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_pathname_length
 *
 * PARAMETERS:  Node        - Namespace node
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
		size += ACPI_PATH_SEGMENT_LENGTH;
		next_node = acpi_ns_get_parent_node(next_node);
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
 *              Buffer                  - Where the pathname is returned
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

	ACPI_FUNCTION_TRACE_PTR("ns_handle_to_pathname", target_handle);

	node = acpi_ns_map_handle_to_node(target_handle);
	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Determine size required for the caller buffer */

	required_size = acpi_ns_get_pathname_length(node);

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(buffer, required_size);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Build the path in the caller buffer */

	acpi_ns_build_external_path(node, required_size, buffer->pointer);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%s [%X] \n",
			  (char *)buffer->pointer, (u32) required_size));
	return_ACPI_STATUS(AE_OK);
}
