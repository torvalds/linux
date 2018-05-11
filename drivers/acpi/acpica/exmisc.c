// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exmisc - ACPI AML (p-code) execution - specific opcodes
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exmisc")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_get_object_reference
 *
 * PARAMETERS:  obj_desc            - Create a reference to this object
 *              return_desc         - Where to store the reference
 *              walk_state          - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain and return a "reference" to the target object
 *              Common code for the ref_of_op and the cond_ref_of_op.
 *
 ******************************************************************************/
acpi_status
acpi_ex_get_object_reference(union acpi_operand_object *obj_desc,
			     union acpi_operand_object **return_desc,
			     struct acpi_walk_state *walk_state)
{
	union acpi_operand_object *reference_obj;
	union acpi_operand_object *referenced_obj;

	ACPI_FUNCTION_TRACE_PTR(ex_get_object_reference, obj_desc);

	*return_desc = NULL;

	switch (ACPI_GET_DESCRIPTOR_TYPE(obj_desc)) {
	case ACPI_DESC_TYPE_OPERAND:

		if (obj_desc->common.type != ACPI_TYPE_LOCAL_REFERENCE) {
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/*
		 * Must be a reference to a Local or Arg
		 */
		switch (obj_desc->reference.class) {
		case ACPI_REFCLASS_LOCAL:
		case ACPI_REFCLASS_ARG:
		case ACPI_REFCLASS_DEBUG:

			/* The referenced object is the pseudo-node for the local/arg */

			referenced_obj = obj_desc->reference.object;
			break;

		default:

			ACPI_ERROR((AE_INFO, "Invalid Reference Class 0x%2.2X",
				    obj_desc->reference.class));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}
		break;

	case ACPI_DESC_TYPE_NAMED:
		/*
		 * A named reference that has already been resolved to a Node
		 */
		referenced_obj = obj_desc;
		break;

	default:

		ACPI_ERROR((AE_INFO, "Invalid descriptor type 0x%X",
			    ACPI_GET_DESCRIPTOR_TYPE(obj_desc)));
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Create a new reference object */

	reference_obj =
	    acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_REFERENCE);
	if (!reference_obj) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	reference_obj->reference.class = ACPI_REFCLASS_REFOF;
	reference_obj->reference.object = referenced_obj;
	*return_desc = reference_obj;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Object %p Type [%s], returning Reference %p\n",
			  obj_desc, acpi_ut_get_object_type_name(obj_desc),
			  *return_desc));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_math_op
 *
 * PARAMETERS:  opcode              - AML opcode
 *              integer0            - Integer operand #0
 *              integer1            - Integer operand #1
 *
 * RETURN:      Integer result of the operation
 *
 * DESCRIPTION: Execute a math AML opcode. The purpose of having all of the
 *              math functions here is to prevent a lot of pointer dereferencing
 *              to obtain the operands.
 *
 ******************************************************************************/

