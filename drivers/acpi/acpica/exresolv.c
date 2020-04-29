// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exresolv - AML Interpreter object resolution
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exresolv")

/* Local prototypes */
static acpi_status
acpi_ex_resolve_object_to_value(union acpi_operand_object **stack_ptr,
				struct acpi_walk_state *walk_state);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_resolve_to_value
 *
 * PARAMETERS:  **stack_ptr         - Points to entry on obj_stack, which can
 *                                    be either an (union acpi_operand_object *)
 *                                    or an acpi_handle.
 *              walk_state          - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert Reference objects to values
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_to_value(union acpi_operand_object **stack_ptr,
			 struct acpi_walk_state *walk_state)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ex_resolve_to_value, stack_ptr);

	if (!stack_ptr || !*stack_ptr) {
		ACPI_ERROR((AE_INFO, "Internal - null pointer"));
		return_ACPI_STATUS(AE_AML_NO_OPERAND);
	}

	/*
	 * The entity pointed to by the stack_ptr can be either
	 * 1) A valid union acpi_operand_object, or
	 * 2) A struct acpi_namespace_node (named_obj)
	 */
	if (ACPI_GET_DESCRIPTOR_TYPE(*stack_ptr) == ACPI_DESC_TYPE_OPERAND) {
		status = acpi_ex_resolve_object_to_value(stack_ptr, walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		if (!*stack_ptr) {
			ACPI_ERROR((AE_INFO, "Internal - null pointer"));
			return_ACPI_STATUS(AE_AML_NO_OPERAND);
		}
	}

	/*
	 * Object on the stack may have changed if acpi_ex_resolve_object_to_value()
	 * was called (i.e., we can't use an _else_ here.)
	 */
	if (ACPI_GET_DESCRIPTOR_TYPE(*stack_ptr) == ACPI_DESC_TYPE_NAMED) {
		status =
		    acpi_ex_resolve_node_to_value(ACPI_CAST_INDIRECT_PTR
						  (struct acpi_namespace_node,
						   stack_ptr), walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Resolved object %p\n", *stack_ptr));
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_resolve_object_to_value
 *
 * PARAMETERS:  stack_ptr       - Pointer to an internal object
 *              walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value from an internal object. The Reference type
 *              uses the associated AML opcode to determine the value.
 *
 ******************************************************************************/

static acpi_status
acpi_ex_resolve_object_to_value(union acpi_operand_object **stack_ptr,
				struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *stack_desc;
	union acpi_operand_object *obj_desc = NULL;
	u8 ref_type;

	ACPI_FUNCTION_TRACE(ex_resolve_object_to_value);

	stack_desc = *stack_ptr;

	/* This is an object of type union acpi_operand_object */

	switch (stack_desc->common.type) {
	case ACPI_TYPE_LOCAL_REFERENCE:

		ref_type = stack_desc->reference.class;

		switch (ref_type) {
		case ACPI_REFCLASS_LOCAL:
		case ACPI_REFCLASS_ARG:
			/*
			 * Get the local from the method's state info
			 * Note: this increments the local's object reference count
			 */
			status = acpi_ds_method_data_get_value(ref_type,
							       stack_desc->
							       reference.value,
							       walk_state,
							       &obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "[Arg/Local %X] ValueObj is %p\n",
					  stack_desc->reference.value,
					  obj_desc));

			/*
			 * Now we can delete the original Reference Object and
			 * replace it with the resolved value
			 */
			acpi_ut_remove_reference(stack_desc);
			*stack_ptr = obj_desc;
			break;

		case ACPI_REFCLASS_INDEX:

			switch (stack_desc->reference.target_type) {
			case ACPI_TYPE_BUFFER_FIELD:

				/* Just return - do not dereference */
				break;

			case ACPI_TYPE_PACKAGE:

				/* If method call or copy_object - do not dereference */

				if ((walk_state->opcode ==
				     AML_INT_METHODCALL_OP)
				    || (walk_state->opcode ==
					AML_COPY_OBJECT_OP)) {
					break;
				}

				/* Otherwise, dereference the package_index to a package element */

				obj_desc = *stack_desc->reference.where;
				if (obj_desc) {
					/*
					 * Valid object descriptor, copy pointer to return value
					 * (i.e., dereference the package index)
					 * Delete the ref object, increment the returned object
					 */
					acpi_ut_add_reference(obj_desc);
					*stack_ptr = obj_desc;
				} else {
					/*
					 * A NULL object descriptor means an uninitialized element of
					 * the package, can't dereference it
					 */
					ACPI_ERROR((AE_INFO,
						    "Attempt to dereference an Index to "
						    "NULL package element Idx=%p",
						    stack_desc));
					status = AE_AML_UNINITIALIZED_ELEMENT;
				}
				break;

			default:

				/* Invalid reference object */

				ACPI_ERROR((AE_INFO,
					    "Unknown TargetType 0x%X in Index/Reference object %p",
					    stack_desc->reference.target_type,
					    stack_desc));
				status = AE_AML_INTERNAL;
				break;
			}
			break;

		case ACPI_REFCLASS_REFOF:
		case ACPI_REFCLASS_DEBUG:
		case ACPI_REFCLASS_TABLE:

			/* Just leave the object as-is, do not dereference */

			break;

		case ACPI_REFCLASS_NAME:	/* Reference to a named object */

			/* Dereference the name */

			if ((stack_desc->reference.node->type ==
			     ACPI_TYPE_DEVICE)
			    || (stack_desc->reference.node->type ==
				ACPI_TYPE_THERMAL)) {

				/* These node types do not have 'real' subobjects */

				*stack_ptr = (void *)stack_desc->reference.node;
			} else {
				/* Get the object pointed to by the namespace node */

				*stack_ptr =
				    (stack_desc->reference.node)->object;
				acpi_ut_add_reference(*stack_ptr);
			}

			acpi_ut_remove_reference(stack_desc);
			break;

		default:

			ACPI_ERROR((AE_INFO,
				    "Unknown Reference type 0x%X in %p",
				    ref_type, stack_desc));
			status = AE_AML_INTERNAL;
			break;
		}
		break;

	case ACPI_TYPE_BUFFER:

		status = acpi_ds_get_buffer_arguments(stack_desc);
		break;

	case ACPI_TYPE_PACKAGE:

		status = acpi_ds_get_package_arguments(stack_desc);
		break;

	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "FieldRead SourceDesc=%p Type=%X\n",
				  stack_desc, stack_desc->common.type));

		status =
		    acpi_ex_read_data_from_field(walk_state, stack_desc,
						 &obj_desc);

		/* Remove a reference to the original operand, then override */

		acpi_ut_remove_reference(*stack_ptr);
		*stack_ptr = (void *)obj_desc;
		break;

	default:

		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_resolve_multiple
 *
 * PARAMETERS:  walk_state          - Current state (contains AML opcode)
 *              operand             - Starting point for resolution
 *              return_type         - Where the object type is returned
 *              return_desc         - Where the resolved object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the base object and type. Traverse a reference list if
 *              necessary to get to the base object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_multiple(struct acpi_walk_state *walk_state,
			 union acpi_operand_object *operand,
			 acpi_object_type *return_type,
			 union acpi_operand_object **return_desc)
{
	union acpi_operand_object *obj_desc = ACPI_CAST_PTR(void, operand);
	struct acpi_namespace_node *node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, operand);
	acpi_object_type type;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_ex_resolve_multiple);

	/* Operand can be either a namespace node or an operand descriptor */

	switch (ACPI_GET_DESCRIPTOR_TYPE(obj_desc)) {
	case ACPI_DESC_TYPE_OPERAND:

		type = obj_desc->common.type;
		break;

	case ACPI_DESC_TYPE_NAMED:

		type = ((struct acpi_namespace_node *)obj_desc)->type;
		obj_desc = acpi_ns_get_attached_object(node);

		/* If we had an Alias node, use the attached object for type info */

		if (type == ACPI_TYPE_LOCAL_ALIAS) {
			type = ((struct acpi_namespace_node *)obj_desc)->type;
			obj_desc = acpi_ns_get_attached_object((struct
								acpi_namespace_node
								*)obj_desc);
		}

		switch (type) {
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_THERMAL:

			/* These types have no attached subobject */
			break;

		default:

			/* All other types require a subobject */

			if (!obj_desc) {
				ACPI_ERROR((AE_INFO,
					    "[%4.4s] Node is unresolved or uninitialized",
					    acpi_ut_get_node_name(node)));
				return_ACPI_STATUS(AE_AML_UNINITIALIZED_NODE);
			}
			break;
		}
		break;

	default:
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/* If type is anything other than a reference, we are done */

	if (type != ACPI_TYPE_LOCAL_REFERENCE) {
		goto exit;
	}

	/*
	 * For reference objects created via the ref_of, Index, or Load/load_table
	 * operators, we need to get to the base object (as per the ACPI
	 * specification of the object_type and size_of operators). This means
	 * traversing the list of possibly many nested references.
	 */
	while (obj_desc->common.type == ACPI_TYPE_LOCAL_REFERENCE) {
		switch (obj_desc->reference.class) {
		case ACPI_REFCLASS_REFOF:
		case ACPI_REFCLASS_NAME:

			/* Dereference the reference pointer */

			if (obj_desc->reference.class == ACPI_REFCLASS_REFOF) {
				node = obj_desc->reference.object;
			} else {	/* AML_INT_NAMEPATH_OP */

				node = obj_desc->reference.node;
			}

			/* All "References" point to a NS node */

			if (ACPI_GET_DESCRIPTOR_TYPE(node) !=
			    ACPI_DESC_TYPE_NAMED) {
				ACPI_ERROR((AE_INFO,
					    "Not a namespace node %p [%s]",
					    node,
					    acpi_ut_get_descriptor_name(node)));
				return_ACPI_STATUS(AE_AML_INTERNAL);
			}

			/* Get the attached object */

			obj_desc = acpi_ns_get_attached_object(node);
			if (!obj_desc) {

				/* No object, use the NS node type */

				type = acpi_ns_get_type(node);
				goto exit;
			}

			/* Check for circular references */

			if (obj_desc == operand) {
				return_ACPI_STATUS(AE_AML_CIRCULAR_REFERENCE);
			}
			break;

		case ACPI_REFCLASS_INDEX:

			/* Get the type of this reference (index into another object) */

			type = obj_desc->reference.target_type;
			if (type != ACPI_TYPE_PACKAGE) {
				goto exit;
			}

			/*
			 * The main object is a package, we want to get the type
			 * of the individual package element that is referenced by
			 * the index.
			 *
			 * This could of course in turn be another reference object.
			 */
			obj_desc = *(obj_desc->reference.where);
			if (!obj_desc) {

				/* NULL package elements are allowed */

				type = 0;	/* Uninitialized */
				goto exit;
			}
			break;

		case ACPI_REFCLASS_TABLE:

			type = ACPI_TYPE_DDB_HANDLE;
			goto exit;

		case ACPI_REFCLASS_LOCAL:
		case ACPI_REFCLASS_ARG:

			if (return_desc) {
				status =
				    acpi_ds_method_data_get_value(obj_desc->
								  reference.
								  class,
								  obj_desc->
								  reference.
								  value,
								  walk_state,
								  &obj_desc);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}
				acpi_ut_remove_reference(obj_desc);
			} else {
				status =
				    acpi_ds_method_data_get_node(obj_desc->
								 reference.
								 class,
								 obj_desc->
								 reference.
								 value,
								 walk_state,
								 &node);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}

				obj_desc = acpi_ns_get_attached_object(node);
				if (!obj_desc) {
					type = ACPI_TYPE_ANY;
					goto exit;
				}
			}
			break;

		case ACPI_REFCLASS_DEBUG:

			/* The Debug Object is of type "DebugObject" */

			type = ACPI_TYPE_DEBUG_OBJECT;
			goto exit;

		default:

			ACPI_ERROR((AE_INFO,
				    "Unknown Reference Class 0x%2.2X",
				    obj_desc->reference.class));
			return_ACPI_STATUS(AE_AML_INTERNAL);
		}
	}

	/*
	 * Now we are guaranteed to have an object that has not been created
	 * via the ref_of or Index operators.
	 */
	type = obj_desc->common.type;

exit:
	/* Convert internal types to external types */

	switch (type) {
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		type = ACPI_TYPE_FIELD_UNIT;
		break;

	case ACPI_TYPE_LOCAL_SCOPE:

		/* Per ACPI Specification, Scope is untyped */

		type = ACPI_TYPE_ANY;
		break;

	default:

		/* No change to Type required */

		break;
	}

	*return_type = type;
	if (return_desc) {
		*return_desc = obj_desc;
	}
	return_ACPI_STATUS(AE_OK);
}
