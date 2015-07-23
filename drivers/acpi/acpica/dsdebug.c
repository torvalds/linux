/******************************************************************************
 *
 * Module Name: dsdebug - Parser/Interpreter interface - debugging
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "acdispat.h"
#include "acnamesp.h"
#ifdef ACPI_DISASSEMBLER
#include "acdisasm.h"
#endif
#include "acinterp.h"

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dsdebug")

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/* Local prototypes */
static void
acpi_ds_print_node_pathname(struct acpi_namespace_node *node,
			    const char *message);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_print_node_pathname
 *
 * PARAMETERS:  node            - Object
 *              message         - Prefix message
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

static void
acpi_ds_print_node_pathname(struct acpi_namespace_node *node,
			    const char *message)
{
	struct acpi_buffer buffer;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ds_print_node_pathname);

	if (!node) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "[NULL NAME]"));
		return_VOID;
	}

	/* Convert handle to full pathname and print it (with supplied message) */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;

	status = acpi_ns_handle_to_pathname(node, &buffer, FALSE);
	if (ACPI_SUCCESS(status)) {
		if (message) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "%s ",
					      message));
		}

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "[%s] (Node %p)",
				      (char *)buffer.pointer, node));
		ACPI_FREE(buffer.pointer);
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_dump_method_stack
 *
 * PARAMETERS:  status          - Method execution status
 *              walk_state      - Current state of the parse tree walk
 *              op              - Executing parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Called when a method has been aborted because of an error.
 *              Dumps the method execution stack.
 *
 ******************************************************************************/

void
acpi_ds_dump_method_stack(acpi_status status,
			  struct acpi_walk_state *walk_state,
			  union acpi_parse_object *op)
{
	union acpi_parse_object *next;
	struct acpi_thread_state *thread;
	struct acpi_walk_state *next_walk_state;
	struct acpi_namespace_node *previous_method = NULL;
	union acpi_operand_object *method_desc;

	ACPI_FUNCTION_TRACE(ds_dump_method_stack);

	/* Ignore control codes, they are not errors */

	if ((status & AE_CODE_MASK) == AE_CODE_CONTROL) {
		return_VOID;
	}

	/* We may be executing a deferred opcode */

	if (walk_state->deferred_node) {
		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "Executing subtree for Buffer/Package/Region\n"));
		return_VOID;
	}

	/*
	 * If there is no Thread, we are not actually executing a method.
	 * This can happen when the iASL compiler calls the interpreter
	 * to perform constant folding.
	 */
	thread = walk_state->thread;
	if (!thread) {
		return_VOID;
	}

	/* Display exception and method name */

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "\n**** Exception %s during execution of method ",
			  acpi_format_exception(status)));
	acpi_ds_print_node_pathname(walk_state->method_node, NULL);

	/* Display stack of executing methods */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH,
			      "\n\nMethod Execution Stack:\n"));
	next_walk_state = thread->walk_state_list;

	/* Walk list of linked walk states */

	while (next_walk_state) {
		method_desc = next_walk_state->method_desc;
		if (method_desc) {
			acpi_ex_stop_trace_method((struct acpi_namespace_node *)
						  method_desc->method.node,
						  method_desc, walk_state);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "    Method [%4.4s] executing: ",
				  acpi_ut_get_node_name(next_walk_state->
							method_node)));

		/* First method is the currently executing method */

		if (next_walk_state == walk_state) {
			if (op) {

				/* Display currently executing ASL statement */

				next = op->common.next;
				op->common.next = NULL;

#ifdef ACPI_DISASSEMBLER
				acpi_dm_disassemble(next_walk_state, op,
						    ACPI_UINT32_MAX);
#endif
				op->common.next = next;
			}
		} else {
			/*
			 * This method has called another method
			 * NOTE: the method call parse subtree is already deleted at this
			 * point, so we cannot disassemble the method invocation.
			 */
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH,
					      "Call to method "));
			acpi_ds_print_node_pathname(previous_method, NULL);
		}

		previous_method = next_walk_state->method_node;
		next_walk_state = next_walk_state->next;
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "\n"));
	}

	return_VOID;
}

#else
void
acpi_ds_dump_method_stack(acpi_status status,
			  struct acpi_walk_state *walk_state,
			  union acpi_parse_object *op)
{
	return;
}

#endif
