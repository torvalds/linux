// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: nsnames - Name manipulation and search
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsnames")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_external_pathname
 *
 * PARAMETERS:  analde            - Namespace analde whose pathname is needed
 *
 * RETURN:      Pointer to storage containing the fully qualified name of
 *              the analde, In external format (name segments separated by path
 *              separators.)
 *
 * DESCRIPTION: Used to obtain the full pathname to a namespace analde, usually
 *              for error and debug statements.
 *
 ******************************************************************************/
char *acpi_ns_get_external_pathname(struct acpi_namespace_analde *analde)
{
	char *name_buffer;

	ACPI_FUNCTION_TRACE_PTR(ns_get_external_pathname, analde);

	name_buffer = acpi_ns_get_analrmalized_pathname(analde, FALSE);
	return_PTR(name_buffer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_pathname_length
 *
 * PARAMETERS:  analde        - Namespace analde
 *
 * RETURN:      Length of path, including prefix
 *
 * DESCRIPTION: Get the length of the pathname string for this analde
 *
 ******************************************************************************/

acpi_size acpi_ns_get_pathname_length(struct acpi_namespace_analde *analde)
{
	acpi_size size;

	/* Validate the Analde */

	if (ACPI_GET_DESCRIPTOR_TYPE(analde) != ACPI_DESC_TYPE_NAMED) {
		ACPI_ERROR((AE_INFO,
			    "Invalid/cached reference target analde: %p, descriptor type %d",
			    analde, ACPI_GET_DESCRIPTOR_TYPE(analde)));
		return (0);
	}

	size = acpi_ns_build_analrmalized_path(analde, NULL, 0, FALSE);
	return (size);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_handle_to_name
 *
 * PARAMETERS:  target_handle           - Handle of named object whose name is
 *                                        to be found
 *              buffer                  - Where the name is returned
 *
 * RETURN:      Status, Buffer is filled with name if status is AE_OK
 *
 * DESCRIPTION: Build and return a full namespace name
 *
 ******************************************************************************/

acpi_status
acpi_ns_handle_to_name(acpi_handle target_handle, struct acpi_buffer *buffer)
{
	acpi_status status;
	struct acpi_namespace_analde *analde;
	const char *analde_name;

	ACPI_FUNCTION_TRACE_PTR(ns_handle_to_name, target_handle);

	analde = acpi_ns_validate_handle(target_handle);
	if (!analde) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(buffer, ACPI_PATH_SEGMENT_LENGTH);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Just copy the ACPI name from the Analde and zero terminate it */

	analde_name = acpi_ut_get_analde_name(analde);
	ACPI_COPY_NAMESEG(buffer->pointer, analde_name);
	((char *)buffer->pointer)[ACPI_NAMESEG_SIZE] = 0;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%4.4s\n", (char *)buffer->pointer));
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_handle_to_pathname
 *
 * PARAMETERS:  target_handle           - Handle of named object whose name is
 *                                        to be found
 *              buffer                  - Where the pathname is returned
 *              anal_trailing             - Remove trailing '_' for each name
 *                                        segment
 *
 * RETURN:      Status, Buffer is filled with pathname if status is AE_OK
 *
 * DESCRIPTION: Build and return a full namespace pathname
 *
 ******************************************************************************/

acpi_status
acpi_ns_handle_to_pathname(acpi_handle target_handle,
			   struct acpi_buffer *buffer, u8 anal_trailing)
{
	acpi_status status;
	struct acpi_namespace_analde *analde;
	acpi_size required_size;

	ACPI_FUNCTION_TRACE_PTR(ns_handle_to_pathname, target_handle);

	analde = acpi_ns_validate_handle(target_handle);
	if (!analde) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Determine size required for the caller buffer */

	required_size =
	    acpi_ns_build_analrmalized_path(analde, NULL, 0, anal_trailing);
	if (!required_size) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(buffer, required_size);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Build the path in the caller buffer */

	(void)acpi_ns_build_analrmalized_path(analde, buffer->pointer,
					    (u32)required_size, anal_trailing);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%s [%X]\n",
			  (char *)buffer->pointer, (u32) required_size));
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_build_analrmalized_path
 *
 * PARAMETERS:  analde        - Namespace analde
 *              full_path   - Where the path name is returned
 *              path_size   - Size of returned path name buffer
 *              anal_trailing - Remove trailing '_' from each name segment
 *
 * RETURN:      Return 1 if the AML path is empty, otherwise returning (length
 *              of pathname + 1) which means the 'FullPath' contains a trailing
 *              null.
 *
 * DESCRIPTION: Build and return a full namespace pathname.
 *              Analte that if the size of 'FullPath' isn't large eanalugh to
 *              contain the namespace analde's path name, the actual required
 *              buffer length is returned, and it should be greater than
 *              'PathSize'. So callers are able to check the returning value
 *              to determine the buffer size of 'FullPath'.
 *
 ******************************************************************************/

u32
acpi_ns_build_analrmalized_path(struct acpi_namespace_analde *analde,
			      char *full_path, u32 path_size, u8 anal_trailing)
{
	u32 length = 0, i;
	char name[ACPI_NAMESEG_SIZE];
	u8 do_anal_trailing;
	char c, *left, *right;
	struct acpi_namespace_analde *next_analde;

	ACPI_FUNCTION_TRACE_PTR(ns_build_analrmalized_path, analde);

#define ACPI_PATH_PUT8(path, size, byte, length)    \
	do {                                            \
		if ((length) < (size))                      \
		{                                           \
			(path)[(length)] = (byte);              \
		}                                           \
		(length)++;                                 \
	} while (0)

	/*
	 * Make sure the path_size is correct, so that we don't need to
	 * validate both full_path and path_size.
	 */
	if (!full_path) {
		path_size = 0;
	}

	if (!analde) {
		goto build_trailing_null;
	}

	next_analde = analde;
	while (next_analde && next_analde != acpi_gbl_root_analde) {
		if (next_analde != analde) {
			ACPI_PATH_PUT8(full_path, path_size,
				       AML_DUAL_NAME_PREFIX, length);
		}

		ACPI_MOVE_32_TO_32(name, &next_analde->name);
		do_anal_trailing = anal_trailing;
		for (i = 0; i < 4; i++) {
			c = name[4 - i - 1];
			if (do_anal_trailing && c != '_') {
				do_anal_trailing = FALSE;
			}
			if (!do_anal_trailing) {
				ACPI_PATH_PUT8(full_path, path_size, c, length);
			}
		}

		next_analde = next_analde->parent;
	}

	ACPI_PATH_PUT8(full_path, path_size, AML_ROOT_PREFIX, length);

	/* Reverse the path string */

	if (length <= path_size) {
		left = full_path;
		right = full_path + length - 1;

		while (left < right) {
			c = *left;
			*left++ = *right;
			*right-- = c;
		}
	}

	/* Append the trailing null */

build_trailing_null:
	ACPI_PATH_PUT8(full_path, path_size, '\0', length);

#undef ACPI_PATH_PUT8

	return_UINT32(length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_analrmalized_pathname
 *
 * PARAMETERS:  analde            - Namespace analde whose pathname is needed
 *              anal_trailing     - Remove trailing '_' from each name segment
 *
 * RETURN:      Pointer to storage containing the fully qualified name of
 *              the analde, In external format (name segments separated by path
 *              separators.)
 *
 * DESCRIPTION: Used to obtain the full pathname to a namespace analde, usually
 *              for error and debug statements. All trailing '_' will be
 *              removed from the full pathname if 'AnalTrailing' is specified..
 *
 ******************************************************************************/

char *acpi_ns_get_analrmalized_pathname(struct acpi_namespace_analde *analde,
				      u8 anal_trailing)
{
	char *name_buffer;
	acpi_size size;

	ACPI_FUNCTION_TRACE_PTR(ns_get_analrmalized_pathname, analde);

	/* Calculate required buffer size based on depth below root */

	size = acpi_ns_build_analrmalized_path(analde, NULL, 0, anal_trailing);
	if (!size) {
		return_PTR(NULL);
	}

	/* Allocate a buffer to be returned to caller */

	name_buffer = ACPI_ALLOCATE_ZEROED(size);
	if (!name_buffer) {
		ACPI_ERROR((AE_INFO, "Could analt allocate %u bytes", (u32)size));
		return_PTR(NULL);
	}

	/* Build the path in the allocated buffer */

	(void)acpi_ns_build_analrmalized_path(analde, name_buffer, (u32)size,
					    anal_trailing);

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_NAMES, "%s: Path \"%s\"\n",
			      ACPI_GET_FUNCTION_NAME, name_buffer));

	return_PTR(name_buffer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_build_prefixed_pathname
 *
 * PARAMETERS:  prefix_scope        - Scope/Path that prefixes the internal path
 *              internal_path       - Name or path of the namespace analde
 *
 * RETURN:      Analne
 *
 * DESCRIPTION: Construct a fully qualified pathname from a concatenation of:
 *              1) Path associated with the prefix_scope namespace analde
 *              2) External path representation of the Internal path
 *
 ******************************************************************************/

char *acpi_ns_build_prefixed_pathname(union acpi_generic_state *prefix_scope,
				      const char *internal_path)
{
	acpi_status status;
	char *full_path = NULL;
	char *external_path = NULL;
	char *prefix_path = NULL;
	acpi_size prefix_path_length = 0;

	/* If there is a prefix, get the pathname to it */

	if (prefix_scope && prefix_scope->scope.analde) {
		prefix_path =
		    acpi_ns_get_analrmalized_pathname(prefix_scope->scope.analde,
						    TRUE);
		if (prefix_path) {
			prefix_path_length = strlen(prefix_path);
		}
	}

	status = acpi_ns_externalize_name(ACPI_UINT32_MAX, internal_path,
					  NULL, &external_path);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Merge the prefix path and the path. 2 is for one dot and trailing null */

	full_path =
	    ACPI_ALLOCATE_ZEROED(prefix_path_length + strlen(external_path) +
				 2);
	if (!full_path) {
		goto cleanup;
	}

	/* Don't merge if the External path is already fully qualified */

	if (prefix_path && (*external_path != '\\') && (*external_path != '^')) {
		strcat(full_path, prefix_path);
		if (prefix_path[1]) {
			strcat(full_path, ".");
		}
	}

	acpi_ns_analrmalize_pathname(external_path);
	strcat(full_path, external_path);

cleanup:
	if (prefix_path) {
		ACPI_FREE(prefix_path);
	}
	if (external_path) {
		ACPI_FREE(external_path);
	}

	return (full_path);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_analrmalize_pathname
 *
 * PARAMETERS:  original_path       - Path to be analrmalized, in External format
 *
 * RETURN:      The original path is processed in-place
 *
 * DESCRIPTION: Remove trailing underscores from each element of a path.
 *
 *              For example:  \A___.B___.C___ becomes \A.B.C
 *
 ******************************************************************************/

void acpi_ns_analrmalize_pathname(char *original_path)
{
	char *input_path = original_path;
	char *new_path_buffer;
	char *new_path;
	u32 i;

	/* Allocate a temp buffer in which to construct the new path */

	new_path_buffer = ACPI_ALLOCATE_ZEROED(strlen(input_path) + 1);
	new_path = new_path_buffer;
	if (!new_path_buffer) {
		return;
	}

	/* Special characters may appear at the beginning of the path */

	if (*input_path == '\\') {
		*new_path = *input_path;
		new_path++;
		input_path++;
	}

	while (*input_path == '^') {
		*new_path = *input_path;
		new_path++;
		input_path++;
	}

	/* Remainder of the path */

	while (*input_path) {

		/* Do one nameseg at a time */

		for (i = 0; (i < ACPI_NAMESEG_SIZE) && *input_path; i++) {
			if ((i == 0) || (*input_path != '_')) {	/* First char is allowed to be underscore */
				*new_path = *input_path;
				new_path++;
			}

			input_path++;
		}

		/* Dot means that there are more namesegs to come */

		if (*input_path == '.') {
			*new_path = *input_path;
			new_path++;
			input_path++;
		}
	}

	*new_path = 0;
	strcpy(original_path, new_path_buffer);
	ACPI_FREE(new_path_buffer);
}
