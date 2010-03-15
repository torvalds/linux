
/******************************************************************************
 *
 * Module Name: exresop - AML Interpreter operand/object resolution
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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
#include "amlcode.h"
#include "acparser.h"
#include "acinterp.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exresop")

/* Local prototypes */
static acpi_status
acpi_ex_check_object_type(acpi_object_type type_needed,
			  acpi_object_type this_type, void *object);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_check_object_type
 *
 * PARAMETERS:  type_needed         Object type needed
 *              this_type           Actual object type
 *              Object              Object pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check required type against actual type
 *
 ******************************************************************************/

static acpi_status
acpi_ex_check_object_type(acpi_object_type type_needed,
			  acpi_object_type this_type, void *object)
{
	ACPI_FUNCTION_ENTRY();

	if (type_needed == ACPI_TYPE_ANY) {

		/* All types OK, so we don't perform any typechecks */

		return (AE_OK);
	}

	if (type_needed == ACPI_TYPE_LOCAL_REFERENCE) {
		/*
		 * Allow the AML "Constant" opcodes (Zero, One, etc.) to be reference
		 * objects and thus allow them to be targets.  (As per the ACPI
		 * specification, a store to a constant is a noop.)
		 */
		if ((this_type == ACPI_TYPE_INTEGER) &&
		    (((union acpi_operand_object *)object)->common.
		     flags & AOPOBJ_AML_CONSTANT)) {
			return (AE_OK);
		}
	}

	if (type_needed != this_type) {
		ACPI_ERROR((AE_INFO,
			    "Needed type [%s], found [%s] %p",
			    acpi_ut_get_type_name(type_needed),
			    acpi_ut_get_type_name(this_type), object));

		return (AE_AML_OPERAND_TYPE);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_resolve_operands
 *
 * PARAMETERS:  Opcode              - Opcode being interpreted
 *              stack_ptr           - Pointer to the operand stack to be
 *                                    resolved
 *              walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert multiple input operands to the types required by the
 *              target operator.
 *
 *      Each 5-bit group in arg_types represents one required
 *      operand and indicates the required Type. The corresponding operand
 *      will be converted to the required type if possible, otherwise we
 *      abort with an exception.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_operands(u16 opcode,
			 union acpi_operand_object ** stack_ptr,
			 struct acpi_walk_state * walk_state)
{
	union acpi_operand_object *obj_desc;
	acpi_status status = AE_OK;
	u8 object_type;
	u32 arg_types;
	const struct acpi_opcode_info *op_info;
	u32 this_arg_type;
	acpi_object_type type_needed;
	u16 target_op = 0;

	ACPI_FUNCTION_TRACE_U32(ex_resolve_operands, opcode);

	op_info = acpi_ps_get_opcode_info(opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		return_ACPI_STATUS(AE_AML_BAD_OPCODE);
	}

	arg_types = op_info->runtime_args;
	if (arg_types == ARGI_INVALID_OPCODE) {
		ACPI_ERROR((AE_INFO, "Unknown AML opcode %X", opcode));

		return_ACPI_STATUS(AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Opcode %X [%s] RequiredOperandTypes=%8.8X\n",
			  opcode, op_info->name, arg_types));

	/*
	 * Normal exit is with (arg_types == 0) at end of argument list.
	 * Function will return an exception from within the loop upon
	 * finding an entry which is not (or cannot be converted
	 * to) the required type; if stack underflows; or upon
	 * finding a NULL stack entry (which should not happen).
	 */
	while (GET_CURRENT_ARG_TYPE(arg_types)) {
		if (!stack_ptr || !*stack_ptr) {
			ACPI_ERROR((AE_INFO, "Null stack entry at %p",
				    stack_ptr));

			return_ACPI_STATUS(AE_AML_INTERNAL);
		}

		/* Extract useful items */

		obj_desc = *stack_ptr;

		/* Decode the descriptor type */

		switch (ACPI_GET_DESCRIPTOR_TYPE(obj_desc)) {
		case ACPI_DESC_TYPE_NAMED:

			/* Namespace Node */

			object_type =
			    ((struct acpi_namespace_node *)obj_desc)->type;

			/*
			 * Resolve an alias object. The construction of these objects
			 * guarantees that there is only one level of alias indirection;
			 * thus, the attached object is always the aliased namespace node
			 */
			if (object_type == ACPI_TYPE_LOCAL_ALIAS) {
				obj_desc =
				    acpi_ns_get_attached_object((struct
								 acpi_namespace_node
								 *)obj_desc);
				*stack_ptr = obj_desc;
				object_type =
				    ((struct acpi_namespace_node *)obj_desc)->
				    type;
			}
			break;

		case ACPI_DESC_TYPE_OPERAND:

			/* ACPI internal object */

			object_type = obj_desc->common.type;

			/* Check for bad acpi_object_type */

			if (!acpi_ut_valid_object_type(object_type)) {
				ACPI_ERROR((AE_INFO,
					    "Bad operand object type [%X]",
					    object_type));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}

			if (object_type == (u8) ACPI_TYPE_LOCAL_REFERENCE) {

				/* Validate the Reference */

				switch (obj_desc->reference.class) {
				case ACPI_REFCLASS_DEBUG:

					target_op = AML_DEBUG_OP;

					/*lint -fallthrough */

				case ACPI_REFCLASS_ARG:
				case ACPI_REFCLASS_LOCAL:
				case ACPI_REFCLASS_INDEX:
				case ACPI_REFCLASS_REFOF:
				case ACPI_REFCLASS_TABLE:	/* ddb_handle from LOAD_OP or LOAD_TABLE_OP */
				case ACPI_REFCLASS_NAME:	/* Reference to a named object */

					ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
							  "Operand is a Reference, Class [%s] %2.2X\n",
							  acpi_ut_get_reference_name
							  (obj_desc),
							  obj_desc->reference.
							  class));
					break;

				default:

					ACPI_ERROR((AE_INFO,
						    "Unknown Reference Class %2.2X in %p",
						    obj_desc->reference.class,
						    obj_desc));

					return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
				}
			}
			break;

		default:

			/* Invalid descriptor */

			ACPI_ERROR((AE_INFO, "Invalid descriptor %p [%s]",
				    obj_desc,
				    acpi_ut_get_descriptor_name(obj_desc)));

			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Get one argument type, point to the next */

		this_arg_type = GET_CURRENT_ARG_TYPE(arg_types);
		INCREMENT_ARG_LIST(arg_types);

		/*
		 * Handle cases where the object does not need to be
		 * resolved to a value
		 */
		switch (this_arg_type) {
		case ARGI_REF_OR_STRING:	/* Can be a String or Reference */

			if ((ACPI_GET_DESCRIPTOR_TYPE(obj_desc) ==
			     ACPI_DESC_TYPE_OPERAND)
			    && (obj_desc->common.type == ACPI_TYPE_STRING)) {
				/*
				 * String found - the string references a named object and
				 * must be resolved to a node
				 */
				goto next_operand;
			}

			/*
			 * Else not a string - fall through to the normal Reference
			 * case below
			 */
			/*lint -fallthrough */

		case ARGI_REFERENCE:	/* References: */
		case ARGI_INTEGER_REF:
		case ARGI_OBJECT_REF:
		case ARGI_DEVICE_REF:
		case ARGI_TARGETREF:	/* Allows implicit conversion rules before store */
		case ARGI_FIXED_TARGET:	/* No implicit conversion before store to target */
		case ARGI_SIMPLE_TARGET:	/* Name, Local, or Arg - no implicit conversion  */

			/*
			 * Need an operand of type ACPI_TYPE_LOCAL_REFERENCE
			 * A Namespace Node is OK as-is
			 */
			if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) ==
			    ACPI_DESC_TYPE_NAMED) {
				goto next_operand;
			}

			status =
			    acpi_ex_check_object_type(ACPI_TYPE_LOCAL_REFERENCE,
						      object_type, obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
			goto next_operand;

		case ARGI_DATAREFOBJ:	/* Store operator only */

			/*
			 * We don't want to resolve index_op reference objects during
			 * a store because this would be an implicit de_ref_of operation.
			 * Instead, we just want to store the reference object.
			 * -- All others must be resolved below.
			 */
			if ((opcode == AML_STORE_OP) &&
			    ((*stack_ptr)->common.type ==
			     ACPI_TYPE_LOCAL_REFERENCE)
			    && ((*stack_ptr)->reference.class == ACPI_REFCLASS_INDEX)) {
				goto next_operand;
			}
			break;

		default:
			/* All cases covered above */
			break;
		}

		/*
		 * Resolve this object to a value
		 */
		status = acpi_ex_resolve_to_value(stack_ptr, walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Get the resolved object */

		obj_desc = *stack_ptr;

		/*
		 * Check the resulting object (value) type
		 */
		switch (this_arg_type) {
			/*
			 * For the simple cases, only one type of resolved object
			 * is allowed
			 */
		case ARGI_MUTEX:

			/* Need an operand of type ACPI_TYPE_MUTEX */

			type_needed = ACPI_TYPE_MUTEX;
			break;

		case ARGI_EVENT:

			/* Need an operand of type ACPI_TYPE_EVENT */

			type_needed = ACPI_TYPE_EVENT;
			break;

		case ARGI_PACKAGE:	/* Package */

			/* Need an operand of type ACPI_TYPE_PACKAGE */

			type_needed = ACPI_TYPE_PACKAGE;
			break;

		case ARGI_ANYTYPE:

			/* Any operand type will do */

			type_needed = ACPI_TYPE_ANY;
			break;

		case ARGI_DDBHANDLE:

			/* Need an operand of type ACPI_TYPE_DDB_HANDLE */

			type_needed = ACPI_TYPE_LOCAL_REFERENCE;
			break;

			/*
			 * The more complex cases allow multiple resolved object types
			 */
		case ARGI_INTEGER:

			/*
			 * Need an operand of type ACPI_TYPE_INTEGER,
			 * But we can implicitly convert from a STRING or BUFFER
			 * Aka - "Implicit Source Operand Conversion"
			 */
			status =
			    acpi_ex_convert_to_integer(obj_desc, stack_ptr, 16);
			if (ACPI_FAILURE(status)) {
				if (status == AE_TYPE) {
					ACPI_ERROR((AE_INFO,
						    "Needed [Integer/String/Buffer], found [%s] %p",
						    acpi_ut_get_object_type_name
						    (obj_desc), obj_desc));

					return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
				}

				return_ACPI_STATUS(status);
			}

			if (obj_desc != *stack_ptr) {
				acpi_ut_remove_reference(obj_desc);
			}
			goto next_operand;

		case ARGI_BUFFER:

			/*
			 * Need an operand of type ACPI_TYPE_BUFFER,
			 * But we can implicitly convert from a STRING or INTEGER
			 * Aka - "Implicit Source Operand Conversion"
			 */
			status = acpi_ex_convert_to_buffer(obj_desc, stack_ptr);
			if (ACPI_FAILURE(status)) {
				if (status == AE_TYPE) {
					ACPI_ERROR((AE_INFO,
						    "Needed [Integer/String/Buffer], found [%s] %p",
						    acpi_ut_get_object_type_name
						    (obj_desc), obj_desc));

					return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
				}

				return_ACPI_STATUS(status);
			}

			if (obj_desc != *stack_ptr) {
				acpi_ut_remove_reference(obj_desc);
			}
			goto next_operand;

		case ARGI_STRING:

			/*
			 * Need an operand of type ACPI_TYPE_STRING,
			 * But we can implicitly convert from a BUFFER or INTEGER
			 * Aka - "Implicit Source Operand Conversion"
			 */
			status = acpi_ex_convert_to_string(obj_desc, stack_ptr,
							   ACPI_IMPLICIT_CONVERT_HEX);
			if (ACPI_FAILURE(status)) {
				if (status == AE_TYPE) {
					ACPI_ERROR((AE_INFO,
						    "Needed [Integer/String/Buffer], found [%s] %p",
						    acpi_ut_get_object_type_name
						    (obj_desc), obj_desc));

					return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
				}

				return_ACPI_STATUS(status);
			}

			if (obj_desc != *stack_ptr) {
				acpi_ut_remove_reference(obj_desc);
			}
			goto next_operand;

		case ARGI_COMPUTEDATA:

			/* Need an operand of type INTEGER, STRING or BUFFER */

			switch (obj_desc->common.type) {
			case ACPI_TYPE_INTEGER:
			case ACPI_TYPE_STRING:
			case ACPI_TYPE_BUFFER:

				/* Valid operand */
				break;

			default:
				ACPI_ERROR((AE_INFO,
					    "Needed [Integer/String/Buffer], found [%s] %p",
					    acpi_ut_get_object_type_name
					    (obj_desc), obj_desc));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			goto next_operand;

		case ARGI_BUFFER_OR_STRING:

			/* Need an operand of type STRING or BUFFER */

			switch (obj_desc->common.type) {
			case ACPI_TYPE_STRING:
			case ACPI_TYPE_BUFFER:

				/* Valid operand */
				break;

			case ACPI_TYPE_INTEGER:

				/* Highest priority conversion is to type Buffer */

				status =
				    acpi_ex_convert_to_buffer(obj_desc,
							      stack_ptr);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}

				if (obj_desc != *stack_ptr) {
					acpi_ut_remove_reference(obj_desc);
				}
				break;

			default:
				ACPI_ERROR((AE_INFO,
					    "Needed [Integer/String/Buffer], found [%s] %p",
					    acpi_ut_get_object_type_name
					    (obj_desc), obj_desc));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			goto next_operand;

		case ARGI_DATAOBJECT:
			/*
			 * ARGI_DATAOBJECT is only used by the size_of operator.
			 * Need a buffer, string, package, or ref_of reference.
			 *
			 * The only reference allowed here is a direct reference to
			 * a namespace node.
			 */
			switch (obj_desc->common.type) {
			case ACPI_TYPE_PACKAGE:
			case ACPI_TYPE_STRING:
			case ACPI_TYPE_BUFFER:
			case ACPI_TYPE_LOCAL_REFERENCE:

				/* Valid operand */
				break;

			default:
				ACPI_ERROR((AE_INFO,
					    "Needed [Buffer/String/Package/Reference], found [%s] %p",
					    acpi_ut_get_object_type_name
					    (obj_desc), obj_desc));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			goto next_operand;

		case ARGI_COMPLEXOBJ:

			/* Need a buffer or package or (ACPI 2.0) String */

			switch (obj_desc->common.type) {
			case ACPI_TYPE_PACKAGE:
			case ACPI_TYPE_STRING:
			case ACPI_TYPE_BUFFER:

				/* Valid operand */
				break;

			default:
				ACPI_ERROR((AE_INFO,
					    "Needed [Buffer/String/Package], found [%s] %p",
					    acpi_ut_get_object_type_name
					    (obj_desc), obj_desc));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			goto next_operand;

		case ARGI_REGION_OR_BUFFER:	/* Used by Load() only */

			/* Need an operand of type REGION or a BUFFER (which could be a resolved region field) */

			switch (obj_desc->common.type) {
			case ACPI_TYPE_BUFFER:
			case ACPI_TYPE_REGION:

				/* Valid operand */
				break;

			default:
				ACPI_ERROR((AE_INFO,
					    "Needed [Region/Buffer], found [%s] %p",
					    acpi_ut_get_object_type_name
					    (obj_desc), obj_desc));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			goto next_operand;

		case ARGI_DATAREFOBJ:

			/* Used by the Store() operator only */

			switch (obj_desc->common.type) {
			case ACPI_TYPE_INTEGER:
			case ACPI_TYPE_PACKAGE:
			case ACPI_TYPE_STRING:
			case ACPI_TYPE_BUFFER:
			case ACPI_TYPE_BUFFER_FIELD:
			case ACPI_TYPE_LOCAL_REFERENCE:
			case ACPI_TYPE_LOCAL_REGION_FIELD:
			case ACPI_TYPE_LOCAL_BANK_FIELD:
			case ACPI_TYPE_LOCAL_INDEX_FIELD:
			case ACPI_TYPE_DDB_HANDLE:

				/* Valid operand */
				break;

			default:

				if (acpi_gbl_enable_interpreter_slack) {
					/*
					 * Enable original behavior of Store(), allowing any and all
					 * objects as the source operand.  The ACPI spec does not
					 * allow this, however.
					 */
					break;
				}

				if (target_op == AML_DEBUG_OP) {

					/* Allow store of any object to the Debug object */

					break;
				}

				ACPI_ERROR((AE_INFO,
					    "Needed Integer/Buffer/String/Package/Ref/Ddb], found [%s] %p",
					    acpi_ut_get_object_type_name
					    (obj_desc), obj_desc));

				return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
			}
			goto next_operand;

		default:

			/* Unknown type */

			ACPI_ERROR((AE_INFO,
				    "Internal - Unknown ARGI (required operand) type %X",
				    this_arg_type));

			return_ACPI_STATUS(AE_BAD_PARAMETER);
		}

		/*
		 * Make sure that the original object was resolved to the
		 * required object type (Simple cases only).
		 */
		status = acpi_ex_check_object_type(type_needed,
						   (*stack_ptr)->common.type,
						   *stack_ptr);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

	      next_operand:
		/*
		 * If more operands needed, decrement stack_ptr to point
		 * to next operand on stack
		 */
		if (GET_CURRENT_ARG_TYPE(arg_types)) {
			stack_ptr--;
		}
	}

	ACPI_DUMP_OPERANDS(walk_state->operands,
			   acpi_ps_get_opcode_name(opcode),
			   walk_state->num_operands);

	return_ACPI_STATUS(status);
}
