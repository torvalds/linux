/******************************************************************************
 *
 * Module Name: psloop - Main AML parse loop
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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

/*
 * Parse the AML and build an operation tree as most interpreters, (such as
 * Perl) do. Parsing is done by hand rather than with a YACC generated parser
 * to tightly constrain stack and dynamic memory usage. Parsing is kept
 * flexible and the code fairly compact by parsing based on a list of AML
 * opcode templates in aml_op_info[].
 */

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "acdispat.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psloop")

static u32 acpi_gbl_depth = 0;

/* Local prototypes */

static acpi_status acpi_ps_get_aml_opcode(struct acpi_walk_state *walk_state);

static acpi_status
acpi_ps_build_named_op(struct acpi_walk_state *walk_state,
		       u8 * aml_op_start,
		       union acpi_parse_object *unnamed_op,
		       union acpi_parse_object **op);

static acpi_status
acpi_ps_create_op(struct acpi_walk_state *walk_state,
		  u8 * aml_op_start, union acpi_parse_object **new_op);

static acpi_status
acpi_ps_get_arguments(struct acpi_walk_state *walk_state,
		      u8 * aml_op_start, union acpi_parse_object *op);

static acpi_status
acpi_ps_complete_op(struct acpi_walk_state *walk_state,
		    union acpi_parse_object **op, acpi_status status);

static acpi_status
acpi_ps_complete_final_op(struct acpi_walk_state *walk_state,
			  union acpi_parse_object *op, acpi_status status);

static void
acpi_ps_link_module_code(union acpi_parse_object *parent_op,
			 u8 *aml_start, u32 aml_length, acpi_owner_id owner_id);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_aml_opcode
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Extract the next AML opcode from the input stream.
 *
 ******************************************************************************/

