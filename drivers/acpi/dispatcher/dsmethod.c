/******************************************************************************
 *
 * Module Name: dsmethod - Parser/Interpreter interface - control method parsing
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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
 * FUNCTION:    acpi_ds_begin_method_execution
 *
 * PARAMETERS:  method_node         - Node of the method
 *              obj_desc            - The method object
 *              calling_method_node - Caller of this method (if non-null)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Prepare a method for execution.  Parses the method if necessary,
 *              increments the thread count, and waits at the method semaphore
 *              for clearance to execute.
 *
 ******************************************************************************/

acpi_status
acpi_ds_begin_method_execution(struct acpi_namespace_node * method_node,
			       union acpi_operand_object * obj_desc,
			       struct acpi_namespace_node * calling_method_node)
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
	 * If there is a concurrency limit on this method, we need to
	 * obtain a unit from the method semaphore.
	 */
	if (obj_desc->method.semaphore) {
		/*
		 * Allow recursive method calls, up to the reentrancy/concurrency
		 * limit imposed by the SERIALIZED rule and the sync_level method
		 * parameter.
		 *
		 * The point of this code is to avoid permanently blocking a
		 * thread that is making recursive method calls.
		 */
		if (method_node == calling_method_node) {
			if (obj_desc->method.thread_count >=
			    obj_desc->method.concurrency) {
				return_ACPI_STATUS(AE_AML_METHOD_LIMIT);
			}
		}

		/*
		 * Get a unit from the method semaphore. This releases the
		 * interpreter if we block (then reacquires it)
		 */
		status =
		    acpi_ex_system_wait_semaphore(obj_desc->method.semaphore,
						  ACPI_WAIT_FOREVER);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
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
	/* On error, must signal the method semaphore if present */

	if (obj_desc->method.semaphore) {
		(void)acpi_os_signal_semaphore(obj_desc->method.semaphore, 1);
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
	struct acpi_parameter_info info;
	u32 i;

	ACPI_FUNCTION_TRACE_PTR(ds_call_control_method, this_walk_state);

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "Execute method %p, currentstate=%p\n",
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

	/* Init for new method, possibly wait on concurrency semaphore */

	status = acpi_ds_begin_method_execution(method_node, obj_desc,
						this_walk_state->method_node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * 1) Parse the method. All "normal" methods are parsed for each execution.
	 * Internal methods (_OSI, etc.) do not require parsing.
	 */
	if (!(obj_desc->method.method_flags & AML_METHOD_INTERNAL_ONLY)) {

		/* Create a new walk state for the parse */

		next_walk_state =
		    acpi_ds_create_walk_state(obj_desc->method.owner_id, op,
					      obj_desc, NULL);
		if (!next_walk_state) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/* Create and init a parse tree root */

		op = acpi_ps_create_scope_op();
		if (!op) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		status = acpi_ds_init_aml_walk(next_walk_state, op, method_node,
					       obj_desc->method.aml_start,
					       obj_desc->method.aml_length,
					       NULL, 1);
		if (ACPI_FAILURE(status)) {
			acpi_ps_delete_parse_tree(op);
			goto cleanup;
		}

		/* Begin AML parse (deletes next_walk_state) */

		status = acpi_ps_parse_aml(next_walk_state);
		acpi_ps_delete_parse_tree(op);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}
	}

	/* 2) Begin method execution. Create a new walk state */

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

	info.parameters = &this_walk_state->operands[0];
	info.parameter_type = ACPI_PARAM_ARGS;

	status = acpi_ds_init_aml_walk(next_walk_state, NULL, method_node,
				       obj_desc->method.aml_start,
				       obj_desc->method.aml_length, &info, 3);
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
			  "Starting nested execution, newstate=%p\n",
			  next_walk_state));

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
			  (char *)&walk_state->method_node->name,
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
	 * Lock the parser while we terminate this method.
	 * If this is the last thread executing the method,
	 * we have additional cleanup to perform
	 */
	status = acpi_ut_acquire_mutex(ACPI_MTX_PARSER);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/* Signal completion of the execution of this method if necessary */

	if (method_desc->method.semaphore) {
		status =
		    acpi_os_signal_semaphore(method_desc->method.semaphore, 1);
		if (ACPI_FAILURE(status)) {

			/* Ignore error and continue */

			ACPI_EXCEPTION((AE_INFO, status,
					"Could not signal method semaphore"));
		}
	}

	if (walk_state) {
		/*
		 * Delete any objects created by this method during execution.
		 * The method Node is stored in the walk state
		 */
		method_node = walk_state->method_node;

		/* Lock namespace for possible update */

		status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/*
		 * Delete any namespace entries created immediately underneath
		 * the method
		 */
		if (method_node && method_node->child) {
			acpi_ns_delete_namespace_subtree(method_node);
		}

		/*
		 * Delete any namespace entries created anywhere else within
		 * the namespace by the execution of this method
		 */
		acpi_ns_delete_namespace_by_owner(method_desc->method.owner_id);
		status = acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
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
		 * does not support multiple thread execution.  The best example of this
		 * is if such a method creates namespace objects and blocks.  A second
		 * thread will fail with an AE_ALREADY_EXISTS exception
		 *
		 * This code is here because we must wait until the last thread exits
		 * before creating the synchronization semaphore.
		 */
		if ((method_desc->method.concurrency == 1) &&
		    (!method_desc->method.semaphore)) {
			status = acpi_os_create_semaphore(1, 1,
							  &method_desc->method.
							  semaphore);
		}

		/* No more threads, we can free the owner_id */

		acpi_ut_release_owner_id(&method_desc->method.owner_id);
	}

      exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_PARSER);
	return_VOID;
}

