// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: utdelete - object deletion and reference count utilities
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acevents.h"

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
 * PARAMETERS:  object         - Object to be deleted
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
	union acpi_operand_object *start_desc;
	union acpi_operand_object **last_obj_ptr;

	ACPI_FUNCTION_TRACE_PTR(ut_delete_internal_obj, object);

	if (!object) {
		return_VOID;
	}

	/*
	 * Must delete or free any pointers within the object that are not
	 * actual ACPI objects (for example, a raw buffer pointer).
	 */
	switch (object->common.type) {
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

		/*
		 * These objects have a possible list of notify handlers.
		 * Device object also may have a GPE block.
		 */
	case ACPI_TYPE_DEVICE:

		if (object->device.gpe_block) {
			(void)acpi_ev_delete_gpe_block(object->device.
						       gpe_block);
		}

		/*lint -fallthrough */

	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_THERMAL:

		/* Walk the address handler list for this object */

		handler_desc = object->common_notify.handler;
		while (handler_desc) {
			next_desc = handler_desc->address_space.next;
			acpi_ut_remove_reference(handler_desc);
			handler_desc = next_desc;
		}
		break;

	case ACPI_TYPE_MUTEX:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Mutex %p, OS Mutex %p\n",
				  object, object->mutex.os_mutex));

		if (object == acpi_gbl_global_lock_mutex) {

			/* Global Lock has extra semaphore */

			(void)
			    acpi_os_delete_semaphore
			    (acpi_gbl_global_lock_semaphore);
			acpi_gbl_global_lock_semaphore = NULL;

			acpi_os_delete_mutex(object->mutex.os_mutex);
			acpi_gbl_global_lock_mutex = NULL;
		} else {
			acpi_ex_unlink_mutex(object);
			acpi_os_delete_mutex(object->mutex.os_mutex);
		}
		break;

	case ACPI_TYPE_EVENT:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Event %p, OS Semaphore %p\n",
				  object, object->event.os_semaphore));

		(void)acpi_os_delete_semaphore(object->event.os_semaphore);
		object->event.os_semaphore = NULL;
		break;

	case ACPI_TYPE_METHOD:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Method %p\n", object));

		/* Delete the method mutex if it exists */

		if (object->method.mutex) {
			acpi_os_delete_mutex(object->method.mutex->mutex.
					     os_mutex);
			acpi_ut_delete_object_desc(object->method.mutex);
			object->method.mutex = NULL;
		}

		if (object->method.node) {
			object->method.node = NULL;
		}
		break;

	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Region %p\n", object));

		/*
		 * Update address_range list. However, only permanent regions
		 * are installed in this list. (Not created within a method)
		 */
		if (!(object->region.node->flags & ANOBJ_TEMPORARY)) {
			acpi_ut_remove_address_range(object->region.space_id,
						     object->region.node);
		}

		second_desc = acpi_ns_get_secondary_object(object);
		if (second_desc) {
			/*
			 * Free the region_context if and only if the handler is one of the
			 * default handlers -- and therefore, we created the context object
			 * locally, it was not created by an external caller.
			 */
			handler_desc = object->region.handler;
			if (handler_desc) {
				next_desc =
				    handler_desc->address_space.region_list;
				start_desc = next_desc;
				last_obj_ptr =
				    &handler_desc->address_space.region_list;

				/* Remove the region object from the handler list */

				while (next_desc) {
					if (next_desc == object) {
						*last_obj_ptr =
						    next_desc->region.next;
						break;
					}

					/* Walk the linked list of handlers */

					last_obj_ptr = &next_desc->region.next;
					next_desc = next_desc->region.next;

					/* Prevent infinite loop if list is corrupted */

					if (next_desc == start_desc) {
						ACPI_ERROR((AE_INFO,
							    "Circular region list in address handler object %p",
							    handler_desc));
						return_VOID;
					}
				}

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
		if (object->field.internal_pcc_buffer) {
			ACPI_FREE(object->field.internal_pcc_buffer);
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

	case ACPI_TYPE_LOCAL_BANK_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "***** Bank Field %p\n", object));

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

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_ALLOCATIONS,
			      "%s: Deleting Object %p [%s]\n",
			      ACPI_GET_FUNCTION_NAME, object,
			      acpi_ut_get_object_type_name(object)));

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

	ACPI_FUNCTION_ENTRY();

	/* Walk the null-terminated internal list */

	for (internal_obj = obj_list; *internal_obj; internal_obj++) {
		acpi_ut_remove_reference(*internal_obj);
	}

	/* Free the combined parameter pointer list and object array */

	ACPI_FREE(obj_list);
	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_update_ref_count
 *
 * PARAMETERS:  object          - Object whose ref count is to be updated
 *              action          - What to do (REF_INCREMENT or REF_DECREMENT)
 *
 * RETURN:      None. Sets new reference count within the object
 *
 * DESCRIPTION: Modify the reference count for an internal acpi object
 *
 ******************************************************************************/

