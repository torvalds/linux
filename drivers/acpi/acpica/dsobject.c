/******************************************************************************
 *
 * Module Name: dsobject - Dispatcher object management routines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
#include "accommon.h"
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dsobject")

/* Local prototypes */
static acpi_status
acpi_ds_build_internal_object(struct acpi_walk_state *walk_state,
			      union acpi_parse_object *op,
			      union acpi_operand_object **obj_desc_ptr);

#ifndef ACPI_NO_METHOD_EXECUTION
/*******************************************************************************
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
 ******************************************************************************/

static acpi_status
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
		 * previously looked up in the namespace, it was stored in this op.
		 * Otherwise, go ahead and look it up now
		 */
		if (!op->common.node) {
			status = acpi_ns_lookup(walk_state->scope_info,
						op->common.value.string,
						ACPI_TYPE_ANY,
						ACPI_IMODE_EXECUTE,
						ACPI_NS_SEARCH_PARENT |
						ACPI_NS_DONT_OPEN_SCOPE, NULL,
						ACPI_CAST_INDIRECT_PTR(struct
								       acpi_namespace_node,
								       &(op->
									 common.
									 node)));
			if (ACPI_FAILURE(status)) {

				/* Check if we are resolving a named reference within a package */

				if ((status == AE_NOT_FOUND)
				    && (acpi_gbl_enable_interpreter_slack)
				    &&
				    ((op->common.parent->common.aml_opcode ==
				      AML_PACKAGE_OP)
				     || (op->common.parent->common.aml_opcode ==
					 AML_VAR_PACKAGE_OP))) {
					/*
					 * We didn't find the target and we are populating elements
					 * of a package - ignore if slack enabled. Some ASL code
					 * contains dangling invalid references in packages and
					 * expects that no exception will be issued. Leave the
					 * element as a null element. It cannot be used, but it
					 * can be overwritten by subsequent ASL code - this is
					 * typically the case.
					 */
					ACPI_DEBUG_PRINT((ACPI_DB_INFO,
							  "Ignoring unresolved reference in package [%4.4s]\n",
							  walk_state->
							  scope_info->scope.
							  node->name.ascii));

					return_ACPI_STATUS(AE_OK);
				} else {
					ACPI_ERROR_NAMESPACE(op->common.value.
							     string, status);
				}

				return_ACPI_STATUS(status);
			}
		}

		/* Special object resolution for elements of a package */

		if ((op->common.parent->common.aml_opcode == AML_PACKAGE_OP) ||
		    (op->common.parent->common.aml_opcode ==
		     AML_VAR_PACKAGE_OP)) {
			/*
			 * Attempt to resolve the node to a value before we insert it into
			 * the package. If this is a reference to a common data type,
			 * resolve it immediately. According to the ACPI spec, package
			 * elements can only be "data objects" or method references.
			 * Attempt to resolve to an Integer, Buffer, String or Package.
			 * If cannot, return the named reference (for things like Devices,
			 * Methods, etc.) Buffer Fields and Fields will resolve to simple
			 * objects (int/buf/str/pkg).
			 *
			 * NOTE: References to things like Devices, Methods, Mutexes, etc.
			 * will remain as named references. This behavior is not described
			 * in the ACPI spec, but it appears to be an oversight.
			 */
			obj_desc =
			    ACPI_CAST_PTR(union acpi_operand_object,
					  op->common.node);

			status =
			    acpi_ex_resolve_node_to_value(ACPI_CAST_INDIRECT_PTR
							  (struct
							   acpi_namespace_node,
							   &obj_desc),
							  walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			switch (op->common.node->type) {
				/*
				 * For these types, we need the actual node, not the subobject.
				 * However, the subobject did not get an extra reference count above.
				 *
				 * TBD: should ex_resolve_node_to_value be changed to fix this?
				 */
			case ACPI_TYPE_DEVICE:
			case ACPI_TYPE_THERMAL:

				acpi_ut_add_reference(op->common.node->object);

				/*lint -fallthrough */
				/*
				 * For these types, we need the actual node, not the subobject.
				 * The subobject got an extra reference count in ex_resolve_node_to_value.
				 */
			case ACPI_TYPE_MUTEX:
			case ACPI_TYPE_METHOD:
			case ACPI_TYPE_POWER:
			case ACPI_TYPE_PROCESSOR:
			case ACPI_TYPE_EVENT:
			case ACPI_TYPE_REGION:

				/* We will create a reference object for these types below */
				break;

			default:
				/*
				 * All other types - the node was resolved to an actual
				 * object, we are done.
				 */
				goto exit;
			}
		}
	}

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

      exit:
	*obj_desc_ptr = obj_desc;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
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
	 * individual bytes or a string initializer.  In either case, a
	 * byte_list appears in the AML.
	 */
	arg = op->common.value.arg;	/* skip first arg */

	byte_list = arg->named.next;
	if (byte_list) {
		if (byte_list->common.aml_opcode != AML_INT_BYTELIST_OP) {
			ACPI_ERROR((AE_INFO,
				    "Expecting bytelist, got AML opcode %X in op %p",
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
			ACPI_MEMCPY(obj_desc->buffer.pointer,
				    byte_list->named.data, byte_list_length);
		}
	}

	obj_desc->buffer.flags |= AOPOBJ_DATA_VALID;
	op->common.node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_desc);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_package_obj
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              Op              - Parser object to be translated
 *              element_count   - Number of elements in the package - this is
 *                                the num_elements argument to Package()
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 * NOTE: The number of elements in the package will be always be the num_elements
 * count, regardless of the number of elements in the package list. If
 * num_elements is smaller, only that many package list elements are used.
 * if num_elements is larger, the Package object is padded out with
 * objects of type Uninitialized (as per ACPI spec.)
 *
 * Even though the ASL compilers do not allow num_elements to be smaller
 * than the Package list length (for the fixed length package opcode), some
 * BIOS code modifies the AML on the fly to adjust the num_elements, and
 * this code compensates for that. This also provides compatibility with
 * other AML interpreters.
 *
 ******************************************************************************/

acpi_status
acpi_ds_build_internal_package_obj(struct acpi_walk_state *walk_state,
				   union acpi_parse_object *op,
				   u32 element_count,
				   union acpi_operand_object **obj_desc_ptr)
{
	union acpi_parse_object *arg;
	union acpi_parse_object *parent;
	union acpi_operand_object *obj_desc = NULL;
	acpi_status status = AE_OK;
	unsigned i;
	u16 index;
	u16 reference_count;

	ACPI_FUNCTION_TRACE(ds_build_internal_package_obj);

	/* Find the parent of a possibly nested package */

	parent = op->common.parent;
	while ((parent->common.aml_opcode == AML_PACKAGE_OP) ||
	       (parent->common.aml_opcode == AML_VAR_PACKAGE_OP)) {
		parent = parent->common.parent;
	}

	/*
	 * If we are evaluating a Named package object "Name (xxxx, Package)",
	 * the package object already exists, otherwise it must be created.
	 */
	obj_desc = *obj_desc_ptr;
	if (!obj_desc) {
		obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_PACKAGE);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		obj_desc->package.node = parent->common.node;
	}

	/*
	 * Allocate the element array (array of pointers to the individual
	 * objects) based on the num_elements parameter. Add an extra pointer slot
	 * so that the list is always null terminated.
	 */
	obj_desc->package.elements = ACPI_ALLOCATE_ZEROED(((acpi_size)
							   element_count +
							   1) * sizeof(void *));

	if (!obj_desc->package.elements) {
		acpi_ut_delete_object_desc(obj_desc);
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	obj_desc->package.count = element_count;

	/*
	 * Initialize the elements of the package, up to the num_elements count.
	 * Package is automatically padded with uninitialized (NULL) elements
	 * if num_elements is greater than the package list length. Likewise,
	 * Package is truncated if num_elements is less than the list length.
	 */
	arg = op->common.value.arg;
	arg = arg->common.next;
	for (i = 0; arg && (i < element_count); i++) {
		if (arg->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
			if (arg->common.node->type == ACPI_TYPE_METHOD) {
				/*
				 * A method reference "looks" to the parser to be a method
				 * invocation, so we special case it here
				 */
				arg->common.aml_opcode = AML_INT_NAMEPATH_OP;
				status =
				    acpi_ds_build_internal_object(walk_state,
								  arg,
								  &obj_desc->
								  package.
								  elements[i]);
			} else {
				/* This package element is already built, just get it */

				obj_desc->package.elements[i] =
				    ACPI_CAST_PTR(union acpi_operand_object,
						  arg->common.node);
			}
		} else {
			status = acpi_ds_build_internal_object(walk_state, arg,
							       &obj_desc->
							       package.
							       elements[i]);
		}

		if (*obj_desc_ptr) {

			/* Existing package, get existing reference count */

			reference_count =
			    (*obj_desc_ptr)->common.reference_count;
			if (reference_count > 1) {

				/* Make new element ref count match original ref count */

				for (index = 0; index < (reference_count - 1);
				     index++) {
					acpi_ut_add_reference((obj_desc->
							       package.
							       elements[i]));
				}
			}
		}

		arg = arg->common.next;
	}

	/* Check for match between num_elements and actual length of package_list */

	if (arg) {
		/*
		 * num_elements was exhausted, but there are remaining elements in the
		 * package_list.
		 *
		 * Note: technically, this is an error, from ACPI spec: "It is an error
		 * for NumElements to be less than the number of elements in the
		 * PackageList". However, for now, we just print an error message and
		 * no exception is returned.
		 */
		while (arg) {

			/* Find out how many elements there really are */

			i++;
			arg = arg->common.next;
		}

		ACPI_WARNING((AE_INFO,
			    "Package List length (%X) larger than NumElements count (%X), truncated\n",
			    i, element_count));
	} else if (i < element_count) {
		/*
		 * Arg list (elements) was exhausted, but we did not reach num_elements count.
		 * Note: this is not an error, the package is padded out with NULLs.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Package List length (%X) smaller than NumElements count (%X), padded with null elements\n",
				  i, element_count));
	}

	obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	op->common.node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
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
	 * parts of the table, we can arrive here twice.  Only init
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

	status = acpi_ds_build_internal_object(walk_state, op->common.value.arg,
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

#endif				/* ACPI_NO_METHOD_EXECUTION */

/*******************************************************************************
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
		 * Defer evaluation of Package term_arg operand
		 */
		obj_desc->package.node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  walk_state->operands[0]);
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

				obj_desc->integer.value = ACPI_INTEGER_MAX;

				/* Truncate value if we are executing from a 32-bit ACPI table */

#ifndef ACPI_NO_METHOD_EXECUTION
				acpi_ex_truncate_for32bit_table(obj_desc);
#endif
				break;

			case AML_REVISION_OP:

				obj_desc->integer.value = ACPI_CA_VERSION;
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Unknown constant opcode %X",
					    opcode));
				status = AE_AML_OPERAND_TYPE;
				break;
			}
			break;

		case AML_TYPE_LITERAL:

			obj_desc->integer.value = op->common.value.integer;
