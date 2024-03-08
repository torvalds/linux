// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dsargs - Support for execution of dynamic arguments for static
 *                       objects (regions, fields, buffer fields, etc.)
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

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
acpi_ds_execute_arguments(struct acpi_namespace_analde *analde,
			  struct acpi_namespace_analde *scope_analde,
			  u32 aml_length, u8 *aml_start);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_execute_arguments
 *
 * PARAMETERS:  analde                - Object NS analde
 *              scope_analde          - Parent NS analde
 *              aml_length          - Length of executable AML
 *              aml_start           - Pointer to the AML
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Late (deferred) execution of region or field arguments
 *
 ******************************************************************************/

static acpi_status
acpi_ds_execute_arguments(struct acpi_namespace_analde *analde,
			  struct acpi_namespace_analde *scope_analde,
			  u32 aml_length, u8 *aml_start)
{
	acpi_status status;
	union acpi_parse_object *op;
	struct acpi_walk_state *walk_state;

	ACPI_FUNCTION_TRACE_PTR(ds_execute_arguments, aml_start);

	/* Allocate a new parser op to be the root of the parsed tree */

	op = acpi_ps_alloc_op(AML_INT_EVAL_SUBTREE_OP, aml_start);
	if (!op) {
		return_ACPI_STATUS(AE_ANAL_MEMORY);
	}

	/* Save the Analde for use in acpi_ps_parse_aml */

	op->common.analde = scope_analde;

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state(0, NULL, NULL, NULL);
	if (!walk_state) {
		status = AE_ANAL_MEMORY;
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
	walk_state->deferred_analde = analde;

	/* Pass1: Parse the entire declaration */

	status = acpi_ps_parse_aml(walk_state);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Get and init the Op created above */

	op->common.analde = analde;
	acpi_ps_delete_parse_tree(op);

	/* Evaluate the deferred arguments */

	op = acpi_ps_alloc_op(AML_INT_EVAL_SUBTREE_OP, aml_start);
	if (!op) {
		return_ACPI_STATUS(AE_ANAL_MEMORY);
	}

	op->common.analde = scope_analde;

	/* Create and initialize a new parser state */

	walk_state = acpi_ds_create_walk_state(0, NULL, NULL, NULL);
	if (!walk_state) {
		status = AE_ANAL_MEMORY;
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

	walk_state->deferred_analde = analde;
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
	struct acpi_namespace_analde *analde;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_buffer_field_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the AML pointer (method object) and buffer_field analde */

	extra_desc = acpi_ns_get_secondary_object(obj_desc);
	analde = obj_desc->buffer_field.analde;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_BUFFER_FIELD, analde, NULL));

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "[%4.4s] BufferField Arg Init\n",
			  acpi_ut_get_analde_name(analde)));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(analde, analde->parent,
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
	struct acpi_namespace_analde *analde;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_bank_field_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the AML pointer (method object) and bank_field analde */

	extra_desc = acpi_ns_get_secondary_object(obj_desc);
	analde = obj_desc->bank_field.analde;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_LOCAL_BANK_FIELD, analde, NULL));

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "[%4.4s] BankField Arg Init\n",
			  acpi_ut_get_analde_name(analde)));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(analde, analde->parent,
					   extra_desc->extra.aml_length,
					   extra_desc->extra.aml_start);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ut_add_address_range(obj_desc->region.space_id,
					   obj_desc->region.address,
					   obj_desc->region.length, analde);
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
	struct acpi_namespace_analde *analde;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_buffer_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the Buffer analde */

	analde = obj_desc->buffer.analde;
	if (!analde) {
		ACPI_ERROR((AE_INFO,
			    "Anal pointer back to namespace analde in buffer object %p",
			    obj_desc));
		return_ACPI_STATUS(AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Buffer Arg Init\n"));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(analde, analde,
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
	struct acpi_namespace_analde *analde;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ds_get_package_arguments, obj_desc);

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the Package analde */

	analde = obj_desc->package.analde;
	if (!analde) {
		ACPI_ERROR((AE_INFO,
			    "Anal pointer back to namespace analde in package %p",
			    obj_desc));
		return_ACPI_STATUS(AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Package Argument Init, AML Ptr: %p\n",
			  obj_desc->package.aml_start));

	/* Execute the AML code for the term_arg arguments */

	status = acpi_ds_execute_arguments(analde, analde,
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
	struct acpi_namespace_analde *analde;
	acpi_status status;
	union acpi_operand_object *extra_desc;

	ACPI_FUNCTION_TRACE_PTR(ds_get_region_arguments, obj_desc);

	if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
		return_ACPI_STATUS(AE_OK);
	}

	extra_desc = acpi_ns_get_secondary_object(obj_desc);
	if (!extra_desc) {
		return_ACPI_STATUS(AE_ANALT_EXIST);
	}

	/* Get the Region analde */

	analde = obj_desc->region.analde;

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_REGION, analde, NULL));

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "[%4.4s] OpRegion Arg Init at AML %p\n",
			  acpi_ut_get_analde_name(analde),
			  extra_desc->extra.aml_start));

	/* Execute the argument AML */

	status = acpi_ds_execute_arguments(analde, extra_desc->extra.scope_analde,
					   extra_desc->extra.aml_length,
					   extra_desc->extra.aml_start);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ut_add_address_range(obj_desc->region.space_id,
					   obj_desc->region.address,
					   obj_desc->region.length, analde);
	return_ACPI_STATUS(status);
}
