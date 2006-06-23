/*******************************************************************************
 *
 * Module Name: utdelete - object deletion and reference count utilities
 *
 ******************************************************************************/

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
#include <acpi/acinterp.h>
#include <acpi/acnamesp.h>
#include <acpi/acevents.h>
#include <acpi/amlcode.h>

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utdelete")

/* Local prototypes */
static void acpi_ut_delete_internal_obj(union acpi_operand_object *object);

static void
acpi_ut_update_ref_count(union acpi_operand_object *object, u32 action);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_internal_obj
 *
 * PARAMETERS:  Object         - Object to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Low level object deletion, after reference counts have been
 *              updated (All reference counts, including sub-objects!)
 *
 ******************************************************************************/

static void acpi_ut_delete_internal_obj(union acpi_operand_object *object)
{
	void *obj_pointer = NULL;
	union acpi_operand_object *handler_desc;
	union acpi_operand_object *second_desc;
	union acpi_operand_object *next_desc;

	ACPI_FUNCTION_TRACE_PTR(ut_delete_internal_obj, object);

	if (!object) {
		return_VOID;
	}

	/*
	 * Must delete or free any pointers within the object that are not
	 * actual ACPI objects (for example, a raw buffer pointer).
	 */
	switch (ACPI_GET_OBJECT_TYPE(object)) {
	case ACPI_TYPE_STRING:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "**** String %p, ptr %p\n", object,
				  object->string.pointer));

		/* Free the actual string buffer */

		if (!(object->common.flags & AOPOBJ_STATIC_POINTER)) {

			/* But only if it is NOT a pointer into an ACPI table */

			obj_pointer = object->string.pointer;
		}
		break;

	case ACPI_TYPE_BUFFER:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "**** Buffer %p, ptr %p\n", object,
				  object->buffer.pointer));

		/* Free the actual buffer */

		if (!(object->common.flags & AOPOBJ_STATIC_POINTER)) {

			/* But only if it is NOT a pointer into an ACPI table */

			obj_pointer = object->buffer.pointer;
		}
		break;

	case ACPI_TYPE_PACKAGE:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  " **** Package of count %X\n",
				  object->package.count));

		/*
		 * Elements of the package are not handled here, they are deleted
		 * separately
		 */

		/* Free the (variable length) element pointer array */

		obj_pointer = object->package.elements;
		break;

	case ACPI_TYPE_DEVICE:

		if (object->device.gpe_block) {
			(void)acpi_ev_delete_gpe_block(object->device.
						       gpe_block);
		}

		/* Walk the handler list for this device */

		handler_desc = object->device.handler;
		while (handler_desc) {
			next_desc = handler_desc->address_space.next;
			acpi_ut_remove_reference(handler_desc);
			handler_desc = next_desc;
		}
		break;

	case ACPI_TYPE_MUTEX:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Mutex %p, Semaphore %p\n",
				  object, object->mutex.semaphore));

		acpi_ex_unlink_mutex(object);
		(void)acpi_os_delete_semaphore(object->mutex.semaphore);
		break;

	case ACPI_TYPE_EVENT:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Event %p, Semaphore %p\n",
				  object, object->event.semaphore));

		(void)acpi_os_delete_semaphore(object->event.semaphore);
		object->event.semaphore = NULL;
		break;

	case ACPI_TYPE_METHOD:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Method %p\n", object));

		/* Delete the method semaphore if it exists */

		if (object->method.semaphore) {
			(void)acpi_os_delete_semaphore(object->method.
						       semaphore);
			object->method.semaphore = NULL;
		}
		break;

	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Region %p\n", object));

		second_desc = acpi_ns_get_secondary_object(object);
		if (second_desc) {
			/*
			 * Free the region_context if and only if the handler is one of the
			 * default handlers -- and therefore, we created the context object
			 * locally, it was not created by an external caller.
			 */
			handler_desc = object->region.handler;
			if (handler_desc) {
				if (handler_desc->address_space.handler_flags &
				    ACPI_ADDR_HANDLER_DEFAULT_INSTALLED) {

					/* Deactivate region and free region context */

					if (handler_desc->address_space.setup) {
						(void)handler_desc->
						    address_space.setup(object,
									ACPI_REGION_DEACTIVATE,
									handler_desc->
									address_space.
									context,
									&second_desc->
									extra.
									region_context);
					}
				}

				acpi_ut_remove_reference(handler_desc);
			}

			/* Now we can free the Extra object */

			acpi_ut_delete_object_desc(second_desc);
		}
		break;

	case ACPI_TYPE_BUFFER_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Buffer Field %p\n", object));

		second_desc = acpi_ns_get_secondary_object(object);
		if (second_desc) {
			acpi_ut_delete_object_desc(second_desc);
		}
		break;

	default:
		break;
	}

	/* Free any allocated memory (pointer within the object) found above */

	if (obj_pointer) {
		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "Deleting Object Subptr %p\n", obj_pointer));
		ACPI_FREE(obj_pointer);
	}

	/* Now the object can be safely deleted */

	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS, "Deleting Object %p [%s]\n",
			  object, acpi_ut_get_object_type_name(object)));

	acpi_ut_delete_object_desc(object);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_internal_object_list
 *
 * PARAMETERS:  obj_list        - Pointer to the list to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: This function deletes an internal object list, including both
 *              simple objects and package objects
 *
 ******************************************************************************/

