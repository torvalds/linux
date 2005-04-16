/******************************************************************************
 *
 * Module Name: psxface - Parser external interfaces
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


#include <acpi/acpi.h>
#include <acpi/acparser.h>
#include <acpi/acdispat.h>
#include <acpi/acinterp.h>
#include <acpi/acnamesp.h>


#define _COMPONENT          ACPI_PARSER
	 ACPI_MODULE_NAME    ("psxface")


/*******************************************************************************
 *
 * FUNCTION:    acpi_psx_execute
 *
 * PARAMETERS:  Info->Node          - A method object containing both the AML
 *                                    address and length.
 *              **Params            - List of parameters to pass to method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *              **return_obj_desc   - Return object from execution of the
 *                                    method.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method
 *
 ******************************************************************************/

acpi_status
acpi_psx_execute (
	struct acpi_parameter_info      *info)
{
	acpi_status                     status;
	union acpi_operand_object       *obj_desc;
	u32                             i;
	union acpi_parse_object         *op;
	struct acpi_walk_state          *walk_state;


	ACPI_FUNCTION_TRACE ("psx_execute");


	/* Validate the Node and get the attached object */

	if (!info || !info->node) {
		return_ACPI_STATUS (AE_NULL_ENTRY);
	}

	obj_desc = acpi_ns_get_attached_object (info->node);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NULL_OBJECT);
	}

	/* Init for new method, wait on concurrency semaphore */

	status = acpi_ds_begin_method_execution (info->node, obj_desc, NULL);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if ((info->parameter_type == ACPI_PARAM_ARGS) &&
		(info->parameters)) {
		/*
		 * The caller "owns" the parameters, so give each one an extra
		 * reference
		 */
		for (i = 0; info->parameters[i]; i++) {
			acpi_ut_add_reference (info->parameters[i]);
		}
	}

	/*
	 * 1) Perform the first pass parse of the method to enter any
	 * named objects that it creates into the namespace
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
		"**** Begin Method Parse **** Entry=%p obj=%p\n",
		info->node, obj_desc));

	/* Create and init a Root Node */

	op = acpi_ps_create_scope_op ();
	if (!op) {
		status = AE_NO_MEMORY;
		goto cleanup1;
	}

	/*
	 * Get a new owner_id for objects created by this method. Namespace
	 * objects (such as Operation Regions) can be created during the
	 * first pass parse.
	 */
	obj_desc->method.owning_id = acpi_ut_allocate_owner_id (ACPI_OWNER_TYPE_METHOD);

	/* Create and initialize a new walk state */

	walk_state = acpi_ds_create_walk_state (obj_desc->method.owning_id,
			   NULL, NULL, NULL);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup2;
	}

	status = acpi_ds_init_aml_walk (walk_state, op, info->node,
			  obj_desc->method.aml_start,
			  obj_desc->method.aml_length, NULL, 1);
	if (ACPI_FAILURE (status)) {
		goto cleanup3;
	}

	/* Parse the AML */

	status = acpi_ps_parse_aml (walk_state);
	acpi_ps_delete_parse_tree (op);
	if (ACPI_FAILURE (status)) {
		goto cleanup1; /* Walk state is already deleted */
	}

	/*
	 * 2) Execute the method.  Performs second pass parse simultaneously
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
		"**** Begin Method Execution **** Entry=%p obj=%p\n",
		info->node, obj_desc));

	/* Create and init a Root Node */

	op = acpi_ps_create_scope_op ();
	if (!op) {
		status = AE_NO_MEMORY;
		goto cleanup1;
	}

	/* Init new op with the method name and pointer back to the NS node */

	acpi_ps_set_name (op, info->node->name.integer);
	op->common.node = info->node;

	/* Create and initialize a new walk state */

	walk_state = acpi_ds_create_walk_state (0, NULL, NULL, NULL);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup2;
	}

	status = acpi_ds_init_aml_walk (walk_state, op, info->node,
			  obj_desc->method.aml_start,
			  obj_desc->method.aml_length, info, 3);
	if (ACPI_FAILURE (status)) {
		goto cleanup3;
	}

	/*
	 * The walk of the parse tree is where we actually execute the method
	 */
	status = acpi_ps_parse_aml (walk_state);
	goto cleanup2; /* Walk state already deleted */


cleanup3:
	acpi_ds_delete_walk_state (walk_state);

cleanup2:
	acpi_ps_delete_parse_tree (op);

cleanup1:
	if ((info->parameter_type == ACPI_PARAM_ARGS) &&
		(info->parameters)) {
		/* Take away the extra reference that we gave the parameters above */

		for (i = 0; info->parameters[i]; i++) {
			/* Ignore errors, just do them all */

			(void) acpi_ut_update_object_reference (info->parameters[i], REF_DECREMENT);
		}
	}

	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * If the method has returned an object, signal this to the caller with
	 * a control exception code
	 */
	if (info->return_object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Method returned obj_desc=%p\n",
			info->return_object));
		ACPI_DUMP_STACK_ENTRY (info->return_object);

		status = AE_CTRL_RETURN_VALUE;
	}

	return_ACPI_STATUS (status);
}


