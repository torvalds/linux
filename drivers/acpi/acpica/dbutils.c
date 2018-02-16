/*******************************************************************************
 *
 * Module Name: dbutils - AML debugger utilities
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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
#include "acdebug.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbutils")

/* Local prototypes */
#ifdef ACPI_OBSOLETE_FUNCTIONS
acpi_status acpi_db_second_pass_parse(union acpi_parse_object *root);

void acpi_db_dump_buffer(u32 address);
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_match_argument
 *
 * PARAMETERS:  user_argument           - User command line
 *              arguments               - Array of commands to match against
 *
 * RETURN:      Index into command array or ACPI_TYPE_NOT_FOUND if not found
 *
 * DESCRIPTION: Search command array for a command match
 *
 ******************************************************************************/

acpi_object_type
acpi_db_match_argument(char *user_argument,
		       struct acpi_db_argument_info *arguments)
{
	u32 i;

	if (!user_argument || user_argument[0] == 0) {
		return (ACPI_TYPE_NOT_FOUND);
	}

	for (i = 0; arguments[i].name; i++) {
		if (strstr(ACPI_CAST_PTR(char, arguments[i].name),
			   ACPI_CAST_PTR(char,
					 user_argument)) == arguments[i].name) {
			return (i);
		}
	}

	/* Argument not recognized */

