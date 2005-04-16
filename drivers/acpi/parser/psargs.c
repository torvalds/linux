/******************************************************************************
 *
 * Module Name: psargs - Parse AML opcode arguments
 *
 *****************************************************************************/

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
#include <acpi/acparser.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_PARSER
	 ACPI_MODULE_NAME    ("psargs")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_package_length
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      Decoded package length.  On completion, the AML pointer points
 *              past the length byte or bytes.
 *
 * DESCRIPTION: Decode and return a package length field
 *
 ******************************************************************************/

u32
acpi_ps_get_next_package_length (
	struct acpi_parse_state         *parser_state)
{
	u32                             encoded_length;
	u32                             length = 0;


	ACPI_FUNCTION_TRACE ("ps_get_next_package_length");


	encoded_length = (u32) ACPI_GET8 (parser_state->aml);
	parser_state->aml++;


	switch (encoded_length >> 6) /* bits 6-7 contain encoding scheme */ {
	case 0: /* 1-byte encoding (bits 0-5) */

		length = (encoded_length & 0x3F);
		break;


	case 1: /* 2-byte encoding (next byte + bits 0-3) */

		length = ((ACPI_GET8 (parser_state->aml) << 04) |
				 (encoded_length & 0x0F));
		parser_state->aml++;
		break;


	case 2: /* 3-byte encoding (next 2 bytes + bits 0-3) */

		length = ((ACPI_GET8 (parser_state->aml + 1) << 12) |
				  (ACPI_GET8 (parser_state->aml)    << 04) |
				  (encoded_length & 0x0F));
		parser_state->aml += 2;
		break;


	case 3: /* 4-byte encoding (next 3 bytes + bits 0-3) */

		length = ((ACPI_GET8 (parser_state->aml + 2) << 20) |
				  (ACPI_GET8 (parser_state->aml + 1) << 12) |
				  (ACPI_GET8 (parser_state->aml)    << 04) |
				  (encoded_length & 0x0F));
		parser_state->aml += 3;
		break;

	default:

		/* Can't get here, only 2 bits / 4 cases */
		break;
	}

	return_VALUE (length);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_package_end
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      Pointer to end-of-package +1
 *
 * DESCRIPTION: Get next package length and return a pointer past the end of
 *              the package.  Consumes the package length field
 *
 ******************************************************************************/

u8 *
acpi_ps_get_next_package_end (
	struct acpi_parse_state         *parser_state)
{
	u8                              *start = parser_state->aml;
	acpi_native_uint                length;


	ACPI_FUNCTION_TRACE ("ps_get_next_package_end");


	/* Function below changes parser_state->Aml */

	length = (acpi_native_uint) acpi_ps_get_next_package_length (parser_state);

