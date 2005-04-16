/******************************************************************************
 *
 * Module Name: dsobject - Dispatcher object management routines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
#include <acpi/acparser.h>
#include <acpi/amlcode.h>
#include <acpi/acdispat.h>
#include <acpi/acnamesp.h>
#include <acpi/acinterp.h>

#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dsobject")


#ifndef ACPI_NO_METHOD_EXECUTION
/*****************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_object
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op object to the equivalent namespace object
 *              Simple objects are any objects other than a package object!
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_object (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op,
	union acpi_operand_object       **obj_desc_ptr)
{
	union acpi_operand_object       *obj_desc;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ds_build_internal_object");


	*obj_desc_ptr = NULL;
	if (op->common.aml_opcode == AML_INT_NAMEPATH_OP) {
		/*
		 * This is an named object reference.  If this name was
		 * previously looked up in the namespace, it was stored in this op.
		 * Otherwise, go ahead and look it up now
		 */
		if (!op->common.node) {
			status = acpi_ns_lookup (walk_state->scope_info, op->common.value.string,
					  ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
					  ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE, NULL,
					  (struct acpi_namespace_node **) &(op->common.node));

			if (ACPI_FAILURE (status)) {
				ACPI_REPORT_NSERROR (op->common.value.string, status);
				return_ACPI_STATUS (status);
			}
		}
	}

	/* Create and init the internal ACPI object */

	obj_desc = acpi_ut_create_internal_object ((acpi_ps_get_opcode_info (op->common.aml_opcode))->object_type);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_object_from_op (walk_state, op, op->common.aml_opcode, &obj_desc);
	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (obj_desc);
		return_ACPI_STATUS (status);
	}

	*obj_desc_ptr = obj_desc;
	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_buffer_obj
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              buffer_length   - Length of the buffer
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_buffer_obj (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op,
	u32                             buffer_length,
	union acpi_operand_object       **obj_desc_ptr)
{
	union acpi_parse_object         *arg;
	union acpi_operand_object       *obj_desc;
	union acpi_parse_object         *byte_list;
	u32                             byte_list_length = 0;


	ACPI_FUNCTION_TRACE ("ds_build_internal_buffer_obj");