#ifdef ACPI_INIT_PARSE_METHODS
	/*
	 * Note 11/2005: Removed this code to parse all methods during table
	 * load because it causes problems if there are any errors during the
	 * parse. Also, it seems like overkill and we probably don't want to
	 * abort a table load because of an issue with a single method.
	 */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_parse_method
 *
 * PARAMETERS:  Node        - Method node
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse the AML that is associated with the method.
 *
 * MUTEX:       Assumes parser is locked
 *
 ******************************************************************************/

acpi_status acpi_ds_parse_method(struct acpi_namespace_node *node)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	union acpi_parse_object *op;
	struct acpi_walk_state *walk_state;

	ACPI_FUNCTION_TRACE_PTR(ds_parse_method, node);

	/* Parameter Validation */

	if (!node) {
		return_ACPI_STATUS(AE_NULL_ENTRY);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
			  "**** Parsing [%4.4s] **** NamedObj=%p\n",
			  acpi_ut_get_node_name(node), node));

	/* Extract the method object from the method Node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Create a mutex for the method if there is a concurrency limit */

	if ((obj_desc->method.concurrency != ACPI_INFINITE_CONCURRENCY) &&
	    (!obj_desc->method.semaphore)) {
		status = acpi_os_create_semaphore(obj_desc->method.concurrency,
						  obj_desc->method.concurrency,
						  &obj_desc->method.semaphore);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * Allocate a new parser op to be the root of the parsed
	 * method tree
	 */
	op = acpi_ps_alloc_op(AML_METHOD_OP);
	if (!op) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Init new op with the method name and pointer back to the Node */

	acpi_ps_set_name(op, node->name.integer);
	op->common.node = node;

	/*
	 * Get a new owner_id for objects created by this method. Namespace
	 * objects (such as Operation Regions) can be created during the
	 * first pass parse.
	 */
	status = acpi_ut_allocate_owner_id(&obj_desc->method.owner_id);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Create and initialize a new walk state */

	walk_state =
	    acpi_ds_create_walk_state(obj_desc->method.owner_id, NULL, NULL,
				      NULL);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup2;
	}

	status = acpi_ds_init_aml_walk(walk_state, op, node,
				       obj_desc->method.aml_start,
				       obj_desc->method.aml_length, NULL, 1);
	if (ACPI_FAILURE(status)) {
		acpi_ds_delete_walk_state(walk_state);
		goto cleanup2;
	}

	/*
	 * Parse the method, first pass
	 *
	 * The first pass load is where newly declared named objects are added into
	 * the namespace.  Actual evaluation of the named objects (what would be
	 * called a "second pass") happens during the actual execution of the
	 * method so that operands to the named objects can take on dynamic
	 * run-time values.
	 */
	status = acpi_ps_parse_aml(walk_state);
	if (ACPI_FAILURE(status)) {
		goto cleanup2;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
			  "**** [%4.4s] Parsed **** NamedObj=%p Op=%p\n",
			  acpi_ut_get_node_name(node), node, op));

	/*
	 * Delete the parse tree. We simply re-parse the method for every
	 * execution since there isn't much overhead (compared to keeping lots
	 * of parse trees around)
	 */
	acpi_ns_delete_namespace_subtree(node);
	acpi_ns_delete_namespace_by_owner(obj_desc->method.owner_id);

      cleanup2:
	acpi_ut_release_owner_id(&obj_desc->method.owner_id);

      cleanup:
	acpi_ps_delete_parse_tree(op);
	return_ACPI_STATUS(status);
}
#endif
