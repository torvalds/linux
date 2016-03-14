/******************************************************************************
 *
 * Module Name: exoparg6 - AML execution - opcodes with 6 arguments
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
#include "acinterp.h"
#include "acparser.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exoparg6")

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
 *                    required for this opcode type (1 through 6 args).
 * yT - TARGETS:      The number of targets (output operands) that are required
 *                    for this opcode type (0, 1, or 2 targets).
 * zR - RETURN VALUE: Indicates whether this opcode type returns a value
 *                    as the function return (0 or 1).
 *
 * The AcpiExOpcode* functions are called via the Dispatcher component with
 * fully resolved operands.
!*/
/* Local prototypes */
static u8
acpi_ex_do_match(u32 match_op,
		 union acpi_operand_object *package_obj,
		 union acpi_operand_object *match_obj);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_match
 *
 * PARAMETERS:  match_op        - The AML match operand
 *              package_obj     - Object from the target package
 *              match_obj       - Object to be matched
 *
 * RETURN:      TRUE if the match is successful, FALSE otherwise
 *
 * DESCRIPTION: Implements the low-level match for the ASL Match operator.
 *              Package elements will be implicitly converted to the type of
 *              the match object (Integer/Buffer/String).
 *
 ******************************************************************************/

static u8
acpi_ex_do_match(u32 match_op,
		 union acpi_operand_object *package_obj,
		 union acpi_operand_object *match_obj)
{
	u8 logical_result = TRUE;
	acpi_status status;

	/*
	 * Note: Since the package_obj/match_obj ordering is opposite to that of
	 * the standard logical operators, we have to reverse them when we call
	 * do_logical_op in order to make the implicit conversion rules work
	 * correctly. However, this means we have to flip the entire equation
	 * also. A bit ugly perhaps, but overall, better than fussing the
	 * parameters around at runtime, over and over again.
	 *
	 * Below, P[i] refers to the package element, M refers to the Match object.
	 */
	switch (match_op) {
	case MATCH_MTR:

		/* Always true */

		break;

	case MATCH_MEQ:
		/*
		 * True if equal: (P[i] == M)
		 * Change to:     (M == P[i])
		 */
		status =
		    acpi_ex_do_logical_op(AML_LEQUAL_OP, match_obj, package_obj,
					  &logical_result);
		if (ACPI_FAILURE(status)) {
			return (FALSE);
		}
		break;

	case MATCH_MLE:
		/*
		 * True if less than or equal: (P[i] <= M) (P[i] not_greater than M)
		 * Change to:                  (M >= P[i]) (M not_less than P[i])
		 */
		status =
		    acpi_ex_do_logical_op(AML_LLESS_OP, match_obj, package_obj,
					  &logical_result);
		if (ACPI_FAILURE(status)) {
			return (FALSE);
		}
		logical_result = (u8) ! logical_result;
		break;

	case MATCH_MLT:
		/*
		 * True if less than: (P[i] < M)
		 * Change to:         (M > P[i])
		 */
		status =
		    acpi_ex_do_logical_op(AML_LGREATER_OP, match_obj,
					  package_obj, &logical_result);
		if (ACPI_FAILURE(status)) {
			return (FALSE);
		}
		break;

	case MATCH_MGE:
		/*
		 * True if greater than or equal: (P[i] >= M) (P[i] not_less than M)
		 * Change to:                     (M <= P[i]) (M not_greater than P[i])
		 */
		status =
		    acpi_ex_do_logical_op(AML_LGREATER_OP, match_obj,
					  package_obj, &logical_result);
		if (ACPI_FAILURE(status)) {
			return (FALSE);
		}
		logical_result = (u8) ! logical_result;
		break;

	case MATCH_MGT:
		/*
		 * True if greater than: (P[i] > M)
		 * Change to:            (M < P[i])
		 */
		status =
		    acpi_ex_do_logical_op(AML_LLESS_OP, match_obj, package_obj,
					  &logical_result);
		if (ACPI_FAILURE(status)) {
			return (FALSE);
		}
		break;

	default:

		/* Undefined */

		return (FALSE);
	}

	return (logical_result);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_opcode_6A_0T_1R
 *
 * PARAMETERS:  walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with 6 arguments, no target, and a return value
 *
 ******************************************************************************/

acpi_status acpi_ex_opcode_6A_0T_1R(struct acpi_walk_state * walk_state)
{
	union acpi_operand_object **operand = &walk_state->operands[0];
	union acpi_operand_object *return_desc = NULL;
	acpi_status status = AE_OK;
	u64 index;
	union acpi_operand_object *this_element;

	ACPI_FUNCTION_TRACE_STR(ex_opcode_6A_0T_1R,
				acpi_ps_get_opcode_name(walk_state->opcode));

	switch (walk_state->opcode) {
	case AML_MATCH_OP:
		/*
		 * Match (search_pkg[0], match_op1[1], match_obj1[2],
		 *                      match_op2[3], match_obj2[4], start_index[5])
		 */

		/* Validate both Match Term Operators (MTR, MEQ, etc.) */

		if ((operand[1]->integer.value > MAX_MATCH_OPERATOR) ||
		    (operand[3]->integer.value > MAX_MATCH_OPERATOR)) {
			ACPI_ERROR((AE_INFO, "Match operator out of range"));
			status = AE_AML_OPERAND_VALUE;
			goto cleanup;
		}

		/* Get the package start_index, validate against the package length */

		index = operand[5]->integer.value;
		if (index >= operand[0]->package.count) {
			ACPI_ERROR((AE_INFO,
				    "Index (0x%8.8X%8.8X) beyond package end (0x%X)",
				    ACPI_FORMAT_UINT64(index),
				    operand[0]->package.count));
			status = AE_AML_PACKAGE_LIMIT;
			goto cleanup;
		}

		/* Create an integer for the return value */
		/* Default return value is ACPI_UINT64_MAX if no match found */

		return_desc = acpi_ut_create_integer_object(ACPI_UINT64_MAX);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;

		}

		/*
		 * Examine each element until a match is found. Both match conditions
		 * must be satisfied for a match to occur. Within the loop,
		 * "continue" signifies that the current element does not match
		 * and the next should be examined.
		 *
		 * Upon finding a match, the loop will terminate via "break" at
		 * the bottom. If it terminates "normally", match_value will be
		 * ACPI_UINT64_MAX (Ones) (its initial value) indicating that no
		 * match was found.
		 */
		for (; index < operand[0]->package.count; index++) {

			/* Get the current package element */

			this_element = operand[0]->package.elements[index];

			/* Treat any uninitialized (NULL) elements as non-matching */

			if (!this_element) {
				continue;
			}

			/*
			 * Both match conditions must be satisfied. Execution of a continue
			 * (proceed to next iteration of enclosing for loop) signifies a
			 * non-match.
			 */
			if (!acpi_ex_do_match((u32) operand[1]->integer.value,
					      this_element, operand[2])) {
				continue;
			}

			if (!acpi_ex_do_match((u32) operand[3]->integer.value,
					      this_element, operand[4])) {
				continue;
			}

			/* Match found: Index is the return value */

			return_desc->integer.value = index;
			break;
		}
		break;

	case AML_LOAD_TABLE_OP:

		status = acpi_ex_load_table_op(walk_state, &return_desc);
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