#ifndef ACPI_NO_METHOD_EXECUTION
			acpi_ex_truncate_for32bit_table(obj_desc);
#endif
			break;

		default:
			ACPI_ERROR((AE_INFO, "Unknown Integer type %X",
				    op_info->type));
			status = AE_AML_OPERAND_TYPE;
			break;
		}
		break;

	case ACPI_TYPE_STRING:

		obj_desc->string.pointer = op->common.value.string;
		obj_desc->string.length =
		    (u32) ACPI_STRLEN(op->common.value.string);

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

			/* Local ID (0-7) is (AML opcode - base AML_LOCAL_OP) */

			obj_desc->reference.value =
			    ((u32)opcode) - AML_LOCAL_OP;
			obj_desc->reference.class = ACPI_REFCLASS_LOCAL;

#ifndef ACPI_NO_METHOD_EXECUTION
			status =
			    acpi_ds_method_data_get_node(ACPI_REFCLASS_LOCAL,
							 obj_desc->reference.
							 value, walk_state,
							 ACPI_CAST_INDIRECT_PTR
							 (struct
							  acpi_namespace_node,
							  &obj_desc->reference.
							  object));
#endif
			break;

		case AML_TYPE_METHOD_ARGUMENT:

			/* Arg ID (0-6) is (AML opcode - base AML_ARG_OP) */

			obj_desc->reference.value = ((u32)opcode) - AML_ARG_OP;
			obj_desc->reference.class = ACPI_REFCLASS_ARG;

#ifndef ACPI_NO_METHOD_EXECUTION
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
#endif
			break;

		default:	/* Object name or Debug object */

			switch (op->common.aml_opcode) {
			case AML_INT_NAMEPATH_OP:

				/* Node was saved in Op */

				obj_desc->reference.node = op->common.node;
				obj_desc->reference.object =
				    op->common.node->object;
				obj_desc->reference.class = ACPI_REFCLASS_NAME;
				break;

			case AML_DEBUG_OP:

				obj_desc->reference.class = ACPI_REFCLASS_DEBUG;
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Unimplemented reference type for AML opcode: %4.4X",
					    opcode));
				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			break;
		}
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unimplemented data type: %X",
			    obj_desc->common.type));

		status = AE_AML_OPERAND_TYPE;
		break;
	}

	return_ACPI_STATUS(status);
}
