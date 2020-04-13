// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: dbxface - AML Debugger external interfaces
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "amlcode.h"
#include "acdebug.h"
#include "acinterp.h"
#include "acparser.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbxface")

/* Local prototypes */
static acpi_status
acpi_db_start_command(struct acpi_walk_state *walk_state,
		      union acpi_parse_object *op);

#ifdef ACPI_OBSOLETE_FUNCTIONS
void acpi_db_method_end(struct acpi_walk_state *walk_state);
#endif

#ifdef ACPI_DISASSEMBLER
static union acpi_parse_object *acpi_db_get_display_op(struct acpi_walk_state
						       *walk_state,
						       union acpi_parse_object
						       *op);
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_start_command
 *
 * PARAMETERS:  walk_state      - Current walk
 *              op              - Current executing Op, from AML interpreter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter debugger command loop
 *
 ******************************************************************************/

static acpi_status
acpi_db_start_command(struct acpi_walk_state *walk_state,
		      union acpi_parse_object *op)
{
	acpi_status status;

	/* TBD: [Investigate] are there namespace locking issues here? */

	/* acpi_ut_release_mutex (ACPI_MTX_NAMESPACE); */

	/* Go into the command loop and await next user command */

	acpi_gbl_method_executing = TRUE;
	status = AE_CTRL_TRUE;

	while (status == AE_CTRL_TRUE) {

		/* Notify the completion of the command */

		status = acpi_os_notify_command_complete();
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		/* Wait the readiness of the command */

		status = acpi_os_wait_command_ready();
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		status =
		    acpi_db_command_dispatch(acpi_gbl_db_line_buf, walk_state,
					     op);
	}

	/* acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE); */

error_exit:
	if (ACPI_FAILURE(status) && status != AE_CTRL_TERMINATE) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While parsing/handling command line"));
	}
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_signal_break_point
 *
 * PARAMETERS:  walk_state      - Current walk
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called for AML_BREAKPOINT_OP
 *
 ******************************************************************************/

void acpi_db_signal_break_point(struct acpi_walk_state *walk_state)
{

#ifndef ACPI_APPLICATION
	if (acpi_gbl_db_thread_id != acpi_os_get_thread_id()) {
		return;
	}
#endif

	/*
	 * Set the single-step flag. This will cause the debugger (if present)
	 * to break to the console within the AML debugger at the start of the
	 * next AML instruction.
	 */
	acpi_gbl_cm_single_step = TRUE;
	acpi_os_printf("**break** Executed AML BreakPoint opcode\n");
}

#ifdef ACPI_DISASSEMBLER
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_display_op
 *
 * PARAMETERS:  walk_state      - Current walk
 *              op              - Current executing op (from aml interpreter)
 *
 * RETURN:      Opcode to display
 *
 * DESCRIPTION: Find the opcode to display during single stepping
 *
 ******************************************************************************/

