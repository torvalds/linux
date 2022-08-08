// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dsobject - Dispatcher object management routines
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dsobject")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_object
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              op              - Parser object to be translated
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op object to the equivalent namespace object
 *              Simple objects are any objects other than a package object!
 *
 ******************************************************************************/
acpi_status
acpi_ds_build_internal_object(struct acpi_walk_state *walk_state,
			      union acpi_parse_object *op,
			      union acpi_operand_object **obj_desc_ptr)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ds_build_internal_object);

	*obj_desc_ptr = NULL;
	if (op->common.aml_opcode == AML_INT_NAMEPATH_OP) {
		/*
		 * This is a named object reference. If this name was
		 * previously looked up in the namespace, it was stored in
		 * this op. Otherwise, go ahead and look it up now
		 */
		if (!op->common.node) {

			/* Check if we are resolving a named reference within a package */

			if ((op->common.parent->common.aml_opcode ==
			     AML_PACKAGE_OP)
			    || (op->common.parent->common.aml_opcode ==
				AML_VARIABLE_PACKAGE_OP)) {
				/*
				 * We won't resolve package elements here, we will do this
				 * after all ACPI tables are loaded into the namespace. This
				 * behavior supports both forward references to named objects
				 * and external references to objects in other tables.
				 */
				goto create_new_object;
			} else {
				status = acpi_ns_lookup(walk_state->scope_info,
							op->common.value.string,
							ACPI_TYPE_ANY,
							ACPI_IMODE_EXECUTE,
							ACPI_NS_SEARCH_PARENT |
							ACPI_NS_DONT_OPEN_SCOPE,
							NULL,
							ACPI_CAST_INDIRECT_PTR
							(struct
							 acpi_namespace_node,
							 &(op->common.node)));
				if (ACPI_FAILURE(status)) {
					ACPI_ERROR_NAMESPACE(walk_state->
							     scope_info,
							     op->common.value.
							     string, status);
					return_ACPI_STATUS(status);
				}
			}
		}
	}

create_new_object:

	/* Create and init a new internal ACPI object */

	obj_desc = acpi_ut_create_internal_object((acpi_ps_get_opcode_info
						   (op->common.aml_opcode))->
						  object_type);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	status =
	    acpi_ds_init_object_from_op(walk_state, op, op->common.aml_opcode,
					&obj_desc);
	if (ACPI_FAILURE(status)) {
		acpi_ut_remove_reference(obj_desc);
		return_ACPI_STATUS(status);
	}

	/*
	 * Handling for unresolved package reference elements.
	 * These are elements that are namepaths.
	 */
	if ((op->common.parent->common.aml_opcode == AML_PACKAGE_OP) ||
	    (op->common.parent->common.aml_opcode == AML_VARIABLE_PACKAGE_OP)) {
		obj_desc->reference.resolved = TRUE;

		if ((op->common.aml_opcode == AML_INT_NAMEPATH_OP) &&
		    !obj_desc->reference.node) {
			/*
			 * Name was unresolved above.
			 * Get the prefix node for later lookup
			 */
			obj_desc->reference.node =
			    walk_state->scope_info->scope.node;
			obj_desc->reference.aml = op->common.aml;
			obj_desc->reference.resolved = FALSE;
		}
	}

	*obj_desc_ptr = obj_desc;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_buffer_obj
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              op              - Parser object to be translated
 *              buffer_length   - Length of the buffer
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 ******************************************************************************/

acpi_status
acpi_ds_build_internal_buffer_obj(struct acpi_walk_state *walk_state,
				  union acpi_parse_object *op,
				  u32 buffer_length,
				  union acpi_operand_object **obj_desc_ptr)
{
	union acpi_parse_object *arg;
	union acpi_operand_object *obj_desc;
	union acpi_parse_object *byte_list;
	u32 byte_list_length = 0;

	ACPI_FUNCTION_TRACE(ds_build_internal_buffer_obj);

