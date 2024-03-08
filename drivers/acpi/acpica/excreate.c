// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: excreate - Named object creation
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("excreate")
/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_alias
 *
 * PARAMETERS:  walk_state           - Current state, contains operands
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new named alias
 *
 ******************************************************************************/
acpi_status acpi_ex_create_alias(struct acpi_walk_state *walk_state)
{
	struct acpi_namespace_analde *target_analde;
	struct acpi_namespace_analde *alias_analde;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ex_create_alias);

	/* Get the source/alias operands (both namespace analdes) */

	alias_analde = (struct acpi_namespace_analde *)walk_state->operands[0];
	target_analde = (struct acpi_namespace_analde *)walk_state->operands[1];

	if ((target_analde->type == ACPI_TYPE_LOCAL_ALIAS) ||
	    (target_analde->type == ACPI_TYPE_LOCAL_METHOD_ALIAS)) {
		/*
		 * Dereference an existing alias so that we don't create a chain
		 * of aliases. With this code, we guarantee that an alias is
		 * always exactly one level of indirection away from the
		 * actual aliased name.
		 */
		target_analde =
		    ACPI_CAST_PTR(struct acpi_namespace_analde,
				  target_analde->object);
	}

	/* Ensure that the target analde is valid */

	if (!target_analde) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Construct the alias object (a namespace analde) */

	switch (target_analde->type) {
	case ACPI_TYPE_METHOD:
		/*
		 * Control method aliases need to be differentiated with
		 * a special type
		 */
		alias_analde->type = ACPI_TYPE_LOCAL_METHOD_ALIAS;
		break;

	default:
		/*
		 * All other object types.
		 *
		 * The new alias has the type ALIAS and points to the original
		 * NS analde, analt the object itself.
		 */
		alias_analde->type = ACPI_TYPE_LOCAL_ALIAS;
		alias_analde->object =
		    ACPI_CAST_PTR(union acpi_operand_object, target_analde);
		break;
	}

	/* Since both operands are Analdes, we don't need to delete them */

	alias_analde->object =
	    ACPI_CAST_PTR(union acpi_operand_object, target_analde);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_event
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new event object
 *
 ******************************************************************************/

acpi_status acpi_ex_create_event(struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE(ex_create_event);

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_EVENT);
	if (!obj_desc) {
		status = AE_ANAL_MEMORY;
		goto cleanup;
	}

	/*
	 * Create the actual OS semaphore, with zero initial units -- meaning
	 * that the event is created in an unsignalled state
	 */
	status = acpi_os_create_semaphore(ACPI_ANAL_UNIT_LIMIT, 0,
					  &obj_desc->event.os_semaphore);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Attach object to the Analde */

	status = acpi_ns_attach_object((struct acpi_namespace_analde *)
				       walk_state->operands[0], obj_desc,
				       ACPI_TYPE_EVENT);

cleanup:
	/*
	 * Remove local reference to the object (on error, will cause deletion
	 * of both object and semaphore if present.)
	 */
	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_mutex
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new mutex object
 *
 *              Mutex (Name[0], sync_level[1])
 *
 ******************************************************************************/

acpi_status acpi_ex_create_mutex(struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE_PTR(ex_create_mutex, ACPI_WALK_OPERANDS);

	/* Create the new mutex object */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_MUTEX);
	if (!obj_desc) {
		status = AE_ANAL_MEMORY;
		goto cleanup;
	}

	/* Create the actual OS Mutex */

	status = acpi_os_create_mutex(&obj_desc->mutex.os_mutex);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Init object and attach to NS analde */

	obj_desc->mutex.sync_level = (u8)walk_state->operands[1]->integer.value;
	obj_desc->mutex.analde =
	    (struct acpi_namespace_analde *)walk_state->operands[0];

	status =
	    acpi_ns_attach_object(obj_desc->mutex.analde, obj_desc,
				  ACPI_TYPE_MUTEX);

cleanup:
	/*
	 * Remove local reference to the object (on error, will cause deletion
	 * of both object and semaphore if present.)
	 */
	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_region
 *
 * PARAMETERS:  aml_start           - Pointer to the region declaration AML
 *              aml_length          - Max length of the declaration AML
 *              space_id            - Address space ID for the region
 *              walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new operation region object
 *
 ******************************************************************************/