static union acpi_parse_object *acpi_db_get_display_op(struct acpi_walk_state
						       *walk_state,
						       union acpi_parse_object
						       *op)
{
	union acpi_parse_object *display_op;
	union acpi_parse_object *parent_op;

	display_op = op;
	parent_op = op->common.parent;
	if (parent_op) {
		if ((walk_state->control_state) &&
		    (walk_state->control_state->common.state ==
		     ACPI_CONTROL_PREDICATE_EXECUTING)) {
			/*
			 * We are executing the predicate of an IF or WHILE statement
			 * Search upwards for the containing IF or WHILE so that the
			 * entire predicate can be displayed.
			 */
			while (parent_op) {
				if ((parent_op->common.aml_opcode == AML_IF_OP)
				    || (parent_op->common.aml_opcode ==
					AML_WHILE_OP)) {
					display_op = parent_op;
					break;
				}
				parent_op = parent_op->common.parent;
			}
		} else {
			while (parent_op) {
				if ((parent_op->common.aml_opcode == AML_IF_OP)
				    || (parent_op->common.aml_opcode ==
					AML_ELSE_OP)
				    || (parent_op->common.aml_opcode ==
					AML_SCOPE_OP)
				    || (parent_op->common.aml_opcode ==
					AML_METHOD_OP)
				    || (parent_op->common.aml_opcode ==
					AML_WHILE_OP)) {
					break;
				}
				display_op = parent_op;
				parent_op = parent_op->common.parent;
			}
		}
	}
	return display_op;
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_single_step
 *
 * PARAMETERS:  walk_state      - Current walk
 *              op              - Current executing op (from aml interpreter)
 *              opcode_class    - Class of the current AML Opcode
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called just before execution of an AML opcode.
 *
 ******************************************************************************/

acpi_status
acpi_db_single_step(struct acpi_walk_state *walk_state,
		    union acpi_parse_object *op, u32 opcode_class)
{
	union acpi_parse_object *next;
	acpi_status status = AE_OK;
	u32 original_debug_level;
	u32 aml_offset;

	ACPI_FUNCTION_ENTRY();

#ifndef ACPI_APPLICATION
	if (acpi_gbl_db_thread_id != acpi_os_get_thread_id()) {
		return (AE_OK);
	}
#endif

	/* Check the abort flag */

	if (acpi_gbl_abort_method) {
		acpi_gbl_abort_method = FALSE;
		return (AE_ABORT_METHOD);
	}

	aml_offset = (u32)ACPI_PTR_DIFF(op->common.aml,
					walk_state->parser_state.aml_start);

	/* Check for single-step breakpoint */

	if (walk_state->method_breakpoint &&
	    (walk_state->method_breakpoint <= aml_offset)) {

		/* Check if the breakpoint has been reached or passed */
		/* Hit the breakpoint, resume single step, reset breakpoint */

		acpi_os_printf("***Break*** at AML offset %X\n", aml_offset);
		acpi_gbl_cm_single_step = TRUE;
		acpi_gbl_step_to_next_call = FALSE;
		walk_state->method_breakpoint = 0;
	}

	/* Check for user breakpoint (Must be on exact Aml offset) */

	else if (walk_state->user_breakpoint &&
		 (walk_state->user_breakpoint == aml_offset)) {
		acpi_os_printf("***UserBreakpoint*** at AML offset %X\n",
			       aml_offset);
		acpi_gbl_cm_single_step = TRUE;
		acpi_gbl_step_to_next_call = FALSE;
		walk_state->method_breakpoint = 0;
	}

	/*
	 * Check if this is an opcode that we are interested in --
	 * namely, opcodes that have arguments
	 */
	if (op->common.aml_opcode == AML_INT_NAMEDFIELD_OP) {
		return (AE_OK);
	}

	switch (opcode_class) {
	case AML_CLASS_UNKNOWN:
	case AML_CLASS_ARGUMENT:	/* constants, literals, etc. do nothing */

		return (AE_OK);

	default:

		/* All other opcodes -- continue */
		break;
	}

	/*
	 * Under certain debug conditions, display this opcode and its operands
	 */
	if ((acpi_gbl_db_output_to_file) ||
	    (acpi_gbl_cm_single_step) || (acpi_dbg_level & ACPI_LV_PARSE)) {
		if ((acpi_gbl_db_output_to_file) ||
		    (acpi_dbg_level & ACPI_LV_PARSE)) {
			acpi_os_printf
			    ("\nAML Debug: Next AML Opcode to execute:\n");
		}

		/*
		 * Display this op (and only this op - zero out the NEXT field
		 * temporarily, and disable parser trace output for the duration of
		 * the display because we don't want the extraneous debug output)
		 */
		original_debug_level = acpi_dbg_level;
		acpi_dbg_level &= ~(ACPI_LV_PARSE | ACPI_LV_FUNCTIONS);
		next = op->common.next;
		op->common.next = NULL;

		/* Now we can disassemble and display it */

#ifdef ACPI_DISASSEMBLER
		acpi_dm_disassemble(walk_state,
				    acpi_db_get_display_op(walk_state, op),
				    ACPI_UINT32_MAX);
#else
		/*
		 * The AML Disassembler is not configured - at least we can
		 * display the opcode value and name
		 */
		acpi_os_printf("AML Opcode: %4.4X %s\n", op->common.aml_opcode,
			       acpi_ps_get_opcode_name(op->common.aml_opcode));
#endif

		if ((op->common.aml_opcode == AML_IF_OP) ||
		    (op->common.aml_opcode == AML_WHILE_OP)) {
			if (walk_state->control_state->common.value) {
				acpi_os_printf
				    ("Predicate = [True], IF block was executed\n");
			} else {
				acpi_os_printf
				    ("Predicate = [False], Skipping IF block\n");
			}
		} else if (op->common.aml_opcode == AML_ELSE_OP) {
			acpi_os_printf
			    ("Predicate = [False], ELSE block was executed\n");
		}

		/* Restore everything */

		op->common.next = next;
		acpi_os_printf("\n");
		if ((acpi_gbl_db_output_to_file) ||
		    (acpi_dbg_level & ACPI_LV_PARSE)) {
			acpi_os_printf("\n");
		}
		acpi_dbg_level = original_debug_level;
	}

	/* If we are not single stepping, just continue executing the method */

	if (!acpi_gbl_cm_single_step) {
		return (AE_OK);
	}

	/*
	 * If we are executing a step-to-call command,
	 * Check if this is a method call.
	 */
	if (acpi_gbl_step_to_next_call) {
		if (op->common.aml_opcode != AML_INT_METHODCALL_OP) {

			/* Not a method call, just keep executing */

			return (AE_OK);
		}

		/* Found a method call, stop executing */

		acpi_gbl_step_to_next_call = FALSE;
	}

	/*
	 * If the next opcode is a method call, we will "step over" it
	 * by default.
	 */
	if (op->common.aml_opcode == AML_INT_METHODCALL_OP) {

		/* Force no more single stepping while executing called method */

		acpi_gbl_cm_single_step = FALSE;

		/*
		 * Set the breakpoint on/before the call, it will stop execution
		 * as soon as we return
		 */
		walk_state->method_breakpoint = 1;	/* Must be non-zero! */
	}

	acpi_ex_exit_interpreter();
	status = acpi_db_start_command(walk_state, op);
	acpi_ex_enter_interpreter();

	/* User commands complete, continue execution of the interrupted method */

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_debugger
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init and start debugger
 *
 ******************************************************************************/

acpi_status acpi_initialize_debugger(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_initialize_debugger);

	/* Init globals */

	acpi_gbl_db_buffer = NULL;
	acpi_gbl_db_filename = NULL;
	acpi_gbl_db_output_to_file = FALSE;

	acpi_gbl_db_debug_level = ACPI_LV_VERBOSITY2;
	acpi_gbl_db_console_debug_level = ACPI_NORMAL_DEFAULT | ACPI_LV_TABLES;
	acpi_gbl_db_output_flags = ACPI_DB_CONSOLE_OUTPUT;

	acpi_gbl_db_opt_no_ini_methods = FALSE;
	acpi_gbl_db_opt_no_region_support = FALSE;

	acpi_gbl_db_buffer = acpi_os_allocate(ACPI_DEBUG_BUFFER_SIZE);
	if (!acpi_gbl_db_buffer) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}
	memset(acpi_gbl_db_buffer, 0, ACPI_DEBUG_BUFFER_SIZE);

	/* Initial scope is the root */

	acpi_gbl_db_scope_buf[0] = AML_ROOT_PREFIX;
	acpi_gbl_db_scope_buf[1] = 0;
	acpi_gbl_db_scope_node = acpi_gbl_root_node;

	/* Initialize user commands loop */

	acpi_gbl_db_terminate_loop = FALSE;

	/*
	 * If configured for multi-thread support, the debug executor runs in
	 * a separate thread so that the front end can be in another address
	 * space, environment, or even another machine.
	 */
	if (acpi_gbl_debugger_configuration & DEBUGGER_MULTI_THREADED) {

		/* These were created with one unit, grab it */

		status = acpi_os_initialize_debugger();
		if (ACPI_FAILURE(status)) {
			acpi_os_printf("Could not get debugger mutex\n");
			return_ACPI_STATUS(status);
		}

		/* Create the debug execution thread to execute commands */

		acpi_gbl_db_threads_terminated = FALSE;
		status = acpi_os_execute(OSL_DEBUGGER_MAIN_THREAD,
					 acpi_db_execute_thread, NULL);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could not start debugger thread"));
			acpi_gbl_db_threads_terminated = TRUE;
			return_ACPI_STATUS(status);
		}
	} else {
		acpi_gbl_db_thread_id = acpi_os_get_thread_id();
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_initialize_debugger)