void acpi_ut_delete_internal_object_list(union acpi_operand_object **obj_list)
{
	union acpi_operand_object **internal_obj;

	ACPI_FUNCTION_TRACE(ut_delete_internal_object_list);

	/* Walk the null-terminated internal list */

	for (internal_obj = obj_list; *internal_obj; internal_obj++) {
		acpi_ut_remove_reference(*internal_obj);
	}

	/* Free the combined parameter pointer list and object array */

	ACPI_FREE(obj_list);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_update_ref_count
 *
 * PARAMETERS:  Object          - Object whose ref count is to be updated
 *              Action          - What to do
 *
 * RETURN:      New ref count
 *
 * DESCRIPTION: Modify the ref count and return it.
 *
 ******************************************************************************/

static void
acpi_ut_update_ref_count(union acpi_operand_object *object, u32 action)
{
	u16 count;
	u16 new_count;

	ACPI_FUNCTION_NAME(ut_update_ref_count);

	if (!object) {
		return;
	}

	count = object->common.reference_count;
	new_count = count;

	/*
	 * Perform the reference count action (increment, decrement, force delete)
	 */
	switch (action) {
	case REF_INCREMENT:

		new_count++;
		object->common.reference_count = new_count;

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "Obj %p Refs=%X, [Incremented]\n",
				  object, new_count));
		break;

	case REF_DECREMENT:

		if (count < 1) {
			ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
					  "Obj %p Refs=%X, can't decrement! (Set to 0)\n",
					  object, new_count));

			new_count = 0;
		} else {
			new_count--;

			ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
					  "Obj %p Refs=%X, [Decremented]\n",
					  object, new_count));
		}

		if (ACPI_GET_OBJECT_TYPE(object) == ACPI_TYPE_METHOD) {
			ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
					  "Method Obj %p Refs=%X, [Decremented]\n",
					  object, new_count));
		}

		object->common.reference_count = new_count;
		if (new_count == 0) {
			acpi_ut_delete_internal_obj(object);
		}
		break;

	case REF_FORCE_DELETE:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "Obj %p Refs=%X, Force delete! (Set to 0)\n",
				  object, count));

		new_count = 0;
		object->common.reference_count = new_count;
		acpi_ut_delete_internal_obj(object);
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unknown action (%X)", action));
		break;
	}

	/*
	 * Sanity check the reference count, for debug purposes only.
	 * (A deleted object will have a huge reference count)
	 */
	if (count > ACPI_MAX_REFERENCE_COUNT) {
		ACPI_WARNING((AE_INFO,
			      "Large Reference Count (%X) in object %p", count,
			      object));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_update_object_reference
 *
 * PARAMETERS:  Object              - Increment ref count for this object
 *                                    and all sub-objects
 *              Action              - Either REF_INCREMENT or REF_DECREMENT or
 *                                    REF_FORCE_DELETE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Increment the object reference count
 *
 * Object references are incremented when:
 * 1) An object is attached to a Node (namespace object)
 * 2) An object is copied (all subobjects must be incremented)
 *
 * Object references are decremented when:
 * 1) An object is detached from an Node
 *
 ******************************************************************************/

