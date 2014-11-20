/******************************************************************************
 *
 * Module Name: psobject - Support for parse objects
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psobject")

/* Local prototypes */
static acpi_status acpi_ps_get_aml_opcode(struct acpi_walk_state *walk_state);

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
	    (u32)ACPI_PTR_DIFF(walk_state->parser_state.aml,
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

		/* The opcode is unrecognized. Complain and skip unknown opcodes */

		if (walk_state->pass_number == 2) {
			ACPI_ERROR((AE_INFO,
				    "Unknown opcode 0x%.2X at table offset 0x%.4X, ignoring",
				    walk_state->opcode,
				    (u32)(walk_state->aml_offset +
					  sizeof(struct acpi_table_header))));

			ACPI_DUMP_BUFFER((walk_state->parser_state.aml - 16),
					 48);

#ifdef ACPI_ASL_COMPILER
			/*
			 * This is executed for the disassembler only. Output goes
			 * to the disassembled ASL output file.
			 */
			acpi_os_printf
			    ("/*\nError: Unknown opcode 0x%.2X at table offset 0x%.4X, context:\n",
			     walk_state->opcode,
			     (u32)(walk_state->aml_offset +
				   sizeof(struct acpi_table_header)));

			/* Dump the context surrounding the invalid opcode */

			acpi_ut_dump_buffer(((u8 *)walk_state->parser_state.
					     aml - 16), 48, DB_BYTE_DISPLAY,
					    (walk_state->aml_offset +
					     sizeof(struct acpi_table_header) -
					     16));
			acpi_os_printf(" */\n");
#endif
		}

		/* Increment past one-byte or two-byte opcode */

		walk_state->parser_state.aml++;
		if (walk_state->opcode > 0xFF) {	/* Can only happen if first byte is 0x5B */
			walk_state->parser_state.aml++;
		}

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
 *              op                  - Returned Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse a named Op
 *
 ******************************************************************************/

acpi_status
acpi_ps_build_named_op(struct acpi_walk_state *walk_state,
		       u8 *aml_op_start,
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
		if (status != AE_CTRL_TERMINATE) {
			ACPI_EXCEPTION((AE_INFO, status,
					"During name lookup/catalog"));
		}
		return_ACPI_STATUS(status);
	}

	if (!*op) {
		return_ACPI_STATUS(AE_CTRL_PARSE_CONTINUE);
	}

	status = acpi_ps_next_parse_state(walk_state, *op, status);
	if (ACPI_FAILURE(status)) {
		if (status == AE_CTRL_PENDING) {
			status = AE_CTRL_PARSE_PENDING;
		}
		return_ACPI_STATUS(status);
	}

	acpi_ps_append_arg(*op, unnamed_op->common.value.arg);

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

acpi_status
acpi_ps_create_op(struct acpi_walk_state *walk_state,
		  u8 *aml_op_start, union acpi_parse_object **new_op)
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
		 * Backup to beginning of create_XXXfield declaration
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
 * FUNCTION:    acpi_ps_complete_op
 *
 * PARAMETERS:  walk_state          - Current state
 *              op                  - Returned Op
 *              status              - Parse status before complete Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Complete Op
 *
 ******************************************************************************/

acpi_status
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

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_complete_final_op
 *
 * PARAMETERS:  walk_state          - Current state
 *              op                  - Current Op
 *              status              - Current parse status before complete last
 *                                    Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Complete last Op.
 *
 ******************************************************************************/

acpi_status
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
