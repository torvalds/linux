/******************************************************************************
 *
 * Module Name: psargs - Parse AML opcode arguments
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
#include "acparser.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acdispat.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psargs")

/* Local prototypes */
static u32
acpi_ps_get_next_package_length(struct acpi_parse_state *parser_state);

static union acpi_parse_object *acpi_ps_get_next_field(struct acpi_parse_state
						       *parser_state);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_package_length
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      Decoded package length. On completion, the AML pointer points
 *              past the length byte or bytes.
 *
 * DESCRIPTION: Decode and return a package length field.
 *              Note: Largest package length is 28 bits, from ACPI specification
 *
 ******************************************************************************/

static u32
acpi_ps_get_next_package_length(struct acpi_parse_state *parser_state)
{
	u8 *aml = parser_state->aml;
	u32 package_length = 0;
	u32 byte_count;
	u8 byte_zero_mask = 0x3F;	/* Default [0:5] */

	ACPI_FUNCTION_TRACE(ps_get_next_package_length);

	/*
	 * Byte 0 bits [6:7] contain the number of additional bytes
	 * used to encode the package length, either 0,1,2, or 3
	 */
	byte_count = (aml[0] >> 6);
	parser_state->aml += ((acpi_size)byte_count + 1);

	/* Get bytes 3, 2, 1 as needed */

	while (byte_count) {
		/*
		 * Final bit positions for the package length bytes:
		 *      Byte3->[20:27]
		 *      Byte2->[12:19]
		 *      Byte1->[04:11]
		 *      Byte0->[00:03]
		 */
		package_length |= (aml[byte_count] << ((byte_count << 3) - 4));

		byte_zero_mask = 0x0F;	/* Use bits [0:3] of byte 0 */
		byte_count--;
	}

	/* Byte 0 is a special case, either bits [0:3] or [0:5] are used */

	package_length |= (aml[0] & byte_zero_mask);
	return_UINT32(package_length);
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
 *              the package. Consumes the package length field
 *
 ******************************************************************************/

u8 *acpi_ps_get_next_package_end(struct acpi_parse_state *parser_state)
{
	u8 *start = parser_state->aml;
	u32 package_length;

	ACPI_FUNCTION_TRACE(ps_get_next_package_end);

	/* Function below updates parser_state->Aml */

	package_length = acpi_ps_get_next_package_length(parser_state);

	return_PTR(start + package_length);	/* end of package */
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
 * DESCRIPTION: Get next raw namestring within the AML stream. Handles all name
 *              prefix characters. Set parser state to point past the string.
 *              (Name is consumed from the AML.)
 *
 ******************************************************************************/

char *acpi_ps_get_next_namestring(struct acpi_parse_state *parser_state)
{
	u8 *start = parser_state->aml;
	u8 *end = parser_state->aml;

	ACPI_FUNCTION_TRACE(ps_get_next_namestring);

	/* Point past any namestring prefix characters (backslash or carat) */

	while (ACPI_IS_ROOT_PREFIX(*end) || ACPI_IS_PARENT_PREFIX(*end)) {
		end++;
	}

	/* Decode the path prefix character */

	switch (*end) {
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

		/* Multiple name segments, 4 chars each, count in next byte */

		end += 2 + (*(end + 1) * ACPI_NAME_SIZE);
		break;

	default:

		/* Single name segment */

		end += ACPI_NAME_SIZE;
		break;
	}

	parser_state->aml = end;
	return_PTR((char *)start);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_namepath
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              arg                 - Where the namepath will be stored
 *              arg_count           - If the namepath points to a control method
 *                                    the method's argument is returned here.
 *              possible_method_call - Whether the namepath can possibly be the
 *                                    start of a method call
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get next name (if method call, return # of required args).
 *              Names are looked up in the internal namespace to determine
 *              if the name represents a control method. If a method
 *              is found, the number of arguments to the method is returned.
 *              This information is critical for parsing to continue correctly.
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_namepath(struct acpi_walk_state *walk_state,
			  struct acpi_parse_state *parser_state,
			  union acpi_parse_object *arg, u8 possible_method_call)
{
	acpi_status status;
	char *path;
	union acpi_parse_object *name_op;
	union acpi_operand_object *method_desc;
	struct acpi_namespace_node *node;
	u8 *start = parser_state->aml;

	ACPI_FUNCTION_TRACE(ps_get_next_namepath);

	path = acpi_ps_get_next_namestring(parser_state);
	acpi_ps_init_op(arg, AML_INT_NAMEPATH_OP);

	/* Null path case is allowed, just exit */

	if (!path) {
		arg->common.value.name = path;
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Lookup the name in the internal namespace, starting with the current
	 * scope. We don't want to add anything new to the namespace here,
	 * however, so we use MODE_EXECUTE.
	 * Allow searching of the parent tree, but don't open a new scope -
	 * we just want to lookup the object (must be mode EXECUTE to perform
	 * the upsearch)
	 */
	status = acpi_ns_lookup(walk_state->scope_info, path,
				ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
				ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
				NULL, &node);

	/*
	 * If this name is a control method invocation, we must
	 * setup the method call
	 */
	if (ACPI_SUCCESS(status) &&
	    possible_method_call && (node->type == ACPI_TYPE_METHOD)) {
		if (walk_state->opcode == AML_UNLOAD_OP) {
			/*
			 * acpi_ps_get_next_namestring has increased the AML pointer,
			 * so we need to restore the saved AML pointer for method call.
			 */
			walk_state->parser_state.aml = start;
			walk_state->arg_count = 1;
			acpi_ps_init_op(arg, AML_INT_METHODCALL_OP);
			return_ACPI_STATUS(AE_OK);
		}

		/* This name is actually a control method invocation */

		method_desc = acpi_ns_get_attached_object(node);
		ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
				  "Control Method - %p Desc %p Path=%p\n", node,
				  method_desc, path));

		name_op = acpi_ps_alloc_op(AML_INT_NAMEPATH_OP, start);
		if (!name_op) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Change Arg into a METHOD CALL and attach name to it */

		acpi_ps_init_op(arg, AML_INT_METHODCALL_OP);
		name_op->common.value.name = path;

		/* Point METHODCALL/NAME to the METHOD Node */

		name_op->common.node = node;
		acpi_ps_append_arg(arg, name_op);

		if (!method_desc) {
			ACPI_ERROR((AE_INFO,
				    "Control Method %p has no attached object",
				    node));
			return_ACPI_STATUS(AE_AML_INTERNAL);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
				  "Control Method - %p Args %X\n",
				  node, method_desc->method.param_count));

		/* Get the number of arguments to expect */

		walk_state->arg_count = method_desc->method.param_count;
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Special handling if the name was not found during the lookup -
	 * some not_found cases are allowed
	 */
	if (status == AE_NOT_FOUND) {

		/* 1) not_found is ok during load pass 1/2 (allow forward references) */

		if ((walk_state->parse_flags & ACPI_PARSE_MODE_MASK) !=
		    ACPI_PARSE_EXECUTE) {
			status = AE_OK;
		}

		/* 2) not_found during a cond_ref_of(x) is ok by definition */

		else if (walk_state->op->common.aml_opcode ==
			 AML_COND_REF_OF_OP) {
			status = AE_OK;
		}

		/*
		 * 3) not_found while building a Package is ok at this point, we
		 * may flag as an error later if slack mode is not enabled.
		 * (Some ASL code depends on allowing this behavior)
		 */
		else if ((arg->common.parent) &&
			 ((arg->common.parent->common.aml_opcode ==
			   AML_PACKAGE_OP)
			  || (arg->common.parent->common.aml_opcode ==
			      AML_VAR_PACKAGE_OP))) {
			status = AE_OK;
		}
	}

	/* Final exception check (may have been changed from code above) */

	if (ACPI_FAILURE(status)) {
		ACPI_ERROR_NAMESPACE(path, status);

		if ((walk_state->parse_flags & ACPI_PARSE_MODE_MASK) ==
		    ACPI_PARSE_EXECUTE) {

			/* Report a control method execution error */

			status = acpi_ds_method_error(status, walk_state);
		}
	}

	/* Save the namepath */

	arg->common.value.name = path;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_simple_arg
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              arg_type            - The argument type (AML_*_ARG)
 *              arg                 - Where the argument is returned
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get the next simple argument (constant, string, or namestring)
 *
 ******************************************************************************/

void
acpi_ps_get_next_simple_arg(struct acpi_parse_state *parser_state,
			    u32 arg_type, union acpi_parse_object *arg)
{
	u32 length;
	u16 opcode;
	u8 *aml = parser_state->aml;

	ACPI_FUNCTION_TRACE_U32(ps_get_next_simple_arg, arg_type);

	switch (arg_type) {
	case ARGP_BYTEDATA:

		/* Get 1 byte from the AML stream */

		opcode = AML_BYTE_OP;
		arg->common.value.integer = (u64) *aml;
		length = 1;
		break;

	case ARGP_WORDDATA:

		/* Get 2 bytes from the AML stream */

		opcode = AML_WORD_OP;
		ACPI_MOVE_16_TO_64(&arg->common.value.integer, aml);
		length = 2;
		break;

	case ARGP_DWORDDATA:

		/* Get 4 bytes from the AML stream */

		opcode = AML_DWORD_OP;
		ACPI_MOVE_32_TO_64(&arg->common.value.integer, aml);
		length = 4;
		break;

	case ARGP_QWORDDATA:

		/* Get 8 bytes from the AML stream */

		opcode = AML_QWORD_OP;
		ACPI_MOVE_64_TO_64(&arg->common.value.integer, aml);
		length = 8;
		break;

	case ARGP_CHARLIST:

		/* Get a pointer to the string, point past the string */

		opcode = AML_STRING_OP;
		arg->common.value.string = ACPI_CAST_PTR(char, aml);

		/* Find the null terminator */

		length = 0;
		while (aml[length]) {
			length++;
		}
		length++;
		break;

	case ARGP_NAME:
	case ARGP_NAMESTRING:

		acpi_ps_init_op(arg, AML_INT_NAMEPATH_OP);
		arg->common.value.name =
		    acpi_ps_get_next_namestring(parser_state);
		return_VOID;

	default:

		ACPI_ERROR((AE_INFO, "Invalid ArgType 0x%X", arg_type));
		return_VOID;
	}

	acpi_ps_init_op(arg, opcode);
	parser_state->aml += length;
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

static union acpi_parse_object *acpi_ps_get_next_field(struct acpi_parse_state
						       *parser_state)
{
	u8 *aml;
	union acpi_parse_object *field;
	union acpi_parse_object *arg = NULL;
	u16 opcode;
	u32 name;
	u8 access_type;
	u8 access_attribute;
	u8 access_length;
	u32 pkg_length;
	u8 *pkg_end;
	u32 buffer_length;

	ACPI_FUNCTION_TRACE(ps_get_next_field);

	aml = parser_state->aml;

	/* Determine field type */

	switch (ACPI_GET8(parser_state->aml)) {
	case AML_FIELD_OFFSET_OP:

		opcode = AML_INT_RESERVEDFIELD_OP;
		parser_state->aml++;
		break;

	case AML_FIELD_ACCESS_OP:

		opcode = AML_INT_ACCESSFIELD_OP;
		parser_state->aml++;
		break;

	case AML_FIELD_CONNECTION_OP:

		opcode = AML_INT_CONNECTION_OP;
		parser_state->aml++;
		break;

	case AML_FIELD_EXT_ACCESS_OP:

		opcode = AML_INT_EXTACCESSFIELD_OP;
		parser_state->aml++;
		break;

	default:

		opcode = AML_INT_NAMEDFIELD_OP;
		break;
	}

	/* Allocate a new field op */

	field = acpi_ps_alloc_op(opcode, aml);
	if (!field) {
		return_PTR(NULL);
	}

	/* Decode the field type */

	switch (opcode) {
	case AML_INT_NAMEDFIELD_OP:

		/* Get the 4-character name */

		ACPI_MOVE_32_TO_32(&name, parser_state->aml);
		acpi_ps_set_name(field, name);
		parser_state->aml += ACPI_NAME_SIZE;

		/* Get the length which is encoded as a package length */

		field->common.value.size =
		    acpi_ps_get_next_package_length(parser_state);
		break;

	case AML_INT_RESERVEDFIELD_OP:

		/* Get the length which is encoded as a package length */

		field->common.value.size =
		    acpi_ps_get_next_package_length(parser_state);
		break;

	case AML_INT_ACCESSFIELD_OP:
	case AML_INT_EXTACCESSFIELD_OP:

		/*
		 * Get access_type and access_attrib and merge into the field Op
		 * access_type is first operand, access_attribute is second. stuff
		 * these bytes into the node integer value for convenience.
		 */

		/* Get the two bytes (Type/Attribute) */

		access_type = ACPI_GET8(parser_state->aml);
		parser_state->aml++;
		access_attribute = ACPI_GET8(parser_state->aml);
		parser_state->aml++;

		field->common.value.integer = (u8)access_type;
		field->common.value.integer |= (u16)(access_attribute << 8);

		/* This opcode has a third byte, access_length */

		if (opcode == AML_INT_EXTACCESSFIELD_OP) {
			access_length = ACPI_GET8(parser_state->aml);
			parser_state->aml++;

			field->common.value.integer |=
			    (u32)(access_length << 16);
		}
		break;

	case AML_INT_CONNECTION_OP:

		/*
		 * Argument for Connection operator can be either a Buffer
		 * (resource descriptor), or a name_string.
		 */
		aml = parser_state->aml;
		if (ACPI_GET8(parser_state->aml) == AML_BUFFER_OP) {
			parser_state->aml++;

			pkg_end = parser_state->aml;
			pkg_length =
			    acpi_ps_get_next_package_length(parser_state);
			pkg_end += pkg_length;

			if (parser_state->aml < pkg_end) {

				/* Non-empty list */

				arg =
				    acpi_ps_alloc_op(AML_INT_BYTELIST_OP, aml);
				if (!arg) {
					acpi_ps_free_op(field);
					return_PTR(NULL);
				}

				/* Get the actual buffer length argument */

				opcode = ACPI_GET8(parser_state->aml);
				parser_state->aml++;

				switch (opcode) {
				case AML_BYTE_OP:	/* AML_BYTEDATA_ARG */

					buffer_length =
					    ACPI_GET8(parser_state->aml);
					parser_state->aml += 1;
					break;

				case AML_WORD_OP:	/* AML_WORDDATA_ARG */

					buffer_length =
					    ACPI_GET16(parser_state->aml);
					parser_state->aml += 2;
					break;

				case AML_DWORD_OP:	/* AML_DWORDATA_ARG */

					buffer_length =
					    ACPI_GET32(parser_state->aml);
					parser_state->aml += 4;
					break;

				default:

					buffer_length = 0;
					break;
				}

				/* Fill in bytelist data */

				arg->named.value.size = buffer_length;
				arg->named.data = parser_state->aml;
			}

			/* Skip to End of byte data */

			parser_state->aml = pkg_end;
		} else {
			arg = acpi_ps_alloc_op(AML_INT_NAMEPATH_OP, aml);
			if (!arg) {
				acpi_ps_free_op(field);
				return_PTR(NULL);
			}

			/* Get the Namestring argument */

			arg->common.value.name =
			    acpi_ps_get_next_namestring(parser_state);
		}

		/* Link the buffer/namestring to parent (CONNECTION_OP) */

		acpi_ps_append_arg(field, arg);
		break;

	default:

		/* Opcode was set in previous switch */
		break;
	}

	return_PTR(field);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_next_arg
 *
 * PARAMETERS:  walk_state          - Current state
 *              parser_state        - Current parser state object
 *              arg_type            - The argument type (AML_*_ARG)
 *              return_arg          - Where the next arg is returned
 *
 * RETURN:      Status, and an op object containing the next argument.
 *
 * DESCRIPTION: Get next argument (including complex list arguments that require
 *              pushing the parser stack)
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_arg(struct acpi_walk_state *walk_state,
		     struct acpi_parse_state *parser_state,
		     u32 arg_type, union acpi_parse_object **return_arg)
{
	union acpi_parse_object *arg = NULL;
	union acpi_parse_object *prev = NULL;
	union acpi_parse_object *field;
	u32 subop;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_PTR(ps_get_next_arg, parser_state);

	switch (arg_type) {
	case ARGP_BYTEDATA:
	case ARGP_WORDDATA:
	case ARGP_DWORDDATA:
	case ARGP_CHARLIST:
	case ARGP_NAME:
	case ARGP_NAMESTRING:

		/* Constants, strings, and namestrings are all the same size */

		arg = acpi_ps_alloc_op(AML_BYTE_OP, parser_state->aml);
		if (!arg) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		acpi_ps_get_next_simple_arg(parser_state, arg_type, arg);
		break;

	case ARGP_PKGLENGTH:

		/* Package length, nothing returned */

		parser_state->pkg_end =
		    acpi_ps_get_next_package_end(parser_state);
		break;

	case ARGP_FIELDLIST:

		if (parser_state->aml < parser_state->pkg_end) {

			/* Non-empty list */

			while (parser_state->aml < parser_state->pkg_end) {
				field = acpi_ps_get_next_field(parser_state);
				if (!field) {
					return_ACPI_STATUS(AE_NO_MEMORY);
				}

				if (prev) {
					prev->common.next = field;
				} else {
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

			arg = acpi_ps_alloc_op(AML_INT_BYTELIST_OP,
					       parser_state->aml);
			if (!arg) {
				return_ACPI_STATUS(AE_NO_MEMORY);
			}

			/* Fill in bytelist data */

			arg->common.value.size = (u32)
			    ACPI_PTR_DIFF(parser_state->pkg_end,
					  parser_state->aml);
			arg->named.data = parser_state->aml;

			/* Skip to End of byte data */

			parser_state->aml = parser_state->pkg_end;
		}
		break;

	case ARGP_TARGET:
	case ARGP_SUPERNAME:
	case ARGP_SIMPLENAME:
	case ARGP_NAME_OR_REF:

		subop = acpi_ps_peek_opcode(parser_state);
		if (subop == 0 ||
		    acpi_ps_is_leading_char(subop) ||
		    ACPI_IS_ROOT_PREFIX(subop) ||
		    ACPI_IS_PARENT_PREFIX(subop)) {

			/* null_name or name_string */

			arg =
			    acpi_ps_alloc_op(AML_INT_NAMEPATH_OP,
					     parser_state->aml);
			if (!arg) {
				return_ACPI_STATUS(AE_NO_MEMORY);
			}

			/* To support super_name arg of Unload */

			if (walk_state->opcode == AML_UNLOAD_OP) {
				status =
				    acpi_ps_get_next_namepath(walk_state,
							      parser_state, arg,
							      ACPI_POSSIBLE_METHOD_CALL);

				/*
				 * If the super_name argument is a method call, we have
				 * already restored the AML pointer, just free this Arg
				 */
				if (arg->common.aml_opcode ==
				    AML_INT_METHODCALL_OP) {
					acpi_ps_free_op(arg);
					arg = NULL;
				}
			} else {
				status =
				    acpi_ps_get_next_namepath(walk_state,
							      parser_state, arg,
							      ACPI_NOT_METHOD_CALL);
			}
		} else {
			/* Single complex argument, nothing returned */

			walk_state->arg_count = 1;
		}
		break;

	case ARGP_DATAOBJ:
	case ARGP_TERMARG:

		/* Single complex argument, nothing returned */

		walk_state->arg_count = 1;
		break;

	case ARGP_DATAOBJLIST:
	case ARGP_TERMLIST:
	case ARGP_OBJLIST:

		if (parser_state->aml < parser_state->pkg_end) {

			/* Non-empty list of variable arguments, nothing returned */

			walk_state->arg_count = ACPI_VAR_ARGS;
		}
		break;

	default:

		ACPI_ERROR((AE_INFO, "Invalid ArgType: 0x%X", arg_type));
		status = AE_AML_OPERAND_TYPE;
		break;
	}

	*return_arg = arg;
	return_ACPI_STATUS(status);
}