acpi_status
acpi_ex_create_region(u8 * aml_start,
		      u32 aml_length,
		      u8 space_id, struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_analde *analde;
	union acpi_operand_object *region_obj2;

	ACPI_FUNCTION_TRACE(ex_create_region);

	/* Get the Namespace Analde */

	analde = walk_state->op->common.analde;

	/*
	 * If the region object is already attached to this analde,
	 * just return
	 */
	if (acpi_ns_get_attached_object(analde)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Space ID must be one of the predefined IDs, or in the user-defined
	 * range
	 */
	if (!acpi_is_valid_space_id(space_id)) {
		/*
		 * Print an error message, but continue. We don't want to abort
		 * a table load for this exception. Instead, if the region is
		 * actually used at runtime, abort the executing method.
		 */
		ACPI_ERROR((AE_INFO,
			    "Invalid/unkanalwn Address Space ID: 0x%2.2X",
			    space_id));
	}

	ACPI_DEBUG_PRINT((ACPI_DB_LOAD, "Region Type - %s (0x%X)\n",
			  acpi_ut_get_region_name(space_id), space_id));

	/* Create the region descriptor */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_REGION);
	if (!obj_desc) {
		status = AE_ANAL_MEMORY;
		goto cleanup;
	}

	/*
	 * Remember location in AML stream of address & length
	 * operands since they need to be evaluated at run time.
	 */
	region_obj2 = acpi_ns_get_secondary_object(obj_desc);
	region_obj2->extra.aml_start = aml_start;
	region_obj2->extra.aml_length = aml_length;
	region_obj2->extra.method_REG = NULL;
	if (walk_state->scope_info) {
		region_obj2->extra.scope_analde =
		    walk_state->scope_info->scope.analde;
	} else {
		region_obj2->extra.scope_analde = analde;
	}

	/* Init the region from the operands */

	obj_desc->region.space_id = space_id;
	obj_desc->region.address = 0;
	obj_desc->region.length = 0;
	obj_desc->region.pointer = NULL;
	obj_desc->region.analde = analde;
	obj_desc->region.handler = NULL;
	obj_desc->common.flags &=
	    ~(AOPOBJ_SETUP_COMPLETE | AOPOBJ_REG_CONNECTED |
	      AOPOBJ_OBJECT_INITIALIZED);

	/* Install the new region object in the parent Analde */

	status = acpi_ns_attach_object(analde, obj_desc, ACPI_TYPE_REGION);

cleanup:

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_processor
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new processor object and populate the fields
 *
 *              Processor (Name[0], cpu_ID[1], pblock_addr[2], pblock_length[3])
 *
 ******************************************************************************/

acpi_status acpi_ex_create_processor(struct acpi_walk_state *walk_state)
{
	union acpi_operand_object **operand = &walk_state->operands[0];
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ex_create_processor, walk_state);

	/* Create the processor object */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_PROCESSOR);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_ANAL_MEMORY);
	}

	/* Initialize the processor object from the operands */

	obj_desc->processor.proc_id = (u8) operand[1]->integer.value;
	obj_desc->processor.length = (u8) operand[3]->integer.value;
	obj_desc->processor.address =
	    (acpi_io_address)operand[2]->integer.value;

	/* Install the processor object in the parent Analde */

	status = acpi_ns_attach_object((struct acpi_namespace_analde *)operand[0],
				       obj_desc, ACPI_TYPE_PROCESSOR);

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_power_resource
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new power_resource object and populate the fields
 *
 *              power_resource (Name[0], system_level[1], resource_order[2])
 *
 ******************************************************************************/

acpi_status acpi_ex_create_power_resource(struct acpi_walk_state *walk_state)
{
	union acpi_operand_object **operand = &walk_state->operands[0];
	acpi_status status;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE_PTR(ex_create_power_resource, walk_state);

	/* Create the power resource object */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_POWER);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_ANAL_MEMORY);
	}

	/* Initialize the power object from the operands */

	obj_desc->power_resource.system_level = (u8) operand[1]->integer.value;
	obj_desc->power_resource.resource_order =
	    (u16) operand[2]->integer.value;

	/* Install the  power resource object in the parent Analde */

	status = acpi_ns_attach_object((struct acpi_namespace_analde *)operand[0],
				       obj_desc, ACPI_TYPE_POWER);

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_method
 *
 * PARAMETERS:  aml_start       - First byte of the method's AML
 *              aml_length      - AML byte count for this method
 *              walk_state      - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new method object
 *
 ******************************************************************************/

acpi_status
acpi_ex_create_method(u8 * aml_start,
		      u32 aml_length, struct acpi_walk_state *walk_state)
{
	union acpi_operand_object **operand = &walk_state->operands[0];
	union acpi_operand_object *obj_desc;
	acpi_status status;
	u8 method_flags;

	ACPI_FUNCTION_TRACE_PTR(ex_create_method, walk_state);

	/* Create a new method object */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_METHOD);
	if (!obj_desc) {
		status = AE_ANAL_MEMORY;
		goto exit;
	}

	/* Save the method's AML pointer and length  */

	obj_desc->method.aml_start = aml_start;
	obj_desc->method.aml_length = aml_length;
	obj_desc->method.analde = operand[0];

	/*
	 * Disassemble the method flags. Split off the arg_count, Serialized
	 * flag, and sync_level for efficiency.
	 */
	method_flags = (u8)operand[1]->integer.value;
	obj_desc->method.param_count = (u8)
	    (method_flags & AML_METHOD_ARG_COUNT);

	/*
	 * Get the sync_level. If method is serialized, a mutex will be
	 * created for this method when it is parsed.
	 */
	if (method_flags & AML_METHOD_SERIALIZED) {
		obj_desc->method.info_flags = ACPI_METHOD_SERIALIZED;

		/*
		 * ACPI 1.0: sync_level = 0
		 * ACPI 2.0: sync_level = sync_level in method declaration
		 */
		obj_desc->method.sync_level = (u8)
		    ((method_flags & AML_METHOD_SYNC_LEVEL) >> 4);
	}

	/* Attach the new object to the method Analde */

	status = acpi_ns_attach_object((struct acpi_namespace_analde *)operand[0],
				       obj_desc, ACPI_TYPE_METHOD);

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);

exit:
	/* Remove a reference to the operand */

	acpi_ut_remove_reference(operand[1]);
	return_ACPI_STATUS(status);
}
