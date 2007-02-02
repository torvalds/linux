/******************************************************************************
 *
 * Module Name: nsutils - Utilities for accessing ACPI namespace, accessing
 *                        parents and siblings and Scope manipulation
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2007, R. Byron Moore
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
#include <acpi/amlcode.h>
#include <acpi/actables.h>

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsutils")

/* Local prototypes */
static u8 acpi_ns_valid_path_separator(char sep);

#ifdef ACPI_OBSOLETE_FUNCTIONS
acpi_name acpi_ns_find_parent_name(struct acpi_namespace_node *node_to_search);
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_report_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              internal_name       - Name or path of the namespace node
 *              lookup_status       - Exception code from NS lookup
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print warning message with full pathname
 *
 ******************************************************************************/

void
acpi_ns_report_error(char *module_name,
		     u32 line_number,
		     char *internal_name, acpi_status lookup_status)
{
	acpi_status status;
	u32 bad_name;
	char *name = NULL;

	acpi_os_printf("ACPI Error (%s-%04d): ", module_name, line_number);

	if (lookup_status == AE_BAD_CHARACTER) {

		/* There is a non-ascii character in the name */

		ACPI_MOVE_32_TO_32(&bad_name, internal_name);
		acpi_os_printf("[0x%4.4X] (NON-ASCII)", bad_name);
	} else {
		/* Convert path to external format */

		status = acpi_ns_externalize_name(ACPI_UINT32_MAX,
						  internal_name, NULL, &name);

		/* Print target name */

		if (ACPI_SUCCESS(status)) {
			acpi_os_printf("[%s]", name);
		} else {
			acpi_os_printf("[COULD NOT EXTERNALIZE NAME]");
		}

		if (name) {
			ACPI_FREE(name);
		}
	}

	acpi_os_printf(" Namespace lookup failure, %s\n",
		       acpi_format_exception(lookup_status));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_report_method_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              Message             - Error message to use on failure
 *              prefix_node         - Prefix relative to the path
 *              Path                - Path to the node (optional)
 *              method_status       - Execution status
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print warning message with full pathname
 *
 ******************************************************************************/

void
acpi_ns_report_method_error(char *module_name,
			    u32 line_number,
			    char *message,
			    struct acpi_namespace_node *prefix_node,
			    char *path, acpi_status method_status)
{
	acpi_status status;
	struct acpi_namespace_node *node = prefix_node;

	acpi_os_printf("ACPI Error (%s-%04d): ", module_name, line_number);

	if (path) {
		status =
		    acpi_ns_get_node(prefix_node, path, ACPI_NS_NO_UPSEARCH,
				     &node);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf("[Could not get node by pathname]");
		}
	}

	acpi_ns_print_node_pathname(node, message);
	acpi_os_printf(", %s\n", acpi_format_exception(method_status));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_print_node_pathname
 *
 * PARAMETERS:  Node            - Object
 *              Message         - Prefix message
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

void
acpi_ns_print_node_pathname(struct acpi_namespace_node *node, char *message)
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
 * FUNCTION:    acpi_ns_valid_root_prefix
 *
 * PARAMETERS:  Prefix          - Character to be checked
 *
 * RETURN:      TRUE if a valid prefix
 *
 * DESCRIPTION: Check if a character is a valid ACPI Root prefix
 *
 ******************************************************************************/

u8 acpi_ns_valid_root_prefix(char prefix)
{

	return ((u8) (prefix == '\\'));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_valid_path_separator
 *
 * PARAMETERS:  Sep         - Character to be checked
 *
 * RETURN:      TRUE if a valid path separator
 *
 * DESCRIPTION: Check if a character is a valid ACPI path separator
 *
 ******************************************************************************/

static u8 acpi_ns_valid_path_separator(char sep)
{

	return ((u8) (sep == '.'));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_type
 *
 * PARAMETERS:  Node        - Parent Node to be examined
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
		return_UINT32(ACPI_TYPE_ANY);
	}

	return_UINT32((acpi_object_type) node->type);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_local
 *
 * PARAMETERS:  Type        - A namespace object type
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

		ACPI_WARNING((AE_INFO, "Invalid Object Type %X", type));
		return_UINT32(ACPI_NS_NORMAL);
	}

	return_UINT32((u32) acpi_gbl_ns_properties[type] & ACPI_NS_LOCAL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_internal_name_length
 *
 * PARAMETERS:  Info            - Info struct initialized with the
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
	char *next_external_char;
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
	if (acpi_ns_valid_root_prefix(next_external_char[0])) {
		info->fully_qualified = TRUE;
		next_external_char++;
	} else {
		/*
		 * Handle Carat prefixes
		 */
		while (*next_external_char == '^') {
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
			if (acpi_ns_valid_path_separator(next_external_char[i])) {
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
 * PARAMETERS:  Info            - Info struct fully initialized
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
	char *external_name = info->next_external_char;
	char *result = NULL;
	acpi_native_uint i;

	ACPI_FUNCTION_TRACE(ns_build_internal_name);

	/* Setup the correct prefixes, counts, and pointers */

	if (info->fully_qualified) {
		internal_name[0] = '\\';

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
				internal_name[i] = '^';
			}
		}

		if (num_segments <= 1) {
			result = &internal_name[i];
		} else if (num_segments == 2) {
			internal_name[i] = AML_DUAL_NAME_PREFIX;
			result = &internal_name[(acpi_native_uint) (i + 1)];
		} else {
			internal_name[i] = AML_MULTI_NAME_PREFIX_OP;
			internal_name[(acpi_native_uint) (i + 1)] =
			    (char)num_segments;
			result = &internal_name[(acpi_native_uint) (i + 2)];
		}
	}

	/* Build the name (minus path separators) */

	for (; num_segments; num_segments--) {
		for (i = 0; i < ACPI_NAME_SIZE; i++) {
			if (acpi_ns_valid_path_separator(*external_name) ||
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

		if (!acpi_ns_valid_path_separator(*external_name) &&
		    (*external_name != 0)) {
			return_ACPI_STATUS(AE_BAD_PARAMETER);
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
 *              **Converted Name        - Where to return the resulting
 *                                        internal represention of the name
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an external representation (e.g. "\_PR_.CPU0")
 *              to internal form (e.g. 5c 2f 02 5f 50 52 5f 43 50 55 30)
 *
 *******************************************************************************/

acpi_status acpi_ns_internalize_name(char *external_name, char **converted_name)
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
			 char *internal_name,
			 u32 * converted_name_length, char **converted_name)
{
	acpi_native_uint names_index = 0;
	acpi_native_uint num_segments = 0;
	acpi_native_uint required_length;
	acpi_native_uint prefix_length = 0;
	acpi_native_uint i = 0;
	acpi_native_uint j = 0;

	ACPI_FUNCTION_TRACE(ns_externalize_name);

	if (!internal_name_length || !internal_name || !converted_name) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Check for a prefix (one '\' | one or more '^').
	 */
	switch (internal_name[0]) {
	case '\\':
		prefix_length = 1;
		break;

	case '^':
		for (i = 0; i < internal_name_length; i++) {
			if (internal_name[i] == '^') {
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
	 * Check for object names.  Note that there could be 0-255 of these
	 * 4-byte elements.
	 */
	if (prefix_length < internal_name_length) {
		switch (internal_name[prefix_length]) {
		case AML_MULTI_NAME_PREFIX_OP:

			/* <count> 4-byte names */

			names_index = prefix_length + 2;
			num_segments = (acpi_native_uint) (u8)
			    internal_name[(acpi_native_uint)
					  (prefix_length + 1)];
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
	 * Check to see if we're still in bounds.  If not, there's a problem
	 * with internal_name (invalid format).
	 */
	if (required_length > internal_name_length) {
		ACPI_ERROR((AE_INFO, "Invalid internal name"));
		return_ACPI_STATUS(AE_BAD_PATHNAME);
	}

	/*
	 * Build converted_name
	 */
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

			(*converted_name)[j++] = internal_name[names_index++];
			(*converted_name)[j++] = internal_name[names_index++];
			(*converted_name)[j++] = internal_name[names_index++];
			(*converted_name)[j++] = internal_name[names_index++];
		}
	}

	if (converted_name_length) {
		*converted_name_length = (u32) required_length;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_map_handle_to_node
 *
 * PARAMETERS:  Handle          - Handle to be converted to an Node
 *
 * RETURN:      A Name table entry pointer
 *
 * DESCRIPTION: Convert a namespace handle to a real Node
 *
 * Note: Real integer handles would allow for more verification
 *       and keep all pointers within this subsystem - however this introduces
 *       more (and perhaps unnecessary) overhead.
 *
 ******************************************************************************/

struct acpi_namespace_node *acpi_ns_map_handle_to_node(acpi_handle handle)
{

	ACPI_FUNCTION_ENTRY();

	/*
	 * Simple implementation
	 */
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
 * FUNCTION:    acpi_ns_convert_entry_to_handle
 *
 * PARAMETERS:  Node          - Node to be converted to a Handle
 *
 * RETURN:      A user handle
 *
 * DESCRIPTION: Convert a real Node to a namespace handle
 *
 ******************************************************************************/

acpi_handle acpi_ns_convert_entry_to_handle(struct acpi_namespace_node *node)
{

	/*
	 * Simple implementation for now;
	 */
	return ((acpi_handle) node);

/* Example future implementation ---------------------

	if (!Node)
	{
		return (NULL);
	}

	if (Node == acpi_gbl_root_node)
	{
		return (ACPI_ROOT_OBJECT);
	}

	return ((acpi_handle) Node);
------------------------------------------------------*/
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
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE(ns_terminate);

	/*
	 * 1) Free the entire namespace -- all nodes and objects
	 *
	 * Delete all object descriptors attached to namepsace nodes
	 */
	acpi_ns_delete_namespace_subtree(acpi_gbl_root_node);

	/* Detach any objects attached to the root */

	obj_desc = acpi_ns_get_attached_object(acpi_gbl_root_node);
	if (obj_desc) {
		acpi_ns_detach_object(acpi_gbl_root_node);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Namespace freed\n"));
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_opens_scope
 *
 * PARAMETERS:  Type        - A valid namespace type
 *
 * RETURN:      NEWSCOPE if the passed type "opens a name scope" according
 *              to the ACPI specification, else 0
 *
 ******************************************************************************/

u32 acpi_ns_opens_scope(acpi_object_type type)
{
	ACPI_FUNCTION_TRACE_STR(ns_opens_scope, acpi_ut_get_type_name(type));

	if (!acpi_ut_valid_object_type(type)) {

		/* type code out of range  */

		ACPI_WARNING((AE_INFO, "Invalid Object Type %X", type));
		return_UINT32(ACPI_NS_NORMAL);
	}

	return_UINT32(((u32) acpi_gbl_ns_properties[type]) & ACPI_NS_NEWSCOPE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_node
 *
 * PARAMETERS:  *Pathname   - Name to be found, in external (ASL) format. The
 *                            \ (backslash) and ^ (carat) prefixes, and the
 *                            . (period) to separate segments are supported.
 *              prefix_node  - Root of subtree to be searched, or NS_ALL for the
 *                            root of the name space.  If Name is fully
 *                            qualified (first s8 is '\'), the passed value
 *                            of Scope will not be accessed.
 *              Flags       - Used to indicate whether to perform upsearch or
 *                            not.
 *              return_node - Where the Node is returned
 *
 * DESCRIPTION: Look up a name relative to a given scope and return the
 *              corresponding Node.  NOTE: Scope can be null.
 *
 * MUTEX:       Locks namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_get_node(struct acpi_namespace_node *prefix_node,
		 char *pathname,
		 u32 flags, struct acpi_namespace_node **return_node)
{
	union acpi_generic_state scope_info;
	acpi_status status;
	char *internal_path;

	ACPI_FUNCTION_TRACE_PTR(ns_get_node, pathname);

	if (!pathname) {
		*return_node = prefix_node;
		if (!prefix_node) {
			*return_node = acpi_gbl_root_node;
		}
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
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "%s, %s\n",
				  pathname, acpi_format_exception(status)));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

      cleanup:
	ACPI_FREE(internal_path);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_parent_node
 *
 * PARAMETERS:  Node       - Current table entry
 *
 * RETURN:      Parent entry of the given entry
 *
 * DESCRIPTION: Obtain the parent entry for a given entry in the namespace.
 *
 ******************************************************************************/

struct acpi_namespace_node *acpi_ns_get_parent_node(struct acpi_namespace_node
						    *node)
{
	ACPI_FUNCTION_ENTRY();

	if (!node) {
		return (NULL);
	}

	/*
	 * Walk to the end of this peer list. The last entry is marked with a flag
	 * and the peer pointer is really a pointer back to the parent. This saves
	 * putting a parent back pointer in each and every named object!
	 */
	while (!(node->flags & ANOBJ_END_OF_PEER_LIST)) {
		node = node->peer;
	}

	return (node->peer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_next_valid_node
 *
 * PARAMETERS:  Node       - Current table entry
 *
 * RETURN:      Next valid Node in the linked node list. NULL if no more valid
 *              nodes.
 *
 * DESCRIPTION: Find the next valid node within a name table.
 *              Useful for implementing NULL-end-of-list loops.
 *
 ******************************************************************************/

struct acpi_namespace_node *acpi_ns_get_next_valid_node(struct
							acpi_namespace_node
							*node)
{

	/* If we are at the end of this peer list, return NULL */

	if (node->flags & ANOBJ_END_OF_PEER_LIST) {
		return NULL;
	}

	/* Otherwise just return the next peer */

	return (node->peer);
}

#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_find_parent_name
 *
 * PARAMETERS:  *child_node            - Named Obj whose name is to be found
 *
 * RETURN:      The ACPI name
 *
 * DESCRIPTION: Search for the given obj in its parent scope and return the
 *              name segment, or "????" if the parent name can't be found
 *              (which "should not happen").
 *
 ******************************************************************************/

acpi_name acpi_ns_find_parent_name(struct acpi_namespace_node * child_node)
{
	struct acpi_namespace_node *parent_node;

	ACPI_FUNCTION_TRACE(ns_find_parent_name);

	if (child_node) {

		/* Valid entry.  Get the parent Node */

		parent_node = acpi_ns_get_parent_node(child_node);
		if (parent_node) {
			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "Parent of %p [%4.4s] is %p [%4.4s]\n",
					  child_node,
					  acpi_ut_get_node_name(child_node),
					  parent_node,
					  acpi_ut_get_node_name(parent_node)));

			if (parent_node->name.integer) {
				return_VALUE((acpi_name) parent_node->name.
					     integer);
			}
		}

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Unable to find parent of %p (%4.4s)\n",
				  child_node,
				  acpi_ut_get_node_name(child_node)));
	}

	return_VALUE(ACPI_UNKNOWN_NAME);
}
#endif
