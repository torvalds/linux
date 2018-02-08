/******************************************************************************
 *
 * Module Name: exoparg1 - AML execution - opcodes with 1 argument
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exoparg1")

/*!
 * Naming convention for AML interpreter execution routines.
 *
 * The routines that begin execution of AML opcodes are named with a common
 * convention based upon the number of arguments, the number of target operands,
 * and whether or not a value is returned:
 *
 *      AcpiExOpcode_xA_yT_zR
 *
 * Where:
 *
 * xA - ARGUMENTS:    The number of arguments (input operands) that are
 *                    required for this opcode type (0 through 6 args).
 * yT - TARGETS:      The number of targets (output operands) that are required
 *                    for this opcode type (0, 1, or 2 targets).
 * zR - RETURN VALUE: Indicates whether this opcode type returns a value
 *                    as the function return (0 or 1).
 *
 * The AcpiExOpcode* functions are called via the Dispatcher component with
 * fully resolved operands.
!*/
/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_opcode_0A_0T_1R
 *
 * PARAMETERS:  walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute operator with no operands, one return value
 *
 ******************************************************************************/
acpi_status acpi_ex_opcode_0A_0T_1R(struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *return_desc = NULL;

	ACPI_FUNCTION_TRACE_STR(ex_opcode_0A_0T_1R,
				acpi_ps_get_opcode_name(walk_state->opcode));

	/* Examine the AML opcode */

	switch (walk_state->opcode) {
	case AML_TIMER_OP:	/*  Timer () */

		/* Create a return object of type Integer */

		return_desc =
		    acpi_ut_create_integer_object(acpi_os_get_timer());
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}
		break;

	default:		/*  Unknown opcode  */

		ACPI_ERROR((AE_INFO, "Unknown AML opcode 0x%X",
			    walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		break;
	}

cleanup:

	/* Delete return object on error */

	if ((ACPI_FAILURE(status)) || walk_state->result_obj) {
		acpi_ut_remove_reference(return_desc);
		walk_state->result_obj = NULL;
	} else {
		/* Save the return value */

		walk_state->result_obj = return_desc;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_opcode_1A_0T_0R
 *
 * PARAMETERS:  walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute Type 1 monadic operator with numeric operand on
 *              object stack
 *
 ******************************************************************************/

acpi_status acpi_ex_opcode_1A_0T_0R(struct acpi_walk_state *walk_state)
{
	union acpi_operand_object **operand = &walk_state->operands[0];
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_STR(ex_opcode_1A_0T_0R,
				acpi_ps_get_opcode_name(walk_state->opcode));

	/* Examine the AML opcode */

	switch (walk_state->opcode) {
	case AML_RELEASE_OP:	/*  Release (mutex_object) */

		status = acpi_ex_release_mutex(operand[0], walk_state);
		break;

	case AML_RESET_OP:	/*  Reset (event_object) */

		status = acpi_ex_system_reset_event(operand[0]);
		break;

	case AML_SIGNAL_OP:	/*  Signal (event_object) */

		status = acpi_ex_system_signal_event(operand[0]);
		break;

	case AML_SLEEP_OP:	/*  Sleep (msec_time) */

		status = acpi_ex_system_do_sleep(operand[0]->integer.value);
		break;

	case AML_STALL_OP:	/*  Stall (usec_time) */

		status =
		    acpi_ex_system_do_stall((u32) operand[0]->integer.value);
		break;

	case AML_UNLOAD_OP:	/*  Unload (Handle) */

		status = acpi_ex_unload_table(operand[0]);
		break;

	default:		/*  Unknown opcode  */

		ACPI_ERROR((AE_INFO, "Unknown AML opcode 0x%X",
			    walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_opcode_1A_1T_0R
 *
 * PARAMETERS:  walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with one argument, one target, and no
 *              return value.
 *
 ******************************************************************************/

acpi_status acpi_ex_opcode_1A_1T_0R(struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object **operand = &walk_state->operands[0];

	ACPI_FUNCTION_TRACE_STR(ex_opcode_1A_1T_0R,
				acpi_ps_get_opcode_name(walk_state->opcode));

	/* Examine the AML opcode */

	switch (walk_state->opcode) {
	case AML_LOAD_OP:

		status = acpi_ex_load_op(operand[0], operand[1], walk_state);
		break;

	default:		/* Unknown opcode */

		ACPI_ERROR((AE_INFO, "Unknown AML opcode 0x%X",
			    walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}

cleanup:

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_opcode_1A_1T_1R
 *
 * PARAMETERS:  walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with one argument, one target, and a
 *              return value.
 *
 ******************************************************************************/

acpi_status acpi_ex_opcode_1A_1T_1R(struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object **operand = &walk_state->operands[0];
	union acpi_operand_object *return_desc = NULL;
	union acpi_operand_object *return_desc2 = NULL;
	u32 temp32;
	u32 i;
	u64 power_of_ten;
	u64 digit;

	ACPI_FUNCTION_TRACE_STR(ex_opcode_1A_1T_1R,
				acpi_ps_get_opcode_name(walk_state->opcode));

	/* Examine the AML opcode */

	switch (walk_state->opcode) {
	case AML_BIT_NOT_OP:
	case AML_FIND_SET_LEFT_BIT_OP:
	case AML_FIND_SET_RIGHT_BIT_OP:
	case AML_FROM_BCD_OP:
	case AML_TO_BCD_OP:
	case AML_CONDITIONAL_REF_OF_OP:

		/* Create a return object of type Integer for these opcodes */

		return_desc = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		switch (walk_state->opcode) {
		case AML_BIT_NOT_OP:	/* Not (Operand, Result)  */

			return_desc->integer.value = ~operand[0]->integer.value;
			break;

		case AML_FIND_SET_LEFT_BIT_OP:	/* find_set_left_bit (Operand, Result) */

			return_desc->integer.value = operand[0]->integer.value;

			/*
			 * Acpi specification describes Integer type as a little
			 * endian unsigned value, so this boundary condition is valid.
			 */
			for (temp32 = 0; return_desc->integer.value &&
			     temp32 < ACPI_INTEGER_BIT_SIZE; ++temp32) {
				return_desc->integer.value >>= 1;
			}

			return_desc->integer.value = temp32;
			break;

		case AML_FIND_SET_RIGHT_BIT_OP:	/* find_set_right_bit (Operand, Result) */

			return_desc->integer.value = operand[0]->integer.value;

			/*
			 * The Acpi specification describes Integer type as a little
			 * endian unsigned value, so this boundary condition is valid.
			 */
			for (temp32 = 0; return_desc->integer.value &&
			     temp32 < ACPI_INTEGER_BIT_SIZE; ++temp32) {
				return_desc->integer.value <<= 1;
			}

			/* Since the bit position is one-based, subtract from 33 (65) */

			return_desc->integer.value =
			    temp32 ==
			    0 ? 0 : (ACPI_INTEGER_BIT_SIZE + 1) - temp32;
			break;

		case AML_FROM_BCD_OP:	/* from_bcd (BCDValue, Result) */
			/*
			 * The 64-bit ACPI integer can hold 16 4-bit BCD characters
			 * (if table is 32-bit, integer can hold 8 BCD characters)
			 * Convert each 4-bit BCD value
			 */
			power_of_ten = 1;
			return_desc->integer.value = 0;
			digit = operand[0]->integer.value;

			/* Convert each BCD digit (each is one nybble wide) */

			for (i = 0;
			     (i < acpi_gbl_integer_nybble_width) && (digit > 0);
			     i++) {

				/* Get the least significant 4-bit BCD digit */

				temp32 = ((u32) digit) & 0xF;

				/* Check the range of the digit */

				if (temp32 > 9) {
					ACPI_ERROR((AE_INFO,
						    "BCD digit too large (not decimal): 0x%X",
						    temp32));

					status = AE_AML_NUMERIC_OVERFLOW;
					goto cleanup;
				}

				/* Sum the digit into the result with the current power of 10 */

				return_desc->integer.value +=
				    (((u64) temp32) * power_of_ten);

				/* Shift to next BCD digit */

				digit >>= 4;

				/* Next power of 10 */

				power_of_ten *= 10;
			}
			break;

		case AML_TO_BCD_OP:	/* to_bcd (Operand, Result) */

			return_desc->integer.value = 0;
			digit = operand[0]->integer.value;

			/* Each BCD digit is one nybble wide */

			for (i = 0;
			     (i < acpi_gbl_integer_nybble_width) && (digit > 0);
			     i++) {
				(void)acpi_ut_short_divide(digit, 10, &digit,
							   &temp32);

				/*
				 * Insert the BCD digit that resides in the
				 * remainder from above
				 */
				return_desc->integer.value |=
				    (((u64) temp32) << ACPI_MUL_4(i));
			}

			/* Overflow if there is any data left in Digit */

			if (digit > 0) {
				ACPI_ERROR((AE_INFO,
					    "Integer too large to convert to BCD: 0x%8.8X%8.8X",
					    ACPI_FORMAT_UINT64(operand[0]->
							       integer.value)));
				status = AE_AML_NUMERIC_OVERFLOW;
				goto cleanup;
			}
			break;

		case AML_CONDITIONAL_REF_OF_OP:	/* cond_ref_of (source_object, Result) */
			/*
			 * This op is a little strange because the internal return value is
			 * different than the return value stored in the result descriptor
			 * (There are really two return values)
			 */
			if ((struct acpi_namespace_node *)operand[0] ==
			    acpi_gbl_root_node) {
				/*
				 * This means that the object does not exist in the namespace,
				 * return FALSE
				 */
				return_desc->integer.value = 0;
				goto cleanup;
			}

			/* Get the object reference, store it, and remove our reference */

			status = acpi_ex_get_object_reference(operand[0],
							      &return_desc2,
							      walk_state);
			if (ACPI_FAILURE(status)) {
				goto cleanup;
			}

			status =
			    acpi_ex_store(return_desc2, operand[1], walk_state);
			acpi_ut_remove_reference(return_desc2);

			/* The object exists in the namespace, return TRUE */

			return_desc->integer.value = ACPI_UINT64_MAX;
			goto cleanup;

		default:

			/* No other opcodes get here */

			break;
		}
		break;

	case AML_STORE_OP:	/* Store (Source, Target) */
		/*
		 * A store operand is typically a number, string, buffer or lvalue
		 * Be careful about deleting the source object,
		 * since the object itself may have been stored.
		 */
		status = acpi_ex_store(operand[0], operand[1], walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* It is possible that the Store already produced a return object */

		if (!walk_state->result_obj) {
			/*
			 * Normally, we would remove a reference on the Operand[0]
			 * parameter; But since it is being used as the internal return
			 * object (meaning we would normally increment it), the two
			 * cancel out, and we simply don't do anything.
			 */
			walk_state->result_obj = operand[0];
			walk_state->operands[0] = NULL;	/* Prevent deletion */
		}
		return_ACPI_STATUS(status);

		/*
		 * ACPI 2.0 Opcodes
		 */
	case AML_COPY_OBJECT_OP:	/* copy_object (Source, Target) */

		status =
		    acpi_ut_copy_iobject_to_iobject(operand[0], &return_desc,
						    walk_state);
		break;

	case AML_TO_DECIMAL_STRING_OP:	/* to_decimal_string (Data, Result) */

		status =
		    acpi_ex_convert_to_string(operand[0], &return_desc,
					      ACPI_EXPLICIT_CONVERT_DECIMAL);
		if (return_desc == operand[0]) {

			/* No conversion performed, add ref to handle return value */

			acpi_ut_add_reference(return_desc);
		}
		break;

	case AML_TO_HEX_STRING_OP:	/* to_hex_string (Data, Result) */

		status =
		    acpi_ex_convert_to_string(operand[0], &return_desc,
					      ACPI_EXPLICIT_CONVERT_HEX);
		if (return_desc == operand[0]) {

			/* No conversion performed, add ref to handle return value */

			acpi_ut_add_reference(return_desc);
		}
		break;

	case AML_TO_BUFFER_OP:	/* to_buffer (Data, Result) */

		status = acpi_ex_convert_to_buffer(operand[0], &return_desc);
		if (return_desc == operand[0]) {

			/* No conversion performed, add ref to handle return value */

			acpi_ut_add_reference(return_desc);
		}
		break;

	case AML_TO_INTEGER_OP:	/* to_integer (Data, Result) */

		/* Perform "explicit" conversion */

		status =
		    acpi_ex_convert_to_integer(operand[0], &return_desc, 0);
		if (return_desc == operand[0]) {

			/* No conversion performed, add ref to handle return value */

			acpi_ut_add_reference(return_desc);
		}
		break;

	case AML_SHIFT_LEFT_BIT_OP:	/* shift_left_bit (Source, bit_num) */
	case AML_SHIFT_RIGHT_BIT_OP:	/* shift_right_bit (Source, bit_num) */

		/* These are two obsolete opcodes */

		ACPI_ERROR((AE_INFO,
			    "%s is obsolete and not implemented",
			    acpi_ps_get_opcode_name(walk_state->opcode)));
		status = AE_SUPPORT;
		goto cleanup;

	default:		/* Unknown opcode */

		ACPI_ERROR((AE_INFO, "Unknown AML opcode 0x%X",
			    walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}

	if (ACPI_SUCCESS(status)) {

		/* Store the return value computed above into the target object */

		status = acpi_ex_store(return_desc, operand[1], walk_state);
	}

cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE(status)) {
		acpi_ut_remove_reference(return_desc);
	}

	/* Save return object on success */

	else if (!walk_state->result_obj) {
		walk_state->result_obj = return_desc;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_opcode_1A_0T_1R
 *
 * PARAMETERS:  walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with one argument, no target, and a return value
 *
 ******************************************************************************/

acpi_status acpi_ex_opcode_1A_0T_1R(struct acpi_walk_state *walk_state)
{
	union acpi_operand_object **operand = &walk_state->operands[0];
	union acpi_operand_object *temp_desc;
	union acpi_operand_object *return_desc = NULL;
	acpi_status status = AE_OK;
	u32 type;
	u64 value;

	ACPI_FUNCTION_TRACE_STR(ex_opcode_1A_0T_1R,
				acpi_ps_get_opcode_name(walk_state->opcode));

	/* Examine the AML opcode */

	switch (walk_state->opcode) {
	case AML_LOGICAL_NOT_OP:	/* LNot (Operand) */

		return_desc = acpi_ut_create_integer_object((u64) 0);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/*
		 * Set result to ONES (TRUE) if Value == 0. Note:
		 * return_desc->Integer.Value is initially == 0 (FALSE) from above.
		 */
		if (!operand[0]->integer.value) {
			return_desc->integer.value = ACPI_UINT64_MAX;
		}
		break;

	case AML_DECREMENT_OP:	/* Decrement (Operand)  */
	case AML_INCREMENT_OP:	/* Increment (Operand)  */
		/*
		 * Create a new integer. Can't just get the base integer and
		 * increment it because it may be an Arg or Field.
		 */
		return_desc = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/*
		 * Since we are expecting a Reference operand, it can be either a
		 * NS Node or an internal object.
		 */
		temp_desc = operand[0];
		if (ACPI_GET_DESCRIPTOR_TYPE(temp_desc) ==
		    ACPI_DESC_TYPE_OPERAND) {

			/* Internal reference object - prevent deletion */

			acpi_ut_add_reference(temp_desc);
		}

		/*
		 * Convert the Reference operand to an Integer (This removes a
		 * reference on the Operand[0] object)
		 *
		 * NOTE:  We use LNOT_OP here in order to force resolution of the
		 * reference operand to an actual integer.
		 */
		status = acpi_ex_resolve_operands(AML_LOGICAL_NOT_OP,
						  &temp_desc, walk_state);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"While resolving operands for [%s]",
					acpi_ps_get_opcode_name(walk_state->
								opcode)));

			goto cleanup;
		}

		/*
		 * temp_desc is now guaranteed to be an Integer object --
		 * Perform the actual increment or decrement
		 */
		if (walk_state->opcode == AML_INCREMENT_OP) {
			return_desc->integer.value =
			    temp_desc->integer.value + 1;
		} else {
			return_desc->integer.value =
			    temp_desc->integer.value - 1;
		}

		/* Finished with this Integer object */

		acpi_ut_remove_reference(temp_desc);

		/*
		 * Store the result back (indirectly) through the original
		 * Reference object
		 */
		status = acpi_ex_store(return_desc, operand[0], walk_state);
		break;

	case AML_OBJECT_TYPE_OP:	/* object_type (source_object) */
		/*
		 * Note: The operand is not resolved at this point because we want to
		 * get the associated object, not its value. For example, we don't
		 * want to resolve a field_unit to its value, we want the actual
		 * field_unit object.
		 */

		/* Get the type of the base object */

		status =
		    acpi_ex_resolve_multiple(walk_state, operand[0], &type,
					     NULL);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}

		/* Allocate a descriptor to hold the type. */

		return_desc = acpi_ut_create_integer_object((u64) type);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}
		break;

	case AML_SIZE_OF_OP:	/* size_of (source_object) */
		/*
		 * Note: The operand is not resolved at this point because we want to
		 * get the associated object, not its value.
		 */

		/* Get the base object */

		status =
		    acpi_ex_resolve_multiple(walk_state, operand[0], &type,
					     &temp_desc);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}

		/*
		 * The type of the base object must be integer, buffer, string, or
		 * package. All others are not supported.
		 *
		 * NOTE: Integer is not specifically supported by the ACPI spec,
		 * but is supported implicitly via implicit operand conversion.
		 * rather than bother with conversion, we just use the byte width
		 * global (4 or 8 bytes).
		 */
		switch (type) {
		case ACPI_TYPE_INTEGER:

			value = acpi_gbl_integer_byte_width;
			break;

		case ACPI_TYPE_STRING:

			value = temp_desc->string.length;
			break;

		case ACPI_TYPE_BUFFER:

			/* Buffer arguments may not be evaluated at this point */

			status = acpi_ds_get_buffer_arguments(temp_desc);
			value = temp_desc->buffer.length;
			break;

		case ACPI_TYPE_PACKAGE:

			/* Package arguments may not be evaluated at this point */

			status = acpi_ds_get_package_arguments(temp_desc);
			value = temp_desc->package.count;
			break;

		default:

			ACPI_ERROR((AE_INFO,
				    "Operand must be Buffer/Integer/String/Package"
				    " - found type %s",
				    acpi_ut_get_type_name(type)));

			status = AE_AML_OPERAND_TYPE;
			goto cleanup;
		}

		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}

		/*
		 * Now that we have the size of the object, create a result
		 * object to hold the value
		 */
		return_desc = acpi_ut_create_integer_object(value);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}
		break;

	case AML_REF_OF_OP:	/* ref_of (source_object) */

		status =
		    acpi_ex_get_object_reference(operand[0], &return_desc,
						 walk_state);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}
		break;

	case AML_DEREF_OF_OP:	/* deref_of (obj_reference | String) */

		/* Check for a method local or argument, or standalone String */

		if (ACPI_GET_DESCRIPTOR_TYPE(operand[0]) ==
		    ACPI_DESC_TYPE_NAMED) {
			temp_desc =
			    acpi_ns_get_attached_object((struct
							 acpi_namespace_node *)
							operand[0]);
			if (temp_desc
			    && ((temp_desc->common.type == ACPI_TYPE_STRING)
				|| (temp_desc->common.type ==
				    ACPI_TYPE_LOCAL_REFERENCE))) {
				operand[0] = temp_desc;
				acpi_ut_add_reference(temp_desc);
			} else {
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
		} else {
			switch ((operand[0])->common.type) {
			case ACPI_TYPE_LOCAL_REFERENCE:
				/*
				 * This is a deref_of (local_x | arg_x)
				 *
				 * Must resolve/dereference the local/arg reference first
				 */
				switch (operand[0]->reference.class) {
				case ACPI_REFCLASS_LOCAL:
				case ACPI_REFCLASS_ARG:

					/* Set Operand[0] to the value of the local/arg */

					status =
					    acpi_ds_method_data_get_value
					    (operand[0]->reference.class,
					     operand[0]->reference.value,
					     walk_state, &temp_desc);
					if (ACPI_FAILURE(status)) {
						goto cleanup;
					}

					/*
					 * Delete our reference to the input object and
					 * point to the object just retrieved
					 */
					acpi_ut_remove_reference(operand[0]);
					operand[0] = temp_desc;
					break;

				case ACPI_REFCLASS_REFOF:

					/* Get the object to which the reference refers */

					temp_desc =
					    operand[0]->reference.object;
					acpi_ut_remove_reference(operand[0]);
					operand[0] = temp_desc;
					break;

				default:

					/* Must be an Index op - handled below */
					break;
				}
				break;

			case ACPI_TYPE_STRING:

				break;

			default:

				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
		}

		if (ACPI_GET_DESCRIPTOR_TYPE(operand[0]) !=
		    ACPI_DESC_TYPE_NAMED) {
			if ((operand[0])->common.type == ACPI_TYPE_STRING) {
				/*
				 * This is a deref_of (String). The string is a reference
				 * to a named ACPI object.
				 *
				 * 1) Find the owning Node
				 * 2) Dereference the node to an actual object. Could be a
				 *    Field, so we need to resolve the node to a value.
				 */
				status =
				    acpi_ns_get_node_unlocked(walk_state->
							      scope_info->scope.
							      node,
							      operand[0]->
							      string.pointer,
							      ACPI_NS_SEARCH_PARENT,
							      ACPI_CAST_INDIRECT_PTR
							      (struct
							       acpi_namespace_node,
							       &return_desc));
				if (ACPI_FAILURE(status)) {
					goto cleanup;
				}

				status =
				    acpi_ex_resolve_node_to_value
				    (ACPI_CAST_INDIRECT_PTR
				     (struct acpi_namespace_node, &return_desc),
				     walk_state);
				goto cleanup;
			}
		}

		/* Operand[0] may have changed from the code above */

		if (ACPI_GET_DESCRIPTOR_TYPE(operand[0]) ==
		    ACPI_DESC_TYPE_NAMED) {
			/*
			 * This is a deref_of (object_reference)
			 * Get the actual object from the Node (This is the dereference).
			 * This case may only happen when a local_x or arg_x is
			 * dereferenced above, or for references to device and
			 * thermal objects.
			 */
			switch (((struct acpi_namespace_node *)operand[0])->
				type) {
			case ACPI_TYPE_DEVICE:
			case ACPI_TYPE_THERMAL:

				/* These types have no node subobject, return the NS node */

				return_desc = operand[0];
				break;

			default:
				/* For most types, get the object attached to the node */

				return_desc = acpi_ns_get_attached_object((struct acpi_namespace_node *)operand[0]);
				acpi_ut_add_reference(return_desc);
				break;
			}
		} else {
			/*
			 * This must be a reference object produced by either the
			 * Index() or ref_of() operator
			 */
			switch (operand[0]->reference.class) {
			case ACPI_REFCLASS_INDEX:
				/*
				 * The target type for the Index operator must be
				 * either a Buffer or a Package
				 */
				switch (operand[0]->reference.target_type) {
				case ACPI_TYPE_BUFFER_FIELD:

					temp_desc =
					    operand[0]->reference.object;

					/*
					 * Create a new object that contains one element of the
					 * buffer -- the element pointed to by the index.
					 *
					 * NOTE: index into a buffer is NOT a pointer to a
					 * sub-buffer of the main buffer, it is only a pointer to a
					 * single element (byte) of the buffer!
					 *
					 * Since we are returning the value of the buffer at the
					 * indexed location, we don't need to add an additional
					 * reference to the buffer itself.
					 */
					return_desc =
					    acpi_ut_create_integer_object((u64)
									  temp_desc->buffer.pointer[operand[0]->reference.value]);
					if (!return_desc) {
						status = AE_NO_MEMORY;
						goto cleanup;
					}
					break;

				case ACPI_TYPE_PACKAGE:
					/*
					 * Return the referenced element of the package. We must
					 * add another reference to the referenced object, however.
					 */
					return_desc =
					    *(operand[0]->reference.where);
					if (!return_desc) {
						/*
						 * Element is NULL, do not allow the dereference.
						 * This provides compatibility with other ACPI
						 * implementations.
						 */
						return_ACPI_STATUS
						    (AE_AML_UNINITIALIZED_ELEMENT);
					}

					acpi_ut_add_reference(return_desc);
					break;

				default:

					ACPI_ERROR((AE_INFO,
						    "Unknown Index TargetType 0x%X in reference object %p",
						    operand[0]->reference.
						    target_type, operand[0]));

					status = AE_AML_OPERAND_TYPE;
					goto cleanup;
				}
				break;

			case ACPI_REFCLASS_REFOF:

				return_desc = operand[0]->reference.object;

				if (ACPI_GET_DESCRIPTOR_TYPE(return_desc) ==
				    ACPI_DESC_TYPE_NAMED) {
					return_desc =
					    acpi_ns_get_attached_object((struct
									 acpi_namespace_node
									 *)
									return_desc);
					if (!return_desc) {
						break;
					}

					/*
					 * June 2013:
					 * buffer_fields/field_units require additional resolution
					 */
					switch (return_desc->common.type) {
					case ACPI_TYPE_BUFFER_FIELD:
					case ACPI_TYPE_LOCAL_REGION_FIELD:
					case ACPI_TYPE_LOCAL_BANK_FIELD:
					case ACPI_TYPE_LOCAL_INDEX_FIELD:

						status =
						    acpi_ex_read_data_from_field
						    (walk_state, return_desc,
						     &temp_desc);
						if (ACPI_FAILURE(status)) {
							goto cleanup;
						}

						return_desc = temp_desc;
						break;

					default:

						/* Add another reference to the object */

						acpi_ut_add_reference
						    (return_desc);
						break;
					}
				}
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Unknown class in reference(%p) - 0x%2.2X",
					    operand[0],
					    operand[0]->reference.class));

				status = AE_TYPE;
				goto cleanup;
			}
		}
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unknown AML opcode 0x%X",
			    walk_state->opcode));

		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}

cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE(status)) {
		acpi_ut_remove_reference(return_desc);
	}

	/* Save return object on success */

	else {
		walk_state->result_obj = return_desc;
	}

	return_ACPI_STATUS(status);
}
