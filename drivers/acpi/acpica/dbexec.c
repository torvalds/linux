/*******************************************************************************
 *
 * Module Name: dbexec - debugger control method execution
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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
#include "acdebug.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbexec")

static struct acpi_db_method_info acpi_gbl_db_method_info;

/* Local prototypes */

static acpi_status
acpi_db_execute_method(struct acpi_db_method_info *info,
		       struct acpi_buffer *return_obj);

static acpi_status acpi_db_execute_setup(struct acpi_db_method_info *info);

static u32 acpi_db_get_outstanding_allocations(void);

static void ACPI_SYSTEM_XFACE acpi_db_method_thread(void *context);

static acpi_status
acpi_db_execution_walk(acpi_handle obj_handle,
		       u32 nesting_level, void *context, void **return_value);

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_delete_objects
 *
 * PARAMETERS:  count               - Count of objects in the list
 *              objects             - Array of ACPI_OBJECTs to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete a list of ACPI_OBJECTS. Handles packages and nested
 *              packages via recursion.
 *
 ******************************************************************************/

void acpi_db_delete_objects(u32 count, union acpi_object *objects)
{
	u32 i;

	for (i = 0; i < count; i++) {
		switch (objects[i].type) {
		case ACPI_TYPE_BUFFER:

			ACPI_FREE(objects[i].buffer.pointer);
			break;

		case ACPI_TYPE_PACKAGE:

			/* Recursive call to delete package elements */

			acpi_db_delete_objects(objects[i].package.count,
					       objects[i].package.elements);

			/* Free the elements array */

			ACPI_FREE(objects[i].package.elements);
			break;

		default:

			break;
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_execute_method
 *
 * PARAMETERS:  info            - Valid info segment
 *              return_obj      - Where to put return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method.
 *
 ******************************************************************************/

static acpi_status
acpi_db_execute_method(struct acpi_db_method_info *info,
		       struct acpi_buffer *return_obj)
{
	acpi_status status;
	struct acpi_object_list param_objects;
	union acpi_object params[ACPI_DEBUGGER_MAX_ARGS + 1];
	u32 i;

	ACPI_FUNCTION_TRACE(db_execute_method);

	if (acpi_gbl_db_output_to_file && !acpi_dbg_level) {
		acpi_os_printf("Warning: debug output is not enabled!\n");
	}

	param_objects.count = 0;
	param_objects.pointer = NULL;

	/* Pass through any command-line arguments */

	if (info->args && info->args[0]) {

		/* Get arguments passed on the command line */

		for (i = 0; (info->args[i] && *(info->args[i])); i++) {

			/* Convert input string (token) to an actual union acpi_object */

			status = acpi_db_convert_to_object(info->types[i],
							   info->args[i],
							   &params[i]);
			if (ACPI_FAILURE(status)) {
				ACPI_EXCEPTION((AE_INFO, status,
						"While parsing method arguments"));
				goto cleanup;
			}
		}

		param_objects.count = i;
		param_objects.pointer = params;
	}

	/* Prepare for a return object of arbitrary size */

	return_obj->pointer = acpi_gbl_db_buffer;
	return_obj->length = ACPI_DEBUG_BUFFER_SIZE;

	/* Do the actual method execution */

	acpi_gbl_method_executing = TRUE;
	status = acpi_evaluate_object(NULL, info->pathname,
				      &param_objects, return_obj);

	acpi_gbl_cm_single_step = FALSE;
	acpi_gbl_method_executing = FALSE;

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"while executing %s from debugger",
				info->pathname));

		if (status == AE_BUFFER_OVERFLOW) {
			ACPI_ERROR((AE_INFO,
				    "Possible overflow of internal debugger "
				    "buffer (size 0x%X needed 0x%X)",
				    ACPI_DEBUG_BUFFER_SIZE,
				    (u32)return_obj->length));
		}
	}

cleanup:
	acpi_db_delete_objects(param_objects.count, params);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_execute_setup
 *
 * PARAMETERS:  info            - Valid method info
 *
 * RETURN:      None
 *
 * DESCRIPTION: Setup info segment prior to method execution
 *
 ******************************************************************************/

static acpi_status acpi_db_execute_setup(struct acpi_db_method_info *info)
{
	acpi_status status;

	ACPI_FUNCTION_NAME(db_execute_setup);

	/* Catenate the current scope to the supplied name */

	info->pathname[0] = 0;
	if ((info->name[0] != '\\') && (info->name[0] != '/')) {
		if (acpi_ut_safe_strcat(info->pathname, sizeof(info->pathname),
					acpi_gbl_db_scope_buf)) {
			status = AE_BUFFER_OVERFLOW;
			goto error_exit;
		}
	}

	if (acpi_ut_safe_strcat(info->pathname, sizeof(info->pathname),
				info->name)) {
		status = AE_BUFFER_OVERFLOW;
		goto error_exit;
	}

	acpi_db_prep_namestring(info->pathname);

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);
	acpi_os_printf("Evaluating %s\n", info->pathname);

	if (info->flags & EX_SINGLE_STEP) {
		acpi_gbl_cm_single_step = TRUE;
		acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
	}

	else {
		/* No single step, allow redirection to a file */

		acpi_db_set_output_destination(ACPI_DB_REDIRECTABLE_OUTPUT);
	}

	return (AE_OK);

error_exit:

	ACPI_EXCEPTION((AE_INFO, status, "During setup for method execution"));
	return (status);
}

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
u32 acpi_db_get_cache_info(struct acpi_memory_list *cache)
{

	return (cache->total_allocated - cache->total_freed -
		cache->current_depth);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_outstanding_allocations
 *
 * PARAMETERS:  None
 *
 * RETURN:      Current global allocation count minus cache entries
 *
 * DESCRIPTION: Determine the current number of "outstanding" allocations --
 *              those allocations that have not been freed and also are not
 *              in one of the various object caches.
 *
 ******************************************************************************/

static u32 acpi_db_get_outstanding_allocations(void)
{
	u32 outstanding = 0;

#ifdef ACPI_DBG_TRACK_ALLOCATIONS

	outstanding += acpi_db_get_cache_info(acpi_gbl_state_cache);
	outstanding += acpi_db_get_cache_info(acpi_gbl_ps_node_cache);
	outstanding += acpi_db_get_cache_info(acpi_gbl_ps_node_ext_cache);
	outstanding += acpi_db_get_cache_info(acpi_gbl_operand_cache);
#endif

	return (outstanding);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_execution_walk
 *
 * PARAMETERS:  WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method. Name is relative to the current
 *              scope.
 *
 ******************************************************************************/

static acpi_status
acpi_db_execution_walk(acpi_handle obj_handle,
		       u32 nesting_level, void *context, void **return_value)
{
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	struct acpi_buffer return_obj;
	acpi_status status;

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc->method.param_count) {
		return (AE_OK);
	}

	return_obj.pointer = NULL;
	return_obj.length = ACPI_ALLOCATE_BUFFER;

	acpi_ns_print_node_pathname(node, "Evaluating");

	/* Do the actual method execution */

	acpi_os_printf("\n");
	acpi_gbl_method_executing = TRUE;

	status = acpi_evaluate_object(node, NULL, NULL, &return_obj);

	acpi_os_printf("Evaluation of [%4.4s] returned %s\n",
		       acpi_ut_get_node_name(node),
		       acpi_format_exception(status));

	acpi_gbl_method_executing = FALSE;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_execute
 *
 * PARAMETERS:  name                - Name of method to execute
 *              args                - Parameters to the method
 *              Types               -
 *              flags               - single step/no single step
 *
 * RETURN:      None
 *
 * DESCRIPTION: Execute a control method. Name is relative to the current
 *              scope.
 *
 ******************************************************************************/

void
acpi_db_execute(char *name, char **args, acpi_object_type *types, u32 flags)
{
	acpi_status status;
	struct acpi_buffer return_obj;
	char *name_string;

#ifdef ACPI_DEBUG_OUTPUT
	u32 previous_allocations;
	u32 allocations;
#endif

	/*
	 * Allow one execution to be performed by debugger or single step
	 * execution will be dead locked by the interpreter mutexes.
	 */
	if (acpi_gbl_method_executing) {
		acpi_os_printf("Only one debugger execution is allowed.\n");
		return;
	}
#ifdef ACPI_DEBUG_OUTPUT
	/* Memory allocation tracking */

	previous_allocations = acpi_db_get_outstanding_allocations();
#endif

	if (*name == '*') {
		(void)acpi_walk_namespace(ACPI_TYPE_METHOD, ACPI_ROOT_OBJECT,
					  ACPI_UINT32_MAX,
					  acpi_db_execution_walk, NULL, NULL,
					  NULL);
		return;
	}

	name_string = ACPI_ALLOCATE(strlen(name) + 1);
	if (!name_string) {
		return;
	}

	memset(&acpi_gbl_db_method_info, 0, sizeof(struct acpi_db_method_info));
	strcpy(name_string, name);
	acpi_ut_strupr(name_string);

	/* Subcommand to Execute all predefined names in the namespace */

	if (!strncmp(name_string, "PREDEF", 6)) {
		acpi_db_evaluate_predefined_names();
		ACPI_FREE(name_string);
		return;
	}

	acpi_gbl_db_method_info.name = name_string;
	acpi_gbl_db_method_info.args = args;
	acpi_gbl_db_method_info.types = types;
	acpi_gbl_db_method_info.flags = flags;

	return_obj.pointer = NULL;
	return_obj.length = ACPI_ALLOCATE_BUFFER;

	status = acpi_db_execute_setup(&acpi_gbl_db_method_info);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(name_string);
		return;
	}

	/* Get the NS node, determines existence also */

	status = acpi_get_handle(NULL, acpi_gbl_db_method_info.pathname,
				 &acpi_gbl_db_method_info.method);
	if (ACPI_SUCCESS(status)) {
		status = acpi_db_execute_method(&acpi_gbl_db_method_info,
						&return_obj);
	}
	ACPI_FREE(name_string);

	/*
	 * Allow any handlers in separate threads to complete.
	 * (Such as Notify handlers invoked from AML executed above).
	 */
	acpi_os_sleep((u64)10);

#ifdef ACPI_DEBUG_OUTPUT

	/* Memory allocation tracking */

	allocations =
	    acpi_db_get_outstanding_allocations() - previous_allocations;

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);

	if (allocations > 0) {
		acpi_os_printf
		    ("0x%X Outstanding allocations after evaluation of %s\n",
		     allocations, acpi_gbl_db_method_info.pathname);
	}
#endif

	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Evaluation of %s failed with status %s\n",
			       acpi_gbl_db_method_info.pathname,
			       acpi_format_exception(status));
	} else {
		/* Display a return object, if any */

		if (return_obj.length) {
			acpi_os_printf("Evaluation of %s returned object %p, "
				       "external buffer length %X\n",
				       acpi_gbl_db_method_info.pathname,
				       return_obj.pointer,
				       (u32)return_obj.length);

			acpi_db_dump_external_object(return_obj.pointer, 1);

			/* Dump a _PLD buffer if present */

			if (ACPI_COMPARE_NAME
			    ((ACPI_CAST_PTR
			      (struct acpi_namespace_node,
			       acpi_gbl_db_method_info.method)->name.ascii),
			     METHOD_NAME__PLD)) {
				acpi_db_dump_pld_buffer(return_obj.pointer);
			}
		} else {
			acpi_os_printf
			    ("No object was returned from evaluation of %s\n",
			     acpi_gbl_db_method_info.pathname);
		}
	}

	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_method_thread
 *
 * PARAMETERS:  context             - Execution info segment
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread. Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE acpi_db_method_thread(void *context)
{
	acpi_status status;
	struct acpi_db_method_info *info = context;
	struct acpi_db_method_info local_info;
	u32 i;
	u8 allow;
	struct acpi_buffer return_obj;

	/*
	 * acpi_gbl_db_method_info.Arguments will be passed as method arguments.
	 * Prevent acpi_gbl_db_method_info from being modified by multiple threads
	 * concurrently.
	 *
	 * Note: The arguments we are passing are used by the ASL test suite
	 * (aslts). Do not change them without updating the tests.
	 */
	(void)acpi_os_wait_semaphore(info->info_gate, 1, ACPI_WAIT_FOREVER);

	if (info->init_args) {
		acpi_db_uint32_to_hex_string(info->num_created,
					     info->index_of_thread_str);
		acpi_db_uint32_to_hex_string((u32)acpi_os_get_thread_id(),
					     info->id_of_thread_str);
	}

	if (info->threads && (info->num_created < info->num_threads)) {
		info->threads[info->num_created++] = acpi_os_get_thread_id();
	}

	local_info = *info;
	local_info.args = local_info.arguments;
	local_info.arguments[0] = local_info.num_threads_str;
	local_info.arguments[1] = local_info.id_of_thread_str;
	local_info.arguments[2] = local_info.index_of_thread_str;
	local_info.arguments[3] = NULL;

	local_info.types = local_info.arg_types;

	(void)acpi_os_signal_semaphore(info->info_gate, 1);

	for (i = 0; i < info->num_loops; i++) {
		status = acpi_db_execute_method(&local_info, &return_obj);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    ("%s During evaluation of %s at iteration %X\n",
			     acpi_format_exception(status), info->pathname, i);
			if (status == AE_ABORT_METHOD) {
				break;
			}
		}
#if 0
		if ((i % 100) == 0) {
			acpi_os_printf("%u loops, Thread 0x%x\n",
				       i, acpi_os_get_thread_id());
		}

		if (return_obj.length) {
			acpi_os_printf
			    ("Evaluation of %s returned object %p Buflen %X\n",
			     info->pathname, return_obj.pointer,
			     (u32)return_obj.length);
			acpi_db_dump_external_object(return_obj.pointer, 1);
		}
#endif
	}

	/* Signal our completion */

	allow = 0;
	(void)acpi_os_wait_semaphore(info->thread_complete_gate,
				     1, ACPI_WAIT_FOREVER);
	info->num_completed++;

	if (info->num_completed == info->num_threads) {

		/* Do signal for main thread once only */
		allow = 1;
	}

	(void)acpi_os_signal_semaphore(info->thread_complete_gate, 1);

	if (allow) {
		status = acpi_os_signal_semaphore(info->main_thread_gate, 1);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    ("Could not signal debugger thread sync semaphore, %s\n",
			     acpi_format_exception(status));
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_create_execution_threads
 *
 * PARAMETERS:  num_threads_arg         - Number of threads to create
 *              num_loops_arg           - Loop count for the thread(s)
 *              method_name_arg         - Control method to execute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create threads to execute method(s)
 *
 ******************************************************************************/

void
acpi_db_create_execution_threads(char *num_threads_arg,
				 char *num_loops_arg, char *method_name_arg)
{
	acpi_status status;
	u32 num_threads;
	u32 num_loops;
	u32 i;
	u32 size;
	acpi_mutex main_thread_gate;
	acpi_mutex thread_complete_gate;
	acpi_mutex info_gate;

	/* Get the arguments */

	num_threads = strtoul(num_threads_arg, NULL, 0);
	num_loops = strtoul(num_loops_arg, NULL, 0);

	if (!num_threads || !num_loops) {
		acpi_os_printf("Bad argument: Threads %X, Loops %X\n",
			       num_threads, num_loops);
		return;
	}

	/*
	 * Create the semaphore for synchronization of
	 * the created threads with the main thread.
	 */
	status = acpi_os_create_semaphore(1, 0, &main_thread_gate);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not create semaphore for "
			       "synchronization with the main thread, %s\n",
			       acpi_format_exception(status));
		return;
	}

	/*
	 * Create the semaphore for synchronization
	 * between the created threads.
	 */
	status = acpi_os_create_semaphore(1, 1, &thread_complete_gate);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not create semaphore for "
			       "synchronization between the created threads, %s\n",
			       acpi_format_exception(status));

		(void)acpi_os_delete_semaphore(main_thread_gate);
		return;
	}

	status = acpi_os_create_semaphore(1, 1, &info_gate);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not create semaphore for "
			       "synchronization of AcpiGbl_DbMethodInfo, %s\n",
			       acpi_format_exception(status));

		(void)acpi_os_delete_semaphore(thread_complete_gate);
		(void)acpi_os_delete_semaphore(main_thread_gate);
		return;
	}

	memset(&acpi_gbl_db_method_info, 0, sizeof(struct acpi_db_method_info));

	/* Array to store IDs of threads */

	acpi_gbl_db_method_info.num_threads = num_threads;
	size = sizeof(acpi_thread_id) * acpi_gbl_db_method_info.num_threads;

	acpi_gbl_db_method_info.threads = acpi_os_allocate(size);
	if (acpi_gbl_db_method_info.threads == NULL) {
		acpi_os_printf("No memory for thread IDs array\n");
		(void)acpi_os_delete_semaphore(main_thread_gate);
		(void)acpi_os_delete_semaphore(thread_complete_gate);
		(void)acpi_os_delete_semaphore(info_gate);
		return;
	}
	memset(acpi_gbl_db_method_info.threads, 0, size);

	/* Setup the context to be passed to each thread */

	acpi_gbl_db_method_info.name = method_name_arg;
	acpi_gbl_db_method_info.flags = 0;
	acpi_gbl_db_method_info.num_loops = num_loops;
	acpi_gbl_db_method_info.main_thread_gate = main_thread_gate;
	acpi_gbl_db_method_info.thread_complete_gate = thread_complete_gate;
	acpi_gbl_db_method_info.info_gate = info_gate;

	/* Init arguments to be passed to method */

	acpi_gbl_db_method_info.init_args = 1;
	acpi_gbl_db_method_info.args = acpi_gbl_db_method_info.arguments;
	acpi_gbl_db_method_info.arguments[0] =
	    acpi_gbl_db_method_info.num_threads_str;
	acpi_gbl_db_method_info.arguments[1] =
	    acpi_gbl_db_method_info.id_of_thread_str;
	acpi_gbl_db_method_info.arguments[2] =
	    acpi_gbl_db_method_info.index_of_thread_str;
	acpi_gbl_db_method_info.arguments[3] = NULL;

	acpi_gbl_db_method_info.types = acpi_gbl_db_method_info.arg_types;
	acpi_gbl_db_method_info.arg_types[0] = ACPI_TYPE_INTEGER;
	acpi_gbl_db_method_info.arg_types[1] = ACPI_TYPE_INTEGER;
	acpi_gbl_db_method_info.arg_types[2] = ACPI_TYPE_INTEGER;

	acpi_db_uint32_to_hex_string(num_threads,
				     acpi_gbl_db_method_info.num_threads_str);

	status = acpi_db_execute_setup(&acpi_gbl_db_method_info);
	if (ACPI_FAILURE(status)) {
		goto cleanup_and_exit;
	}

	/* Get the NS node, determines existence also */

	status = acpi_get_handle(NULL, acpi_gbl_db_method_info.pathname,
				 &acpi_gbl_db_method_info.method);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("%s Could not get handle for %s\n",
			       acpi_format_exception(status),
			       acpi_gbl_db_method_info.pathname);
		goto cleanup_and_exit;
	}

	/* Create the threads */

	acpi_os_printf("Creating %X threads to execute %X times each\n",
		       num_threads, num_loops);

	for (i = 0; i < (num_threads); i++) {
		status =
		    acpi_os_execute(OSL_DEBUGGER_EXEC_THREAD,
				    acpi_db_method_thread,
				    &acpi_gbl_db_method_info);
		if (ACPI_FAILURE(status)) {
			break;
		}
	}

	/* Wait for all threads to complete */

	(void)acpi_os_wait_semaphore(main_thread_gate, 1, ACPI_WAIT_FOREVER);

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);
	acpi_os_printf("All threads (%X) have completed\n", num_threads);
	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);

cleanup_and_exit:

	/* Cleanup and exit */

	(void)acpi_os_delete_semaphore(main_thread_gate);
	(void)acpi_os_delete_semaphore(thread_complete_gate);
	(void)acpi_os_delete_semaphore(info_gate);

	acpi_os_free(acpi_gbl_db_method_info.threads);
	acpi_gbl_db_method_info.threads = NULL;
}
