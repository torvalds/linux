/******************************************************************************
 *
 * Module Name: excreate - Named object creation
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
#include <acpi/acinterp.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>
#include <acpi/acevents.h>
#include <acpi/actables.h>

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("excreate")

#ifndef ACPI_NO_METHOD_EXECUTION
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
	struct acpi_namespace_node *target_node;
	struct acpi_namespace_node *alias_node;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ex_create_alias);

	/* Get the source/alias operands (both namespace nodes) */

	alias_node = (struct acpi_namespace_node *)walk_state->operands[0];
	target_node = (struct acpi_namespace_node *)walk_state->operands[1];

	if ((target_node->type == ACPI_TYPE_LOCAL_ALIAS) ||
	    (target_node->type == ACPI_TYPE_LOCAL_METHOD_ALIAS)) {
		/*
		 * Dereference an existing alias so that we don't create a chain
		 * of aliases.  With this code, we guarantee that an alias is
		 * always exactly one level of indirection away from the
		 * actual aliased name.
		 */
		target_node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  target_node->object);
	}

	/*
	 * For objects that can never change (i.e., the NS node will
	 * permanently point to the same object), we can simply attach
	 * the object to the new NS node.  For other objects (such as
	 * Integers, buffers, etc.), we have to point the Alias node
	 * to the original Node.
	 */
	switch (target_node->type) {
	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:
	case ACPI_TYPE_PACKAGE:
	case ACPI_TYPE_BUFFER_FIELD:

		/*
		 * The new alias has the type ALIAS and points to the original
		 * NS node, not the object itself.  This is because for these
		 * types, the object can change dynamically via a Store.
		 */
		alias_node->type = ACPI_TYPE_LOCAL_ALIAS;
		alias_node->object =
		    ACPI_CAST_PTR(union acpi_operand_object, target_node);
		break;

	case ACPI_TYPE_METHOD:

		/*
		 * The new alias has the type ALIAS and points to the original
		 * NS node, not the object itself.  This is because for these
		 * types, the object can change dynamically via a Store.
		 */
		alias_node->type = ACPI_TYPE_LOCAL_METHOD_ALIAS;
		alias_node->object =
		    ACPI_CAST_PTR(union acpi_operand_object, target_node);
		break;

	default:

		/* Attach the original source object to the new Alias Node */

		/*
		 * The new alias assumes the type of the target, and it points
		 * to the same object.  The reference count of the object has an
		 * additional reference to prevent deletion out from under either the
		 * target node or the alias Node
		 */
		status = acpi_ns_attach_object(alias_node,
					       acpi_ns_get_attached_object
					       (target_node),
					       target_node->type);
		break;
	}

	/* Since both operands are Nodes, we don't need to delete them */

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
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * Create the actual OS semaphore, with zero initial units -- meaning
	 * that the event is created in an unsignalled state
	 */
	status = acpi_os_create_semaphore(ACPI_NO_UNIT_LIMIT, 0,
					  &obj_desc->event.os_semaphore);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Attach object to the Node */

	status =
	    acpi_ns_attach_object((struct acpi_namespace_node *)walk_state->
				  operands[0], obj_desc, ACPI_TYPE_EVENT);

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
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Create the actual OS Mutex */

	status = acpi_os_create_mutex(&obj_desc->mutex.os_mutex);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Init object and attach to NS node */

	obj_desc->mutex.sync_level =
	    (u8) walk_state->operands[1]->integer.value;
	obj_desc->mutex.node =
	    (struct acpi_namespace_node *)walk_state->operands[0];

	status =
	    acpi_ns_attach_object(obj_desc->mutex.node, obj_desc,
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
 *              region_space        - space_iD for the region
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
		      u8 region_space, struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	union acpi_operand_object *region_obj2;

	ACPI_FUNCTION_TRACE(ex_create_region);

	/* Get the Namespace Node */

	node = walk_state->op->common.node;

	/*
	 * If the region object is already attached to this node,
	 * just return
	 */
	if (acpi_ns_get_attached_object(node)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Space ID must be one of the predefined IDs, or in the user-defined
	 * range
	 */
	if ((region_space >= ACPI_NUM_PREDEFINED_REGIONS) &&
	    (region_space < ACPI_USER_REGION_BEGIN)) {
		ACPI_ERROR((AE_INFO, "Invalid AddressSpace type %X",
			    region_space));
		return_ACPI_STATUS(AE_AML_INVALID_SPACE_ID);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_LOAD, "Region Type - %s (%X)\n",
			  acpi_ut_get_region_name(region_space), region_space));

	/* Create the region descriptor */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_REGION);
	if (!obj_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * Remember location in AML stream of address & length
	 * operands since they need to be evaluated at run time.
	 */
	region_obj2 = obj_desc->common.next_object;
	region_obj2->extra.aml_start = aml_start;
	region_obj2->extra.aml_length = aml_length;

	/* Init the region from the operands */

	obj_desc->region.space_id = region_space;
	obj_desc->region.address = 0;
	obj_desc->region.length = 0;
	obj_desc->region.node = node;

	/* Install the new region object in the parent Node */

	status = acpi_ns_attach_object(node, obj_desc, ACPI_TYPE_REGION);

      cleanup:

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_create_table_region
 *
 * PARAMETERS:  walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new data_table_region object
 *
 ******************************************************************************/

acpi_status acpi_ex_create_table_region(struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object **operand = &walk_state->operands[0];
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	union acpi_operand_object *region_obj2;
	acpi_native_uint table_index;
	struct acpi_table_header *table;

	ACPI_FUNCTION_TRACE(ex_create_table_region);

	/* Get the Node from the object stack  */

	node = walk_state->op->common.node;

	/*
	 * If the region object is already attached to this node,
	 * just return
	 */
	if (acpi_ns_get_attached_object(node)) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Find the ACPI table */

	status = acpi_tb_find_table(operand[1]->string.pointer,
				    operand[2]->string.pointer,
				    operand[3]->string.pointer, &table_index);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Create the region descriptor */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_REGION);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	region_obj2 = obj_desc->common.next_object;
	region_obj2->extra.region_context = NULL;

	status = acpi_get_table_by_index(table_index, &table);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Init the region from the operands */

	obj_desc->region.space_id = REGION_DATA_TABLE;
	obj_desc->region.address =
	    (acpi_physical_address) ACPI_TO_INTEGER(table);
	obj_desc->region.length = table->length;
	obj_desc->region.node = node;
	obj_desc->region.flags = AOPOBJ_DATA_VALID;

	/* Install the new region object in the parent Node */

	status = acpi_ns_attach_object(node, obj_desc, ACPI_TYPE_REGION);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	status = acpi_ev_initialize_region(obj_desc, FALSE);
	if (ACPI_FAILURE(status)) {
		if (status == AE_NOT_EXIST) {
			status = AE_OK;
		} else {
			goto cleanup;
		}
	}

	obj_desc->region.flags |= AOPOBJ_SETUP_COMPLETE;

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
 *              Processor (Name[0], cpu_iD[1], pblock_addr[2], pblock_length[3])
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
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Initialize the processor object from the operands */

	obj_desc->processor.proc_id = (u8) operand[1]->integer.value;
	obj_desc->processor.length = (u8) operand[3]->integer.value;
	obj_desc->processor.address =
	    (acpi_io_address) operand[2]->integer.value;

	/* Install the processor object in the parent Node */

	status = acpi_ns_attach_object((struct acpi_namespace_node *)operand[0],
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
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Initialize the power object from the operands */

	obj_desc->power_resource.system_level = (u8) operand[1]->integer.value;
	obj_desc->power_resource.resource_order =
	    (u16) operand[2]->integer.value;

	/* Install the  power resource object in the parent Node */

	status = acpi_ns_attach_object((struct acpi_namespace_node *)operand[0],
				       obj_desc, ACPI_TYPE_POWER);

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}
#endif

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
		status = AE_NO_MEMORY;
		goto exit;
	}

	/* Save the method's AML pointer and length  */

	obj_desc->method.aml_start = aml_start;
	obj_desc->method.aml_length = aml_length;

	/*
	 * Disassemble the method flags. Split off the Arg Count
	 * for efficiency
	 */
	method_flags = (u8) operand[1]->integer.value;

	obj_desc->method.method_flags =
	    (u8) (method_flags & ~AML_METHOD_ARG_COUNT);
	obj_desc->method.param_count =
	    (u8) (method_flags & AML_METHOD_ARG_COUNT);

	/*
	 * Get the sync_level. If method is serialized, a mutex will be
	 * created for this method when it is parsed.
	 */
	if (acpi_gbl_all_methods_serialized) {
		obj_desc->method.sync_level = 0;
		obj_desc->method.method_flags |= AML_METHOD_SERIALIZED;
	} else if (method_flags & AML_METHOD_SERIALIZED) {
		/*
		 * ACPI 1.0: sync_level = 0
		 * ACPI 2.0: sync_level = sync_level in method declaration
		 */
		obj_desc->method.sync_level = (u8)
		    ((method_flags & AML_METHOD_SYNCH_LEVEL) >> 4);
	}

	/* Attach the new object to the method Node */

	status = acpi_ns_attach_object((struct acpi_namespace_node *)operand[0],
				       obj_desc, ACPI_TYPE_METHOD);

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);

      exit:
	/* Remove a reference to the operand */

	acpi_ut_remove_reference(operand[1]);
	return_ACPI_STATUS(status);
}