u64 acpi_ex_do_math_op(u16 opcode, u64 integer0, u64 integer1)
{

	ACPI_FUNCTION_ENTRY();

	switch (opcode) {
	case AML_ADD_OP:	/* Add (Integer0, Integer1, Result) */

		return (integer0 + integer1);

	case AML_BIT_AND_OP:	/* And (Integer0, Integer1, Result) */

		return (integer0 & integer1);

	case AML_BIT_NAND_OP:	/* NAnd (Integer0, Integer1, Result) */

		return (~(integer0 & integer1));

	case AML_BIT_OR_OP:	/* Or (Integer0, Integer1, Result) */

		return (integer0 | integer1);

	case AML_BIT_NOR_OP:	/* NOr (Integer0, Integer1, Result) */

		return (~(integer0 | integer1));

	case AML_BIT_XOR_OP:	/* XOr (Integer0, Integer1, Result) */

		return (integer0 ^ integer1);

	case AML_MULTIPLY_OP:	/* Multiply (Integer0, Integer1, Result) */

		return (integer0 * integer1);

	case AML_SHIFT_LEFT_OP:	/* shift_left (Operand, shift_count, Result) */

		/*
		 * We need to check if the shiftcount is larger than the integer bit
		 * width since the behavior of this is not well-defined in the C language.
		 */
		if (integer1 >= acpi_gbl_integer_bit_width) {
			return (0);
		}
		return (integer0 << integer1);

	case AML_SHIFT_RIGHT_OP:	/* shift_right (Operand, shift_count, Result) */

		/*
		 * We need to check if the shiftcount is larger than the integer bit
		 * width since the behavior of this is not well-defined in the C language.
		 */
		if (integer1 >= acpi_gbl_integer_bit_width) {
			return (0);
		}
		return (integer0 >> integer1);

	case AML_SUBTRACT_OP:	/* Subtract (Integer0, Integer1, Result) */

		return (integer0 - integer1);

	default:

		return (0);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_logical_numeric_op
 *
 * PARAMETERS:  opcode              - AML opcode
 *              integer0            - Integer operand #0
 *              integer1            - Integer operand #1
 *              logical_result      - TRUE/FALSE result of the operation
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a logical "Numeric" AML opcode. For these Numeric
 *              operators (LAnd and LOr), both operands must be integers.
 *
 *              Note: cleanest machine code seems to be produced by the code
 *              below, rather than using statements of the form:
 *                  Result = (Integer0 && Integer1);
 *
 ******************************************************************************/

acpi_status
acpi_ex_do_logical_numeric_op(u16 opcode,
			      u64 integer0, u64 integer1, u8 *logical_result)
{
	acpi_status status = AE_OK;
	u8 local_result = FALSE;

	ACPI_FUNCTION_TRACE(ex_do_logical_numeric_op);

	switch (opcode) {
	case AML_LOGICAL_AND_OP:	/* LAnd (Integer0, Integer1) */

		if (integer0 && integer1) {
			local_result = TRUE;
		}
		break;

	case AML_LOGICAL_OR_OP:	/* LOr (Integer0, Integer1) */

		if (integer0 || integer1) {
			local_result = TRUE;
		}
		break;

	default:

		ACPI_ERROR((AE_INFO,
			    "Invalid numeric logical opcode: %X", opcode));
		status = AE_AML_INTERNAL;
		break;
	}

	/* Return the logical result and status */

	*logical_result = local_result;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_logical_op
 *
 * PARAMETERS:  opcode              - AML opcode
 *              operand0            - operand #0
 *              operand1            - operand #1
 *              logical_result      - TRUE/FALSE result of the operation
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a logical AML opcode. The purpose of having all of the
 *              functions here is to prevent a lot of pointer dereferencing
 *              to obtain the operands and to simplify the generation of the
 *              logical value. For the Numeric operators (LAnd and LOr), both
 *              operands must be integers. For the other logical operators,
 *              operands can be any combination of Integer/String/Buffer. The
 *              first operand determines the type to which the second operand
 *              will be converted.
 *
 *              Note: cleanest machine code seems to be produced by the code
 *              below, rather than using statements of the form:
 *                  Result = (Operand0 == Operand1);
 *
 ******************************************************************************/

acpi_status
acpi_ex_do_logical_op(u16 opcode,
		      union acpi_operand_object *operand0,
		      union acpi_operand_object *operand1, u8 * logical_result)
{
	union acpi_operand_object *local_operand1 = operand1;
	u64 integer0;
	u64 integer1;
	u32 length0;
	u32 length1;
	acpi_status status = AE_OK;
	u8 local_result = FALSE;
	int compare;

	ACPI_FUNCTION_TRACE(ex_do_logical_op);

	/*
	 * Convert the second operand if necessary. The first operand
	 * determines the type of the second operand, (See the Data Types
	 * section of the ACPI 3.0+ specification.)  Both object types are
	 * guaranteed to be either Integer/String/Buffer by the operand
	 * resolution mechanism.
	 */
	switch (operand0->common.type) {
	case ACPI_TYPE_INTEGER:

		status = acpi_ex_convert_to_integer(operand1, &local_operand1,
						    ACPI_IMPLICIT_CONVERSION);
		break;

	case ACPI_TYPE_STRING:

		status =
		    acpi_ex_convert_to_string(operand1, &local_operand1,
					      ACPI_IMPLICIT_CONVERT_HEX);
		break;

	case ACPI_TYPE_BUFFER:

		status = acpi_ex_convert_to_buffer(operand1, &local_operand1);
		break;

	default:

		ACPI_ERROR((AE_INFO,
			    "Invalid object type for logical operator: %X",
			    operand0->common.type));
		status = AE_AML_INTERNAL;
		break;
	}

	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/*
	 * Two cases: 1) Both Integers, 2) Both Strings or Buffers
	 */
	if (operand0->common.type == ACPI_TYPE_INTEGER) {
		/*
		 * 1) Both operands are of type integer
		 *    Note: local_operand1 may have changed above
		 */
		integer0 = operand0->integer.value;
		integer1 = local_operand1->integer.value;

		switch (opcode) {
		case AML_LOGICAL_EQUAL_OP:	/* LEqual (Operand0, Operand1) */

			if (integer0 == integer1) {
				local_result = TRUE;
			}
			break;

		case AML_LOGICAL_GREATER_OP:	/* LGreater (Operand0, Operand1) */

			if (integer0 > integer1) {
				local_result = TRUE;
			}
			break;

		case AML_LOGICAL_LESS_OP:	/* LLess (Operand0, Operand1) */

			if (integer0 < integer1) {
				local_result = TRUE;
			}
			break;

		default:

			ACPI_ERROR((AE_INFO,
				    "Invalid comparison opcode: %X", opcode));
			status = AE_AML_INTERNAL;
			break;
		}
	} else {
		/*
		 * 2) Both operands are Strings or both are Buffers
		 *    Note: Code below takes advantage of common Buffer/String
		 *          object fields. local_operand1 may have changed above. Use
		 *          memcmp to handle nulls in buffers.
		 */
		length0 = operand0->buffer.length;
		length1 = local_operand1->buffer.length;

		/* Lexicographic compare: compare the data bytes */

		compare = memcmp(operand0->buffer.pointer,
				 local_operand1->buffer.pointer,
				 (length0 > length1) ? length1 : length0);

		switch (opcode) {
		case AML_LOGICAL_EQUAL_OP:	/* LEqual (Operand0, Operand1) */

			/* Length and all bytes must be equal */

			if ((length0 == length1) && (compare == 0)) {

				/* Length and all bytes match ==> TRUE */

				local_result = TRUE;
			}
			break;

		case AML_LOGICAL_GREATER_OP:	/* LGreater (Operand0, Operand1) */

			if (compare > 0) {
				local_result = TRUE;
				goto cleanup;	/* TRUE */
			}
			if (compare < 0) {
				goto cleanup;	/* FALSE */
			}

			/* Bytes match (to shortest length), compare lengths */

			if (length0 > length1) {
				local_result = TRUE;
			}
			break;

		case AML_LOGICAL_LESS_OP:	/* LLess (Operand0, Operand1) */

			if (compare > 0) {
				goto cleanup;	/* FALSE */
			}
			if (compare < 0) {
				local_result = TRUE;
				goto cleanup;	/* TRUE */
			}

			/* Bytes match (to shortest length), compare lengths */

			if (length0 < length1) {
				local_result = TRUE;
			}
			break;

		default:

			ACPI_ERROR((AE_INFO,
				    "Invalid comparison opcode: %X", opcode));
			status = AE_AML_INTERNAL;
			break;
		}
	}

cleanup:

	/* New object was created if implicit conversion performed - delete */

	if (local_operand1 != operand1) {
		acpi_ut_remove_reference(local_operand1);
	}

	/* Return the logical result and status */

	*logical_result = local_result;
	return_ACPI_STATUS(status);
}
