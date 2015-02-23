/******************************************************************************
 *
 * Module Name: nsutils - Utilities for accessing ACPI namespace, accessing
 *                        parents and siblings and Scope manipulation
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "acnamesp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsutils")

/* Local prototypes */
#ifdef ACPI_OBSOLETE_FUNCTIONS
acpi_name acpi_ns_find_parent_name(struct acpi_namespace_node *node_to_search);
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_print_node_pathname
 *
 * PARAMETERS:  node            - Object
 *              message         - Prefix message
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

void
acpi_ns_print_node_pathname(struct acpi_namespace_node *node,
			    const char *message)
{
	struct acpi_buffer buffer;
	acpi_status status;

	if (!node) {
		acpi_os_printf("[NULL NAME]");
		return;
	}

	/* Convert handle to full pathname and print it (with supplied message) */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;

	status = acpi_ns_handle_to_pathname(node, &buffer);
	if (ACPI_SUCCESS(status)) {
		if (message) {
			acpi_os_printf("%s ", message);
		}

		acpi_os_printf("[%s] (Node %p)", (char *)buffer.pointer, node);
		ACPI_FREE(buffer.pointer);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_type
 *
 * PARAMETERS:  node        - Parent Node to be examined
 *
 * RETURN:      Type field from Node whose handle is passed
 *
 * DESCRIPTION: Return the type of a Namespace node
 *
 ******************************************************************************/

acpi_object_type acpi_ns_get_type(struct acpi_namespace_node * node)
{
	ACPI_FUNCTION_TRACE(ns_get_type);

	if (!node) {
		ACPI_WARNING((AE_INFO, "Null Node parameter"));
		return_UINT8(ACPI_TYPE_ANY);
	}

	return_UINT8(node->type);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_local
 *
 * PARAMETERS:  type        - A namespace object type
 *
 * RETURN:      LOCAL if names must be found locally in objects of the
 *              passed type, 0 if enclosing scopes should be searched
 *
 * DESCRIPTION: Returns scope rule for the given object type.
 *
 ******************************************************************************/

u32 acpi_ns_local(acpi_object_type type)
{
	ACPI_FUNCTION_TRACE(ns_local);

	if (!acpi_ut_valid_object_type(type)) {

		/* Type code out of range  */

		ACPI_WARNING((AE_INFO, "Invalid Object Type 0x%X", type));
		return_UINT32(ACPI_NS_NORMAL);
	}

	return_UINT32(acpi_gbl_ns_properties[type] & ACPI_NS_LOCAL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_internal_name_length
 *
 * PARAMETERS:  info            - Info struct initialized with the
 *                                external name pointer.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Calculate the length of the internal (AML) namestring
 *              corresponding to the external (ASL) namestring.
 *
 ******************************************************************************/

void acpi_ns_get_internal_name_length(struct acpi_namestring_info *info)
{
	const char *next_external_char;
	u32 i;

	ACPI_FUNCTION_ENTRY();

	next_external_char = info->external_name;
	info->num_carats = 0;
	info->num_segments = 0;
	info->fully_qualified = FALSE;

	/*
	 * For the internal name, the required length is 4 bytes per segment, plus
	 * 1 each for root_prefix, multi_name_prefix_op, segment count, trailing null
	 * (which is not really needed, but no there's harm in putting it there)
	 *
	 * strlen() + 1 covers the first name_seg, which has no path separator
	 */
	if (ACPI_IS_ROOT_PREFIX(*next_external_char)) {
		info->fully_qualified = TRUE;
		next_external_char++;

		/* Skip redundant root_prefix, like \\_SB.PCI0.SBRG.EC0 */

		while (ACPI_IS_ROOT_PREFIX(*next_external_char)) {
			next_external_char++;
		}
	} else {
		/* Handle Carat prefixes */

		while (ACPI_IS_PARENT_PREFIX(*next_external_char)) {
			info->num_carats++;
			next_external_char++;
		}
	}

	/*
	 * Determine the number of ACPI name "segments" by counting the number of
	 * path separators within the string. Start with one segment since the
	 * segment count is [(# separators) + 1], and zero separators is ok.
	 */
	if (*next_external_char) {
		info->num_segments = 1;
		for (i = 0; next_external_char[i]; i++) {
			if (ACPI_IS_PATH_SEPARATOR(next_external_char[i])) {
				info->num_segments++;
			}
		}
	}

	info->length = (ACPI_NAME_SIZE * info->num_segments) +
	    4 + info->num_carats;

	info->next_external_char = next_external_char;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_build_internal_name
 *
 * PARAMETERS:  info            - Info struct fully initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct the internal (AML) namestring
 *              corresponding to the external (ASL) namestring.
 *
 ******************************************************************************/

acpi_status acpi_ns_build_internal_name(struct acpi_namestring_info *info)
{
	u32 num_segments = info->num_segments;
	char *internal_name = info->internal_name;
	const char *external_name = info->next_external_char;
	char *result = NULL;
	u32 i;

	ACPI_FUNCTION_TRACE(ns_build_internal_name);

	/* Setup the correct prefixes, counts, and pointers */

	if (info->fully_qualified) {
		internal_name[0] = AML_ROOT_PREFIX;

		if (num_segments <= 1) {
			result = &internal_name[1];
		} else if (num_segments == 2) {
			internal_name[1] = AML_DUAL_NAME_PREFIX;
			result = &internal_name[2];
		} else {
			internal_name[1] = AML_MULTI_NAME_PREFIX_OP;
			internal_name[2] = (char)num_segments;
			result = &internal_name[3];
		}
	} else {
		/*
		 * Not fully qualified.
		 * Handle Carats first, then append the name segments
		 */
		i = 0;
		if (info->num_carats) {
			for (i = 0; i < info->num_carats; i++) {
				internal_name[i] = AML_PARENT_PREFIX;
			}
		}

		if (num_segments <= 1) {
			result = &internal_name[i];
		} else if (num_segments == 2) {
			internal_name[i] = AML_DUAL_NAME_PREFIX;
			result = &internal_name[(acpi_size) i + 1];
		} else {
			internal_name[i] = AML_MULTI_NAME_PREFIX_OP;
			internal_name[(acpi_size) i + 1] = (char)num_segments;
			result = &internal_name[(acpi_size) i + 2];
		}
	}

	/* Build the name (minus path separators) */

	for (; num_segments; num_segments--) {
		for (i = 0; i < ACPI_NAME_SIZE; i++) {
			if (ACPI_IS_PATH_SEPARATOR(*external_name) ||
			    (*external_name == 0)) {

				/* Pad the segment with underscore(s) if segment is short */

				result[i] = '_';
			} else {
				/* Convert the character to uppercase and save it */

				result[i] =
				    (char)ACPI_TOUPPER((int)*external_name);
				external_name++;
			}
		}

		/* Now we must have a path separator, or the pathname is bad */

		if (!ACPI_IS_PATH_SEPARATOR(*external_name) &&
		    (*external_name != 0)) {
			return_ACPI_STATUS(AE_BAD_PATHNAME);
		}

		/* Move on the next segment */

		external_name++;
		result += ACPI_NAME_SIZE;
	}

	/* Terminate the string */

	*result = 0;

	if (info->fully_qualified) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Returning [%p] (abs) \"\\%s\"\n",
				  internal_name, internal_name));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Returning [%p] (rel) \"%s\"\n",
				  internal_name, internal_name));
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_internalize_name
 *
 * PARAMETERS:  *external_name          - External representation of name
 *              **Converted name        - Where to return the resulting
 *                                        internal represention of the name
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an external representation (e.g. "\_PR_.CPU0")
 *              to internal form (e.g. 5c 2f 02 5f 50 52 5f 43 50 55 30)
 *
 *******************************************************************************/

acpi_status
acpi_ns_internalize_name(const char *external_name, char **converted_name)
{
	char *internal_name;
	struct acpi_namestring_info info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_internalize_name);

	if ((!external_name) || (*external_name == 0) || (!converted_name)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the length of the new internal name */

	info.external_name = external_name;
	acpi_ns_get_internal_name_length(&info);

	/* We need a segment to store the internal  name */

	internal_name = ACPI_ALLOCATE_ZEROED(info.length);
	if (!internal_name) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Build the name */

	info.internal_name = internal_name;
	status = acpi_ns_build_internal_name(&info);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(internal_name);
		return_ACPI_STATUS(status);
	}

	*converted_name = internal_name;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_externalize_name
 *
 * PARAMETERS:  internal_name_length - Lenth of the internal name below
 *              internal_name       - Internal representation of name
 *              converted_name_length - Where the length is returned
 *              converted_name      - Where the resulting external name
 *                                    is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert internal name (e.g. 5c 2f 02 5f 50 52 5f 43 50 55 30)
 *              to its external (printable) form (e.g. "\_PR_.CPU0")
 *
 ******************************************************************************/

acpi_status
acpi_ns_externalize_name(u32 internal_name_length,
			 const char *internal_name,
			 u32 * converted_name_length, char **converted_name)
{
	u32 names_index = 0;
	u32 num_segments = 0;
	u32 required_length;
	u32 prefix_length = 0;
	u32 i = 0;
	u32 j = 0;

	ACPI_FUNCTION_TRACE(ns_externalize_name);

	if (!internal_name_length || !internal_name || !converted_name) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Check for a prefix (one '\' | one or more '^') */

	switch (internal_name[0]) {
	case AML_ROOT_PREFIX:

		prefix_length = 1;
		break;

	case AML_PARENT_PREFIX:

		for (i = 0; i < internal_name_length; i++) {
			if (ACPI_IS_PARENT_PREFIX(internal_name[i])) {
				prefix_length = i + 1;
			} else {
				break;
			}
		}

		if (i == internal_name_length) {
			prefix_length = i;
		}

		break;

	default:

		break;
	}

	/*
	 * Check for object names. Note that there could be 0-255 of these
	 * 4-byte elements.
	 */
	if (prefix_length < internal_name_length) {
		switch (internal_name[prefix_length]) {
		case AML_MULTI_NAME_PREFIX_OP:

			/* <count> 4-byte names */

			names_index = prefix_length + 2;
			num_segments = (u8)
			    internal_name[(acpi_size) prefix_length + 1];
			break;

		case AML_DUAL_NAME_PREFIX:

			/* Two 4-byte names */

			names_index = prefix_length + 1;
			num_segments = 2;
			break;

		case 0:

			/* null_name */

			names_index = 0;
			num_segments = 0;
			break;

		default:

			/* one 4-byte name */

			names_index = prefix_length;
			num_segments = 1;
			break;
		}
	}

	/*
	 * Calculate the length of converted_name, which equals the length
	 * of the prefix, length of all object names, length of any required
	 * punctuation ('.') between object names, plus the NULL terminator.
	 */
	required_length = prefix_length + (4 * num_segments) +
	    ((num_segments > 0) ? (num_segments - 1) : 0) + 1;

	/*
	 * Check to see if we're still in bounds. If not, there's a problem
	 * with internal_name (invalid format).
	 */
	if (required_length > internal_name_length) {
		ACPI_ERROR((AE_INFO, "Invalid internal name"));
		return_ACPI_STATUS(AE_BAD_PATHNAME);
	}

	/* Build the converted_name */

	*converted_name = ACPI_ALLOCATE_ZEROED(required_length);
	if (!(*converted_name)) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	j = 0;

	for (i = 0; i < prefix_length; i++) {
		(*converted_name)[j++] = internal_name[i];
	}

	if (num_segments > 0) {
		for (i = 0; i < num_segments; i++) {
			if (i > 0) {
				(*converted_name)[j++] = '.';
			}

			/* Copy and validate the 4-char name segment */

			ACPI_MOVE_NAME(&(*converted_name)[j],
				       &internal_name[names_index]);
			acpi_ut_repair_name(&(*converted_name)[j]);

			j += ACPI_NAME_SIZE;
			names_index += ACPI_NAME_SIZE;
		}
	}

	if (converted_name_length) {
		*converted_name_length = (u32) required_length;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_validate_handle
 *
 * PARAMETERS:  handle          - Handle to be validated and typecast to a
 *                                namespace node.
 *
 * RETURN:      A pointer to a namespace node
 *
 * DESCRIPTION: Convert a namespace handle to a namespace node. Handles special
 *              cases for the root node.
 *
 * NOTE: Real integer handles would allow for more verification
 *       and keep all pointers within this subsystem - however this introduces
 *       more overhead and has not been necessary to this point. Drivers
 *       holding handles are typically notified before a node becomes invalid
 *       due to a table unload.
 *
 ******************************************************************************/

struct acpi_namespace_node *acpi_ns_validate_handle(acpi_handle handle)
{

	ACPI_FUNCTION_ENTRY();

	/* Parameter validation */

	if ((!handle) || (handle == ACPI_ROOT_OBJECT)) {
		return (acpi_gbl_root_node);
	}

	/* We can at least attempt to verify the handle */

	if (ACPI_GET_DESCRIPTOR_TYPE(handle) != ACPI_DESC_TYPE_NAMED) {
		return (NULL);
	}

	return (ACPI_CAST_PTR(struct acpi_namespace_node, handle));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: free memory allocated for namespace and ACPI table storage.
 *
 ******************************************************************************/

void acpi_ns_terminate(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_terminate);

	/*
	 * Free the entire namespace -- all nodes and all objects
	 * attached to the nodes
	 */
	acpi_ns_delete_namespace_subtree(acpi_gbl_root_node);

	/* Delete any objects attached to the root node */

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	acpi_ns_delete_node(acpi_gbl_root_node);
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Namespace freed\n"));
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_opens_scope
 *
 * PARAMETERS:  type        - A valid namespace type
 *
 * RETURN:      NEWSCOPE if the passed type "opens a name scope" according
 *              to the ACPI specification, else 0
 *
 ******************************************************************************/

u32 acpi_ns_opens_scope(acpi_object_type type)
{
	ACPI_FUNCTION_ENTRY();

	if (type > ACPI_TYPE_LOCAL_MAX) {

		/* type code out of range  */

		ACPI_WARNING((AE_INFO, "Invalid Object Type 0x%X", type));
		return (ACPI_NS_NORMAL);
	}

	return (((u32)acpi_gbl_ns_properties[type]) & ACPI_NS_NEWSCOPE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_node
 *
 * PARAMETERS:  *pathname   - Name to be found, in external (ASL) format. The
 *                            \ (backslash) and ^ (carat) prefixes, and the
 *                            . (period) to separate segments are supported.
 *              prefix_node  - Root of subtree to be searched, or NS_ALL for the
 *                            root of the name space. If Name is fully
 *                            qualified (first s8 is '\'), the passed value
 *                            of Scope will not be accessed.
 *              flags       - Used to indicate whether to perform upsearch or
 *                            not.
 *              return_node - Where the Node is returned
 *
 * DESCRIPTION: Look up a name relative to a given scope and return the
 *              corresponding Node. NOTE: Scope can be null.
 *
 * MUTEX:       Locks namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_get_node(struct acpi_namespace_node *prefix_node,
		 const char *pathname,
		 u32 flags, struct acpi_namespace_node **return_node)
{
	union acpi_generic_state scope_info;
	acpi_status status;
	char *internal_path;

	ACPI_FUNCTION_TRACE_PTR(ns_get_node, ACPI_CAST_PTR(char, pathname));

	/* Simplest case is a null pathname */

	if (!pathname) {
		*return_node = prefix_node;
		if (!prefix_node) {
			*return_node = acpi_gbl_root_node;
		}
		return_ACPI_STATUS(AE_OK);
	}

	/* Quick check for a reference to the root */

	if (ACPI_IS_ROOT_PREFIX(pathname[0]) && (!pathname[1])) {
		*return_node = acpi_gbl_root_node;
		return_ACPI_STATUS(AE_OK);
	}

	/* Convert path to internal representation */

	status = acpi_ns_internalize_name(pathname, &internal_path);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Must lock namespace during lookup */

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Setup lookup scope (search starting point) */

	scope_info.scope.node = prefix_node;

	/* Lookup the name in the namespace */

	status = acpi_ns_lookup(&scope_info, internal_path, ACPI_TYPE_ANY,
				ACPI_IMODE_EXECUTE,
				(flags | ACPI_NS_DONT_OPEN_SCOPE), NULL,
				return_node);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%s, %s\n",
				  pathname, acpi_format_exception(status)));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

cleanup:
	ACPI_FREE(internal_path);
	return_ACPI_STATUS(status);
}