static acpi_status acpi_ps_get_aml_opcode(struct acpi_walk_state *walk_state)
{

	ACPI_FUNCTION_TRACE_PTR(ps_get_aml_opcode, walk_state);

	walk_state->aml_offset =
	    (u32) ACPI_PTR_DIFF(walk_state->parser_state.aml,
				walk_state->parser_state.aml_start);
	walk_state->opcode = acpi_ps_peek_opcode(&(walk_state->parser_state));

	/*
	 * First cut to determine what we have found:
	 * 1) A valid AML opcode
	 * 2) A name string
	 * 3) An unknown/invalid opcode
	 */
	walk_state->op_info = acpi_ps_get_opcode_info(walk_state->opcode);

	switch (walk_state->op_info->class) {
	case AML_CLASS_ASCII:
	case AML_CLASS_PREFIX:
		/*
		 * Starts with a valid prefix or ASCII char, this is a name
		 * string. Convert the bare name string to a namepath.
		 */
		walk_state->opcode = AML_INT_NAMEPATH_OP;
		walk_state->arg_types = ARGP_NAMESTRING;
		break;

	case AML_CLASS_UNKNOWN:

		/* The opcode is unrecognized. Just skip unknown opcodes */

		ACPI_ERROR((AE_INFO,
			    "Found unknown opcode 0x%X at AML address %p offset 0x%X, ignoring",
			    walk_state->opcode, walk_state->parser_state.aml,
			    walk_state->aml_offset));

		ACPI_DUMP_BUFFER(walk_state->parser_state.aml, 128);

		/* Assume one-byte bad opcode */

		walk_state->parser_state.aml++;
		return_ACPI_STATUS(AE_CTRL_PARSE_CONTINUE);

	default:

		/* Found opcode info, this is a normal opcode */

		walk_state->parser_state.aml +=
		    acpi_ps_get_opcode_size(walk_state->opcode);
		walk_state->arg_types = walk_state->op_info->parse_args;
		break;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_build_named_op
 *
 * PARAMETERS:  walk_state          - Current state
 *              aml_op_start        - Begin of named Op in AML
 *              unnamed_op          - Early Op (not a named Op)
 *              Op                  - Returned Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse a named Op
 *
 ******************************************************************************/

static acpi_status
acpi_ps_build_named_op(struct acpi_walk_state *walk_state,
		       u8 * aml_op_start,
		       union acpi_parse_object *unnamed_op,
		       union acpi_parse_object **op)
{
	acpi_status status = AE_OK;
	union acpi_parse_object *arg = NULL;

	ACPI_FUNCTION_TRACE_PTR(ps_build_named_op, walk_state);

	unnamed_op->common.value.arg = NULL;
	unnamed_op->common.arg_list_length = 0;
	unnamed_op->common.aml_opcode = walk_state->opcode;

	/*
	 * Get and append arguments until we find the node that contains
	 * the name (the type ARGP_NAME).
	 */
	while (GET_CURRENT_ARG_TYPE(walk_state->arg_types) &&
	       (GET_CURRENT_ARG_TYPE(walk_state->arg_types) != ARGP_NAME)) {
		status =
		    acpi_ps_get_next_arg(walk_state,
					 &(walk_state->parser_state),
					 GET_CURRENT_ARG_TYPE(walk_state->
							      arg_types), &arg);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		acpi_ps_append_arg(unnamed_op, arg);
		INCREMENT_ARG_LIST(walk_state->arg_types);
	}

	/*
	 * Make sure that we found a NAME and didn't run out of arguments
	 */
	if (!GET_CURRENT_ARG_TYPE(walk_state->arg_types)) {
		return_ACPI_STATUS(AE_AML_NO_OPERAND);
	}

	/* We know that this arg is a name, move to next arg */

	INCREMENT_ARG_LIST(walk_state->arg_types);

	/*
	 * Find the object. This will either insert the object into
	 * the namespace or simply look it up
	 */
	walk_state->op = NULL;

	status = walk_state->descending_callback(walk_state, op);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "During name lookup/catalog"));
		return_ACPI_STATUS(status);
	}

	if (!*op) {
		return_ACPI_STATUS(AE_CTRL_PARSE_CONTINUE);
	}

	status = acpi_ps_next_parse_state(walk_state, *op, status);
	if (ACPI_FAILURE(status)) {
		if (status == AE_CTRL_PENDING) {
			return_ACPI_STATUS(AE_CTRL_PARSE_PENDING);
		}
		return_ACPI_STATUS(status);
	}

	acpi_ps_append_arg(*op, unnamed_op->common.value.arg);
	acpi_gbl_depth++;

	if ((*op)->common.aml_opcode == AML_REGION_OP ||
	    (*op)->common.aml_opcode == AML_DATA_REGION_OP) {
		/*
		 * Defer final parsing of an operation_region body, because we don't
		 * have enough info in the first pass to parse it correctly (i.e.,
		 * there may be method calls within the term_arg elements of the body.)
		 *
		 * However, we must continue parsing because the opregion is not a
		 * standalone package -- we don't know where the end is at this point.
		 *
		 * (Length is unknown until parse of the body complete)
		 */
		(*op)->named.data = aml_op_start;
		(*op)->named.length = 0;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_create_op
 *
 * PARAMETERS:  walk_state          - Current state
 *              aml_op_start        - Op start in AML
 *              new_op              - Returned Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get Op from AML
 *
 ******************************************************************************/

static acpi_status
acpi_ps_create_op(struct acpi_walk_state *walk_state,
		  u8 * aml_op_start, union acpi_parse_object **new_op)
{
	acpi_status status = AE_OK;
	union acpi_parse_object *op;
	union acpi_parse_object *named_op = NULL;
	union acpi_parse_object *parent_scope;
	u8 argument_count;
	const struct acpi_opcode_info *op_info;

	ACPI_FUNCTION_TRACE_PTR(ps_create_op, walk_state);

	status = acpi_ps_get_aml_opcode(walk_state);
	if (status == AE_CTRL_PARSE_CONTINUE) {
		return_ACPI_STATUS(AE_CTRL_PARSE_CONTINUE);
	}

	/* Create Op structure and append to parent's argument list */

	walk_state->op_info = acpi_ps_get_opcode_info(walk_state->opcode);
	op = acpi_ps_alloc_op(walk_state->opcode);
	if (!op) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	if (walk_state->op_info->flags & AML_NAMED) {
		status =
		    acpi_ps_build_named_op(walk_state, aml_op_start, op,
					   &named_op);
		acpi_ps_free_op(op);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		*new_op = named_op;
		return_ACPI_STATUS(AE_OK);
	}

	/* Not a named opcode, just allocate Op and append to parent */

	if (walk_state->op_info->flags & AML_CREATE) {
		/*
		 * Backup to beginning of create_xXXfield declaration
		 * body_length is unknown until we parse the body
		 */
		op->named.data = aml_op_start;
		op->named.length = 0;
	}

	if (walk_state->opcode == AML_BANK_FIELD_OP) {
		/*
		 * Backup to beginning of bank_field declaration
		 * body_length is unknown until we parse the body
		 */
		op->named.data = aml_op_start;
		op->named.length = 0;
	}

	parent_scope = acpi_ps_get_parent_scope(&(walk_state->parser_state));
	acpi_ps_append_arg(parent_scope, op);

	if (parent_scope) {
		op_info =
		    acpi_ps_get_opcode_info(parent_scope->common.aml_opcode);
		if (op_info->flags & AML_HAS_TARGET) {
			argument_count =
			    acpi_ps_get_argument_count(op_info->type);
			if (parent_scope->common.arg_list_length >
			    argument_count) {
				op->common.flags |= ACPI_PARSEOP_TARGET;
			}
		} else if (parent_scope->common.aml_opcode == AML_INCREMENT_OP) {
			op->common.flags |= ACPI_PARSEOP_TARGET;
		}
	}

	if (walk_state->descending_callback != NULL) {
		/*
		 * Find the object. This will either insert the object into
		 * the namespace or simply look it up
		 */
		walk_state->op = *new_op = op;

		status = walk_state->descending_callback(walk_state, &op);
		status = acpi_ps_next_parse_state(walk_state, op, status);
		if (status == AE_CTRL_PENDING) {
			status = AE_CTRL_PARSE_PENDING;
		}
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_arguments
 *
 * PARAMETERS:  walk_state          - Current state
 *              aml_op_start        - Op start in AML
 *              Op                  - Current Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get arguments for passed Op.
 *
 ******************************************************************************/

static acpi_status
acpi_ps_get_arguments(struct acpi_walk_state *walk_state,
		      u8 * aml_op_start, union acpi_parse_object *op)
{
	acpi_status status = AE_OK;
	union acpi_parse_object *arg = NULL;
	const struct acpi_opcode_info *op_info;

	ACPI_FUNCTION_TRACE_PTR(ps_get_arguments, walk_state);

	switch (op->common.aml_opcode) {
	case AML_BYTE_OP:	/* AML_BYTEDATA_ARG */
	case AML_WORD_OP:	/* AML_WORDDATA_ARG */
	case AML_DWORD_OP:	/* AML_DWORDATA_ARG */
	case AML_QWORD_OP:	/* AML_QWORDATA_ARG */
	case AML_STRING_OP:	/* AML_ASCIICHARLIST_ARG */

		/* Fill in constant or string argument directly */

		acpi_ps_get_next_simple_arg(&(walk_state->parser_state),
					    GET_CURRENT_ARG_TYPE(walk_state->
								 arg_types),
					    op);
		break;

	case AML_INT_NAMEPATH_OP:	/* AML_NAMESTRING_ARG */

		status =
		    acpi_ps_get_next_namepath(walk_state,
					      &(walk_state->parser_state), op,
					      1);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		walk_state->arg_types = 0;
		break;

	default:
		/*
		 * Op is not a constant or string, append each argument to the Op
		 */
		while (GET_CURRENT_ARG_TYPE(walk_state->arg_types)
		       && !walk_state->arg_count) {
			walk_state->aml_offset =
			    (u32) ACPI_PTR_DIFF(walk_state->parser_state.aml,
						walk_state->parser_state.
						aml_start);

			status =
			    acpi_ps_get_next_arg(walk_state,
						 &(walk_state->parser_state),
						 GET_CURRENT_ARG_TYPE
						 (walk_state->arg_types), &arg);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			if (arg) {
				arg->common.aml_offset = walk_state->aml_offset;
				acpi_ps_append_arg(op, arg);
			}

			INCREMENT_ARG_LIST(walk_state->arg_types);
		}

		/*
		 * Handle executable code at "module-level". This refers to
		 * executable opcodes that appear outside of any control method.
		 */
		if ((walk_state->pass_number <= ACPI_IMODE_LOAD_PASS2) &&
		    ((walk_state->parse_flags & ACPI_PARSE_DISASSEMBLE) == 0)) {
			/*
			 * We want to skip If/Else/While constructs during Pass1 because we
			 * want to actually conditionally execute the code during Pass2.
			 *
			 * Except for disassembly, where we always want to walk the
			 * If/Else/While packages
			 */
			switch (op->common.aml_opcode) {
			case AML_IF_OP:
			case AML_ELSE_OP:
			case AML_WHILE_OP:

				/*
				 * Currently supported module-level opcodes are:
				 * IF/ELSE/WHILE. These appear to be the most common,
				 * and easiest to support since they open an AML
				 * package.
				 */
				if (walk_state->pass_number ==
				    ACPI_IMODE_LOAD_PASS1) {
					acpi_ps_link_module_code(op->common.
								 parent,
								 aml_op_start,
								 (u32)
								 (walk_state->
								 parser_state.
								 pkg_end -
								 aml_op_start),
								 walk_state->
								 owner_id);
				}

				ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
						  "Pass1: Skipping an If/Else/While body\n"));

				/* Skip body of if/else/while in pass 1 */

				walk_state->parser_state.aml =
				    walk_state->parser_state.pkg_end;
				walk_state->arg_count = 0;
				break;

			default:
				/*
				 * Check for an unsupported executable opcode at module
				 * level. We must be in PASS1, the parent must be a SCOPE,
				 * The opcode class must be EXECUTE, and the opcode must
				 * not be an argument to another opcode.
				 */
				if ((walk_state->pass_number ==
				     ACPI_IMODE_LOAD_PASS1)
				    && (op->common.parent->common.aml_opcode ==
					AML_SCOPE_OP)) {
					op_info =
					    acpi_ps_get_opcode_info(op->common.
								    aml_opcode);
					if ((op_info->class ==
					     AML_CLASS_EXECUTE) && (!arg)) {
						ACPI_WARNING((AE_INFO,
							      "Detected an unsupported executable opcode "
							      "at module-level: [0x%.4X] at table offset 0x%.4X",
							      op->common.aml_opcode,
							      (u32)((aml_op_start - walk_state->parser_state.aml_start)
								+ sizeof(struct acpi_table_header))));
					}
				}
				break;
			}
		}

		/* Special processing for certain opcodes */

		switch (op->common.aml_opcode) {
		case AML_METHOD_OP:
			/*
			 * Skip parsing of control method because we don't have enough
			 * info in the first pass to parse it correctly.
			 *
			 * Save the length and address of the body
			 */
			op->named.data = walk_state->parser_state.aml;
			op->named.length = (u32)
			    (walk_state->parser_state.pkg_end -
			     walk_state->parser_state.aml);

			/* Skip body of method */

			walk_state->parser_state.aml =
			    walk_state->parser_state.pkg_end;
			walk_state->arg_count = 0;
			break;

		case AML_BUFFER_OP:
		case AML_PACKAGE_OP:
		case AML_VAR_PACKAGE_OP:

			if ((op->common.parent) &&
			    (op->common.parent->common.aml_opcode ==
			     AML_NAME_OP)
			    && (walk_state->pass_number <=
				ACPI_IMODE_LOAD_PASS2)) {
				/*
				 * Skip parsing of Buffers and Packages because we don't have
				 * enough info in the first pass to parse them correctly.
				 */
				op->named.data = aml_op_start;
				op->named.length = (u32)
				    (walk_state->parser_state.pkg_end -
				     aml_op_start);

				/* Skip body */

				walk_state->parser_state.aml =
				    walk_state->parser_state.pkg_end;
				walk_state->arg_count = 0;
			}
			break;

		case AML_WHILE_OP:

			if (walk_state->control_state) {
				walk_state->control_state->control.package_end =
				    walk_state->parser_state.pkg_end;
			}
			break;

		default:

			/* No action for all other opcodes */
			break;
		}

		break;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_link_module_code
 *
 * PARAMETERS:  parent_op           - Parent parser op
 *              aml_start           - Pointer to the AML
 *              aml_length          - Length of executable AML
 *              owner_id            - owner_id of module level code
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Wrap the module-level code with a method object and link the
 *              object to the global list. Note, the mutex field of the method
 *              object is used to link multiple module-level code objects.
 *
 ******************************************************************************/

static void
acpi_ps_link_module_code(union acpi_parse_object *parent_op,
			 u8 *aml_start, u32 aml_length, acpi_owner_id owner_id)
{
	union acpi_operand_object *prev;
	union acpi_operand_object *next;
	union acpi_operand_object *method_obj;
	struct acpi_namespace_node *parent_node;

	/* Get the tail of the list */

	prev = next = acpi_gbl_module_code_list;
	while (next) {
		prev = next;
		next = next->method.mutex;
	}

	/*
	 * Insert the module level code into the list. Merge it if it is
	 * adjacent to the previous element.
	 */
	if (!prev ||
	    ((prev->method.aml_start + prev->method.aml_length) != aml_start)) {

		/* Create, initialize, and link a new temporary method object */

		method_obj = acpi_ut_create_internal_object(ACPI_TYPE_METHOD);
		if (!method_obj) {
			return;
		}

		if (parent_op->common.node) {
			parent_node = parent_op->common.node;
		} else {
			parent_node = acpi_gbl_root_node;
		}

		method_obj->method.aml_start = aml_start;
		method_obj->method.aml_length = aml_length;
		method_obj->method.owner_id = owner_id;
		method_obj->method.flags |= AOPOBJ_MODULE_LEVEL;

		/*
		 * Save the parent node in next_object. This is cheating, but we
		 * don't want to expand the method object.
		 */
		method_obj->method.next_object =
		    ACPI_CAST_PTR(union acpi_operand_object, parent_node);

		if (!prev) {
			acpi_gbl_module_code_list = method_obj;
		} else {
			prev->method.mutex = method_obj;
		}
	} else {
		prev->method.aml_length += aml_length;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_complete_op
 *
 * PARAMETERS:  walk_state          - Current state
 *              Op                  - Returned Op
 *              Status              - Parse status before complete Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Complete Op
 *
 ******************************************************************************/

static acpi_status
acpi_ps_complete_op(struct acpi_walk_state *walk_state,
		    union acpi_parse_object **op, acpi_status status)
{
	acpi_status status2;

	ACPI_FUNCTION_TRACE_PTR(ps_complete_op, walk_state);

	/*
	 * Finished one argument of the containing scope
	 */
	walk_state->parser_state.scope->parse_scope.arg_count--;

	/* Close this Op (will result in parse subtree deletion) */

	status2 = acpi_ps_complete_this_op(walk_state, *op);
	if (ACPI_FAILURE(status2)) {
		return_ACPI_STATUS(status2);
	}

	*op = NULL;

	switch (status) {
	case AE_OK:
		break;

	case AE_CTRL_TRANSFER:

		/* We are about to transfer to a called method */

		walk_state->prev_op = NULL;
		walk_state->prev_arg_types = walk_state->arg_types;
		return_ACPI_STATUS(status);

	case AE_CTRL_END:

		acpi_ps_pop_scope(&(walk_state->parser_state), op,
				  &walk_state->arg_types,
				  &walk_state->arg_count);

		if (*op) {
			walk_state->op = *op;
			walk_state->op_info =
			    acpi_ps_get_opcode_info((*op)->common.aml_opcode);
			walk_state->opcode = (*op)->common.aml_opcode;

			status = walk_state->ascending_callback(walk_state);
			status =
			    acpi_ps_next_parse_state(walk_state, *op, status);

			status2 = acpi_ps_complete_this_op(walk_state, *op);
			if (ACPI_FAILURE(status2)) {
				return_ACPI_STATUS(status2);
			}
		}

		status = AE_OK;
		break;

	case AE_CTRL_BREAK:
	case AE_CTRL_CONTINUE:

		/* Pop off scopes until we find the While */

		while (!(*op) || ((*op)->common.aml_opcode != AML_WHILE_OP)) {
			acpi_ps_pop_scope(&(walk_state->parser_state), op,
					  &walk_state->arg_types,
					  &walk_state->arg_count);
		}

		/* Close this iteration of the While loop */

		walk_state->op = *op;
		walk_state->op_info =
		    acpi_ps_get_opcode_info((*op)->common.aml_opcode);
		walk_state->opcode = (*op)->common.aml_opcode;

		status = walk_state->ascending_callback(walk_state);
		status = acpi_ps_next_parse_state(walk_state, *op, status);

		status2 = acpi_ps_complete_this_op(walk_state, *op);
		if (ACPI_FAILURE(status2)) {
			return_ACPI_STATUS(status2);
		}

		status = AE_OK;
		break;

	case AE_CTRL_TERMINATE:

		/* Clean up */
		do {
			if (*op) {
				status2 =
				    acpi_ps_complete_this_op(walk_state, *op);
				if (ACPI_FAILURE(status2)) {
					return_ACPI_STATUS(status2);
				}

				acpi_ut_delete_generic_state
				    (acpi_ut_pop_generic_state
				     (&walk_state->control_state));
			}

			acpi_ps_pop_scope(&(walk_state->parser_state), op,
					  &walk_state->arg_types,
					  &walk_state->arg_count);

		} while (*op);

		return_ACPI_STATUS(AE_OK);

	default:		/* All other non-AE_OK status */

		do {
			if (*op) {
				status2 =
				    acpi_ps_complete_this_op(walk_state, *op);
				if (ACPI_FAILURE(status2)) {
					return_ACPI_STATUS(status2);
				}
			}

			acpi_ps_pop_scope(&(walk_state->parser_state), op,
					  &walk_state->arg_types,
					  &walk_state->arg_count);

		} while (*op);

#if 0
		/*
		 * TBD: Cleanup parse ops on error
		 */
		if (*op == NULL) {
			acpi_ps_pop_scope(parser_state, op,
					  &walk_state->arg_types,
					  &walk_state->arg_count);
		}
#endif
		walk_state->prev_op = NULL;
		walk_state->prev_arg_types = walk_state->arg_types;
		return_ACPI_STATUS(status);
	}

	/* This scope complete? */

	if (acpi_ps_has_completed_scope(&(walk_state->parser_state))) {
		acpi_ps_pop_scope(&(walk_state->parser_state), op,
				  &walk_state->arg_types,
				  &walk_state->arg_count);
		ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "Popped scope, Op=%p\n", *op));
	} else {
		*op = NULL;
	}

	ACPI_PREEMPTION_POINT();

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_complete_final_op
 *
 * PARAMETERS:  walk_state          - Current state
 *              Op                  - Current Op
 *              Status              - Current parse status before complete last
 *                                    Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Complete last Op.
 *
 ******************************************************************************/

static acpi_status
acpi_ps_complete_final_op(struct acpi_walk_state *walk_state,
			  union acpi_parse_object *op, acpi_status status)
{
	acpi_status status2;

	ACPI_FUNCTION_TRACE_PTR(ps_complete_final_op, walk_state);

	/*
	 * Complete the last Op (if not completed), and clear the scope stack.
	 * It is easily possible to end an AML "package" with an unbounded number
	 * of open scopes (such as when several ASL blocks are closed with
	 * sequential closing braces). We want to terminate each one cleanly.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "AML package complete at Op %p\n",
			  op));
	do {
		if (op) {
			if (walk_state->ascending_callback != NULL) {
				walk_state->op = op;
				walk_state->op_info =
				    acpi_ps_get_opcode_info(op->common.
							    aml_opcode);
				walk_state->opcode = op->common.aml_opcode;

				status =
				    walk_state->ascending_callback(walk_state);
				status =
				    acpi_ps_next_parse_state(walk_state, op,
							     status);
				if (status == AE_CTRL_PENDING) {
					status =
					    acpi_ps_complete_op(walk_state, &op,
								AE_OK);
					if (ACPI_FAILURE(status)) {
						return_ACPI_STATUS(status);
					}
				}

				if (status == AE_CTRL_TERMINATE) {
					status = AE_OK;

					/* Clean up */
					do {
						if (op) {
							status2 =
							    acpi_ps_complete_this_op
							    (walk_state, op);
							if (ACPI_FAILURE
							    (status2)) {
								return_ACPI_STATUS
								    (status2);
							}
						}

						acpi_ps_pop_scope(&
								  (walk_state->
								   parser_state),
								  &op,
								  &walk_state->
								  arg_types,
								  &walk_state->
								  arg_count);

					} while (op);

					return_ACPI_STATUS(status);
				}

				else if (ACPI_FAILURE(status)) {

					/* First error is most important */

					(void)
					    acpi_ps_complete_this_op(walk_state,
								     op);
					return_ACPI_STATUS(status);
				}
			}

			status2 = acpi_ps_complete_this_op(walk_state, op);
			if (ACPI_FAILURE(status2)) {
				return_ACPI_STATUS(status2);
			}
		}

		acpi_ps_pop_scope(&(walk_state->parser_state), &op,
				  &walk_state->arg_types,
				  &walk_state->arg_count);

	} while (op);

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_parse_loop
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse AML (pointed to by the current parser state) and return
 *              a tree of ops.
 *
 ******************************************************************************/

acpi_status acpi_ps_parse_loop(struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_parse_object *op = NULL;	/* current op */
	struct acpi_parse_state *parser_state;
	u8 *aml_op_start = NULL;

	ACPI_FUNCTION_TRACE_PTR(ps_parse_loop, walk_state);

	if (walk_state->descending_callback == NULL) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	parser_state = &walk_state->parser_state;
	walk_state->arg_types = 0;

#if (!defined (ACPI_NO_METHOD_EXECUTION) && !defined (ACPI_CONSTANT_EVAL_ONLY))

	if (walk_state->walk_type & ACPI_WALK_METHOD_RESTART) {

		/* We are restarting a preempted control method */

		if (acpi_ps_has_completed_scope(parser_state)) {
			/*
			 * We must check if a predicate to an IF or WHILE statement
			 * was just completed
			 */
			if ((parser_state->scope->parse_scope.op) &&
			    ((parser_state->scope->parse_scope.op->common.
			      aml_opcode == AML_IF_OP)
			     || (parser_state->scope->parse_scope.op->common.
				 aml_opcode == AML_WHILE_OP))
			    && (walk_state->control_state)
			    && (walk_state->control_state->common.state ==
				ACPI_CONTROL_PREDICATE_EXECUTING)) {
				/*
				 * A predicate was just completed, get the value of the
				 * predicate and branch based on that value
				 */
				walk_state->op = NULL;
				status =
				    acpi_ds_get_predicate_value(walk_state,
								ACPI_TO_POINTER
								(TRUE));
				if (ACPI_FAILURE(status)
				    && ((status & AE_CODE_MASK) !=
					AE_CODE_CONTROL)) {
					if (status == AE_AML_NO_RETURN_VALUE) {
						ACPI_EXCEPTION((AE_INFO, status,
								"Invoked method did not return a value"));
					}

					ACPI_EXCEPTION((AE_INFO, status,
							"GetPredicate Failed"));
					return_ACPI_STATUS(status);
				}

				status =
				    acpi_ps_next_parse_state(walk_state, op,
							     status);
			}

			acpi_ps_pop_scope(parser_state, &op,
					  &walk_state->arg_types,
					  &walk_state->arg_count);
			ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
					  "Popped scope, Op=%p\n", op));
		} else if (walk_state->prev_op) {

			/* We were in the middle of an op */

			op = walk_state->prev_op;
			walk_state->arg_types = walk_state->prev_arg_types;
		}
	}
