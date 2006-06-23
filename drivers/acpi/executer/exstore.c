
/******************************************************************************
 *
 * Module Name: exstore - AML Interpreter object store support
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
#include <acpi/acdispat.h>
#include <acpi/acinterp.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exstore")

/* Local prototypes */
static void
acpi_ex_do_debug_object(union acpi_operand_object *source_desc,
			u32 level, u32 index);

static acpi_status
acpi_ex_store_object_to_index(union acpi_operand_object *val_desc,
			      union acpi_operand_object *dest_desc,
			      struct acpi_walk_state *walk_state);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_debug_object
 *
 * PARAMETERS:  source_desc         - Value to be stored
 *              Level               - Indentation level (used for packages)
 *              Index               - Current package element, zero if not pkg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Handles stores to the Debug Object.
 *
 ******************************************************************************/

static void
acpi_ex_do_debug_object(union acpi_operand_object *source_desc,
			u32 level, u32 index)
{
	u32 i;

	ACPI_FUNCTION_TRACE_PTR(ex_do_debug_object, source_desc);

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "[ACPI Debug] %*s",
			      level, " "));

	/* Display index for package output only */

	if (index > 0) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT,
				      "(%.2u) ", index - 1));
	}

	if (!source_desc) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "<Null Object>\n"));
		return_VOID;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) == ACPI_DESC_TYPE_OPERAND) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "%s: ",
				      acpi_ut_get_object_type_name
				      (source_desc)));

		if (!acpi_ut_valid_internal_object(source_desc)) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT,
					      "%p, Invalid Internal Object!\n",
					      source_desc));
			return_VOID;
		}
	} else if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) ==
		   ACPI_DESC_TYPE_NAMED) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "%s: %p\n",
				      acpi_ut_get_type_name(((struct
							      acpi_namespace_node
							      *)source_desc)->
							    type),
				      source_desc));
		return_VOID;
	} else {
		return_VOID;
	}

	switch (ACPI_GET_OBJECT_TYPE(source_desc)) {
	case ACPI_TYPE_INTEGER:

		/* Output correct integer width */

		if (acpi_gbl_integer_byte_width == 4) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "0x%8.8X\n",
					      (u32) source_desc->integer.
					      value));
		} else {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT,
					      "0x%8.8X%8.8X\n",
					      ACPI_FORMAT_UINT64(source_desc->
								 integer.
								 value)));
		}
		break;

	case ACPI_TYPE_BUFFER:

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "[0x%.2X]\n",
				      (u32) source_desc->buffer.length));
		ACPI_DUMP_BUFFER(source_desc->buffer.pointer,
				 (source_desc->buffer.length <
				  32) ? source_desc->buffer.length : 32);
		break;

	case ACPI_TYPE_STRING:

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "[0x%.2X] \"%s\"\n",
				      source_desc->string.length,
				      source_desc->string.pointer));
		break;

	case ACPI_TYPE_PACKAGE:

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT,
				      "[0x%.2X Elements]\n",
				      source_desc->package.count));

		/* Output the entire contents of the package */

		for (i = 0; i < source_desc->package.count; i++) {
			acpi_ex_do_debug_object(source_desc->package.
						elements[i], level + 4, i + 1);
		}
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		if (source_desc->reference.opcode == AML_INDEX_OP) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT,
					      "[%s, 0x%X]\n",
					      acpi_ps_get_opcode_name
					      (source_desc->reference.opcode),
					      source_desc->reference.offset));
		} else {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "[%s]\n",
					      acpi_ps_get_opcode_name
					      (source_desc->reference.opcode)));
		}

		if (source_desc->reference.object) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.object) ==
			    ACPI_DESC_TYPE_NAMED) {
				acpi_ex_do_debug_object(((struct
							  acpi_namespace_node *)
							 source_desc->reference.
							 object)->object,
							level + 4, 0);
			} else {
				acpi_ex_do_debug_object(source_desc->reference.
							object, level + 4, 0);
			}
		} else if (source_desc->reference.node) {
			acpi_ex_do_debug_object((source_desc->reference.node)->
						object, level + 4, 0);
		}
		break;

	default:

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_DEBUG_OBJECT, "%p %s\n",
				      source_desc,
				      acpi_ut_get_object_type_name
				      (source_desc)));
		break;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EXEC, "\n"));
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store
 *
 * PARAMETERS:  *source_desc        - Value to be stored
 *              *dest_desc          - Where to store it.  Must be an NS node
 *                                    or an union acpi_operand_object of type
 *                                    Reference;
 *              walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value described by source_desc into the location
 *              described by dest_desc. Called by various interpreter
 *              functions to store the result of an operation into
 *              the destination operand -- not just simply the actual "Store"
 *              ASL operator.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store(union acpi_operand_object *source_desc,
	      union acpi_operand_object *dest_desc,
	      struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *ref_desc = dest_desc;

	ACPI_FUNCTION_TRACE_PTR(ex_store, dest_desc);

	/* Validate parameters */

	if (!source_desc || !dest_desc) {
		ACPI_ERROR((AE_INFO, "Null parameter"));
		return_ACPI_STATUS(AE_AML_NO_OPERAND);
	}

	/* dest_desc can be either a namespace node or an ACPI object */

	if (ACPI_GET_DESCRIPTOR_TYPE(dest_desc) == ACPI_DESC_TYPE_NAMED) {
		/*
		 * Dest is a namespace node,
		 * Storing an object into a Named node.
		 */
		status = acpi_ex_store_object_to_node(source_desc,
						      (struct
						       acpi_namespace_node *)
						      dest_desc, walk_state,
						      ACPI_IMPLICIT_CONVERSION);

		return_ACPI_STATUS(status);
	}

	/* Destination object must be a Reference or a Constant object */

	switch (ACPI_GET_OBJECT_TYPE(dest_desc)) {
	case ACPI_TYPE_LOCAL_REFERENCE:
		break;

	case ACPI_TYPE_INTEGER:

		/* Allow stores to Constants -- a Noop as per ACPI spec */

		if (dest_desc->common.flags & AOPOBJ_AML_CONSTANT) {
			return_ACPI_STATUS(AE_OK);
		}

		/*lint -fallthrough */

	default:

		/* Destination is not a Reference object */

		ACPI_ERROR((AE_INFO,
			    "Target is not a Reference or Constant object - %s [%p]",
			    acpi_ut_get_object_type_name(dest_desc),
			    dest_desc));

		ACPI_DUMP_STACK_ENTRY(source_desc);
		ACPI_DUMP_STACK_ENTRY(dest_desc);
		ACPI_DUMP_OPERANDS(&dest_desc, ACPI_IMODE_EXECUTE, "ExStore",
				   2,
				   "Target is not a Reference or Constant object");

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/*
	 * Examine the Reference opcode.  These cases are handled:
	 *
	 * 1) Store to Name (Change the object associated with a name)
	 * 2) Store to an indexed area of a Buffer or Package
	 * 3) Store to a Method Local or Arg
	 * 4) Store to the debug object
	 */
	switch (ref_desc->reference.opcode) {
	case AML_NAME_OP:
	case AML_REF_OF_OP:

		/* Storing an object into a Name "container" */

		status = acpi_ex_store_object_to_node(source_desc,
						      ref_desc->reference.
						      object, walk_state,
						      ACPI_IMPLICIT_CONVERSION);
		break;

	case AML_INDEX_OP:

		/* Storing to an Index (pointer into a packager or buffer) */

		status =
		    acpi_ex_store_object_to_index(source_desc, ref_desc,
						  walk_state);
		break;

	case AML_LOCAL_OP:
	case AML_ARG_OP:

		/* Store to a method local/arg  */

		status =
		    acpi_ds_store_object_to_local(ref_desc->reference.opcode,
						  ref_desc->reference.offset,
						  source_desc, walk_state);
		break;

	case AML_DEBUG_OP:

		/*
		 * Storing to the Debug object causes the value stored to be
		 * displayed and otherwise has no effect -- see ACPI Specification
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "**** Write to Debug Object: Object %p %s ****:\n\n",
				  source_desc,
				  acpi_ut_get_object_type_name(source_desc)));

		acpi_ex_do_debug_object(source_desc, 0, 0);
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unknown Reference opcode %X",
			    ref_desc->reference.opcode));
		ACPI_DUMP_ENTRY(ref_desc, ACPI_LV_ERROR);

		status = AE_AML_INTERNAL;
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_object_to_index
 *
 * PARAMETERS:  *source_desc            - Value to be stored
 *              *dest_desc              - Named object to receive the value
 *              walk_state              - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to indexed Buffer or Package element
 *
 ******************************************************************************/

static acpi_status
acpi_ex_store_object_to_index(union acpi_operand_object *source_desc,
			      union acpi_operand_object *index_desc,
			      struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *new_desc;
	u8 value = 0;
	u32 i;

	ACPI_FUNCTION_TRACE(ex_store_object_to_index);

	/*
	 * Destination must be a reference pointer, and
	 * must point to either a buffer or a package
	 */
	switch (index_desc->reference.target_type) {
	case ACPI_TYPE_PACKAGE:
		/*
		 * Storing to a package element. Copy the object and replace
		 * any existing object with the new object. No implicit
		 * conversion is performed.
		 *
		 * The object at *(index_desc->Reference.Where) is the
		 * element within the package that is to be modified.
		 * The parent package object is at index_desc->Reference.Object
		 */
		obj_desc = *(index_desc->reference.where);

		status =
		    acpi_ut_copy_iobject_to_iobject(source_desc, &new_desc,
						    walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		if (obj_desc) {

			/* Decrement reference count by the ref count of the parent package */

			for (i = 0; i < ((union acpi_operand_object *)
					 index_desc->reference.object)->common.
			     reference_count; i++) {
				acpi_ut_remove_reference(obj_desc);
			}
		}

		*(index_desc->reference.where) = new_desc;

		/* Increment ref count by the ref count of the parent package-1 */

		for (i = 1; i < ((union acpi_operand_object *)
				 index_desc->reference.object)->common.
		     reference_count; i++) {
			acpi_ut_add_reference(new_desc);
		}

		break;

	case ACPI_TYPE_BUFFER_FIELD:

		/*
		 * Store into a Buffer or String (not actually a real buffer_field)
		 * at a location defined by an Index.
		 *
		 * The first 8-bit element of the source object is written to the
		 * 8-bit Buffer location defined by the Index destination object,
		 * according to the ACPI 2.0 specification.
		 */

		/*
		 * Make sure the target is a Buffer or String. An error should
		 * not happen here, since the reference_object was constructed
		 * by the INDEX_OP code.
		 */
		obj_desc = index_desc->reference.object;
		if ((ACPI_GET_OBJECT_TYPE(obj_desc) != ACPI_TYPE_BUFFER) &&
		    (ACPI_GET_OBJECT_TYPE(obj_desc) != ACPI_TYPE_STRING)) {
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/*
		 * The assignment of the individual elements will be slightly
		 * different for each source type.
		 */
		switch (ACPI_GET_OBJECT_TYPE(source_desc)) {
		case ACPI_TYPE_INTEGER:

			/* Use the least-significant byte of the integer */

			value = (u8) (source_desc->integer.value);
			break;

		case ACPI_TYPE_BUFFER:
		case ACPI_TYPE_STRING:

			/* Note: Takes advantage of common string/buffer fields */

			value = source_desc->buffer.pointer[0];
			break;

		default:

			/* All other types are invalid */

			ACPI_ERROR((AE_INFO,
				    "Source must be Integer/Buffer/String type, not %s",
				    acpi_ut_get_object_type_name(source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Store the source value into the target buffer byte */

		obj_desc->buffer.pointer[index_desc->reference.offset] = value;
		break;

	default:
		ACPI_ERROR((AE_INFO, "Target is not a Package or BufferField"));
		status = AE_AML_OPERAND_TYPE;
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_object_to_node
 *
 * PARAMETERS:  source_desc             - Value to be stored
 *              Node                    - Named object to receive the value
 *              walk_state              - Current walk state
 *              implicit_conversion     - Perform implicit conversion (yes/no)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 *              The Assignment of an object to a named object is handled here
 *              The value passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              Assumes parameters are already validated.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_node(union acpi_operand_object *source_desc,
			     struct acpi_namespace_node *node,
			     struct acpi_walk_state *walk_state,
			     u8 implicit_conversion)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *target_desc;
	union acpi_operand_object *new_desc;
	acpi_object_type target_type;

	ACPI_FUNCTION_TRACE_PTR(ex_store_object_to_node, source_desc);

	/* Get current type of the node, and object attached to Node */

	target_type = acpi_ns_get_type(node);
	target_desc = acpi_ns_get_attached_object(node);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Storing %p(%s) into node %p(%s)\n",
			  source_desc,
			  acpi_ut_get_object_type_name(source_desc), node,
			  acpi_ut_get_type_name(target_type)));

	/*
	 * Resolve the source object to an actual value
	 * (If it is a reference object)
	 */
	status = acpi_ex_resolve_object(&source_desc, target_type, walk_state);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* If no implicit conversion, drop into the default case below */

	if ((!implicit_conversion) || (walk_state->opcode == AML_COPY_OP)) {

		/* Force execution of default (no implicit conversion) */

		target_type = ACPI_TYPE_ANY;
	}

	/* Do the actual store operation */

	switch (target_type) {
	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		/* For fields, copy the source data to the target field. */

		status = acpi_ex_write_data_to_field(source_desc, target_desc,
						     &walk_state->result_obj);
		break;

	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:

		/*
		 * These target types are all of type Integer/String/Buffer, and
		 * therefore support implicit conversion before the store.
		 *
		 * Copy and/or convert the source object to a new target object
		 */
		status =
		    acpi_ex_store_object_to_object(source_desc, target_desc,
						   &new_desc, walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		if (new_desc != target_desc) {
			/*
			 * Store the new new_desc as the new value of the Name, and set
			 * the Name's type to that of the value being stored in it.
			 * source_desc reference count is incremented by attach_object.
			 *
			 * Note: This may change the type of the node if an explicit store
			 * has been performed such that the node/object type has been
			 * changed.
			 */
			status =
			    acpi_ns_attach_object(node, new_desc,
						  new_desc->common.type);

			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "Store %s into %s via Convert/Attach\n",
					  acpi_ut_get_object_type_name
					  (source_desc),
					  acpi_ut_get_object_type_name
					  (new_desc)));
		}
		break;

	default:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Storing %s (%p) directly into node (%p) with no implicit conversion\n",
				  acpi_ut_get_object_type_name(source_desc),
				  source_desc, node));

		/* No conversions for all other types.  Just attach the source object */

		status = acpi_ns_attach_object(node, source_desc,
					       ACPI_GET_OBJECT_TYPE
					       (source_desc));
		break;
	}

	return_ACPI_STATUS(status);
}