	return_PTR (start + length); /* end of package */
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_namestring
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      Pointer to the start of the name string (pointer points into
 *              the AML.
 *
 * DESCRIPTION: Get next raw namestring within the AML stream.  Handles all name
 *              prefix characters.  Set parser state to point past the string.
 *              (Name is consumed from the AML.)
 *
 ******************************************************************************/

char *
acpi_ps_get_next_namestring (
	struct acpi_parse_state         *parser_state)
{
	u8                              *start = parser_state->aml;
	u8                              *end = parser_state->aml;


	ACPI_FUNCTION_TRACE ("ps_get_next_namestring");


	/* Handle multiple prefix characters */

	while (acpi_ps_is_prefix_char (ACPI_GET8 (end))) {
		/* Include prefix '\\' or '^' */

		end++;
	}

	/* Decode the path */

	switch (ACPI_GET8 (end)) {
	case 0:

		/* null_name */

		if (end == start) {
			start = NULL;
		}
		end++;
		break;

	case AML_DUAL_NAME_PREFIX:

		/* Two name segments */

		end += 1 + (2 * ACPI_NAME_SIZE);
		break;

	case AML_MULTI_NAME_PREFIX_OP:

		/* Multiple name segments, 4 chars each */

		end += 2 + ((acpi_size) ACPI_GET8 (end + 1) * ACPI_NAME_SIZE);
		break;

	default:

		/* Single name segment */

		end += ACPI_NAME_SIZE;
		break;
	}

	parser_state->aml = (u8*) end;
	return_PTR ((char *) start);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_namepath
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              Arg                 - Where the namepath will be stored
 *              arg_count           - If the namepath points to a control method
 *                                    the method's argument is returned here.
 *              method_call         - Whether the namepath can possibly be the
 *                                    start of a method call
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get next name (if method call, return # of required args).
 *              Names are looked up in the internal namespace to determine
 *              if the name represents a control method.  If a method
 *              is found, the number of arguments to the method is returned.
 *              This information is critical for parsing to continue correctly.
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_namepath (
	struct acpi_walk_state          *walk_state,
	struct acpi_parse_state         *parser_state,
	union acpi_parse_object         *arg,
	u8                              method_call)
{
	char                            *path;
	union acpi_parse_object         *name_op;
	acpi_status                     status = AE_OK;
	union acpi_operand_object       *method_desc;
	struct acpi_namespace_node      *node;
	union acpi_generic_state        scope_info;


	ACPI_FUNCTION_TRACE ("ps_get_next_namepath");


	path = acpi_ps_get_next_namestring (parser_state);

	/* Null path case is allowed */

	if (path) {
		/*
		 * Lookup the name in the internal namespace
		 */
		scope_info.scope.node = NULL;
		node = parser_state->start_node;
		if (node) {
			scope_info.scope.node = node;
		}

		/*
		 * Lookup object.  We don't want to add anything new to the namespace
		 * here, however.  So we use MODE_EXECUTE.  Allow searching of the
		 * parent tree, but don't open a new scope -- we just want to lookup the
		 * object  (MUST BE mode EXECUTE to perform upsearch)
		 */
		status = acpi_ns_lookup (&scope_info, path, ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
				 ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE, NULL, &node);
		if (ACPI_SUCCESS (status) && method_call) {
			if (node->type == ACPI_TYPE_METHOD) {
				/*
				 * This name is actually a control method invocation
				 */
				method_desc = acpi_ns_get_attached_object (node);
				ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
					"Control Method - %p Desc %p Path=%p\n",
					node, method_desc, path));

				name_op = acpi_ps_alloc_op (AML_INT_NAMEPATH_OP);
				if (!name_op) {
					return_ACPI_STATUS (AE_NO_MEMORY);
				}

				/* Change arg into a METHOD CALL and attach name to it */

				acpi_ps_init_op (arg, AML_INT_METHODCALL_OP);
				name_op->common.value.name = path;

				/* Point METHODCALL/NAME to the METHOD Node */

				name_op->common.node = node;
				acpi_ps_append_arg (arg, name_op);

				if (!method_desc) {
					ACPI_REPORT_ERROR ((
						"ps_get_next_namepath: Control Method %p has no attached object\n",
						node));
					return_ACPI_STATUS (AE_AML_INTERNAL);
				}

				ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
					"Control Method - %p Args %X\n",
					node, method_desc->method.param_count));

				/* Get the number of arguments to expect */

				walk_state->arg_count = method_desc->method.param_count;
				return_ACPI_STATUS (AE_OK);
			}

			/*
			 * Else this is normal named object reference.
			 * Just init the NAMEPATH object with the pathname.
			 * (See code below)
			 */
		}

		if (ACPI_FAILURE (status)) {
			/*
			 * 1) Any error other than NOT_FOUND is always severe
			 * 2) NOT_FOUND is only important if we are executing a method.
			 * 3) If executing a cond_ref_of opcode, NOT_FOUND is ok.
			 */
			if ((((walk_state->parse_flags & ACPI_PARSE_MODE_MASK) == ACPI_PARSE_EXECUTE) &&
				(status == AE_NOT_FOUND)                                                &&
				(walk_state->op->common.aml_opcode != AML_COND_REF_OF_OP)) ||

				(status != AE_NOT_FOUND)) {
				ACPI_REPORT_NSERROR (path, status);

				acpi_os_printf ("search_node %p start_node %p return_node %p\n",
					scope_info.scope.node, parser_state->start_node, node);


			}
			else {
				/*
				 * We got a NOT_FOUND during table load or we encountered
				 * a cond_ref_of(x) where the target does not exist.
				 * -- either case is ok
				 */
				status = AE_OK;
			}
		}
	}