#endif

	/* Iterative parsing loop, while there is more AML to process: */

	while ((parser_state->aml < parser_state->aml_end) || (op)) {
		aml_op_start = parser_state->aml;
		if (!op) {
			status =
			    acpi_ps_create_op(walk_state, aml_op_start, &op);
			if (ACPI_FAILURE(status)) {
				if (status == AE_CTRL_PARSE_CONTINUE) {
					continue;
				}

				if (status == AE_CTRL_PARSE_PENDING) {
					status = AE_OK;
				}

				status =
				    acpi_ps_complete_op(walk_state, &op,
							status);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}

				continue;
			}

			op->common.aml_offset = walk_state->aml_offset;

			if (walk_state->op_info) {
				ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
						  "Opcode %4.4X [%s] Op %p Aml %p AmlOffset %5.5X\n",
						  (u32) op->common.aml_opcode,
						  walk_state->op_info->name, op,
						  parser_state->aml,
						  op->common.aml_offset));
			}
		}

		/*
		 * Start arg_count at zero because we don't know if there are
		 * any args yet
		 */
		walk_state->arg_count = 0;

		/* Are there any arguments that must be processed? */

		if (walk_state->arg_types) {

			/* Get arguments */

			status =
			    acpi_ps_get_arguments(walk_state, aml_op_start, op);
			if (ACPI_FAILURE(status)) {
				status =
				    acpi_ps_complete_op(walk_state, &op,
							status);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}

				continue;
			}
		}

		/* Check for arguments that need to be processed */

		if (walk_state->arg_count) {
			/*
			 * There are arguments (complex ones), push Op and
			 * prepare for argument
			 */
			status = acpi_ps_push_scope(parser_state, op,
						    walk_state->arg_types,
						    walk_state->arg_count);
			if (ACPI_FAILURE(status)) {
				status =
				    acpi_ps_complete_op(walk_state, &op,
							status);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}

				continue;
			}

			op = NULL;
			continue;
		}

		/*
		 * All arguments have been processed -- Op is complete,
		 * prepare for next
		 */
		walk_state->op_info =
		    acpi_ps_get_opcode_info(op->common.aml_opcode);
		if (walk_state->op_info->flags & AML_NAMED) {
			if (acpi_gbl_depth) {
				acpi_gbl_depth--;
			}

			if (op->common.aml_opcode == AML_REGION_OP ||
			    op->common.aml_opcode == AML_DATA_REGION_OP) {
				/*
				 * Skip parsing of control method or opregion body,
				 * because we don't have enough info in the first pass
				 * to parse them correctly.
				 *
				 * Completed parsing an op_region declaration, we now
				 * know the length.
				 */
				op->named.length =
				    (u32) (parser_state->aml - op->named.data);
			}
		}

		if (walk_state->op_info->flags & AML_CREATE) {
			/*
			 * Backup to beginning of create_xXXfield declaration (1 for
			 * Opcode)
			 *
			 * body_length is unknown until we parse the body
			 */
			op->named.length =
			    (u32) (parser_state->aml - op->named.data);
		}

		if (op->common.aml_opcode == AML_BANK_FIELD_OP) {
			/*
			 * Backup to beginning of bank_field declaration
			 *
			 * body_length is unknown until we parse the body
			 */
			op->named.length =
			    (u32) (parser_state->aml - op->named.data);
		}

		/* This op complete, notify the dispatcher */

		if (walk_state->ascending_callback != NULL) {
			walk_state->op = op;
			walk_state->opcode = op->common.aml_opcode;

			status = walk_state->ascending_callback(walk_state);
			status =
			    acpi_ps_next_parse_state(walk_state, op, status);
			if (status == AE_CTRL_PENDING) {
				status = AE_OK;
			}
		}

		status = acpi_ps_complete_op(walk_state, &op, status);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

	}			/* while parser_state->Aml */

	status = acpi_ps_complete_final_op(walk_state, op, status);
	return_ACPI_STATUS(status);
}