/*******************************************************************************
 *
 * FUNCTION:    acpi_terminate_debugger
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop debugger
 *
 ******************************************************************************/
void acpi_terminate_debugger(void)
{

	/* Terminate the AML Debugger */

	acpi_gbl_db_terminate_loop = TRUE;

	if (acpi_gbl_debugger_configuration & DEBUGGER_MULTI_THREADED) {

		/* Wait the AML Debugger threads */

		while (!acpi_gbl_db_threads_terminated) {
			acpi_os_sleep(100);
		}

		acpi_os_terminate_debugger();
	}

	if (acpi_gbl_db_buffer) {
		acpi_os_free(acpi_gbl_db_buffer);
		acpi_gbl_db_buffer = NULL;
	}

	/* Ensure that debug output is now disabled */

	acpi_gbl_db_output_flags = ACPI_DB_DISABLE_OUTPUT;
}

ACPI_EXPORT_SYMBOL(acpi_terminate_debugger)

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_debugger_thread_id
 *
 * PARAMETERS:  thread_id       - Debugger thread ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set debugger thread ID
 *
 ******************************************************************************/
void acpi_set_debugger_thread_id(acpi_thread_id thread_id)
{
	acpi_gbl_db_thread_id = thread_id;
}

ACPI_EXPORT_SYMBOL(acpi_set_debugger_thread_id)