	return (ACPI_TYPE_NOT_FOUND);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_set_output_destination
 *
 * PARAMETERS:  output_flags        - Current flags word
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the current destination for debugger output. Also sets
 *              the debug output level accordingly.
 *
 ******************************************************************************/

void acpi_db_set_output_destination(u32 output_flags)
{

	acpi_gbl_db_output_flags = (u8)output_flags;

	if ((output_flags & ACPI_DB_REDIRECTABLE_OUTPUT) &&
	    acpi_gbl_db_output_to_file) {
		acpi_dbg_level = acpi_gbl_db_debug_level;
	} else {
		acpi_dbg_level = acpi_gbl_db_console_debug_level;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_external_object
 *
 * PARAMETERS:  obj_desc        - External ACPI object to dump
 *              level           - Nesting level.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the contents of an ACPI external object
 *
 ******************************************************************************/

void acpi_db_dump_external_object(union acpi_object *obj_desc, u32 level)
{
	u32 i;

	if (!obj_desc) {
		acpi_os_printf("[Null Object]\n");
		return;
	}

	for (i = 0; i < level; i++) {
		acpi_os_printf(" ");
	}

	switch (obj_desc->type) {
	case ACPI_TYPE_ANY:

		acpi_os_printf("[Null Object] (Type=0)\n");
		break;

	case ACPI_TYPE_INTEGER:

		acpi_os_printf("[Integer] = %8.8X%8.8X\n",
			       ACPI_FORMAT_UINT64(obj_desc->integer.value));
		break;

	case ACPI_TYPE_STRING:

		acpi_os_printf("[String] Length %.2X = ",
			       obj_desc->string.length);
		acpi_ut_print_string(obj_desc->string.pointer, ACPI_UINT8_MAX);
		acpi_os_printf("\n");
		break;

	case ACPI_TYPE_BUFFER:

		acpi_os_printf("[Buffer] Length %.2X = ",
			       obj_desc->buffer.length);
		if (obj_desc->buffer.length) {
			if (obj_desc->buffer.length > 16) {
				acpi_os_printf("\n");
			}

			acpi_ut_debug_dump_buffer(ACPI_CAST_PTR
						  (u8,
						   obj_desc->buffer.pointer),
						  obj_desc->buffer.length,
						  DB_BYTE_DISPLAY, _COMPONENT);
		} else {
			acpi_os_printf("\n");
		}
		break;

	case ACPI_TYPE_PACKAGE:

		acpi_os_printf("[Package] Contains %u Elements:\n",
			       obj_desc->package.count);

		for (i = 0; i < obj_desc->package.count; i++) {
			acpi_db_dump_external_object(&obj_desc->package.
						     elements[i], level + 1);
		}
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		acpi_os_printf("[Object Reference] = ");
		acpi_db_display_internal_object(obj_desc->reference.handle,
						NULL);
		break;

	case ACPI_TYPE_PROCESSOR:

		acpi_os_printf("[Processor]\n");
		break;

	case ACPI_TYPE_POWER:

		acpi_os_printf("[Power Resource]\n");
		break;

	default:

		acpi_os_printf("[Unknown Type] %X\n", obj_desc->type);
		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_prep_namestring
 *
 * PARAMETERS:  name            - String to prepare
 *
 * RETURN:      None
 *
 * DESCRIPTION: Translate all forward slashes and dots to backslashes.
 *
 ******************************************************************************/

void acpi_db_prep_namestring(char *name)
{

	if (!name) {
		return;
	}

	acpi_ut_strupr(name);

	/* Convert a leading forward slash to a backslash */

	if (*name == '/') {
		*name = '\\';
	}

	/* Ignore a leading backslash, this is the root prefix */

	if (ACPI_IS_ROOT_PREFIX(*name)) {
		name++;
	}

	/* Convert all slash path separators to dots */

	while (*name) {
		if ((*name == '/') || (*name == '\\')) {
			*name = '.';
		}

		name++;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_local_ns_lookup
 *
 * PARAMETERS:  name            - Name to lookup
 *
 * RETURN:      Pointer to a namespace node, null on failure
 *
 * DESCRIPTION: Lookup a name in the ACPI namespace
 *
 * Note: Currently begins search from the root. Could be enhanced to use
 * the current prefix (scope) node as the search beginning point.
 *
 ******************************************************************************/

struct acpi_namespace_node *acpi_db_local_ns_lookup(char *name)
{
	char *internal_path;
	acpi_status status;
	struct acpi_namespace_node *node = NULL;

	acpi_db_prep_namestring(name);

	/* Build an internal namestring */

	status = acpi_ns_internalize_name(name, &internal_path);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Invalid namestring: %s\n", name);
		return (NULL);
	}

	/*
	 * Lookup the name.
	 * (Uses root node as the search starting point)
	 */
	status = acpi_ns_lookup(NULL, internal_path, ACPI_TYPE_ANY,
				ACPI_IMODE_EXECUTE,
				ACPI_NS_NO_UPSEARCH | ACPI_NS_DONT_OPEN_SCOPE,
				NULL, &node);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not locate name: %s, %s\n",
			       name, acpi_format_exception(status));
	}

	ACPI_FREE(internal_path);
	return (node);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_uint32_to_hex_string
 *
 * PARAMETERS:  value           - The value to be converted to string
 *              buffer          - Buffer for result (not less than 11 bytes)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert the unsigned 32-bit value to the hexadecimal image
 *
 * NOTE: It is the caller's responsibility to ensure that the length of buffer
 *       is sufficient.
 *
 ******************************************************************************/

void acpi_db_uint32_to_hex_string(u32 value, char *buffer)
{
	int i;

	if (value == 0) {
		strcpy(buffer, "0");
		return;
	}

	buffer[8] = '\0';

	for (i = 7; i >= 0; i--) {
		buffer[i] = acpi_gbl_upper_hex_digits[value & 0x0F];
		value = value >> 4;
	}
}

#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_second_pass_parse
 *
 * PARAMETERS:  root            - Root of the parse tree
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Second pass parse of the ACPI tables. We need to wait until
 *              second pass to parse the control methods
 *
 ******************************************************************************/

acpi_status acpi_db_second_pass_parse(union acpi_parse_object *root)
{
	union acpi_parse_object *op = root;
	union acpi_parse_object *method;
	union acpi_parse_object *search_op;
	union acpi_parse_object *start_op;
	acpi_status status = AE_OK;
	u32 base_aml_offset;
	struct acpi_walk_state *walk_state;

	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("Pass two parse ....\n");

	while (op) {
		if (op->common.aml_opcode == AML_METHOD_OP) {
			method = op;

			/* Create a new walk state for the parse */

			walk_state =
			    acpi_ds_create_walk_state(0, NULL, NULL, NULL);
			if (!walk_state) {
				return (AE_NO_MEMORY);
			}

			/* Init the Walk State */

			walk_state->parser_state.aml =
			    walk_state->parser_state.aml_start =
			    method->named.data;
			walk_state->parser_state.aml_end =
			    walk_state->parser_state.pkg_end =
			    method->named.data + method->named.length;
			walk_state->parser_state.start_scope = op;

			walk_state->descending_callback =
			    acpi_ds_load1_begin_op;
			walk_state->ascending_callback = acpi_ds_load1_end_op;

			/* Perform the AML parse */

			status = acpi_ps_parse_aml(walk_state);

			base_aml_offset =
			    (method->common.value.arg)->common.aml_offset + 1;
			start_op = (method->common.value.arg)->common.next;
			search_op = start_op;

			while (search_op) {
				search_op->common.aml_offset += base_aml_offset;
				search_op =
				    acpi_ps_get_depth_next(start_op, search_op);
			}
		}

		if (op->common.aml_opcode == AML_REGION_OP) {

			/* TBD: [Investigate] this isn't quite the right thing to do! */
			/*
			 *
			 * Method = (ACPI_DEFERRED_OP *) Op;
			 * Status = acpi_ps_parse_aml (Op, Method->Body, Method->body_length);
			 */
		}

		if (ACPI_FAILURE(status)) {
			break;
		}

		op = acpi_ps_get_depth_next(root, op);
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_buffer
 *
 * PARAMETERS:  address             - Pointer to the buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a portion of a buffer
 *
 ******************************************************************************/

void acpi_db_dump_buffer(u32 address)
{

	acpi_os_printf("\nLocation %X:\n", address);

	acpi_dbg_level |= ACPI_LV_TABLES;
	acpi_ut_debug_dump_buffer(ACPI_TO_POINTER(address), 64, DB_BYTE_DISPLAY,
				  ACPI_UINT32_MAX);
}
#endif
