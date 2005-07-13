/******************************************************************************
 *
 * Module Name: psparse - Parser top level AML parse routines
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


/*
 * Parse the AML and build an operation tree as most interpreters,
 * like Perl, do.  Parsing is done by hand rather than with a YACC
 * generated parser to tightly constrain stack and dynamic memory
 * usage.  At the same time, parsing is kept flexible and the code
 * fairly compact by parsing based on a list of AML opcode
 * templates in aml_op_info[]
 */

#include <acpi/acpi.h>
#include <acpi/acparser.h>
#include <acpi/acdispat.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>
#include <acpi/acinterp.h>

#define _COMPONENT          ACPI_PARSER
	 ACPI_MODULE_NAME    ("psparse")


static u32                          acpi_gbl_depth = 0;

/* Local prototypes */

static void
acpi_ps_complete_this_op (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op);

static acpi_status
acpi_ps_next_parse_state (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op,
	acpi_status                     callback_status);

static acpi_status
acpi_ps_parse_loop (
	struct acpi_walk_state          *walk_state);


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_opcode_size
 *
 * PARAMETERS:  Opcode          - An AML opcode
 *
 * RETURN:      Size of the opcode, in bytes (1 or 2)
 *
 * DESCRIPTION: Get the size of the current opcode.
 *
 ******************************************************************************/