	/*
	 * Regardless of success/failure above,
	 * Just initialize the Op with the pathname.
	 */
	acpi_ps_init_op (arg, AML_INT_NAMEPATH_OP);
	arg->common.value.name = path;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_simple_arg
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              arg_type            - The argument type (AML_*_ARG)
 *              Arg                 - Where the argument is returned
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get the next simple argument (constant, string, or namestring)
 *
 ******************************************************************************/

void
acpi_ps_get_next_simple_arg (
	struct acpi_parse_state         *parser_state,
	u32                             arg_type,
	union acpi_parse_object         *arg)
{

	ACPI_FUNCTION_TRACE_U32 ("ps_get_next_simple_arg", arg_type);


	switch (arg_type) {
	case ARGP_BYTEDATA:

		acpi_ps_init_op (arg, AML_BYTE_OP);
		arg->common.value.integer = (u32) ACPI_GET8 (parser_state->aml);
		parser_state->aml++;
		break;


	case ARGP_WORDDATA:

		acpi_ps_init_op (arg, AML_WORD_OP);

		/* Get 2 bytes from the AML stream */

		ACPI_MOVE_16_TO_32 (&arg->common.value.integer, parser_state->aml);
		parser_state->aml += 2;
		break;


	case ARGP_DWORDDATA:

		acpi_ps_init_op (arg, AML_DWORD_OP);

		/* Get 4 bytes from the AML stream */

		ACPI_MOVE_32_TO_32 (&arg->common.value.integer, parser_state->aml);
		parser_state->aml += 4;
		break;


	case ARGP_QWORDDATA:

		acpi_ps_init_op (arg, AML_QWORD_OP);

		/* Get 8 bytes from the AML stream */

		ACPI_MOVE_64_TO_64 (&arg->common.value.integer, parser_state->aml);
		parser_state->aml += 8;
		break;


	case ARGP_CHARLIST:

		acpi_ps_init_op (arg, AML_STRING_OP);
		arg->common.value.string = (char *) parser_state->aml;

		while (ACPI_GET8 (parser_state->aml) != '\0') {
			parser_state->aml++;
		}
		parser_state->aml++;
		break;


	case ARGP_NAME:
	case ARGP_NAMESTRING:

		acpi_ps_init_op (arg, AML_INT_NAMEPATH_OP);
		arg->common.value.name = acpi_ps_get_next_namestring (parser_state);
		break;


	default:

		ACPI_REPORT_ERROR (("Invalid arg_type %X\n", arg_type));
		break;
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_field
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      A newly allocated FIELD op
 *
 * DESCRIPTION: Get next field (named_field, reserved_field, or access_field)
 *
 ******************************************************************************/

union acpi_parse_object *
acpi_ps_get_next_field (
	struct acpi_parse_state         *parser_state)
{
	u32                             aml_offset = (u32) ACPI_PTR_DIFF (parser_state->aml,
			 parser_state->aml_start);
	union acpi_parse_object         *field;
	u16                             opcode;
	u32                             name;


	ACPI_FUNCTION_TRACE ("ps_get_next_field");


	/* determine field type */

	switch (ACPI_GET8 (parser_state->aml)) {
	default:

		opcode = AML_INT_NAMEDFIELD_OP;
		break;

	case 0x00:

		opcode = AML_INT_RESERVEDFIELD_OP;
		parser_state->aml++;
		break;

	case 0x01:

		opcode = AML_INT_ACCESSFIELD_OP;
		parser_state->aml++;
		break;
	}


	/* Allocate a new field op */

	field = acpi_ps_alloc_op (opcode);
	if (!field) {
		return_PTR (NULL);
	}

	field->common.aml_offset = aml_offset;

	/* Decode the field type */

	switch (opcode) {
	case AML_INT_NAMEDFIELD_OP:

		/* Get the 4-character name */

		ACPI_MOVE_32_TO_32 (&name, parser_state->aml);
		acpi_ps_set_name (field, name);
		parser_state->aml += ACPI_NAME_SIZE;

		/* Get the length which is encoded as a package length */

		field->common.value.size = acpi_ps_get_next_package_length (parser_state);
		break;


	case AML_INT_RESERVEDFIELD_OP:

		/* Get the length which is encoded as a package length */

		field->common.value.size = acpi_ps_get_next_package_length (parser_state);
		break;


	case AML_INT_ACCESSFIELD_OP:

		/*
		 * Get access_type and access_attrib and merge into the field Op
		 * access_type is first operand, access_attribute is second
		 */
		field->common.value.integer = (ACPI_GET8 (parser_state->aml) << 8);
		parser_state->aml++;
		field->common.value.integer |= ACPI_GET8 (parser_state->aml);
		parser_state->aml++;
		break;

	default:

		/* Opcode was set in previous switch */
		break;
	}

	return_PTR (field);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_arg
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              arg_type            - The argument type (AML_*_ARG)
 *              arg_count           - If the argument points to a control method
 *                                    the method's argument is returned here.
 *
 * RETURN:      Status, and an op object containing the next argument.
 *
 * DESCRIPTION: Get next argument (including complex list arguments that require
 *              pushing the parser stack)
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_arg (
	struct acpi_walk_state          *walk_state,
	struct acpi_parse_state         *parser_state,
	u32                             arg_type,
	union acpi_parse_object         **return_arg)
{
	union acpi_parse_object         *arg = NULL;
	union acpi_parse_object         *prev = NULL;
	union acpi_parse_object         *field;
	u32                             subop;
	acpi_status                     status = AE_OK;


	ACPI_FUNCTION_TRACE_PTR ("ps_get_next_arg", parser_state);


	switch (arg_type) {
	case ARGP_BYTEDATA:
	case ARGP_WORDDATA:
	case ARGP_DWORDDATA:
	case ARGP_CHARLIST:
	case ARGP_NAME:
	case ARGP_NAMESTRING:

		/* constants, strings, and namestrings are all the same size */

		arg = acpi_ps_alloc_op (AML_BYTE_OP);
		if (!arg) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}
		acpi_ps_get_next_simple_arg (parser_state, arg_type, arg);
		break;


	case ARGP_PKGLENGTH:

		/* Package length, nothing returned */

		parser_state->pkg_end = acpi_ps_get_next_package_end (parser_state);
		break;


	case ARGP_FIELDLIST:

		if (parser_state->aml < parser_state->pkg_end) {
			/* Non-empty list */

			while (parser_state->aml < parser_state->pkg_end) {
				field = acpi_ps_get_next_field (parser_state);
				if (!field) {
					return_ACPI_STATUS (AE_NO_MEMORY);
				}

				if (prev) {
					prev->common.next = field;
				}
				else {
					arg = field;
				}

				prev = field;
			}

			/* Skip to End of byte data */

			parser_state->aml = parser_state->pkg_end;
		}
		break;


	case ARGP_BYTELIST:

		if (parser_state->aml < parser_state->pkg_end) {
			/* Non-empty list */

			arg = acpi_ps_alloc_op (AML_INT_BYTELIST_OP);
			if (!arg) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			/* Fill in bytelist data */

			arg->common.value.size = (u32) ACPI_PTR_DIFF (parser_state->pkg_end,
					  parser_state->aml);
			arg->named.data = parser_state->aml;

			/* Skip to End of byte data */

			parser_state->aml = parser_state->pkg_end;
		}
		break;


	case ARGP_TARGET:
	case ARGP_SUPERNAME:
	case ARGP_SIMPLENAME:

		subop = acpi_ps_peek_opcode (parser_state);
		if (subop == 0                  ||
			acpi_ps_is_leading_char (subop) ||
			acpi_ps_is_prefix_char (subop)) {
			/* null_name or name_string */

			arg = acpi_ps_alloc_op (AML_INT_NAMEPATH_OP);
			if (!arg) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			status = acpi_ps_get_next_namepath (walk_state, parser_state, arg, 0);
		}
		else {
			/* single complex argument, nothing returned */

			walk_state->arg_count = 1;
		}
		break;


	case ARGP_DATAOBJ:
	case ARGP_TERMARG:

		/* single complex argument, nothing returned */

		walk_state->arg_count = 1;
		break;


	case ARGP_DATAOBJLIST:
	case ARGP_TERMLIST:
	case ARGP_OBJLIST:

		if (parser_state->aml < parser_state->pkg_end) {
			/* non-empty list of variable arguments, nothing returned */

			walk_state->arg_count = ACPI_VAR_ARGS;
		}
		break;


	default:

		ACPI_REPORT_ERROR (("Invalid arg_type: %X\n", arg_type));
		status = AE_AML_OPERAND_TYPE;
		break;
	}

	*return_arg = arg;
	return_ACPI_STATUS (status);
}
