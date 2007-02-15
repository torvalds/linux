/******************************************************************************
 *
 * Module Name: dsmethod - Parser/Interpreter interface - control method parsing
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2007, R. Byron Moore
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
#include <acpi/acdispat.h>
#include <acpi/acinterp.h>
#include <acpi/acnamesp.h>
#include <acpi/acdisasm.h>

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dsmethod")

/* Local prototypes */
static acpi_status
acpi_ds_create_method_mutex(union acpi_operand_object *method_desc);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_method_error
 *
 * PARAMETERS:  Status          - Execution status
 *              walk_state      - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called on method error. Invoke the global exception handler if
 *              present, dump the method data if the disassembler is configured
 *
 *              Note: Allows the exception handler to change the status code
 *
 ******************************************************************************/

acpi_status
acpi_ds_method_error(acpi_status status, struct acpi_walk_state *walk_state)
{
	ACPI_FUNCTION_ENTRY();

	/* Ignore AE_OK and control exception codes */

	if (ACPI_SUCCESS(status) || (status & AE_CODE_CONTROL)) {
		return (status);
	}

	/* Invoke the global exception handler */

	if (acpi_gbl_exception_handler) {

		/* Exit the interpreter, allow handler to execute methods */

		acpi_ex_exit_interpreter();

		/*
		 * Handler can map the exception code to anything it wants, including
		 * AE_OK, in which case the executing method will not be aborted.
		 */
		status = acpi_gbl_exception_handler(status,
						    walk_state->method_node ?
						    walk_state->method_node->
						    name.integer : 0,
						    walk_state->opcode,
						    walk_state->aml_offset,
						    NULL);
		(void)acpi_ex_enter_interpreter();
	}
#ifdef ACPI_DISASSEMBLER
	if (ACPI_FAILURE(status)) {

		/* Display method locals/args if disassembler is present */

		acpi_dm_dump_method_info(status, walk_state, walk_state->op);
	}
#endif

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_create_method_mutex
 *
 * PARAMETERS:  obj_desc            - The method object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a mutex object for a serialized control method
 *
 ******************************************************************************/

static acpi_status
acpi_ds_create_method_mutex(union acpi_operand_object *method_desc)
{
	union acpi_operand_object *mutex_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ds_create_method_mutex);

	/* Create the new mutex object */

	mutex_desc = acpi_ut_create_internal_object(ACPI_TYPE_MUTEX);
	if (!mutex_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Create the actual OS Mutex */

	status = acpi_os_create_mutex(&mutex_desc->mutex.os_mutex);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	mutex_desc->mutex.sync_level = method_desc->method.sync_level;
	method_desc->method.mutex = mutex_desc;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_begin_method_execution
 *
 * PARAMETERS:  method_node         - Node of the method
 *              obj_desc            - The method object
 *              walk_state          - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Prepare a method for execution.  Parses the method if necessary,
 *              increments the thread count, and waits at the method semaphore
 *              for clearance to execute.
 *
 ******************************************************************************/

acpi_status
acpi_ds_begin_method_execution(struct acpi_namespace_node *method_node,
			       union acpi_operand_object *obj_desc,
			       struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_PTR(ds_begin_method_execution, method_node);

	if (!method_node) {
		return_ACPI_STATUS(AE_NULL_ENTRY);
	}

	/* Prevent wraparound of thread count */

	if (obj_desc->method.thread_count == ACPI_UINT8_MAX) {
		ACPI_ERROR((AE_INFO,
			    "Method reached maximum reentrancy limit (255)"));
		return_ACPI_STATUS(AE_AML_METHOD_LIMIT);
	}

	/*
	 * If this method is serialized, we need to acquire the method mutex.
	 */
	if (obj_desc->method.method_flags & AML_METHOD_SERIALIZED) {
		/*
		 * Create a mutex for the method if it is defined to be Serialized
		 * and a mutex has not already been created. We defer the mutex creation
		 * until a method is actually executed, to minimize the object count
		 */
		if (!obj_desc->method.mutex) {
			status = acpi_ds_create_method_mutex(obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}

		/*
		 * The current_sync_level (per-thread) must be less than or equal to
		 * the sync level of the method. This mechanism provides some
		 * deadlock prevention
		 *
		 * Top-level method invocation has no walk state at this point
		 */
		if (walk_state &&
		    (walk_state->thread->current_sync_level >
		     obj_desc->method.mutex->mutex.sync_level)) {
			ACPI_ERROR((AE_INFO,
				    "Cannot acquire Mutex for method [%4.4s], current SyncLevel is too large (%d)",
				    acpi_ut_get_node_name(method_node),
				    walk_state->thread->current_sync_level));

			return_ACPI_STATUS(AE_AML_MUTEX_ORDER);
		}

		/*
		 * Obtain the method mutex if necessary. Do not acquire mutex for a
		 * recursive call.
		 */
		if (acpi_os_get_thread_id() !=
		    obj_desc->method.mutex->mutex.owner_thread_id) {
			/*
			 * Acquire the method mutex. This releases the interpreter if we
			 * block (and reacquires it before it returns)
			 */
			status =
			    acpi_ex_system_wait_mutex(obj_desc->method.mutex->
						      mutex.os_mutex,
						      ACPI_WAIT_FOREVER);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			/* Update the mutex and walk info and save the original sync_level */
			obj_desc->method.mutex->mutex.owner_thread_id =
				acpi_os_get_thread_id();

			if (walk_state) {
				obj_desc->method.mutex->mutex.
				    original_sync_level =
				    walk_state->thread->current_sync_level;

				walk_state->thread->current_sync_level =
				    obj_desc->method.sync_level;
			} else {
				obj_desc->method.mutex->mutex.
				    original_sync_level =
				    obj_desc->method.mutex->mutex.sync_level;
			}
		}

		/* Always increase acquisition depth */

		obj_desc->method.mutex->mutex.acquisition_depth++;
	}

	/*
	 * Allocate an Owner ID for this method, only if this is the first thread
	 * to begin concurrent execution. We only need one owner_id, even if the
	 * method is invoked recursively.
	 */
	if (!obj_desc->method.owner_id) {
		status = acpi_ut_allocate_owner_id(&obj_desc->method.owner_id);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}
	}

	/*
	 * Increment the method parse tree thread count since it has been
	 * reentered one more time (even if it is the same thread)
	 */
	obj_desc->method.thread_count++;
	return_ACPI_STATUS(status);

      cleanup:
	/* On error, must release the method mutex (if present) */

	if (obj_desc->method.mutex) {
		acpi_os_release_mutex(obj_desc->method.mutex->mutex.os_mutex);
	}
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_call_control_method
 *
 * PARAMETERS:  Thread              - Info for this thread
 *              this_walk_state     - Current walk state
 *              Op                  - Current Op to be walked
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfer execution to a called control method
 *
 ******************************************************************************/

acpi_status
acpi_ds_call_control_method(struct acpi_thread_state *thread,
			    struct acpi_walk_state *this_walk_state,
			    union acpi_parse_object *op)
{
	acpi_status status;
	struct acpi_namespace_node *method_node;
	struct acpi_walk_state *next_walk_state = NULL;
	union acpi_operand_object *obj_desc;
	struct acpi_evaluate_info *info;
	u32 i;

	ACPI_FUNCTION_TRACE_PTR(ds_call_control_method, this_walk_state);

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "Calling method %p, currentstate=%p\n",
			  this_walk_state->prev_op, this_walk_state));

	/*
	 * Get the namespace entry for the control method we are about to call
	 */
	method_node = this_walk_state->method_call_node;
	if (!method_node) {
		return_ACPI_STATUS(AE_NULL_ENTRY);
	}

	obj_desc = acpi_ns_get_attached_object(method_node);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Init for new method, possibly wait on method mutex */

	status = acpi_ds_begin_method_execution(method_node, obj_desc,
						this_walk_state);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Begin method parse/execution. Create a new walk state */

	next_walk_state = acpi_ds_create_walk_state(obj_desc->method.owner_id,
						    NULL, obj_desc, thread);
	if (!next_walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * The resolved arguments were put on the previous walk state's operand
	 * stack. Operands on the previous walk state stack always
	 * start at index 0. Also, null terminate the list of arguments
	 */
	this_walk_state->operands[this_walk_state->num_operands] = NULL;

	/*
	 * Allocate and initialize the evaluation information block
	 * TBD: this is somewhat inefficient, should change interface to
	 * ds_init_aml_walk. For now, keeps this struct off the CPU stack
	 */
	info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
	if (!info) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	info->parameters = &this_walk_state->operands[0];
	info->parameter_type = ACPI_PARAM_ARGS;

	status = acpi_ds_init_aml_walk(next_walk_state, NULL, method_node,
				       obj_desc->method.aml_start,
				       obj_desc->method.aml_length, info,
				       ACPI_IMODE_EXECUTE);

	ACPI_FREE(info);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/*
	 * Delete the operands on the previous walkstate operand stack
	 * (they were copied to new objects)
	 */
	for (i = 0; i < obj_desc->method.param_count; i++) {
		acpi_ut_remove_reference(this_walk_state->operands[i]);
		this_walk_state->operands[i] = NULL;
	}

	/* Clear the operand stack */

	this_walk_state->num_operands = 0;

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "**** Begin nested execution of [%4.4s] **** WalkState=%p\n",
			  method_node->name.ascii, next_walk_state));

	/* Invoke an internal method if necessary */

	if (obj_desc->method.method_flags & AML_METHOD_INTERNAL_ONLY) {
		status = obj_desc->method.implementation(next_walk_state);
	}

	return_ACPI_STATUS(status);

      cleanup:

	/* On error, we must terminate the method properly */

	acpi_ds_terminate_control_method(obj_desc, next_walk_state);
	if (next_walk_state) {
		acpi_ds_delete_walk_state(next_walk_state);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_restart_control_method
 *
 * PARAMETERS:  walk_state          - State for preempted method (caller)
 *              return_desc         - Return value from the called method
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Restart a method that was preempted by another (nested) method
 *              invocation.  Handle the return value (if any) from the callee.
 *
 ******************************************************************************/

acpi_status
acpi_ds_restart_control_method(struct acpi_walk_state *walk_state,
			       union acpi_operand_object *return_desc)
{
	acpi_status status;
	int same_as_implicit_return;

	ACPI_FUNCTION_TRACE_PTR(ds_restart_control_method, walk_state);

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "****Restart [%4.4s] Op %p ReturnValueFromCallee %p\n",
			  acpi_ut_get_node_name(walk_state->method_node),
			  walk_state->method_call_op, return_desc));

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "    ReturnFromThisMethodUsed?=%X ResStack %p Walk %p\n",
			  walk_state->return_used,
			  walk_state->results, walk_state));

	/* Did the called method return a value? */

	if (return_desc) {

		/* Is the implicit return object the same as the return desc? */

		same_as_implicit_return =
		    (walk_state->implicit_return_obj == return_desc);

		/* Are we actually going to use the return value? */

		if (walk_state->return_used) {

			/* Save the return value from the previous method */

			status = acpi_ds_result_push(return_desc, walk_state);
			if (ACPI_FAILURE(status)) {
				acpi_ut_remove_reference(return_desc);
				return_ACPI_STATUS(status);
			}

			/*
			 * Save as THIS method's return value in case it is returned
			 * immediately to yet another method
			 */
			walk_state->return_desc = return_desc;
		}

		/*
		 * The following code is the optional support for the so-called
		 * "implicit return". Some AML code assumes that the last value of the
		 * method is "implicitly" returned to the caller, in the absence of an
		 * explicit return value.
		 *
		 * Just save the last result of the method as the return value.
		 *
		 * NOTE: this is optional because the ASL language does not actually
		 * support this behavior.
		 */
		else if (!acpi_ds_do_implicit_return
			 (return_desc, walk_state, FALSE)
			 || same_as_implicit_return) {
			/*
			 * Delete the return value if it will not be used by the
			 * calling method or remove one reference if the explicit return
			 * is the same as the implicit return value.
			 */
			acpi_ut_remove_reference(return_desc);
		}
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_terminate_control_method
 *
 * PARAMETERS:  method_desc         - Method object
 *              walk_state          - State associated with the method
 *
 * RETURN:      None
 *
 * DESCRIPTION: Terminate a control method.  Delete everything that the method
 *              created, delete all locals and arguments, and delete the parse
 *              tree if requested.
 *
 * MUTEX:       Interpreter is locked
 *
 ******************************************************************************/

void
acpi_ds_terminate_control_method(union acpi_operand_object *method_desc,
				 struct acpi_walk_state *walk_state)
{
	struct acpi_namespace_node *method_node;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_terminate_control_method, walk_state);

	/* method_desc is required, walk_state is optional */

	if (!method_desc) {
		return_VOID;
	}

	if (walk_state) {

		/* Delete all arguments and locals */

		acpi_ds_method_data_delete_all(walk_state);
	}

	/*
	 * If method is serialized, release the mutex and restore the
	 * current sync level for this thread
	 */
	if (method_desc->method.mutex) {

		/* Acquisition Depth handles recursive calls */

		method_desc->method.mutex->mutex.acquisition_depth--;
		if (!method_desc->method.mutex->mutex.acquisition_depth) {
			walk_state->thread->current_sync_level =
			    method_desc->method.mutex->mutex.
			    original_sync_level;

			acpi_os_release_mutex(method_desc->method.mutex->mutex.
					      os_mutex);
			method_desc->method.mutex->mutex.owner_thread_id = ACPI_MUTEX_NOT_ACQUIRED;
		}
	}

	if (walk_state) {
		/*
		 * Delete any objects created by this method during execution.
		 * The method Node is stored in the walk state
		 */
		method_node = walk_state->method_node;

		/*
		 * Delete any namespace objects created anywhere within
		 * the namespace by the execution of this method
		 */
		acpi_ns_delete_namespace_by_owner(method_desc->method.owner_id);
	}

	/* Decrement the thread count on the method */

	if (method_desc->method.thread_count) {
		method_desc->method.thread_count--;
	} else {
		ACPI_ERROR((AE_INFO, "Invalid zero thread count in method"));
	}

	/* Are there any other threads currently executing this method? */

	if (method_desc->method.thread_count) {
		/*
		 * Additional threads. Do not release the owner_id in this case,
		 * we immediately reuse it for the next thread executing this method
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "*** Completed execution of one thread, %d threads remaining\n",
				  method_desc->method.thread_count));
	} else {
		/* This is the only executing thread for this method */

		/*
		 * Support to dynamically change a method from not_serialized to
		 * Serialized if it appears that the method is incorrectly written and
		 * does not support multiple thread execution. The best example of this
		 * is if such a method creates namespace objects and blocks. A second
		 * thread will fail with an AE_ALREADY_EXISTS exception
		 *
		 * This code is here because we must wait until the last thread exits
		 * before creating the synchronization semaphore.
		 */
		if ((method_desc->method.method_flags & AML_METHOD_SERIALIZED)
		    && (!method_desc->method.mutex)) {
			status = acpi_ds_create_method_mutex(method_desc);
		}

		/* No more threads, we can free the owner_id */

		acpi_ut_release_owner_id(&method_desc->method.owner_id);
	}

	return_VOID;
}