u32
acpi_ps_get_opcode_size (
	u32                             opcode)
{

	/* Extended (2-byte) opcode if > 255 */

	if (opcode > 0x00FF) {
		return (2);
	}

	/* Otherwise, just a single byte opcode */

	return (1);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_peek_opcode
 *
 * PARAMETERS:  parser_state        - A parser state object
 *
 * RETURN:      Next AML opcode
 *
 * DESCRIPTION: Get next AML opcode (without incrementing AML pointer)
 *
 ******************************************************************************/

u16
acpi_ps_peek_opcode (
	struct acpi_parse_state         *parser_state)
{
	u8                              *aml;
	u16                             opcode;


	aml = parser_state->aml;
	opcode = (u16) ACPI_GET8 (aml);

	if (opcode == AML_EXTOP) {
		/* Extended opcode */

		aml++;
		opcode = (u16) ((opcode << 8) | ACPI_GET8 (aml));
	}

	return (opcode);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_complete_this_op
 *
 * PARAMETERS:  walk_state      - Current State
 *              Op              - Op to complete
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Perform any cleanup at the completion of an Op.
 *
 ******************************************************************************/

static void
acpi_ps_complete_this_op (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op)
{
	union acpi_parse_object         *prev;
	union acpi_parse_object         *next;
	const struct acpi_opcode_info   *parent_info;
	union acpi_parse_object         *replacement_op = NULL;


	ACPI_FUNCTION_TRACE_PTR ("ps_complete_this_op", op);


	/* Check for null Op, can happen if AML code is corrupt */

	if (!op) {
		return_VOID;
	}

	/* Delete this op and the subtree below it if asked to */

	if (((walk_state->parse_flags & ACPI_PARSE_TREE_MASK) != ACPI_PARSE_DELETE_TREE) ||
		 (walk_state->op_info->class == AML_CLASS_ARGUMENT)) {
		return_VOID;
	}

	/* Make sure that we only delete this subtree */

	if (op->common.parent) {
		/*
		 * Check if we need to replace the operator and its subtree
		 * with a return value op (placeholder op)
		 */
		parent_info = acpi_ps_get_opcode_info (op->common.parent->common.aml_opcode);

		switch (parent_info->class) {
		case AML_CLASS_CONTROL:
			break;

		case AML_CLASS_CREATE:

			/*
			 * These opcodes contain term_arg operands. The current
			 * op must be replaced by a placeholder return op
			 */
			replacement_op = acpi_ps_alloc_op (AML_INT_RETURN_VALUE_OP);
			if (!replacement_op) {
				goto cleanup;
			}
			break;

		case AML_CLASS_NAMED_OBJECT:

			/*
			 * These opcodes contain term_arg operands. The current
			 * op must be replaced by a placeholder return op
			 */
			if ((op->common.parent->common.aml_opcode == AML_REGION_OP)      ||
				(op->common.parent->common.aml_opcode == AML_DATA_REGION_OP) ||
				(op->common.parent->common.aml_opcode == AML_BUFFER_OP)      ||
				(op->common.parent->common.aml_opcode == AML_PACKAGE_OP)     ||
				(op->common.parent->common.aml_opcode == AML_VAR_PACKAGE_OP)) {
				replacement_op = acpi_ps_alloc_op (AML_INT_RETURN_VALUE_OP);
				if (!replacement_op) {
					goto cleanup;
				}
			}

			if ((op->common.parent->common.aml_opcode == AML_NAME_OP) &&
				(walk_state->descending_callback != acpi_ds_exec_begin_op)) {
				if ((op->common.aml_opcode == AML_BUFFER_OP) ||
					(op->common.aml_opcode == AML_PACKAGE_OP) ||
					(op->common.aml_opcode == AML_VAR_PACKAGE_OP)) {
					replacement_op = acpi_ps_alloc_op (op->common.aml_opcode);
					if (!replacement_op) {
						goto cleanup;
					}

					replacement_op->named.data = op->named.data;
					replacement_op->named.length = op->named.length;
				}
			}
			break;

		default:
			replacement_op = acpi_ps_alloc_op (AML_INT_RETURN_VALUE_OP);
			if (!replacement_op) {
				goto cleanup;
			}
		}

		/* We must unlink this op from the parent tree */

		prev = op->common.parent->common.value.arg;
		if (prev == op) {
			/* This op is the first in the list */

			if (replacement_op) {
				replacement_op->common.parent       = op->common.parent;
				replacement_op->common.value.arg    = NULL;
				replacement_op->common.node         = op->common.node;
				op->common.parent->common.value.arg = replacement_op;
				replacement_op->common.next         = op->common.next;
			}
			else {
				op->common.parent->common.value.arg = op->common.next;
			}
		}

		/* Search the parent list */

		else while (prev) {
			/* Traverse all siblings in the parent's argument list */

			next = prev->common.next;
			if (next == op) {
				if (replacement_op) {
					replacement_op->common.parent   = op->common.parent;
					replacement_op->common.value.arg = NULL;
					replacement_op->common.node     = op->common.node;
					prev->common.next               = replacement_op;
					replacement_op->common.next     = op->common.next;
					next = NULL;
				}
				else {
					prev->common.next = op->common.next;
					next = NULL;
				}
			}
			prev = next;
		}
	}


cleanup:

	/* Now we can actually delete the subtree rooted at Op */

	acpi_ps_delete_parse_tree (op);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_next_parse_state
 *
 * PARAMETERS:  walk_state          - Current state
 *              Op                  - Current parse op
 *              callback_status     - Status from previous operation
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Update the parser state based upon the return exception from
 *              the parser callback.
 *
 ******************************************************************************/

static acpi_status
acpi_ps_next_parse_state (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op,
	acpi_status                     callback_status)
{
	struct acpi_parse_state         *parser_state = &walk_state->parser_state;
	acpi_status                     status = AE_CTRL_PENDING;


	ACPI_FUNCTION_TRACE_PTR ("ps_next_parse_state", op);


	switch (callback_status) {
	case AE_CTRL_TERMINATE:

		/*
		 * A control method was terminated via a RETURN statement.
		 * The walk of this method is complete.
		 */
		parser_state->aml = parser_state->aml_end;
		status = AE_CTRL_TERMINATE;
		break;


	case AE_CTRL_BREAK:

		parser_state->aml = walk_state->aml_last_while;
		walk_state->control_state->common.value = FALSE;
		status = AE_CTRL_BREAK;
		break;

	case AE_CTRL_CONTINUE:


		parser_state->aml = walk_state->aml_last_while;
		status = AE_CTRL_CONTINUE;
		break;

	case AE_CTRL_PENDING:

		parser_state->aml = walk_state->aml_last_while;
		break;

#if 0
	case AE_CTRL_SKIP:

		parser_state->aml = parser_state->scope->parse_scope.pkg_end;
		status = AE_OK;
		break;
#endif

	case AE_CTRL_TRUE:

		/*
		 * Predicate of an IF was true, and we are at the matching ELSE.
		 * Just close out this package
		 */
		parser_state->aml = acpi_ps_get_next_package_end (parser_state);
		break;


	case AE_CTRL_FALSE:

		/*
		 * Either an IF/WHILE Predicate was false or we encountered a BREAK
		 * opcode.  In both cases, we do not execute the rest of the
		 * package;  We simply close out the parent (finishing the walk of
		 * this branch of the tree) and continue execution at the parent
		 * level.
		 */
		parser_state->aml = parser_state->scope->parse_scope.pkg_end;

		/* In the case of a BREAK, just force a predicate (if any) to FALSE */

		walk_state->control_state->common.value = FALSE;
		status = AE_CTRL_END;
		break;


	case AE_CTRL_TRANSFER:

		/* A method call (invocation) -- transfer control */

		status = AE_CTRL_TRANSFER;
		walk_state->prev_op = op;
		walk_state->method_call_op = op;
		walk_state->method_call_node = (op->common.value.arg)->common.node;

		/* Will return value (if any) be used by the caller? */

		walk_state->return_used = acpi_ds_is_result_used (op, walk_state);
		break;


	default:

		status = callback_status;
		if ((callback_status & AE_CODE_MASK) == AE_CODE_CONTROL) {
			status = AE_OK;
		}
		break;
	}

	return_ACPI_STATUS (status);
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

static acpi_status
acpi_ps_parse_loop (
	struct acpi_walk_state          *walk_state)
{
	acpi_status                     status = AE_OK;
	union acpi_parse_object         *op = NULL;     /* current op */
	union acpi_parse_object         *arg = NULL;
	union acpi_parse_object         *pre_op = NULL;
	struct acpi_parse_state         *parser_state;
	u8                              *aml_op_start = NULL;


	ACPI_FUNCTION_TRACE_PTR ("ps_parse_loop", walk_state);

	if (walk_state->descending_callback == NULL) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	parser_state = &walk_state->parser_state;
	walk_state->arg_types = 0;

#if (!defined (ACPI_NO_METHOD_EXECUTION) && !defined (ACPI_CONSTANT_EVAL_ONLY))

	if (walk_state->walk_type & ACPI_WALK_METHOD_RESTART) {
		/* We are restarting a preempted control method */

		if (acpi_ps_has_completed_scope (parser_state)) {
			/*
			 * We must check if a predicate to an IF or WHILE statement
			 * was just completed
			 */
			if ((parser_state->scope->parse_scope.op) &&
			   ((parser_state->scope->parse_scope.op->common.aml_opcode == AML_IF_OP) ||
				(parser_state->scope->parse_scope.op->common.aml_opcode == AML_WHILE_OP)) &&
				(walk_state->control_state) &&
				(walk_state->control_state->common.state ==
					ACPI_CONTROL_PREDICATE_EXECUTING)) {
				/*
				 * A predicate was just completed, get the value of the
				 * predicate and branch based on that value
				 */
				walk_state->op = NULL;
				status = acpi_ds_get_predicate_value (walk_state, ACPI_TO_POINTER (TRUE));
				if (ACPI_FAILURE (status) &&
					((status & AE_CODE_MASK) != AE_CODE_CONTROL)) {
					if (status == AE_AML_NO_RETURN_VALUE) {
						ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
							"Invoked method did not return a value, %s\n",
							acpi_format_exception (status)));

					}
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"get_predicate Failed, %s\n",
						acpi_format_exception (status)));
					return_ACPI_STATUS (status);
				}

				status = acpi_ps_next_parse_state (walk_state, op, status);
			}

			acpi_ps_pop_scope (parser_state, &op,
				&walk_state->arg_types, &walk_state->arg_count);
			ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Popped scope, Op=%p\n", op));
		}
		else if (walk_state->prev_op) {
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
			/* Get the next opcode from the AML stream */

			walk_state->aml_offset = (u32) ACPI_PTR_DIFF (parser_state->aml,
					  parser_state->aml_start);
			walk_state->opcode   = acpi_ps_peek_opcode (parser_state);

			/*
			 * First cut to determine what we have found:
			 * 1) A valid AML opcode
			 * 2) A name string
			 * 3) An unknown/invalid opcode
			 */
			walk_state->op_info = acpi_ps_get_opcode_info (walk_state->opcode);
			switch (walk_state->op_info->class) {
			case AML_CLASS_ASCII:
			case AML_CLASS_PREFIX:
				/*
				 * Starts with a valid prefix or ASCII char, this is a name
				 * string.  Convert the bare name string to a namepath.
				 */
				walk_state->opcode = AML_INT_NAMEPATH_OP;
				walk_state->arg_types = ARGP_NAMESTRING;
				break;

			case AML_CLASS_UNKNOWN:

				/* The opcode is unrecognized.  Just skip unknown opcodes */

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Found unknown opcode %X at AML address %p offset %X, ignoring\n",
					walk_state->opcode, parser_state->aml, walk_state->aml_offset));

				ACPI_DUMP_BUFFER (parser_state->aml, 128);

				/* Assume one-byte bad opcode */

				parser_state->aml++;
				continue;

			default:

				/* Found opcode info, this is a normal opcode */

				parser_state->aml += acpi_ps_get_opcode_size (walk_state->opcode);
				walk_state->arg_types = walk_state->op_info->parse_args;
				break;
			}

			/* Create Op structure and append to parent's argument list */

			if (walk_state->op_info->flags & AML_NAMED) {
				/* Allocate a new pre_op if necessary */

				if (!pre_op) {
					pre_op = acpi_ps_alloc_op (walk_state->opcode);
					if (!pre_op) {
						status = AE_NO_MEMORY;
						goto close_this_op;
					}
				}

				pre_op->common.value.arg = NULL;
				pre_op->common.aml_opcode = walk_state->opcode;

				/*
				 * Get and append arguments until we find the node that contains
				 * the name (the type ARGP_NAME).
				 */
				while (GET_CURRENT_ARG_TYPE (walk_state->arg_types) &&
					  (GET_CURRENT_ARG_TYPE (walk_state->arg_types) != ARGP_NAME)) {
					status = acpi_ps_get_next_arg (walk_state, parser_state,
							 GET_CURRENT_ARG_TYPE (walk_state->arg_types), &arg);
					if (ACPI_FAILURE (status)) {
						goto close_this_op;
					}

					acpi_ps_append_arg (pre_op, arg);
					INCREMENT_ARG_LIST (walk_state->arg_types);
				}

				/*
				 * Make sure that we found a NAME and didn't run out of
				 * arguments
				 */
				if (!GET_CURRENT_ARG_TYPE (walk_state->arg_types)) {
					status = AE_AML_NO_OPERAND;
					goto close_this_op;
				}

				/* We know that this arg is a name, move to next arg */

				INCREMENT_ARG_LIST (walk_state->arg_types);

				/*
				 * Find the object.  This will either insert the object into
				 * the namespace or simply look it up
				 */
				walk_state->op = NULL;

				status = walk_state->descending_callback (walk_state, &op);
				if (ACPI_FAILURE (status)) {
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"During name lookup/catalog, %s\n",
						acpi_format_exception (status)));
					goto close_this_op;
				}

				if (!op) {
					continue;
				}

				status = acpi_ps_next_parse_state (walk_state, op, status);
				if (status == AE_CTRL_PENDING) {
					status = AE_OK;
					goto close_this_op;
				}

				if (ACPI_FAILURE (status)) {
					goto close_this_op;
				}

				acpi_ps_append_arg (op, pre_op->common.value.arg);
				acpi_gbl_depth++;

				if (op->common.aml_opcode == AML_REGION_OP) {
					/*
					 * Defer final parsing of an operation_region body,
					 * because we don't have enough info in the first pass
					 * to parse it correctly (i.e., there may be method
					 * calls within the term_arg elements of the body.)
					 *
					 * However, we must continue parsing because
					 * the opregion is not a standalone package --
					 * we don't know where the end is at this point.
					 *
					 * (Length is unknown until parse of the body complete)
					 */
					op->named.data    = aml_op_start;
					op->named.length  = 0;
				}
			}
			else {
				/* Not a named opcode, just allocate Op and append to parent */

				walk_state->op_info = acpi_ps_get_opcode_info (walk_state->opcode);
				op = acpi_ps_alloc_op (walk_state->opcode);
				if (!op) {
					status = AE_NO_MEMORY;
					goto close_this_op;
				}

				if (walk_state->op_info->flags & AML_CREATE) {
					/*
					 * Backup to beginning of create_xXXfield declaration
					 * body_length is unknown until we parse the body
					 */
					op->named.data    = aml_op_start;
					op->named.length  = 0;
				}

				acpi_ps_append_arg (acpi_ps_get_parent_scope (parser_state), op);

				if ((walk_state->descending_callback != NULL)) {
					/*
					 * Find the object. This will either insert the object into
					 * the namespace or simply look it up
					 */
					walk_state->op = op;

					status = walk_state->descending_callback (walk_state, &op);
					status = acpi_ps_next_parse_state (walk_state, op, status);
					if (status == AE_CTRL_PENDING) {
						status = AE_OK;
						goto close_this_op;
					}

					if (ACPI_FAILURE (status)) {
						goto close_this_op;
					}
				}
			}

			op->common.aml_offset = walk_state->aml_offset;

			if (walk_state->op_info) {
				ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
					"Opcode %4.4X [%s] Op %p Aml %p aml_offset %5.5X\n",
					 (u32) op->common.aml_opcode, walk_state->op_info->name,
					 op, parser_state->aml, op->common.aml_offset));
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

			switch (op->common.aml_opcode) {
			case AML_BYTE_OP:       /* AML_BYTEDATA_ARG */
			case AML_WORD_OP:       /* AML_WORDDATA_ARG */
			case AML_DWORD_OP:      /* AML_DWORDATA_ARG */
			case AML_QWORD_OP:      /* AML_QWORDATA_ARG */
			case AML_STRING_OP:     /* AML_ASCIICHARLIST_ARG */

				/* Fill in constant or string argument directly */

				acpi_ps_get_next_simple_arg (parser_state,
					GET_CURRENT_ARG_TYPE (walk_state->arg_types), op);
				break;

			case AML_INT_NAMEPATH_OP:   /* AML_NAMESTRING_ARG */

				status = acpi_ps_get_next_namepath (walk_state, parser_state, op, 1);
				if (ACPI_FAILURE (status)) {
					goto close_this_op;
				}

				walk_state->arg_types = 0;
				break;

			default:

				/*
				 * Op is not a constant or string, append each argument
				 * to the Op
				 */
				while (GET_CURRENT_ARG_TYPE (walk_state->arg_types) &&
						!walk_state->arg_count) {
					walk_state->aml_offset = (u32)
						ACPI_PTR_DIFF (parser_state->aml, parser_state->aml_start);

					status = acpi_ps_get_next_arg (walk_state, parser_state,
							 GET_CURRENT_ARG_TYPE (walk_state->arg_types),
							 &arg);
					if (ACPI_FAILURE (status)) {
						goto close_this_op;
					}

					if (arg) {
						arg->common.aml_offset = walk_state->aml_offset;
						acpi_ps_append_arg (op, arg);
					}
					INCREMENT_ARG_LIST (walk_state->arg_types);
				}

				/* Special processing for certain opcodes */

				switch (op->common.aml_opcode) {
				case AML_METHOD_OP:

					/*
					 * Skip parsing of control method
					 * because we don't have enough info in the first pass
					 * to parse it correctly.
					 *
					 * Save the length and address of the body
					 */
					op->named.data   = parser_state->aml;
					op->named.length = (u32) (parser_state->pkg_end -
							   parser_state->aml);

					/* Skip body of method */

					parser_state->aml   = parser_state->pkg_end;
					walk_state->arg_count = 0;
					break;

				case AML_BUFFER_OP:
				case AML_PACKAGE_OP:
				case AML_VAR_PACKAGE_OP:

					if ((op->common.parent) &&
						(op->common.parent->common.aml_opcode == AML_NAME_OP) &&
						(walk_state->descending_callback != acpi_ds_exec_begin_op)) {
						/*
						 * Skip parsing of Buffers and Packages
						 * because we don't have enough info in the first pass
						 * to parse them correctly.
						 */
						op->named.data   = aml_op_start;
						op->named.length = (u32) (parser_state->pkg_end -
								   aml_op_start);

						/* Skip body */

						parser_state->aml   = parser_state->pkg_end;
						walk_state->arg_count = 0;
					}
					break;

				case AML_WHILE_OP:

					if (walk_state->control_state) {
						walk_state->control_state->control.package_end =
							parser_state->pkg_end;
					}
					break;

				default:

					/* No action for all other opcodes */
					break;
				}
				break;
			}
		}

		/* Check for arguments that need to be processed */

		if (walk_state->arg_count) {
			/*
			 * There are arguments (complex ones), push Op and
			 * prepare for argument
			 */
			status = acpi_ps_push_scope (parser_state, op,
					 walk_state->arg_types, walk_state->arg_count);
			if (ACPI_FAILURE (status)) {
				goto close_this_op;
			}
			op = NULL;
			continue;
		}

		/*
		 * All arguments have been processed -- Op is complete,
		 * prepare for next
		 */
		walk_state->op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
		if (walk_state->op_info->flags & AML_NAMED) {
			if (acpi_gbl_depth) {
				acpi_gbl_depth--;
			}

			if (op->common.aml_opcode == AML_REGION_OP) {
				/*
				 * Skip parsing of control method or opregion body,
				 * because we don't have enough info in the first pass
				 * to parse them correctly.
				 *
				 * Completed parsing an op_region declaration, we now
				 * know the length.
				 */
				op->named.length = (u32) (parser_state->aml - op->named.data);
			}
		}

		if (walk_state->op_info->flags & AML_CREATE) {
			/*
			 * Backup to beginning of create_xXXfield declaration (1 for
			 * Opcode)
			 *
			 * body_length is unknown until we parse the body
			 */
			op->named.length = (u32) (parser_state->aml - op->named.data);
		}

		/* This op complete, notify the dispatcher */

		if (walk_state->ascending_callback != NULL) {
			walk_state->op    = op;
			walk_state->opcode = op->common.aml_opcode;

			status = walk_state->ascending_callback (walk_state);
			status = acpi_ps_next_parse_state (walk_state, op, status);
			if (status == AE_CTRL_PENDING) {
				status = AE_OK;
				goto close_this_op;
			}
		}