	/*
	 * If we are evaluating a Named buffer object "Name (xxxx, Buffer)".
	 * The buffer object already exists (from the NS node), otherwise it must
	 * be created.
	 */
	obj_desc = *obj_desc_ptr;
	if (!obj_desc) {

		/* Create a new buffer object */

		obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_BUFFER);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}
	}

	/*
	 * Second arg is the buffer data (optional) byte_list can be either
	 * individual bytes or a string initializer. In either case, a
	 * byte_list appears in the AML.
	 */
	arg = op->common.value.arg;	/* skip first arg */

	byte_list = arg->named.next;
	if (byte_list) {
		if (byte_list->common.aml_opcode != AML_INT_BYTELIST_OP) {
			ACPI_ERROR((AE_INFO,
				    "Expecting bytelist, found AML opcode 0x%X in op %p",
				    byte_list->common.aml_opcode, byte_list));

			acpi_ut_remove_reference(obj_desc);
			return (AE_TYPE);
		}

		byte_list_length = (u32) byte_list->common.value.integer;
	}

	/*
	 * The buffer length (number of bytes) will be the larger of:
	 * 1) The specified buffer length and
	 * 2) The length of the initializer byte list
	 */
	obj_desc->buffer.length = buffer_length;
	if (byte_list_length > buffer_length) {
		obj_desc->buffer.length = byte_list_length;
	}

	/* Allocate the buffer */

	if (obj_desc->buffer.length == 0) {
		obj_desc->buffer.pointer = NULL;
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Buffer defined with zero length in AML, creating\n"));
	} else {
		obj_desc->buffer.pointer =
		    ACPI_ALLOCATE_ZEROED(obj_desc->buffer.length);
		if (!obj_desc->buffer.pointer) {
			acpi_ut_delete_object_desc(obj_desc);
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Initialize buffer from the byte_list (if present) */

		if (byte_list) {
			memcpy(obj_desc->buffer.pointer, byte_list->named.data,
			       byte_list_length);
		}
	}

	obj_desc->buffer.flags |= AOPOBJ_DATA_VALID;
	op->common.node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_desc);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_create_node
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              node            - NS Node to be initialized
 *              op              - Parser object to be translated
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the object to be associated with a namespace node
 *
 ******************************************************************************/

