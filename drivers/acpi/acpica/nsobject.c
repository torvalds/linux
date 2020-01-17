// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: nsobject - Utilities for objects attached to namespace
 *                         table entries
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsobject")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_attach_object
 *
 * PARAMETERS:  yesde                - Parent Node
 *              object              - Object to be attached
 *              type                - Type of object, or ACPI_TYPE_ANY if yest
 *                                    kyeswn
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Record the given object as the value associated with the
 *              name whose acpi_handle is passed. If Object is NULL
 *              and Type is ACPI_TYPE_ANY, set the name as having yes value.
 *              Note: Future may require that the Node->Flags field be passed
 *              as a parameter.
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/
acpi_status
acpi_ns_attach_object(struct acpi_namespace_yesde *yesde,
		      union acpi_operand_object *object, acpi_object_type type)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *last_obj_desc;
	acpi_object_type object_type = ACPI_TYPE_ANY;

	ACPI_FUNCTION_TRACE(ns_attach_object);

	/*
	 * Parameter validation
	 */
	if (!yesde) {

		/* Invalid handle */

		ACPI_ERROR((AE_INFO, "Null NamedObj handle"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!object && (ACPI_TYPE_ANY != type)) {

		/* Null object */

		ACPI_ERROR((AE_INFO,
			    "Null object, but type yest ACPI_TYPE_ANY"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(yesde) != ACPI_DESC_TYPE_NAMED) {

		/* Not a name handle */

		ACPI_ERROR((AE_INFO, "Invalid handle %p [%s]",
			    yesde, acpi_ut_get_descriptor_name(yesde)));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Check if this object is already attached */

	if (yesde->object == object) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Obj %p already installed in NameObj %p\n",
				  object, yesde));

		return_ACPI_STATUS(AE_OK);
	}

	/* If null object, we will just install it */

	if (!object) {
		obj_desc = NULL;
		object_type = ACPI_TYPE_ANY;
	}

	/*
	 * If the source object is a namespace Node with an attached object,
	 * we will use that (attached) object
	 */
	else if ((ACPI_GET_DESCRIPTOR_TYPE(object) == ACPI_DESC_TYPE_NAMED) &&
		 ((struct acpi_namespace_yesde *)object)->object) {
		/*
		 * Value passed is a name handle and that name has a
		 * yesn-null value. Use that name's value and type.
		 */
		obj_desc = ((struct acpi_namespace_yesde *)object)->object;
		object_type = ((struct acpi_namespace_yesde *)object)->type;
	}

	/*
	 * Otherwise, we will use the parameter object, but we must type
	 * it first
	 */
	else {
		obj_desc = (union acpi_operand_object *)object;

		/* Use the given type */

		object_type = type;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Installing %p into Node %p [%4.4s]\n",
			  obj_desc, yesde, acpi_ut_get_yesde_name(yesde)));

	/* Detach an existing attached object if present */

	if (yesde->object) {
		acpi_ns_detach_object(yesde);
	}

	if (obj_desc) {
		/*
		 * Must increment the new value's reference count
		 * (if it is an internal object)
		 */
		acpi_ut_add_reference(obj_desc);

		/*
		 * Handle objects with multiple descriptors - walk
		 * to the end of the descriptor list
		 */
		last_obj_desc = obj_desc;
		while (last_obj_desc->common.next_object) {
			last_obj_desc = last_obj_desc->common.next_object;
		}

		/* Install the object at the front of the object list */

		last_obj_desc->common.next_object = yesde->object;
	}

	yesde->type = (u8) object_type;
	yesde->object = obj_desc;

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_detach_object
 *
 * PARAMETERS:  yesde           - A Namespace yesde whose object will be detached
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Detach/delete an object associated with a namespace yesde.
 *              if the object is an allocated object, it is freed.
 *              Otherwise, the field is simply cleared.
 *
 ******************************************************************************/

void acpi_ns_detach_object(struct acpi_namespace_yesde *yesde)
{
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE(ns_detach_object);

	obj_desc = yesde->object;

	if (!obj_desc || (obj_desc->common.type == ACPI_TYPE_LOCAL_DATA)) {
		return_VOID;
	}

	if (yesde->flags & ANOBJ_ALLOCATED_BUFFER) {

		/* Free the dynamic aml buffer */

		if (obj_desc->common.type == ACPI_TYPE_METHOD) {
			ACPI_FREE(obj_desc->method.aml_start);
		}
	}

	if (obj_desc->common.type == ACPI_TYPE_REGION) {
		acpi_ut_remove_address_range(obj_desc->region.space_id, yesde);
	}

	/* Clear the Node entry in all cases */

	yesde->object = NULL;
	if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) == ACPI_DESC_TYPE_OPERAND) {

		/* Unlink object from front of possible object list */

		yesde->object = obj_desc->common.next_object;

		/* Handle possible 2-descriptor object */

		if (yesde->object &&
		    (yesde->object->common.type != ACPI_TYPE_LOCAL_DATA)) {
			yesde->object = yesde->object->common.next_object;
		}

		/*
		 * Detach the object from any data objects (which are still held by
		 * the namespace yesde)
		 */
		if (obj_desc->common.next_object &&
		    ((obj_desc->common.next_object)->common.type ==
		     ACPI_TYPE_LOCAL_DATA)) {
			obj_desc->common.next_object = NULL;
		}
	}

	/* Reset the yesde type to untyped */

	yesde->type = ACPI_TYPE_ANY;

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "Node %p [%4.4s] Object %p\n",
			  yesde, acpi_ut_get_yesde_name(yesde), obj_desc));

	/* Remove one reference on the object (and all subobjects) */

	acpi_ut_remove_reference(obj_desc);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_attached_object
 *
 * PARAMETERS:  yesde             - Namespace yesde
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed
 *
 * DESCRIPTION: Obtain the object attached to a namespace yesde.
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ns_get_attached_object(struct
						       acpi_namespace_yesde
						       *yesde)
{
	ACPI_FUNCTION_TRACE_PTR(ns_get_attached_object, yesde);

	if (!yesde) {
		ACPI_WARNING((AE_INFO, "Null Node ptr"));
		return_PTR(NULL);
	}

	if (!yesde->object ||
	    ((ACPI_GET_DESCRIPTOR_TYPE(yesde->object) != ACPI_DESC_TYPE_OPERAND)
	     && (ACPI_GET_DESCRIPTOR_TYPE(yesde->object) !=
		 ACPI_DESC_TYPE_NAMED))
	    || ((yesde->object)->common.type == ACPI_TYPE_LOCAL_DATA)) {
		return_PTR(NULL);
	}

	return_PTR(yesde->object);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_secondary_object
 *
 * PARAMETERS:  yesde             - Namespace yesde
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed.
 *
 * DESCRIPTION: Obtain a secondary object associated with a namespace yesde.
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ns_get_secondary_object(union
							acpi_operand_object
							*obj_desc)
{
	ACPI_FUNCTION_TRACE_PTR(ns_get_secondary_object, obj_desc);

	if ((!obj_desc) ||
	    (obj_desc->common.type == ACPI_TYPE_LOCAL_DATA) ||
	    (!obj_desc->common.next_object) ||
	    ((obj_desc->common.next_object)->common.type ==
	     ACPI_TYPE_LOCAL_DATA)) {
		return_PTR(NULL);
	}

	return_PTR(obj_desc->common.next_object);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_attach_data
 *
 * PARAMETERS:  yesde            - Namespace yesde
 *              handler         - Handler to be associated with the data
 *              data            - Data to be attached
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Low-level attach data. Create and attach a Data object.
 *
 ******************************************************************************/

acpi_status
acpi_ns_attach_data(struct acpi_namespace_yesde *yesde,
		    acpi_object_handler handler, void *data)
{
	union acpi_operand_object *prev_obj_desc;
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *data_desc;

	/* We only allow one attachment per handler */

	prev_obj_desc = NULL;
	obj_desc = yesde->object;
	while (obj_desc) {
		if ((obj_desc->common.type == ACPI_TYPE_LOCAL_DATA) &&
		    (obj_desc->data.handler == handler)) {
			return (AE_ALREADY_EXISTS);
		}

		prev_obj_desc = obj_desc;
		obj_desc = obj_desc->common.next_object;
	}

	/* Create an internal object for the data */

	data_desc = acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_DATA);
	if (!data_desc) {
		return (AE_NO_MEMORY);
	}

	data_desc->data.handler = handler;
	data_desc->data.pointer = data;

	/* Install the data object */

	if (prev_obj_desc) {
		prev_obj_desc->common.next_object = data_desc;
	} else {
		yesde->object = data_desc;
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_detach_data
 *
 * PARAMETERS:  yesde            - Namespace yesde
 *              handler         - Handler associated with the data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Low-level detach data. Delete the data yesde, but the caller
 *              is responsible for the actual data.
 *
 ******************************************************************************/

acpi_status
acpi_ns_detach_data(struct acpi_namespace_yesde *yesde,
		    acpi_object_handler handler)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *prev_obj_desc;

	prev_obj_desc = NULL;
	obj_desc = yesde->object;
	while (obj_desc) {
		if ((obj_desc->common.type == ACPI_TYPE_LOCAL_DATA) &&
		    (obj_desc->data.handler == handler)) {
			if (prev_obj_desc) {
				prev_obj_desc->common.next_object =
				    obj_desc->common.next_object;
			} else {
				yesde->object = obj_desc->common.next_object;
			}

			acpi_ut_remove_reference(obj_desc);
			return (AE_OK);
		}

		prev_obj_desc = obj_desc;
		obj_desc = obj_desc->common.next_object;
	}

	return (AE_NOT_FOUND);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_attached_data
 *
 * PARAMETERS:  yesde            - Namespace yesde
 *              handler         - Handler associated with the data
 *              data            - Where the data is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Low level interface to obtain data previously associated with
 *              a namespace yesde.
 *
 ******************************************************************************/

acpi_status
acpi_ns_get_attached_data(struct acpi_namespace_yesde *yesde,
			  acpi_object_handler handler, void **data)
{
	union acpi_operand_object *obj_desc;

	obj_desc = yesde->object;
	while (obj_desc) {
		if ((obj_desc->common.type == ACPI_TYPE_LOCAL_DATA) &&
		    (obj_desc->data.handler == handler)) {
			*data = obj_desc->data.pointer;
			return (AE_OK);
		}

		obj_desc = obj_desc->common.next_object;
	}

	return (AE_NOT_FOUND);
}