close_this_op:
		/*
		 * Finished one argument of the containing scope
		 */
		parser_state->scope->parse_scope.arg_count--;

		/* Close this Op (will result in parse subtree deletion) */

		acpi_ps_complete_this_op (walk_state, op);
		op = NULL;
		if (pre_op) {
			acpi_ps_free_op (pre_op);
			pre_op = NULL;
		}

		switch (status) {
		case AE_OK:
			break;


		case AE_CTRL_TRANSFER:

			/* We are about to transfer to a called method. */

			walk_state->prev_op = op;
			walk_state->prev_arg_types = walk_state->arg_types;
			return_ACPI_STATUS (status);


		case AE_CTRL_END:

			acpi_ps_pop_scope (parser_state, &op,
				&walk_state->arg_types, &walk_state->arg_count);

			if (op) {
				walk_state->op    = op;
				walk_state->op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
				walk_state->opcode = op->common.aml_opcode;

				status = walk_state->ascending_callback (walk_state);
				status = acpi_ps_next_parse_state (walk_state, op, status);

				acpi_ps_complete_this_op (walk_state, op);
				op = NULL;
			}
			status = AE_OK;
			break;


		case AE_CTRL_BREAK:
		case AE_CTRL_CONTINUE:

			/* Pop off scopes until we find the While */

			while (!op || (op->common.aml_opcode != AML_WHILE_OP)) {
				acpi_ps_pop_scope (parser_state, &op,
					&walk_state->arg_types, &walk_state->arg_count);
			}

			/* Close this iteration of the While loop */

			walk_state->op    = op;
			walk_state->op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
			walk_state->opcode = op->common.aml_opcode;

			status = walk_state->ascending_callback (walk_state);
			status = acpi_ps_next_parse_state (walk_state, op, status);

			acpi_ps_complete_this_op (walk_state, op);
			op = NULL;

			status = AE_OK;
			break;


		case AE_CTRL_TERMINATE:

			status = AE_OK;

			/* Clean up */
			do {
				if (op) {
					acpi_ps_complete_this_op (walk_state, op);
				}
				acpi_ps_pop_scope (parser_state, &op,
					&walk_state->arg_types, &walk_state->arg_count);

			} while (op);

			return_ACPI_STATUS (status);


		default:  /* All other non-AE_OK status */

			do {
				if (op) {
					acpi_ps_complete_this_op (walk_state, op);
				}
				acpi_ps_pop_scope (parser_state, &op,
					&walk_state->arg_types, &walk_state->arg_count);

			} while (op);


			/*
			 * TBD: Cleanup parse ops on error
			 */
#if 0
			if (op == NULL) {
				acpi_ps_pop_scope (parser_state, &op,
					&walk_state->arg_types, &walk_state->arg_count);
			}
#endif
			walk_state->prev_op = op;
			walk_state->prev_arg_types = walk_state->arg_types;
			return_ACPI_STATUS (status);
		}

		/* This scope complete? */

		if (acpi_ps_has_completed_scope (parser_state)) {
			acpi_ps_pop_scope (parser_state, &op,
				&walk_state->arg_types, &walk_state->arg_count);
			ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Popped scope, Op=%p\n", op));
		}
		else {
			op = NULL;
		}

	} /* while parser_state->Aml */


	/*
	 * Complete the last Op (if not completed), and clear the scope stack.
	 * It is easily possible to end an AML "package" with an unbounded number
	 * of open scopes (such as when several ASL blocks are closed with
	 * sequential closing braces).  We want to terminate each one cleanly.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "AML package complete at Op %p\n", op));
	do {
		if (op) {
			if (walk_state->ascending_callback != NULL) {
				walk_state->op    = op;
				walk_state->op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
				walk_state->opcode = op->common.aml_opcode;

				status = walk_state->ascending_callback (walk_state);
				status = acpi_ps_next_parse_state (walk_state, op, status);
				if (status == AE_CTRL_PENDING) {
					status = AE_OK;
					goto close_this_op;
				}

				if (status == AE_CTRL_TERMINATE) {
					status = AE_OK;

					/* Clean up */
					do {
						if (op) {
							acpi_ps_complete_this_op (walk_state, op);
						}

						acpi_ps_pop_scope (parser_state, &op,
							&walk_state->arg_types, &walk_state->arg_count);

					} while (op);

					return_ACPI_STATUS (status);
				}

				else if (ACPI_FAILURE (status)) {
					acpi_ps_complete_this_op (walk_state, op);
					return_ACPI_STATUS (status);
				}
			}

			acpi_ps_complete_this_op (walk_state, op);
		}

		acpi_ps_pop_scope (parser_state, &op, &walk_state->arg_types,
			&walk_state->arg_count);

	} while (op);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_parse_aml
 *
 * PARAMETERS:  walk_state      - Current state
 *
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse raw AML and return a tree of ops
 *
 ******************************************************************************/

