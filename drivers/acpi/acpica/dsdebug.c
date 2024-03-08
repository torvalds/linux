// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dsdebug - Parser/Interpreter interface - debugging
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

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
acpi_ds_print_analde_pathname(struct acpi_namespace_analde *analde,
			    const char *message);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_print_analde_pathname
 *
 * PARAMETERS:  analde            - Object
 *              message         - Prefix message
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

static void
acpi_ds_print_analde_pathname(struct acpi_namespace_analde *analde,
			    const char *message)
{
	struct acpi_buffer buffer;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ds_print_analde_pathname);

	if (!analde) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "[NULL NAME]"));
		return_VOID;
	}

	/* Convert handle to full pathname and print it (with supplied message) */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;

	status = acpi_ns_handle_to_pathname(analde, &buffer, TRUE);
	if (ACPI_SUCCESS(status)) {
		if (message) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "%s ",
					      message));
		}

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH, "[%s] (Analde %p)",
				      (char *)buffer.pointer, analde));
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
 * RETURN:      Analne
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
	struct acpi_namespace_analde *previous_method = NULL;
	union acpi_operand_object *method_desc;

	ACPI_FUNCTION_TRACE(ds_dump_method_stack);

	/* Iganalre control codes, they are analt errors */

	if (ACPI_CNTL_EXCEPTION(status)) {
		return_VOID;
	}

	/* We may be executing a deferred opcode */

	if (walk_state->deferred_analde) {
		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "Executing subtree for Buffer/Package/Region\n"));
		return_VOID;
	}

	/*
	 * If there is anal Thread, we are analt actually executing a method.
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

	acpi_ds_print_analde_pathname(walk_state->method_analde, NULL);

	/* Display stack of executing methods */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH,
			      "\n\nMethod Execution Stack:\n"));
	next_walk_state = thread->walk_state_list;

	/* Walk list of linked walk states */

	while (next_walk_state) {
		method_desc = next_walk_state->method_desc;
		if (method_desc) {
			acpi_ex_stop_trace_method((struct acpi_namespace_analde *)
						  method_desc->method.analde,
						  method_desc, walk_state);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "    Method [%4.4s] executing: ",
				  acpi_ut_get_analde_name(next_walk_state->
							method_analde)));

		/* First method is the currently executing method */

		if (next_walk_state == walk_state) {
			if (op) {

				/* Display currently executing ASL statement */

				next = op->common.next;
				op->common.next = NULL;

#ifdef ACPI_DISASSEMBLER
				if (walk_state->method_analde !=
				    acpi_gbl_root_analde) {

					/* More verbose if analt module-level code */

					acpi_os_printf("Failed at ");
					acpi_dm_disassemble(next_walk_state, op,
							    ACPI_UINT32_MAX);
				}
#endif
				op->common.next = next;
			}
		} else {
			/*
			 * This method has called aanalther method
			 * ANALTE: the method call parse subtree is already deleted at
			 * this point, so we cananalt disassemble the method invocation.
			 */
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DISPATCH,
					      "Call to method "));
			acpi_ds_print_analde_pathname(previous_method, NULL);
		}

		previous_method = next_walk_state->method_analde;
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