	obj_desc = *obj_desc_ptr;
	if (obj_desc) {
		/*
		 * We are evaluating a Named buffer object "Name (xxxx, Buffer)".
		 * The buffer object already exists (from the NS node)
		 */
	}
	else {
		/* Create a new buffer object */

		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}
	}

	/*
	 * Second arg is the buffer data (optional) byte_list can be either
	 * individual bytes or a string initializer.  In either case, a
	 * byte_list appears in the AML.
	 */
	arg = op->common.value.arg;         /* skip first arg */

	byte_list = arg->named.next;
	if (byte_list) {
		if (byte_list->common.aml_opcode != AML_INT_BYTELIST_OP) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Expecting bytelist, got AML opcode %X in op %p\n",
				byte_list->common.aml_opcode, byte_list));

			acpi_ut_remove_reference (obj_desc);
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
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
			"Buffer defined with zero length in AML, creating\n"));
	}
	else {
		obj_desc->buffer.pointer = ACPI_MEM_CALLOCATE (
				   obj_desc->buffer.length);
		if (!obj_desc->buffer.pointer) {
			acpi_ut_delete_object_desc (obj_desc);
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Initialize buffer from the byte_list (if present) */

		if (byte_list) {
			ACPI_MEMCPY (obj_desc->buffer.pointer, byte_list->named.data,
					  byte_list_length);
		}
	}

	obj_desc->buffer.flags |= AOPOBJ_DATA_VALID;
	op->common.node = (struct acpi_namespace_node *) obj_desc;
	return_ACPI_STATUS (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_package_obj
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              package_length  - Number of elements in the package
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 ****************************************************************************/

acpi_status
acpi_ds_build_internal_package_obj (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op,
	u32                             package_length,
	union acpi_operand_object       **obj_desc_ptr)
{
	union acpi_parse_object         *arg;
	union acpi_parse_object         *parent;
	union acpi_operand_object       *obj_desc = NULL;
	u32                             package_list_length;
	acpi_status                     status = AE_OK;
	u32                             i;


	ACPI_FUNCTION_TRACE ("ds_build_internal_package_obj");


	/* Find the parent of a possibly nested package */

	parent = op->common.parent;
	while ((parent->common.aml_opcode == AML_PACKAGE_OP)    ||
		   (parent->common.aml_opcode == AML_VAR_PACKAGE_OP)) {
		parent = parent->common.parent;
	}

	obj_desc = *obj_desc_ptr;
	if (obj_desc) {
		/*
		 * We are evaluating a Named package object "Name (xxxx, Package)".
		 * Get the existing package object from the NS node
		 */
	}
	else {
		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_PACKAGE);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		obj_desc->package.node = parent->common.node;
	}

	obj_desc->package.count = package_length;

	/* Count the number of items in the package list */

	package_list_length = 0;
	arg = op->common.value.arg;
	arg = arg->common.next;
	while (arg) {
		package_list_length++;
		arg = arg->common.next;
	}

	/*
	 * The package length (number of elements) will be the greater
	 * of the specified length and the length of the initializer list
	 */
	if (package_list_length > package_length) {
		obj_desc->package.count = package_list_length;
	}

	/*
	 * Allocate the pointer array (array of pointers to the
	 * individual objects). Add an extra pointer slot so
	 * that the list is always null terminated.
	 */
	obj_desc->package.elements = ACPI_MEM_CALLOCATE (
			 ((acpi_size) obj_desc->package.count + 1) * sizeof (void *));

	if (!obj_desc->package.elements) {
		acpi_ut_delete_object_desc (obj_desc);
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * Now init the elements of the package
	 */
	i = 0;
	arg = op->common.value.arg;
	arg = arg->common.next;
	while (arg) {
		if (arg->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
			/* Object (package or buffer) is already built */

			obj_desc->package.elements[i] = ACPI_CAST_PTR (union acpi_operand_object, arg->common.node);
		}
		else {
			status = acpi_ds_build_internal_object (walk_state, arg,
					  &obj_desc->package.elements[i]);
		}

		i++;
		arg = arg->common.next;
	}

	obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	op->common.node = (struct acpi_namespace_node *) obj_desc;
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ds_create_node
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              Node            - NS Node to be initialized
 *              Op              - Parser object to be translated
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the object to be associated with a namespace node
 *
 ****************************************************************************/

acpi_status
acpi_ds_create_node (
	struct acpi_walk_state          *walk_state,
	struct acpi_namespace_node      *node,
	union acpi_parse_object         *op)
{
	acpi_status                     status;
	union acpi_operand_object       *obj_desc;


	ACPI_FUNCTION_TRACE_PTR ("ds_create_node", op);


	/*
	 * Because of the execution pass through the non-control-method
	 * parts of the table, we can arrive here twice.  Only init
	 * the named object node the first time through
	 */
	if (acpi_ns_get_attached_object (node)) {
		return_ACPI_STATUS (AE_OK);
	}

	if (!op->common.value.arg) {
		/* No arguments, there is nothing to do */

		return_ACPI_STATUS (AE_OK);
	}

	/* Build an internal object for the argument(s) */

	status = acpi_ds_build_internal_object (walk_state, op->common.value.arg, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Re-type the object according to its argument */

	node->type = ACPI_GET_OBJECT_TYPE (obj_desc);

	/* Attach obj to node */

	status = acpi_ns_attach_object (node, obj_desc, node->type);

	/* Remove local reference to the object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}

#endif /* ACPI_NO_METHOD_EXECUTION */


/*****************************************************************************
 *
 * FUNCTION:    acpi_ds_init_object_from_op
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              Op              - Parser op used to init the internal object
 *              Opcode          - AML opcode associated with the object
 *              ret_obj_desc    - Namespace object to be initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize a namespace object from a parser Op and its
 *              associated arguments.  The namespace object is a more compact
 *              representation of the Op and its arguments.
 *
 ****************************************************************************/

acpi_status
acpi_ds_init_object_from_op (
	struct acpi_walk_state          *walk_state,
	union acpi_parse_object         *op,
	u16                             opcode,
	union acpi_operand_object       **ret_obj_desc)
{
	const struct acpi_opcode_info   *op_info;
	union acpi_operand_object       *obj_desc;
	acpi_status                     status = AE_OK;


	ACPI_FUNCTION_TRACE ("ds_init_object_from_op");


	obj_desc = *ret_obj_desc;
	op_info = acpi_ps_get_opcode_info (opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		/* Unknown opcode */

		return_ACPI_STATUS (AE_TYPE);
	}

	/* Perform per-object initialization */

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_BUFFER:

		/*
		 * Defer evaluation of Buffer term_arg operand
		 */
		obj_desc->buffer.node     = (struct acpi_namespace_node *) walk_state->operands[0];
		obj_desc->buffer.aml_start = op->named.data;
		obj_desc->buffer.aml_length = op->named.length;
		break;


	case ACPI_TYPE_PACKAGE:

		/*
		 * Defer evaluation of Package term_arg operand
		 */
		obj_desc->package.node     = (struct acpi_namespace_node *) walk_state->operands[0];
		obj_desc->package.aml_start = op->named.data;
		obj_desc->package.aml_length = op->named.length;
		break;


	case ACPI_TYPE_INTEGER:

		switch (op_info->type) {
		case AML_TYPE_CONSTANT:
			/*
			 * Resolve AML Constants here - AND ONLY HERE!
			 * All constants are integers.
			 * We mark the integer with a flag that indicates that it started life
			 * as a constant -- so that stores to constants will perform as expected (noop).
			 * (zero_op is used as a placeholder for optional target operands.)
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

				obj_desc->integer.value = ACPI_INTEGER_MAX;

				/* Truncate value if we are executing from a 32-bit ACPI table */

#ifndef ACPI_NO_METHOD_EXECUTION
				acpi_ex_truncate_for32bit_table (obj_desc);
#endif
				break;

			case AML_REVISION_OP:

				obj_desc->integer.value = ACPI_CA_VERSION;
				break;

			default:

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown constant opcode %X\n", opcode));
				status = AE_AML_OPERAND_TYPE;
				break;
			}
			break;


		case AML_TYPE_LITERAL:

			obj_desc->integer.value = op->common.value.integer;
			break;


		default:
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Integer type %X\n", op_info->type));
			status = AE_AML_OPERAND_TYPE;
			break;
		}
		break;


	case ACPI_TYPE_STRING:

		obj_desc->string.pointer = op->common.value.string;
		obj_desc->string.length = (u32) ACPI_STRLEN (op->common.value.string);

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

			/* Split the opcode into a base opcode + offset */

			obj_desc->reference.opcode = AML_LOCAL_OP;
			obj_desc->reference.offset = opcode - AML_LOCAL_OP;

#ifndef ACPI_NO_METHOD_EXECUTION
			status = acpi_ds_method_data_get_node (AML_LOCAL_OP, obj_desc->reference.offset,
					 walk_state, (struct acpi_namespace_node **) &obj_desc->reference.object);
#endif
			break;


		case AML_TYPE_METHOD_ARGUMENT:

			/* Split the opcode into a base opcode + offset */

			obj_desc->reference.opcode = AML_ARG_OP;
			obj_desc->reference.offset = opcode - AML_ARG_OP;

#ifndef ACPI_NO_METHOD_EXECUTION
			status = acpi_ds_method_data_get_node (AML_ARG_OP, obj_desc->reference.offset,
					 walk_state, (struct acpi_namespace_node **) &obj_desc->reference.object);
#endif
			break;

		default: /* Other literals, etc.. */

			if (op->common.aml_opcode == AML_INT_NAMEPATH_OP) {
				/* Node was saved in Op */

				obj_desc->reference.node = op->common.node;
			}

			obj_desc->reference.opcode = opcode;
			break;
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unimplemented data type: %X\n",
			ACPI_GET_OBJECT_TYPE (obj_desc)));

		status = AE_AML_OPERAND_TYPE;
		break;
	}

	return_ACPI_STATUS (status);
}