acpi_status
acpi_ut_update_object_reference(union acpi_operand_object *object, u16 action)
{
	acpi_status status = AE_OK;
	union acpi_generic_state *state_list = NULL;
	union acpi_operand_object *next_object = NULL;
	union acpi_generic_state *state;
	acpi_native_uint i;

	ACPI_FUNCTION_TRACE_PTR(ut_update_object_reference, object);

	while (object) {

		/* Make sure that this isn't a namespace handle */

		if (ACPI_GET_DESCRIPTOR_TYPE(object) == ACPI_DESC_TYPE_NAMED) {
			ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
					  "Object %p is NS handle\n", object));
			return_ACPI_STATUS(AE_OK);
		}

		/*
		 * All sub-objects must have their reference count incremented also.
		 * Different object types have different subobjects.
		 */
		switch (ACPI_GET_OBJECT_TYPE(object)) {
		case ACPI_TYPE_DEVICE:

			acpi_ut_update_ref_count(object->device.system_notify,
						 action);
			acpi_ut_update_ref_count(object->device.device_notify,
						 action);
			break;

		case ACPI_TYPE_PACKAGE:
			/*
			 * We must update all the sub-objects of the package,
			 * each of whom may have their own sub-objects.
			 */
			for (i = 0; i < object->package.count; i++) {
				/*
				 * Push each element onto the stack for later processing.
				 * Note: There can be null elements within the package,
				 * these are simply ignored
				 */
				status =
				    acpi_ut_create_update_state_and_push
				    (object->package.elements[i], action,
				     &state_list);
				if (ACPI_FAILURE(status)) {
					goto error_exit;
				}
			}
			break;

		case ACPI_TYPE_BUFFER_FIELD:

			next_object = object->buffer_field.buffer_obj;
			break;

		case ACPI_TYPE_LOCAL_REGION_FIELD:

			next_object = object->field.region_obj;
			break;

		case ACPI_TYPE_LOCAL_BANK_FIELD:

			next_object = object->bank_field.bank_obj;
			status =
			    acpi_ut_create_update_state_and_push(object->
								 bank_field.
								 region_obj,
								 action,
								 &state_list);
			if (ACPI_FAILURE(status)) {
				goto error_exit;
			}
			break;

		case ACPI_TYPE_LOCAL_INDEX_FIELD:

			next_object = object->index_field.index_obj;
			status =
			    acpi_ut_create_update_state_and_push(object->
								 index_field.
								 data_obj,
								 action,
								 &state_list);
			if (ACPI_FAILURE(status)) {
				goto error_exit;
			}
			break;

		case ACPI_TYPE_LOCAL_REFERENCE:
			/*
			 * The target of an Index (a package, string, or buffer) must track
			 * changes to the ref count of the index.
			 */
			if (object->reference.opcode == AML_INDEX_OP) {
				next_object = object->reference.object;
			}
			break;

		case ACPI_TYPE_REGION:
		default:
			break;	/* No subobjects for all other types */
		}

		/*
		 * Now we can update the count in the main object. This can only
		 * happen after we update the sub-objects in case this causes the
		 * main object to be deleted.
		 */
		acpi_ut_update_ref_count(object, action);
		object = NULL;

		/* Move on to the next object to be updated */

		if (next_object) {
			object = next_object;
			next_object = NULL;
		} else if (state_list) {
			state = acpi_ut_pop_generic_state(&state_list);
			object = state->update.object;
			acpi_ut_delete_generic_state(state);
		}
	}

	return_ACPI_STATUS(AE_OK);

      error_exit:

	ACPI_EXCEPTION((AE_INFO, status,
			"Could not update object reference count"));

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_add_reference
 *
 * PARAMETERS:  Object          - Object whose reference count is to be
 *                                incremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add one reference to an ACPI object
 *
 ******************************************************************************/

void acpi_ut_add_reference(union acpi_operand_object *object)
{

	ACPI_FUNCTION_TRACE_PTR(ut_add_reference, object);

	/* Ensure that we have a valid object */

	if (!acpi_ut_valid_internal_object(object)) {
		return_VOID;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
			  "Obj %p Current Refs=%X [To Be Incremented]\n",
			  object, object->common.reference_count));

	/* Increment the reference count */

	(void)acpi_ut_update_object_reference(object, REF_INCREMENT);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_remove_reference
 *
 * PARAMETERS:  Object         - Object whose ref count will be decremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decrement the reference count of an ACPI internal object
 *
 ******************************************************************************/

void acpi_ut_remove_reference(union acpi_operand_object *object)
{

	ACPI_FUNCTION_TRACE_PTR(ut_remove_reference, object);

	/*
	 * Allow a NULL pointer to be passed in, just ignore it. This saves
	 * each caller from having to check. Also, ignore NS nodes.
	 *
	 */
	if (!object ||
	    (ACPI_GET_DESCRIPTOR_TYPE(object) == ACPI_DESC_TYPE_NAMED)) {
		return_VOID;
	}

	/* Ensure that we have a valid object */

	if (!acpi_ut_valid_internal_object(object)) {
		return_VOID;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
			  "Obj %p Current Refs=%X [To Be Decremented]\n",
			  object, object->common.reference_count));

	/*
	 * Decrement the reference count, and only actually delete the object
	 * if the reference count becomes 0. (Must also decrement the ref count
	 * of all subobjects!)
	 */
	(void)acpi_ut_update_object_reference(object, REF_DECREMENT);
	return_VOID;
}