acpi_status
acpi_ps_parse_aml (
	struct acpi_walk_state          *walk_state)
{
	acpi_status                     status;
	acpi_status                     terminate_status;
	struct acpi_thread_state        *thread;
	struct acpi_thread_state        *prev_walk_list = acpi_gbl_current_walk_list;
	struct acpi_walk_state          *previous_walk_state;


	ACPI_FUNCTION_TRACE ("ps_parse_aml");

	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
		"Entered with walk_state=%p Aml=%p size=%X\n",
		walk_state, walk_state->parser_state.aml,
		walk_state->parser_state.aml_size));


	/* Create and initialize a new thread state */

	thread = acpi_ut_create_thread_state ();
	if (!thread) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	walk_state->thread = thread;
	acpi_ds_push_walk_state (walk_state, thread);

	/*
	 * This global allows the AML debugger to get a handle to the currently
	 * executing control method.
	 */
	acpi_gbl_current_walk_list = thread;

	/*
	 * Execute the walk loop as long as there is a valid Walk State.  This
	 * handles nested control method invocations without recursion.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "State=%p\n", walk_state));

	status = AE_OK;
	while (walk_state) {
		if (ACPI_SUCCESS (status)) {
			/*
			 * The parse_loop executes AML until the method terminates
			 * or calls another method.
			 */
			status = acpi_ps_parse_loop (walk_state);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
			"Completed one call to walk loop, %s State=%p\n",
			acpi_format_exception (status), walk_state));

		if (status == AE_CTRL_TRANSFER) {
			/*
			 * A method call was detected.
			 * Transfer control to the called control method
			 */
			status = acpi_ds_call_control_method (thread, walk_state, NULL);

			/*
			 * If the transfer to the new method method call worked, a new walk
			 * state was created -- get it
			 */
			walk_state = acpi_ds_get_current_walk_state (thread);
			continue;
		}
		else if (status == AE_CTRL_TERMINATE) {
			status = AE_OK;
		}
		else if ((status != AE_OK) && (walk_state->method_desc)) {
			ACPI_REPORT_METHOD_ERROR ("Method execution failed",
				walk_state->method_node, NULL, status);

			/* Check for possible multi-thread reentrancy problem */

			if ((status == AE_ALREADY_EXISTS) &&
				(!walk_state->method_desc->method.semaphore)) {
				/*
				 * This method is marked not_serialized, but it tried to create
				 * a named object, causing the second thread entrance to fail.
				 * We will workaround this by marking the method permanently
				 * as Serialized.
				 */
				walk_state->method_desc->method.method_flags |= AML_METHOD_SERIALIZED;
				walk_state->method_desc->method.concurrency = 1;
			}
		}

		if (walk_state->method_desc) {
			/* Decrement the thread count on the method parse tree */

			if (walk_state->method_desc->method.thread_count) {
				walk_state->method_desc->method.thread_count--;
			}
		}

		/* We are done with this walk, move on to the parent if any */

		walk_state = acpi_ds_pop_walk_state (thread);

		/* Reset the current scope to the beginning of scope stack */

		acpi_ds_scope_stack_clear (walk_state);

		/*
		 * If we just returned from the execution of a control method,
		 * there's lots of cleanup to do
		 */
		if ((walk_state->parse_flags & ACPI_PARSE_MODE_MASK) == ACPI_PARSE_EXECUTE) {
			terminate_status = acpi_ds_terminate_control_method (walk_state);
			if (ACPI_FAILURE (terminate_status)) {
				ACPI_REPORT_ERROR ((
					"Could not terminate control method properly\n"));

				/* Ignore error and continue */
			}
		}

		/* Delete this walk state and all linked control states */

		acpi_ps_cleanup_scope (&walk_state->parser_state);

		previous_walk_state = walk_state;

		ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
			"return_value=%p, implicit_value=%p State=%p\n",
			walk_state->return_desc, walk_state->implicit_return_obj, walk_state));

		/* Check if we have restarted a preempted walk */

		walk_state = acpi_ds_get_current_walk_state (thread);
		if (walk_state) {
			if (ACPI_SUCCESS (status)) {
				/*
				 * There is another walk state, restart it.
				 * If the method return value is not used by the parent,
				 * The object is deleted
				 */
				if (!previous_walk_state->return_desc) {
					status = acpi_ds_restart_control_method (walk_state,
							 previous_walk_state->implicit_return_obj);
				}
				else {
					/*
					 * We have a valid return value, delete any implicit
					 * return value.
					 */
					acpi_ds_clear_implicit_return (previous_walk_state);

					status = acpi_ds_restart_control_method (walk_state,
							 previous_walk_state->return_desc);
				}
				if (ACPI_SUCCESS (status)) {
					walk_state->walk_type |= ACPI_WALK_METHOD_RESTART;
				}
			}
			else {
				/* On error, delete any return object */

				acpi_ut_remove_reference (previous_walk_state->return_desc);
			}
		}

		/*
		 * Just completed a 1st-level method, save the final internal return
		 * value (if any)
		 */
		else if (previous_walk_state->caller_return_desc) {
			if (previous_walk_state->implicit_return_obj) {
				*(previous_walk_state->caller_return_desc) =
					previous_walk_state->implicit_return_obj;
			}
			else {
				 /* NULL if no return value */

				*(previous_walk_state->caller_return_desc) =
					previous_walk_state->return_desc;
			}
		}
		else {
			if (previous_walk_state->return_desc) {
				/* Caller doesn't want it, must delete it */

				acpi_ut_remove_reference (previous_walk_state->return_desc);
			}
			if (previous_walk_state->implicit_return_obj) {
				/* Caller doesn't want it, must delete it */

				acpi_ut_remove_reference (previous_walk_state->implicit_return_obj);
			}
		}

		acpi_ds_delete_walk_state (previous_walk_state);
	}

	/* Normal exit */

	acpi_ex_release_all_mutexes (thread);
	acpi_ut_delete_generic_state (ACPI_CAST_PTR (union acpi_generic_state, thread));
	acpi_gbl_current_walk_list = prev_walk_list;
	return_ACPI_STATUS (status);
}


