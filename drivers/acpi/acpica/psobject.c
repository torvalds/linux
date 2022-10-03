// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: psobject - Support for parse objects
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "amlcode.h"
#include "acconvert.h"
#include "acnamesp.h"

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
	ACPI_ERROR_ONLY(u32 aml_offset);

	ACPI_FUNCTION_TRACE_PTR(ps_get_aml_opcode, walk_state);

	walk_state->aml = walk_state->parser_state.aml;
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
			ACPI_ERROR_ONLY(aml_offset =
					(u32)ACPI_PTR_DIFF(walk_state->aml,
							   walk_state->
							   parser_state.
							   aml_start));

			ACPI_ERROR((AE_INFO,
				    "Unknown opcode 0x%.2X at table offset 0x%.4X, ignoring",
				    walk_state->opcode,
				    (u32)(aml_offset +
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
			     (u32)(aml_offset +
				   sizeof(struct acpi_table_header)));

			ACPI_ERROR((AE_INFO,
				    "Aborting disassembly, AML byte code is corrupt"));

			/* Dump the context surrounding the invalid opcode */

			acpi_ut_dump_buffer(((u8 *)walk_state->parser_state.
					     aml - 16), 48, DB_BYTE_DISPLAY,
					    (aml_offset +
					     sizeof(struct acpi_table_header) -
					     16));
			acpi_os_printf(" */\n");

			/*
			 * Just abort the disassembly, cannot continue because the
			 * parser is essentially lost. The disassembler can then
			 * randomly fail because an ill-constructed parse tree
			 * can result.
			 */
			return_ACPI_STATUS(AE_AML_BAD_OPCODE);
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
		ASL_CV_CAPTURE_COMMENTS(walk_state);
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

	/* are there any inline comments associated with the name_seg?? If so, save this. */

	ASL_CV_CAPTURE_COMMENTS(walk_state);

#ifdef ACPI_ASL_COMPILER
	if (acpi_gbl_current_inline_comment != NULL) {
		unnamed_op->common.name_comment =
		    acpi_gbl_current_inline_comment;
		acpi_gbl_current_inline_comment = NULL;
	}
#endif

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

#ifdef ACPI_ASL_COMPILER

	/* save any comments that might be associated with unnamed_op. */

	(*op)->common.inline_comment = unnamed_op->common.inline_comment;
	(*op)->common.end_node_comment = unnamed_op->common.end_node_comment;
	(*op)->common.close_brace_comment =
	    unnamed_op->common.close_brace_comment;
	(*op)->common.name_comment = unnamed_op->common.name_comment;
	(*op)->common.comment_list = unnamed_op->common.comment_list;
	(*op)->common.end_blk_comment = unnamed_op->common.end_blk_comment;
	(*op)->common.cv_filename = unnamed_op->common.cv_filename;
	(*op)->common.cv_parent_filename =
	    unnamed_op->common.cv_parent_filename;
	(*op)->named.aml = unnamed_op->common.aml;

	unnamed_op->common.inline_comment = NULL;
	unnamed_op->common.end_node_comment = NULL;
	unnamed_op->common.close_brace_comment = NULL;
	unnamed_op->common.name_comment = NULL;
	unnamed_op->common.comment_list = NULL;
	unnamed_op->common.end_blk_comment = NULL;
#endif

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
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Create Op structure and append to parent's argument list */

	walk_state->op_info = acpi_ps_get_opcode_info(walk_state->opcode);
	op = acpi_ps_alloc_op(walk_state->opcode, aml_op_start);
	if (!op) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	if (walk_state->op_info->flags & AML_NAMED) {
		status =
		    acpi_ps_build_named_op(walk_state, aml_op_start, op,
					   &named_op);
		acpi_ps_free_op(op);

#ifdef ACPI_ASL_COMPILER
		if (acpi_gbl_disasm_flag
		    && walk_state->opcode == AML_EXTERNAL_OP
		    && status == AE_NOT_FOUND) {
			/*
			 * If parsing of AML_EXTERNAL_OP's name path fails, then skip
			 * past this opcode and keep parsing. This is a much better
			 * alternative than to abort the entire disassembler. At this
			 * point, the parser_state is at the end of the namepath of the
			 * external declaration opcode. Setting walk_state->Aml to
			 * walk_state->parser_state.Aml + 2 moves increments the
			 * walk_state->Aml past the object type and the paramcount of the
			 * external opcode.
			 */
			walk_state->aml = walk_state->parser_state.aml + 2;
			walk_state->parser_state.aml = walk_state->aml;
			return_ACPI_STATUS(AE_CTRL_PARSE_CONTINUE);
		}
#endif
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
		}

		/*
		 * Special case for both Increment() and Decrement(), where
		 * the lone argument is both a source and a target.
		 */
		else if ((parent_scope->common.aml_opcode == AML_INCREMENT_OP)
			 || (parent_scope->common.aml_opcode ==
			     AML_DECREMENT_OP)) {
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
			(void)acpi_ps_next_parse_state(walk_state, *op, status);

			status2 = acpi_ps_complete_this_op(walk_state, *op);
			if (ACPI_FAILURE(status2)) {
				return_ACPI_STATUS(status2);
			}
		}

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
		(void)acpi_ps_next_parse_state(walk_state, *op, status);

		status2 = acpi_ps_complete_this_op(walk_state, *op);
		if (ACPI_FAILURE(status2)) {
			return_ACPI_STATUS(status2);
		}

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
				/*
				 * These Opcodes need to be removed from the namespace because they
				 * get created even if these opcodes cannot be created due to
				 * errors.
				 */
				if (((*op)->common.aml_opcode == AML_REGION_OP)
				    || ((*op)->common.aml_opcode ==
					AML_DATA_REGION_OP)) {
					acpi_ns_delete_children((*op)->common.
								node);
					acpi_ns_remove_node((*op)->common.node);
					(*op)->common.node = NULL;
					acpi_ps_delete_parse_tree(*op);
				}

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

		if (walk_state->parse_flags & ACPI_PARSE_MODULE_LEVEL) {
			/*
			 * There was something that went wrong while executing code at the
			 * module-level. We need to skip parsing whatever caused the
			 * error and keep going. One runtime error during the table load
			 * should not cause the entire table to not be loaded. This is
			 * because there could be correct AML beyond the parts that caused
			 * the runtime error.
			 */
			ACPI_INFO(("Ignoring error and continuing table load"));
			return_ACPI_STATUS(AE_OK);
		}
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