acpi_status
acpi_ds_create_node(struct acpi_walk_state *walk_state,
		    struct acpi_namespace_node *node,
		    union acpi_parse_object *op)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE_PTR(ds_create_node, op);

	/*
	 * Because of the execution pass through the non-control-method
	 * parts of the table, we can arrive here twice. Only init
	 * the named object node the first time through
	 */
	if (acpi_ns_get_attached_object(node)) {
		return_ACPI_STATUS(AE_OK);
	}

	if (!op->common.value.arg) {

		/* No arguments, there is nothing to do */

		return_ACPI_STATUS(AE_OK);
	}

	/* Build an internal object for the argument(s) */

	status =
	    acpi_ds_build_internal_object(walk_state, op->common.value.arg,
					  &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Re-type the object according to its argument */

	node->type = obj_desc->common.type;

	/* Attach obj to node */

	status = acpi_ns_attach_object(node, obj_desc, node->type);

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_init_object_from_op
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              op              - Parser op used to init the internal object
 *              opcode          - AML opcode associated with the object
 *              ret_obj_desc    - Namespace object to be initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize a namespace object from a parser Op and its
 *              associated arguments. The namespace object is a more compact
 *              representation of the Op and its arguments.
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_object_from_op(struct acpi_walk_state *walk_state,
			    union acpi_parse_object *op,
			    u16 opcode,
			    union acpi_operand_object **ret_obj_desc)
{
	const struct acpi_opcode_info *op_info;
	union acpi_operand_object *obj_desc;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ds_init_object_from_op);

	obj_desc = *ret_obj_desc;
	op_info = acpi_ps_get_opcode_info(opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {

		/* Unknown opcode */

		return_ACPI_STATUS(AE_TYPE);
	}

	/* Perform per-object initialization */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_BUFFER:
		/*
		 * Defer evaluation of Buffer term_arg operand
		 */
		obj_desc->buffer.node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  walk_state->operands[0]);
		obj_desc->buffer.aml_start = op->named.data;
		obj_desc->buffer.aml_length = op->named.length;
		break;

	case ACPI_TYPE_PACKAGE:
		/*
		 * Defer evaluation of Package term_arg operand and all
		 * package elements. (01/2017): We defer the element
		 * resolution to allow forward references from the package
		 * in order to provide compatibility with other ACPI
		 * implementations.
		 */
		obj_desc->package.node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  walk_state->operands[0]);

		if (!op->named.data) {
			return_ACPI_STATUS(AE_OK);
		}

		obj_desc->package.aml_start = op->named.data;
		obj_desc->package.aml_length = op->named.length;
		break;

	case ACPI_TYPE_INTEGER:

		switch (op_info->type) {
		case AML_TYPE_CONSTANT:
			/*
			 * Resolve AML Constants here - AND ONLY HERE!
			 * All constants are integers.
			 * We mark the integer with a flag that indicates that it started
			 * life as a constant -- so that stores to constants will perform
			 * as expected (noop). zero_op is used as a placeholder for optional
			 * target operands.
			 */
			obj_desc->common.flags = AOPOBJ_AML_CONSTANT;

			switch (opcode) {
			case AML_ZERO_OP:

				obj_desc->integer.value = 0;
				break;

			case AML_ONE_OP:

				obj_desc->integer.value = 1;
				break;

			case AML_ONES_OP:

				obj_desc->integer.value = ACPI_UINT64_MAX;

				/* Truncate value if we are executing from a 32-bit ACPI table */

				(void)acpi_ex_truncate_for32bit_table(obj_desc);
				break;

			case AML_REVISION_OP:

				obj_desc->integer.value = ACPI_CA_VERSION;
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Unknown constant opcode 0x%X",
					    opcode));
				status = AE_AML_OPERAND_TYPE;
				break;
			}
			break;

		case AML_TYPE_LITERAL:

			obj_desc->integer.value = op->common.value.integer;

			if (acpi_ex_truncate_for32bit_table(obj_desc)) {

				/* Warn if we found a 64-bit constant in a 32-bit table */

				ACPI_WARNING((AE_INFO,
					      "Truncated 64-bit constant found in 32-bit table: %8.8X%8.8X => %8.8X",
					      ACPI_FORMAT_UINT64(op->common.
								 value.integer),
					      (u32)obj_desc->integer.value));
			}
			break;

		default:

			ACPI_ERROR((AE_INFO, "Unknown Integer type 0x%X",
				    op_info->type));
			status = AE_AML_OPERAND_TYPE;
			break;
		}
		break;

	case ACPI_TYPE_STRING:

		obj_desc->string.pointer = op->common.value.string;
		obj_desc->string.length = (u32)strlen(op->common.value.string);

		/*
		 * The string is contained in the ACPI table, don't ever try
		 * to delete it
		 */
		obj_desc->common.flags |= AOPOBJ_STATIC_POINTER;
		break;

	case ACPI_TYPE_METHOD:
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		switch (op_info->type) {
		case AML_TYPE_LOCAL_VARIABLE:

			/* Local ID (0-7) is (AML opcode - base AML_FIRST_LOCAL_OP) */

			obj_desc->reference.value =
			    ((u32)opcode) - AML_FIRST_LOCAL_OP;
			obj_desc->reference.class = ACPI_REFCLASS_LOCAL;

			status =
			    acpi_ds_method_data_get_node(ACPI_REFCLASS_LOCAL,
							 obj_desc->reference.
							 value, walk_state,
							 ACPI_CAST_INDIRECT_PTR
							 (struct
							  acpi_namespace_node,
							  &obj_desc->reference.
							  object));
			break;

		case AML_TYPE_METHOD_ARGUMENT:

			/* Arg ID (0-6) is (AML opcode - base AML_FIRST_ARG_OP) */

			obj_desc->reference.value =
			    ((u32)opcode) - AML_FIRST_ARG_OP;
			obj_desc->reference.class = ACPI_REFCLASS_ARG;

			status = acpi_ds_method_data_get_node(ACPI_REFCLASS_ARG,
							      obj_desc->
							      reference.value,
							      walk_state,
							      ACPI_CAST_INDIRECT_PTR
							      (struct
							       acpi_namespace_node,
							       &obj_desc->
							       reference.
							       object));
			break;

		default:	/* Object name or Debug object */

			switch (op->common.aml_opcode) {
			case AML_INT_NAMEPATH_OP:

				/* Node was saved in Op */

				obj_desc->reference.node = op->common.node;
				obj_desc->reference.class = ACPI_REFCLASS_NAME;
				if (op->common.node) {
					obj_desc->reference.object =
					    op->common.node->object;
				}
				break;

			case AML_DEBUG_OP:

				obj_desc->reference.class = ACPI_REFCLASS_DEBUG;
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Unimplemented reference type for AML opcode: 0x%4.4X",
					    opcode));
				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			break;
		}
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unimplemented data type: 0x%X",
			    obj_desc->common.type));

		status = AE_AML_OPERAND_TYPE;
		break;
	}

	return_ACPI_STATUS(status);
}
