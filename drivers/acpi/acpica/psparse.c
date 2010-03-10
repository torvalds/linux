/******************************************************************************
 *
 * Module Name: psparse - Parser top level AML parse routines
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
 * Parse the AML and build an operation tree as most interpreters,
 * like Perl, do.  Parsing is done by hand rather than with a YACC
 * generated parser to tightly constrain stack and dynamic memory
 * usage.  At the same time, parsing is kept flexible and the code
 * fairly compact by parsing based on a list of AML opcode
 * templates in aml_op_info[]
 */

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "acdispat.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psparse")

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
u32 acpi_ps_get_opcode_size(u32 opcode)
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

u16 acpi_ps_peek_opcode(struct acpi_parse_state * parser_state)
{
	u8 *aml;
	u16 opcode;

	aml = parser_state->aml;
	opcode = (u16) ACPI_GET8(aml);

	if (opcode == AML_EXTENDED_OP_PREFIX) {

		/* Extended opcode, get the second opcode byte */

		aml++;
		opcode = (u16) ((opcode << 8) | ACPI_GET8(aml));
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
 * RETURN:      Status
 *
 * DESCRIPTION: Perform any cleanup at the completion of an Op.
 *
 ******************************************************************************/

acpi_status
acpi_ps_complete_this_op(struct acpi_walk_state * walk_state,
			 union acpi_parse_object * op)
{
	union acpi_parse_object *prev;
	union acpi_parse_object *next;
	const struct acpi_opcode_info *parent_info;
	union acpi_parse_object *replacement_op = NULL;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_PTR(ps_complete_this_op, op);

	/* Check for null Op, can happen if AML code is corrupt */

	if (!op) {
		return_ACPI_STATUS(AE_OK);	/* OK for now */
	}

	/* Delete this op and the subtree below it if asked to */

	if (((walk_state->parse_flags & ACPI_PARSE_TREE_MASK) !=
	     ACPI_PARSE_DELETE_TREE)
	    || (walk_state->op_info->class == AML_CLASS_ARGUMENT)) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Make sure that we only delete this subtree */

	if (op->common.parent) {
		prev = op->common.parent->common.value.arg;
		if (!prev) {

			/* Nothing more to do */

			goto cleanup;
		}

		/*
		 * Check if we need to replace the operator and its subtree
		 * with a return value op (placeholder op)
		 */
		parent_info =
		    acpi_ps_get_opcode_info(op->common.parent->common.
					    aml_opcode);

		switch (parent_info->class) {
		case AML_CLASS_CONTROL:
			break;

		case AML_CLASS_CREATE:

			/*
			 * These opcodes contain term_arg operands. The current
			 * op must be replaced by a placeholder return op
			 */
			replacement_op =
			    acpi_ps_alloc_op(AML_INT_RETURN_VALUE_OP);
			if (!replacement_op) {
				status = AE_NO_MEMORY;
			}
			break;

		case AML_CLASS_NAMED_OBJECT:

			/*
			 * These opcodes contain term_arg operands. The current
			 * op must be replaced by a placeholder return op
			 */
			if ((op->common.parent->common.aml_opcode ==
			     AML_REGION_OP)
			    || (op->common.parent->common.aml_opcode ==
				AML_DATA_REGION_OP)
			    || (op->common.parent->common.aml_opcode ==
				AML_BUFFER_OP)
			    || (op->common.parent->common.aml_opcode ==
				AML_PACKAGE_OP)
			    || (op->common.parent->common.aml_opcode ==
				AML_BANK_FIELD_OP)
			    || (op->common.parent->common.aml_opcode ==
				AML_VAR_PACKAGE_OP)) {
				replacement_op =
				    acpi_ps_alloc_op(AML_INT_RETURN_VALUE_OP);
				if (!replacement_op) {
					status = AE_NO_MEMORY;
				}
			} else
			    if ((op->common.parent->common.aml_opcode ==
				 AML_NAME_OP)
				&& (walk_state->pass_number <=
				    ACPI_IMODE_LOAD_PASS2)) {
				if ((op->common.aml_opcode == AML_BUFFER_OP)
				    || (op->common.aml_opcode == AML_PACKAGE_OP)
				    || (op->common.aml_opcode ==
					AML_VAR_PACKAGE_OP)) {
					replacement_op =
					    acpi_ps_alloc_op(op->common.
							     aml_opcode);
					if (!replacement_op) {
						status = AE_NO_MEMORY;
					} else {
						replacement_op->named.data =
						    op->named.data;
						replacement_op->named.length =
						    op->named.length;
					}
				}
			}
			break;

		default:

			replacement_op =
			    acpi_ps_alloc_op(AML_INT_RETURN_VALUE_OP);
			if (!replacement_op) {
				status = AE_NO_MEMORY;
			}
		}

		/* We must unlink this op from the parent tree */

		if (prev == op) {

			/* This op is the first in the list */

			if (replacement_op) {
				replacement_op->common.parent =
				    op->common.parent;
				replacement_op->common.value.arg = NULL;
				replacement_op->common.node = op->common.node;
				op->common.parent->common.value.arg =
				    replacement_op;
				replacement_op->common.next = op->common.next;
			} else {
				op->common.parent->common.value.arg =
				    op->common.next;
			}
		}

		/* Search the parent list */

		else
			while (prev) {

				/* Traverse all siblings in the parent's argument list */

				next = prev->common.next;
				if (next == op) {
					if (replacement_op) {
						replacement_op->common.parent =
						    op->common.parent;
						replacement_op->common.value.
						    arg = NULL;
						replacement_op->common.node =
						    op->common.node;
						prev->common.next =
						    replacement_op;
						replacement_op->common.next =
						    op->common.next;
						next = NULL;
					} else {
						prev->common.next =
						    op->common.next;
						next = NULL;
					}
				}
				prev = next;
			}
	}

      cleanup:

	/* Now we can actually delete the subtree rooted at Op */

	acpi_ps_delete_parse_tree(op);
	return_ACPI_STATUS(status);
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

acpi_status
acpi_ps_next_parse_state(struct acpi_walk_state *walk_state,
			 union acpi_parse_object *op,
			 acpi_status callback_status)
{
	struct acpi_parse_state *parser_state = &walk_state->parser_state;
	acpi_status status = AE_CTRL_PENDING;

	ACPI_FUNCTION_TRACE_PTR(ps_next_parse_state, op);

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
		parser_state->aml = acpi_ps_get_next_package_end(parser_state);
		status = AE_CTRL_PENDING;
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
		walk_state->method_call_node =
		    (op->common.value.arg)->common.node;

		/* Will return value (if any) be used by the caller? */

		walk_state->return_used =
		    acpi_ds_is_result_used(op, walk_state);
		break;

	default:

		status = callback_status;
		if ((callback_status & AE_CODE_MASK) == AE_CODE_CONTROL) {
			status = AE_OK;
		}
		break;
	}

	return_ACPI_STATUS(status);
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

acpi_status acpi_ps_parse_aml(struct acpi_walk_state *walk_state)
{
	acpi_status status;
	struct acpi_thread_state *thread;
	struct acpi_thread_state *prev_walk_list = acpi_gbl_current_walk_list;
	struct acpi_walk_state *previous_walk_state;

	ACPI_FUNCTION_TRACE(ps_parse_aml);

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
			  "Entered with WalkState=%p Aml=%p size=%X\n",
			  walk_state, walk_state->parser_state.aml,
			  walk_state->parser_state.aml_size));

	if (!walk_state->parser_state.aml) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Create and initialize a new thread state */

	thread = acpi_ut_create_thread_state();
	if (!thread) {
		if (walk_state->method_desc) {

			/* Executing a control method - additional cleanup */

			acpi_ds_terminate_control_method(
				walk_state->method_desc, walk_state);
		}

		acpi_ds_delete_walk_state(walk_state);
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	walk_state->thread = thread;

	/*
	 * If executing a method, the starting sync_level is this method's
	 * sync_level
	 */
	if (walk_state->method_desc) {
		walk_state->thread->current_sync_level =
		    walk_state->method_desc->method.sync_level;
	}

	acpi_ds_push_walk_state(walk_state, thread);

	/*
	 * This global allows the AML debugger to get a handle to the currently
	 * executing control method.
	 */
	acpi_gbl_current_walk_list = thread;

	/*
	 * Execute the walk loop as long as there is a valid Walk State.  This
	 * handles nested control method invocations without recursion.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "State=%p\n", walk_state));

	status = AE_OK;
	while (walk_state) {
		if (ACPI_SUCCESS(status)) {
			/*
			 * The parse_loop executes AML until the method terminates
			 * or calls another method.
			 */
			status = acpi_ps_parse_loop(walk_state);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
				  "Completed one call to walk loop, %s State=%p\n",
				  acpi_format_exception(status), walk_state));

		if (status == AE_CTRL_TRANSFER) {
			/*
			 * A method call was detected.
			 * Transfer control to the called control method
			 */
			status =
			    acpi_ds_call_control_method(thread, walk_state,
							NULL);
			if (ACPI_FAILURE(status)) {
				status =
				    acpi_ds_method_error(status, walk_state);
			}

			/*
			 * If the transfer to the new method method call worked, a new walk
			 * state was created -- get it
			 */
			walk_state = acpi_ds_get_current_walk_state(thread);
			continue;
		} else if (status == AE_CTRL_TERMINATE) {
			status = AE_OK;
		} else if ((status != AE_OK) && (walk_state->method_desc)) {

			/* Either the method parse or actual execution failed */

			ACPI_ERROR_METHOD("Method parse/execution failed",
					  walk_state->method_node, NULL,
					  status);

			/* Check for possible multi-thread reentrancy problem */

			if ((status == AE_ALREADY_EXISTS) &&
			    (!walk_state->method_desc->method.mutex)) {
				ACPI_INFO((AE_INFO,
					   "Marking method %4.4s as Serialized because of AE_ALREADY_EXISTS error",
					   walk_state->method_node->name.
					   ascii));

				/*
				 * Method tried to create an object twice. The probable cause is
				 * that the method cannot handle reentrancy.
				 *
				 * The method is marked not_serialized, but it tried to create
				 * a named object, causing the second thread entrance to fail.
				 * Workaround this problem by marking the method permanently
				 * as Serialized.
				 */
				walk_state->method_desc->method.method_flags |=
				    AML_METHOD_SERIALIZED;
				walk_state->method_desc->method.sync_level = 0;
			}
		}

		/* We are done with this walk, move on to the parent if any */

		walk_state = acpi_ds_pop_walk_state(thread);

		/* Reset the current scope to the beginning of scope stack */

		acpi_ds_scope_stack_clear(walk_state);

		/*
		 * If we just returned from the execution of a control method or if we
		 * encountered an error during the method parse phase, there's lots of
		 * cleanup to do
		 */
		if (((walk_state->parse_flags & ACPI_PARSE_MODE_MASK) ==
		     ACPI_PARSE_EXECUTE) || (ACPI_FAILURE(status))) {
			acpi_ds_terminate_control_method(walk_state->
							 method_desc,
							 walk_state);
		}

		/* Delete this walk state and all linked control states */

		acpi_ps_cleanup_scope(&walk_state->parser_state);
		previous_walk_state = walk_state;

		ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
				  "ReturnValue=%p, ImplicitValue=%p State=%p\n",
				  walk_state->return_desc,
				  walk_state->implicit_return_obj, walk_state));

		/* Check if we have restarted a preempted walk */

		walk_state = acpi_ds_get_current_walk_state(thread);
		if (walk_state) {
			if (ACPI_SUCCESS(status)) {
				/*
				 * There is another walk state, restart it.
				 * If the method return value is not used by the parent,
				 * The object is deleted
				 */
				if (!previous_walk_state->return_desc) {
					/*
					 * In slack mode execution, if there is no return value
					 * we should implicitly return zero (0) as a default value.
					 */
					if (acpi_gbl_enable_interpreter_slack &&
					    !previous_walk_state->
					    implicit_return_obj) {
						previous_walk_state->
						    implicit_return_obj =
						    acpi_ut_create_integer_object
						    ((u64) 0);
						if (!previous_walk_state->
						    implicit_return_obj) {
							return_ACPI_STATUS
							    (AE_NO_MEMORY);
						}
					}

					/* Restart the calling control method */

					status =
					    acpi_ds_restart_control_method
					    (walk_state,
					     previous_walk_state->
					     implicit_return_obj);
				} else {
					/*
					 * We have a valid return value, delete any implicit
					 * return value.
					 */
					acpi_ds_clear_implicit_return
					    (previous_walk_state);

					status =
					    acpi_ds_restart_control_method
					    (walk_state,
					     previous_walk_state->return_desc);
				}
				if (ACPI_SUCCESS(status)) {
					walk_state->walk_type |=
					    ACPI_WALK_METHOD_RESTART;
				}
			} else {
				/* On error, delete any return object or implicit return */

				acpi_ut_remove_reference(previous_walk_state->
							 return_desc);
				acpi_ds_clear_implicit_return
				    (previous_walk_state);
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
			} else {
				/* NULL if no return value */

				*(previous_walk_state->caller_return_desc) =
				    previous_walk_state->return_desc;
			}
		} else {
			if (previous_walk_state->return_desc) {

				/* Caller doesn't want it, must delete it */

				acpi_ut_remove_reference(previous_walk_state->
							 return_desc);
			}
			if (previous_walk_state->implicit_return_obj) {

				/* Caller doesn't want it, must delete it */

				acpi_ut_remove_reference(previous_walk_state->
							 implicit_return_obj);
			}
		}

		acpi_ds_delete_walk_state(previous_walk_state);
	}

	/* Normal exit */

	acpi_ex_release_all_mutexes(thread);
	acpi_ut_delete_generic_state(ACPI_CAST_PTR
				     (union acpi_generic_state, thread));
	acpi_gbl_current_walk_list = prev_walk_list;
	return_ACPI_STATUS(status);
}
