
/******************************************************************************
 *
 * Module Name: exresolv - AML Interpreter object resolution
 *
 *****************************************************************************/

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
#include <acpi/amlcode.h>
#include <acpi/acdispat.h>
#include <acpi/acinterp.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>

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
	void *temp_node;
	union acpi_operand_object *obj_desc;
	u16 opcode;

	ACPI_FUNCTION_TRACE(ex_resolve_object_to_value);

	stack_desc = *stack_ptr;

	/* This is an union acpi_operand_object    */

	switch (ACPI_GET_OBJECT_TYPE(stack_desc)) {
	case ACPI_TYPE_LOCAL_REFERENCE:

		opcode = stack_desc->reference.opcode;

		switch (opcode) {
		case AML_NAME_OP:

			/*
			 * Convert name reference to a namespace node
			 * Then, acpi_ex_resolve_node_to_value can be used to get the value
			 */
			temp_node = stack_desc->reference.object;

			/* Delete the Reference Object */

			acpi_ut_remove_reference(stack_desc);

			/* Return the namespace node */

			(*stack_ptr) = temp_node;
			break;

		case AML_LOCAL_OP:
		case AML_ARG_OP:

			/*
			 * Get the local from the method's state info
			 * Note: this increments the local's object reference count
			 */
			status = acpi_ds_method_data_get_value(opcode,
							       stack_desc->
							       reference.offset,
							       walk_state,
							       &obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "[Arg/Local %X] ValueObj is %p\n",
					  stack_desc->reference.offset,
					  obj_desc));

			/*
			 * Now we can delete the original Reference Object and
			 * replace it with the resolved value
			 */
			acpi_ut_remove_reference(stack_desc);
			*stack_ptr = obj_desc;
			break;

		case AML_INDEX_OP:

			switch (stack_desc->reference.target_type) {
			case ACPI_TYPE_BUFFER_FIELD:

				/* Just return - leave the Reference on the stack */
				break;

			case ACPI_TYPE_PACKAGE:

				obj_desc = *stack_desc->reference.where;
				if (obj_desc) {
					/*
					 * Valid obj descriptor, copy pointer to return value
					 * (i.e., dereference the package index)
					 * Delete the ref object, increment the returned object
					 */
					acpi_ut_remove_reference(stack_desc);
					acpi_ut_add_reference(obj_desc);
					*stack_ptr = obj_desc;
				} else {
					/*
					 * A NULL object descriptor means an unitialized element of
					 * the package, can't dereference it
					 */
					ACPI_ERROR((AE_INFO,
						    "Attempt to deref an Index to NULL pkg element Idx=%p",
						    stack_desc));
					status = AE_AML_UNINITIALIZED_ELEMENT;
				}
				break;

			default:

				/* Invalid reference object */

				ACPI_ERROR((AE_INFO,
					    "Unknown TargetType %X in Index/Reference obj %p",
					    stack_desc->reference.target_type,
					    stack_desc));
				status = AE_AML_INTERNAL;
				break;
			}
			break;

		case AML_REF_OF_OP:
		case AML_DEBUG_OP:
		case AML_LOAD_OP:

			/* Just leave the object as-is */

			break;

		case AML_INT_NAMEPATH_OP:	/* Reference to a named object */

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
				    "Unknown Reference opcode %X (%s) in %p",
				    opcode, acpi_ps_get_opcode_name(opcode),
				    stack_desc));
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

		/* These cases may never happen here, but just in case.. */

	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "FieldRead SourceDesc=%p Type=%X\n",
				  stack_desc,
				  ACPI_GET_OBJECT_TYPE(stack_desc)));

		status =
		    acpi_ex_read_data_from_field(walk_state, stack_desc,
						 &obj_desc);
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
 *              Operand             - Starting point for resolution
 *              return_type         - Where the object type is returned
 *              return_desc         - Where the resolved object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the base object and type.  Traverse a reference list if
 *              necessary to get to the base object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_multiple(struct acpi_walk_state *walk_state,
			 union acpi_operand_object *operand,
			 acpi_object_type * return_type,
			 union acpi_operand_object **return_desc)
{
	union acpi_operand_object *obj_desc = (void *)operand;
	struct acpi_namespace_node *node;
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
		obj_desc =
		    acpi_ns_get_attached_object((struct acpi_namespace_node *)
						obj_desc);

		/* If we had an Alias node, use the attached object for type info */

		if (type == ACPI_TYPE_LOCAL_ALIAS) {
			type = ((struct acpi_namespace_node *)obj_desc)->type;
			obj_desc =
			    acpi_ns_get_attached_object((struct
							 acpi_namespace_node *)
							obj_desc);
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
	 * For reference objects created via the ref_of or Index operators,
	 * we need to get to the base object (as per the ACPI specification
	 * of the object_type and size_of operators). This means traversing
	 * the list of possibly many nested references.
	 */
	while (ACPI_GET_OBJECT_TYPE(obj_desc) == ACPI_TYPE_LOCAL_REFERENCE) {
		switch (obj_desc->reference.opcode) {
		case AML_REF_OF_OP:
		case AML_INT_NAMEPATH_OP:

			/* Dereference the reference pointer */

			if (obj_desc->reference.opcode == AML_REF_OF_OP) {
				node = obj_desc->reference.object;
			} else {	/* AML_INT_NAMEPATH_OP */

				node = obj_desc->reference.node;
			}

			/* All "References" point to a NS node */

			if (ACPI_GET_DESCRIPTOR_TYPE(node) !=
			    ACPI_DESC_TYPE_NAMED) {
				ACPI_ERROR((AE_INFO, "Not a NS node %p [%s]",
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

		case AML_INDEX_OP:

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

		case AML_LOCAL_OP:
		case AML_ARG_OP:

			if (return_desc) {
				status =
				    acpi_ds_method_data_get_value(obj_desc->
								  reference.
								  opcode,
								  obj_desc->
								  reference.
								  offset,
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
								 opcode,
								 obj_desc->
								 reference.
								 offset,
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

		case AML_DEBUG_OP:

			/* The Debug Object is of type "DebugObject" */

			type = ACPI_TYPE_DEBUG_OBJECT;
			goto exit;

		default:

			ACPI_ERROR((AE_INFO,
				    "Unknown Reference subtype %X",
				    obj_desc->reference.opcode));
			return_ACPI_STATUS(AE_AML_INTERNAL);
		}
	}

	/*
	 * Now we are guaranteed to have an object that has not been created
	 * via the ref_of or Index operators.
	 */
	type = ACPI_GET_OBJECT_TYPE(obj_desc);

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
