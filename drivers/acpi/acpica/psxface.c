/******************************************************************************
 *
 * Module Name: psxface - Parser external interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2011, Intel Corp.
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
#include "acdispat.h"
#include "acinterp.h"
#include "actables.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psxface")

/* Local Prototypes */
static void acpi_ps_start_trace(struct acpi_evaluate_info *info);

static void acpi_ps_stop_trace(struct acpi_evaluate_info *info);

static void
acpi_ps_update_parameter_list(struct acpi_evaluate_info *info, u16 action);

/*******************************************************************************
 *
 * FUNCTION:    acpi_debug_trace
 *
 * PARAMETERS:  method_name     - Valid ACPI name string
 *              debug_level     - Optional level mask. 0 to use default
 *              debug_layer     - Optional layer mask. 0 to use default
 *              Flags           - bit 1: one shot(1) or persistent(0)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: External interface to enable debug tracing during control
 *              method execution
 *
 ******************************************************************************/

acpi_status
acpi_debug_trace(char *name, u32 debug_level, u32 debug_layer, u32 flags)
{
	acpi_status status;

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* TBDs: Validate name, allow full path or just nameseg */

	acpi_gbl_trace_method_name = *ACPI_CAST_PTR(u32, name);
	acpi_gbl_trace_flags = flags;

	if (debug_level) {
		acpi_gbl_trace_dbg_level = debug_level;
	}
	if (debug_layer) {
		acpi_gbl_trace_dbg_layer = debug_layer;
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_start_trace
 *
 * PARAMETERS:  Info        - Method info struct
 *
 * RETURN:      None
 *
 * DESCRIPTION: Start control method execution trace
 *
 ******************************************************************************/

static void acpi_ps_start_trace(struct acpi_evaluate_info *info)
{
	acpi_status status;

	ACPI_FUNCTION_ENTRY();

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return;
	}

	if ((!acpi_gbl_trace_method_name) ||
	    (acpi_gbl_trace_method_name != info->resolved_node->name.integer)) {
		goto exit;
	}

	acpi_gbl_original_dbg_level = acpi_dbg_level;
	acpi_gbl_original_dbg_layer = acpi_dbg_layer;

	acpi_dbg_level = 0x00FFFFFF;
	acpi_dbg_layer = ACPI_UINT32_MAX;

	if (acpi_gbl_trace_dbg_level) {
		acpi_dbg_level = acpi_gbl_trace_dbg_level;
	}
	if (acpi_gbl_trace_dbg_layer) {
		acpi_dbg_layer = acpi_gbl_trace_dbg_layer;
	}

      exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_stop_trace
 *
 * PARAMETERS:  Info        - Method info struct
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop control method execution trace
 *
 ******************************************************************************/

static void acpi_ps_stop_trace(struct acpi_evaluate_info *info)
{
	acpi_status status;

	ACPI_FUNCTION_ENTRY();

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return;
	}

	if ((!acpi_gbl_trace_method_name) ||
	    (acpi_gbl_trace_method_name != info->resolved_node->name.integer)) {
		goto exit;
	}

	/* Disable further tracing if type is one-shot */

	if (acpi_gbl_trace_flags & 1) {
		acpi_gbl_trace_method_name = 0;
		acpi_gbl_trace_dbg_level = 0;
		acpi_gbl_trace_dbg_layer = 0;
	}

	acpi_dbg_level = acpi_gbl_original_dbg_level;
	acpi_dbg_layer = acpi_gbl_original_dbg_layer;

      exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_execute_method
 *
 * PARAMETERS:  Info            - Method info block, contains:
 *                  Node            - Method Node to execute
 *                  obj_desc        - Method object
 *                  Parameters      - List of parameters to pass to the method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  parameter_type  - Type of Parameter list
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  pass_number     - Parse or execute pass
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method
 *
 ******************************************************************************/

acpi_status acpi_ps_execute_method(struct acpi_evaluate_info *info)
{
	acpi_status status;
	union acpi_parse_object *op;
	struct acpi_walk_state *walk_state;

	ACPI_FUNCTION_TRACE(ps_execute_method);

	/* Quick validation of DSDT header */

	acpi_tb_check_dsdt_header();

	/* Validate the Info and method Node */

	if (!info || !info->resolved_node) {
		return_ACPI_STATUS(AE_NULL_ENTRY);
	}

	/* Init for new method, wait on concurrency semaphore */

	status =
	    acpi_ds_begin_method_execution(info->resolved_node, info->obj_desc,
					   NULL);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * The caller "owns" the parameters, so give each one an extra reference
	 */
	acpi_ps_update_parameter_list(info, REF_INCREMENT);

	/* Begin tracing if requested */

	acpi_ps_start_trace(info);

	/*
	 * Execute the method. Performs parse simultaneously
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
			  "**** Begin Method Parse/Execute [%4.4s] **** Node=%p Obj=%p\n",
			  info->resolved_node->name.ascii, info->resolved_node,
			  info->obj_desc));

	/* Create and init a Root Node */

	op = acpi_ps_create_scope_op();
	if (!op) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Create and initialize a new walk state */

	info->pass_number = ACPI_IMODE_EXECUTE;
	walk_state =
	    acpi_ds_create_walk_state(info->obj_desc->method.owner_id, NULL,
				      NULL, NULL);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	status = acpi_ds_init_aml_walk(walk_state, op, info->resolved_node,
				       info->obj_desc->method.aml_start,
				       info->obj_desc->method.aml_length, info,
				       info->pass_number);
	if (ACPI_FAILURE(status)) {
		acpi_ds_delete_walk_state(walk_state);
		goto cleanup;
	}

	if (info->obj_desc->method.info_flags & ACPI_METHOD_MODULE_LEVEL) {
		walk_state->parse_flags |= ACPI_PARSE_MODULE_LEVEL;
	}

	/* Invoke an internal method if necessary */

	if (info->obj_desc->method.info_flags & ACPI_METHOD_INTERNAL_ONLY) {
		status =
		    info->obj_desc->method.dispatch.implementation(walk_state);
		info->return_object = walk_state->return_desc;

		/* Cleanup states */

		acpi_ds_scope_stack_clear(walk_state);
		acpi_ps_cleanup_scope(&walk_state->parser_state);
		acpi_ds_terminate_control_method(walk_state->method_desc,
						 walk_state);
		acpi_ds_delete_walk_state(walk_state);
		goto cleanup;
	}

	/*
	 * Start method evaluation with an implicit return of zero.
	 * This is done for Windows compatibility.
	 */
	if (acpi_gbl_enable_interpreter_slack) {
		walk_state->implicit_return_obj =
		    acpi_ut_create_integer_object((u64) 0);
		if (!walk_state->implicit_return_obj) {
			status = AE_NO_MEMORY;
			acpi_ds_delete_walk_state(walk_state);
			goto cleanup;
		}
	}

	/* Parse the AML */

	status = acpi_ps_parse_aml(walk_state);

	/* walk_state was deleted by parse_aml */

      cleanup:
	acpi_ps_delete_parse_tree(op);

	/* End optional tracing */

	acpi_ps_stop_trace(info);

	/* Take away the extra reference that we gave the parameters above */

	acpi_ps_update_parameter_list(info, REF_DECREMENT);

	/* Exit now if error above */

	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * If the method has returned an object, signal this to the caller with
	 * a control exception code
	 */
	if (info->return_object) {
		ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "Method returned ObjDesc=%p\n",
				  info->return_object));
		ACPI_DUMP_STACK_ENTRY(info->return_object);

		status = AE_CTRL_RETURN_VALUE;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_update_parameter_list
 *
 * PARAMETERS:  Info            - See struct acpi_evaluate_info
 *                                (Used: parameter_type and Parameters)
 *              Action          - Add or Remove reference
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Update reference count on all method parameter objects
 *
 ******************************************************************************/

static void
acpi_ps_update_parameter_list(struct acpi_evaluate_info *info, u16 action)
{
	u32 i;

	if (info->parameters) {

		/* Update reference count for each parameter */

		for (i = 0; info->parameters[i]; i++) {

			/* Ignore errors, just do them all */

			(void)acpi_ut_update_object_reference(info->
							      parameters[i],
							      action);
		}
	}
}
