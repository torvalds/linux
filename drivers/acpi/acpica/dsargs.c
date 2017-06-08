/******************************************************************************
 *
 * Module Name: dsargs - Support for execution of dynamic arguments for static
 *                       objects (regions, fields, buffer fields, etc.)
 *
 *****************************************************************************/

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
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dsargs")

/* Local prototypes */
static acpi_status
acpi_ds_execute_arguments(struct acpi_namespace_node *node,
			  struct acpi_namespace_node *scope_node,
			  u32 aml_length, u8 *aml_start);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_execute_arguments
 *
 * PARAMETERS:  node                - Object NS node
 *              scope_node          - Parent NS node
 *              aml_length          - Length of executable AML
 *              aml_start           - Pointer to the AML
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Late (deferred) execution of region or field arguments
 *
 ******************************************************************************/

static acpi_status
acpi_ds_execute_arguments(struct acpi_namespace_node *node,
			  struct acpi_namespace_node *scope_node,
			  u32 aml_length, u8 *aml_start)
{
	acpi_status status;
	union acpi_parse_object *op;
	struct acpi_walk_state *walk_state;

	ACPI_FUNCTION_TRACE(ds_execute_arguments);

	/* Allocate a new parser op to be the root of the parsed tree */

	op = acpi_ps_alloc_op(AML_INT_EVAL_SUBTREE_OP, aml_start);
	if (!op) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Save the Node for use in acpi_ps_parse_aml */

	op->common.node = scope_node;

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state(0, NULL, NULL, NULL);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	status = acpi_ds_init_aml_walk(walk_state, op, NULL, aml_start,
				       aml_length, NULL, ACPI_IMODE_LOAD_PASS1);
	if (ACPI_FAILURE(status)) {
		acpi_ds_delete_walk_state(walk_state);
		goto cleanup;
	}

	/* Mark this parse as a deferred opcode */

	walk_state->parse_flags = ACPI_PARSE_DEFERRED_OP;
	walk_state->deferred_node = node;

	/* Pass1: Parse the entire declaration */

	status = acpi_ps_parse_aml(walk_state);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Get and init the Op created above */

	op->common.node = node;
	acpi_ps_delete_parse_tree(op);

	/* Evaluate the deferred arguments */

	op = acpi_ps_alloc_op(AML_INT_EVAL_SUBTREE_OP, aml_start);
	if (!op) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	op->common.node = scope_node;

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state(0, NULL, NULL, NULL);
	if (!walk_state) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Execute the opcode and arguments */

	status = acpi_ds_init_aml_walk(walk_state, op, NULL, aml_start,
				       aml_length, NULL, ACPI_IMODE_EXECUTE);
	if (ACPI_FAILURE(status)) {
		acpi_ds_delete_walk_state(walk_state);
		goto cleanup;
	}

	/* Mark this execution as a deferred opcode */

	walk_state->deferred_node = node;
	status = acpi_ps_parse_aml(walk_state);

cleanup:
	acpi_ps_delete_parse_tree(op);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_get_buffer_field_arguments
 *
 * PARAMETERS:  obj_desc        - A valid buffer_field object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get buffer_field Buffer and Index. This implements the late
 *              evaluation of these field attributes.
 *
 ******************************************************************************/

acpi_status
acpi_ds_get_buffer_field_arguments(union acpi_operand_object *obj_desc)
{
	union acpi_operand_object *extra_desc;
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_buffer_field_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the AML pointer (method object) and buffer_field node */

	extra_desc = acpi_ns_get_secondary_object(obj_desc);
	node = obj_desc->buffer_field.node;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_BUFFER_FIELD, node, NULL));

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "[%4.4s] BufferField Arg Init\n",
			  acpi_ut_get_node_name(node)));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(node, node->parent,
					   extra_desc->extra.aml_length,
					   extra_desc->extra.aml_start);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_get_bank_field_arguments
 *
 * PARAMETERS:  obj_desc        - A valid bank_field object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get bank_field bank_value. This implements the late
 *              evaluation of these field attributes.
 *
 ******************************************************************************/

acpi_status
acpi_ds_get_bank_field_arguments(union acpi_operand_object *obj_desc)
{
	union acpi_operand_object *extra_desc;
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_bank_field_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the AML pointer (method object) and bank_field node */

	extra_desc = acpi_ns_get_secondary_object(obj_desc);
	node = obj_desc->bank_field.node;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_LOCAL_BANK_FIELD, node, NULL));

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "[%4.4s] BankField Arg Init\n",
			  acpi_ut_get_node_name(node)));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(node, node->parent,
					   extra_desc->extra.aml_length,
					   extra_desc->extra.aml_start);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ut_add_address_range(obj_desc->region.space_id,
					   obj_desc->region.address,
					   obj_desc->region.length, node);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_get_buffer_arguments
 *
 * PARAMETERS:  obj_desc        - A valid Buffer object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get Buffer length and initializer byte list. This implements
 *              the late evaluation of these attributes.
 *
 ******************************************************************************/

acpi_status acpi_ds_get_buffer_arguments(union acpi_operand_object *obj_desc)
{
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_buffer_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the Buffer node */

	node = obj_desc->buffer.node;
	if (!node) {
		ACPI_ERROR((AE_INFO,
			    "No pointer back to namespace node in buffer object %p",
			    obj_desc));
		return_ACPI_STATUS(AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Buffer Arg Init\n"));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(node, node,
					   obj_desc->buffer.aml_length,
					   obj_desc->buffer.aml_start);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_get_package_arguments
 *
 * PARAMETERS:  obj_desc        - A valid Package object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get Package length and initializer byte list. This implements
 *              the late evaluation of these attributes.
 *
 ******************************************************************************/

acpi_status acpi_ds_get_package_arguments(union acpi_operand_object *obj_desc)
{
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_package_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the Package node */

	node = obj_desc->package.node;
	if (!node) {
		ACPI_ERROR((AE_INFO,
			    "No pointer back to namespace node in package %p",
			    obj_desc));
		return_ACPI_STATUS(AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Package Arg Init\n"));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(node, node,
					   obj_desc->package.aml_length,
					   obj_desc->package.aml_start);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_get_region_arguments
 *
 * PARAMETERS:  obj_desc        - A valid region object
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Get region address and length. This implements the late
 *              evaluation of these region attributes.
 *
 ******************************************************************************/

acpi_status acpi_ds_get_region_arguments(union acpi_operand_object *obj_desc)
{
	struct acpi_namespace_node *node;
	acpi_status status;
	union acpi_operand_object *extra_desc;

	ACPI_FUNCTION_TRACE_PTR(ds_get_region_arguments, obj_desc);

	if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	extra_desc = acpi_ns_get_secondary_object(obj_desc);
	if (!extra_desc) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Get the Region node */

	node = obj_desc->region.node;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_REGION, node, NULL));

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "[%4.4s] OpRegion Arg Init at AML %p\n",
			  acpi_ut_get_node_name(node),
			  extra_desc->extra.aml_start));

	/* Execute the argument AML */

	status = acpi_ds_execute_arguments(node, extra_desc->extra.scope_node,
					   extra_desc->extra.aml_length,
					   extra_desc->extra.aml_start);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ut_add_address_range(obj_desc->region.space_id,
					   obj_desc->region.address,
					   obj_desc->region.length, node);
	return_ACPI_STATUS(status);
}