static void
acpi_ut_update_ref_count(union acpi_operand_object *object, u32 action)
{
	u16 original_count;
	u16 new_count = 0;
	acpi_cpu_flags lock_flags;
	char *message;

	ACPI_FUNCTION_NAME(ut_update_ref_count);

	if (!object) {
		return;
	}

	/*
	 * Always get the reference count lock. Note: Interpreter and/or
	 * Namespace is not always locked when this function is called.
	 */
	lock_flags = acpi_os_acquire_lock(acpi_gbl_reference_count_lock);
	original_count = object->common.reference_count;

	/* Perform the reference count action (increment, decrement) */

	switch (action) {
	case REF_INCREMENT:

		new_count = original_count + 1;
		object->common.reference_count = new_count;
		acpi_os_release_lock(acpi_gbl_reference_count_lock, lock_flags);

		/* The current reference count should never be zero here */

		if (!original_count) {
			ACPI_WARNING((AE_INFO,
				      "Obj %p, Reference Count was zero before increment\n",
				      object));
		}

		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "Obj %p Type %.2X [%s] Refs %.2X [Incremented]\n",
				  object, object->common.type,
				  acpi_ut_get_object_type_name(object),
				  new_count));
		message = "Incremement";
		break;

	case REF_DECREMENT:

		/* The current reference count must be non-zero */

		if (original_count) {
			new_count = original_count - 1;
			object->common.reference_count = new_count;
		}

		acpi_os_release_lock(acpi_gbl_reference_count_lock, lock_flags);

		if (!original_count) {
			ACPI_WARNING((AE_INFO,
				      "Obj %p, Reference Count is already zero, cannot decrement\n",
				      object));
		}

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_ALLOCATIONS,
				      "%s: Obj %p Type %.2X Refs %.2X [Decremented]\n",
				      ACPI_GET_FUNCTION_NAME, object,
				      object->common.type, new_count));

		/* Actually delete the object on a reference count of zero */

		if (new_count == 0) {
			acpi_ut_delete_internal_obj(object);
		}
		message = "Decrement";
		break;

	default:

		acpi_os_release_lock(acpi_gbl_reference_count_lock, lock_flags);
		ACPI_ERROR((AE_INFO, "Unknown Reference Count action (0x%X)",
			    action));
		return;
	}

	/*
	 * Sanity check the reference count, for debug purposes only.
	 * (A deleted object will have a huge reference count)
	 */
	if (new_count > ACPI_MAX_REFERENCE_COUNT) {
		ACPI_WARNING((AE_INFO,
			      "Large Reference Count (0x%X) in object %p, Type=0x%.2X Operation=%s",
			      new_count, object, object->common.type, message));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_update_object_reference
 *
 * PARAMETERS:  object              - Increment or decrement the ref count for
 *                                    this object and all sub-objects
 *              action              - Either REF_INCREMENT or REF_DECREMENT
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Increment or decrement the object reference count
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
	union acpi_operand_object *prev_object;
	union acpi_generic_state *state;
	u32 i;

	ACPI_FUNCTION_NAME(ut_update_object_reference);

	while (object) {

		/* Make sure that this isn't a namespace handle */

		if (ACPI_GET_DESCRIPTOR_TYPE(object) == ACPI_DESC_TYPE_NAMED) {
			ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
					  "Object %p is NS handle\n", object));
			return (AE_OK);
		}

		/*
		 * All sub-objects must have their reference count updated
		 * also. Different object types have different subobjects.
		 */
		switch (object->common.type) {
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_PROCESSOR:
		case ACPI_TYPE_POWER:
		case ACPI_TYPE_THERMAL:
			/*
			 * Update the notify objects for these types (if present)
			 * Two lists, system and device notify handlers.
			 */
			for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++) {
				prev_object =
				    object->common_notify.notify_list[i];
				while (prev_object) {
					next_object =
					    prev_object->notify.next[i];
					acpi_ut_update_ref_count(prev_object,
								 action);
					prev_object = next_object;
				}
			}
			break;

		case ACPI_TYPE_PACKAGE:
			/*
			 * We must update all the sub-objects of the package,
			 * each of whom may have their own sub-objects.
			 */
			for (i = 0; i < object->package.count; i++) {
				/*
				 * Null package elements are legal and can be simply
				 * ignored.
				 */
				next_object = object->package.elements[i];
				if (!next_object) {
					continue;
				}

				switch (next_object->common.type) {
				case ACPI_TYPE_INTEGER:
				case ACPI_TYPE_STRING:
				case ACPI_TYPE_BUFFER:
					/*
					 * For these very simple sub-objects, we can just
					 * update the reference count here and continue.
					 * Greatly increases performance of this operation.
					 */
					acpi_ut_update_ref_count(next_object,
								 action);
					break;

				default:
					/*
					 * For complex sub-objects, push them onto the stack
					 * for later processing (this eliminates recursion.)
					 */
					status =
					    acpi_ut_create_update_state_and_push
					    (next_object, action, &state_list);
					if (ACPI_FAILURE(status)) {
						goto error_exit;
					}
					break;
				}
			}

			next_object = NULL;
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
			 * The target of an Index (a package, string, or buffer) or a named
			 * reference must track changes to the ref count of the index or
			 * target object.
			 */
			if ((object->reference.class == ACPI_REFCLASS_INDEX) ||
			    (object->reference.class == ACPI_REFCLASS_NAME)) {
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

	return (AE_OK);

error_exit:

	ACPI_EXCEPTION((AE_INFO, status,
			"Could not update object reference count"));

	/* Free any stacked Update State objects */

	while (state_list) {
		state = acpi_ut_pop_generic_state(&state_list);
		acpi_ut_delete_generic_state(state);
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_add_reference
 *
 * PARAMETERS:  object          - Object whose reference count is to be
 *                                incremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add one reference to an ACPI object
 *
 ******************************************************************************/

void acpi_ut_add_reference(union acpi_operand_object *object)
{

	ACPI_FUNCTION_NAME(ut_add_reference);

	/* Ensure that we have a valid object */

	if (!acpi_ut_valid_internal_object(object)) {
		return;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
			  "Obj %p Current Refs=%X [To Be Incremented]\n",
			  object, object->common.reference_count));

	/* Increment the reference count */

	(void)acpi_ut_update_object_reference(object, REF_INCREMENT);
	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_remove_reference
 *
 * PARAMETERS:  object         - Object whose ref count will be decremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decrement the reference count of an ACPI internal object
 *
 ******************************************************************************/

void acpi_ut_remove_reference(union acpi_operand_object *object)
{

	ACPI_FUNCTION_NAME(ut_remove_reference);

	/*
	 * Allow a NULL pointer to be passed in, just ignore it. This saves
	 * each caller from having to check. Also, ignore NS nodes.
	 */
	if (!object ||
	    (ACPI_GET_DESCRIPTOR_TYPE(object) == ACPI_DESC_TYPE_NAMED)) {
		return;
	}

	/* Ensure that we have a valid object */

	if (!acpi_ut_valid_internal_object(object)) {
		return;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_ALLOCATIONS,
			      "%s: Obj %p Current Refs=%X [To Be Decremented]\n",
			      ACPI_GET_FUNCTION_NAME, object,
			      object->common.reference_count));

	/*
	 * Decrement the reference count, and only actually delete the object
	 * if the reference count becomes 0. (Must also decrement the ref count
	 * of all subobjects!)
	 */
	(void)acpi_ut_update_object_reference(object, REF_DECREMENT);
	return;
}
