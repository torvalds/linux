
/******************************************************************************
 *
 * Module Name: exutils - interpreter/scanner utilities
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

/*
 * DEFINE_AML_GLOBALS is tested in amlcode.h
 * to determine whether certain global names should be "defined" or only
 * "declared" in the current compilation.  This enhances maintainability
 * by enabling a single header file to embody all knowledge of the names
 * in question.
 *
 * Exactly one module of any executable should #define DEFINE_GLOBALS
 * before #including the header files which use this convention.  The
 * names in question will be defined and initialized in that module,
 * and declared as extern in all other modules which #include those
 * header files.
 */

#define DEFINE_AML_GLOBALS

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exutils")

/* Local prototypes */
static u32 acpi_ex_digits_needed(u64 value, u32 base);

#ifndef ACPI_NO_METHOD_EXECUTION
/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_enter_interpreter
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Enter the interpreter execution region. Failure to enter
 *              the interpreter region is a fatal system error. Used in
 *              conjunction with exit_interpreter.
 *
 ******************************************************************************/

void acpi_ex_enter_interpreter(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ex_enter_interpreter);

	status = acpi_ut_acquire_mutex(ACPI_MTX_INTERPRETER);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO,
			    "Could not acquire AML Interpreter mutex"));
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_reacquire_interpreter
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Reacquire the interpreter execution region from within the
 *              interpreter code. Failure to enter the interpreter region is a
 *              fatal system error. Used in  conjunction with
 *              relinquish_interpreter
 *
 ******************************************************************************/

void acpi_ex_reacquire_interpreter(void)
{
	ACPI_FUNCTION_TRACE(ex_reacquire_interpreter);

	/*
	 * If the global serialized flag is set, do not release the interpreter,
	 * since it was not actually released by acpi_ex_relinquish_interpreter.
	 * This forces the interpreter to be single threaded.
	 */
	if (!acpi_gbl_all_methods_serialized) {
		acpi_ex_enter_interpreter();
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_exit_interpreter
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Exit the interpreter execution region. This is the top level
 *              routine used to exit the interpreter when all processing has
 *              been completed.
 *
 ******************************************************************************/

void acpi_ex_exit_interpreter(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ex_exit_interpreter);

	status = acpi_ut_release_mutex(ACPI_MTX_INTERPRETER);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO,
			    "Could not release AML Interpreter mutex"));
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_relinquish_interpreter
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Exit the interpreter execution region, from within the
 *              interpreter - before attempting an operation that will possibly
 *              block the running thread.
 *
 * Cases where the interpreter is unlocked internally
 *      1) Method to be blocked on a Sleep() AML opcode
 *      2) Method to be blocked on an Acquire() AML opcode
 *      3) Method to be blocked on a Wait() AML opcode
 *      4) Method to be blocked to acquire the global lock
 *      5) Method to be blocked waiting to execute a serialized control method
 *          that is currently executing
 *      6) About to invoke a user-installed opregion handler
 *
 ******************************************************************************/

void acpi_ex_relinquish_interpreter(void)
{
	ACPI_FUNCTION_TRACE(ex_relinquish_interpreter);

	/*
	 * If the global serialized flag is set, do not release the interpreter.
	 * This forces the interpreter to be single threaded.
	 */
	if (!acpi_gbl_all_methods_serialized) {
		acpi_ex_exit_interpreter();
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_truncate_for32bit_table
 *
 * PARAMETERS:  obj_desc        - Object to be truncated
 *
 * RETURN:      none
 *
 * DESCRIPTION: Truncate an ACPI Integer to 32 bits if the execution mode is
 *              32-bit, as determined by the revision of the DSDT.
 *
 ******************************************************************************/

void acpi_ex_truncate_for32bit_table(union acpi_operand_object *obj_desc)
{

	ACPI_FUNCTION_ENTRY();

	/*
	 * Object must be a valid number and we must be executing
	 * a control method. NS node could be there for AML_INT_NAMEPATH_OP.
	 */
	if ((!obj_desc) ||
	    (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) != ACPI_DESC_TYPE_OPERAND) ||
	    (obj_desc->common.type != ACPI_TYPE_INTEGER)) {
		return;
	}

	if (acpi_gbl_integer_byte_width == 4) {
		/*
		 * We are running a method that exists in a 32-bit ACPI table.
		 * Truncate the value to 32 bits by zeroing out the upper 32-bit field
		 */
		obj_desc->integer.value &= (u64) ACPI_UINT32_MAX;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_acquire_global_lock
 *
 * PARAMETERS:  field_flags           - Flags with Lock rule:
 *                                      always_lock or never_lock
 *
 * RETURN:      None
 *
 * DESCRIPTION: Obtain the ACPI hardware Global Lock, only if the field
 *              flags specifiy that it is to be obtained before field access.
 *
 ******************************************************************************/

void acpi_ex_acquire_global_lock(u32 field_flags)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ex_acquire_global_lock);

	/* Only use the lock if the always_lock bit is set */

	if (!(field_flags & AML_FIELD_LOCK_RULE_MASK)) {
		return_VOID;
	}

	/* Attempt to get the global lock, wait forever */

	status = acpi_ex_acquire_mutex_object(ACPI_WAIT_FOREVER,
					      acpi_gbl_global_lock_mutex,
					      acpi_os_get_thread_id());

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not acquire Global Lock"));
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_release_global_lock
 *
 * PARAMETERS:  field_flags           - Flags with Lock rule:
 *                                      always_lock or never_lock
 *
 * RETURN:      None
 *
 * DESCRIPTION: Release the ACPI hardware Global Lock
 *
 ******************************************************************************/

void acpi_ex_release_global_lock(u32 field_flags)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ex_release_global_lock);

	/* Only use the lock if the always_lock bit is set */

	if (!(field_flags & AML_FIELD_LOCK_RULE_MASK)) {
		return_VOID;
	}

	/* Release the global lock */

	status = acpi_ex_release_mutex_object(acpi_gbl_global_lock_mutex);
	if (ACPI_FAILURE(status)) {

		/* Report the error, but there isn't much else we can do */

		ACPI_EXCEPTION((AE_INFO, status,
				"Could not release Global Lock"));
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_digits_needed
 *
 * PARAMETERS:  Value           - Value to be represented
 *              Base            - Base of representation
 *
 * RETURN:      The number of digits.
 *
 * DESCRIPTION: Calculate the number of digits needed to represent the Value
 *              in the given Base (Radix)
 *
 ******************************************************************************/

static u32 acpi_ex_digits_needed(u64 value, u32 base)
{
	u32 num_digits;
	u64 current_value;

	ACPI_FUNCTION_TRACE(ex_digits_needed);

	/* u64 is unsigned, so we don't worry about a '-' prefix */

	if (value == 0) {
		return_UINT32(1);
	}

	current_value = value;
	num_digits = 0;

	/* Count the digits in the requested base */

	while (current_value) {
		(void)acpi_ut_short_divide(current_value, base, &current_value,
					   NULL);
		num_digits++;
	}

	return_UINT32(num_digits);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_eisa_id_to_string
 *
 * PARAMETERS:  compressed_id   - EISAID to be converted
 *              out_string      - Where to put the converted string (8 bytes)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert a numeric EISAID to string representation. Return
 *              buffer must be large enough to hold the string. The string
 *              returned is always exactly of length ACPI_EISAID_STRING_SIZE
 *              (includes null terminator). The EISAID is always 32 bits.
 *
 ******************************************************************************/

void acpi_ex_eisa_id_to_string(char *out_string, u64 compressed_id)
{
	u32 swapped_id;

	ACPI_FUNCTION_ENTRY();

	/* The EISAID should be a 32-bit integer */

	if (compressed_id > ACPI_UINT32_MAX) {
		ACPI_WARNING((AE_INFO,
			      "Expected EISAID is larger than 32 bits: 0x%8.8X%8.8X, truncating",
			      ACPI_FORMAT_UINT64(compressed_id)));
	}

	/* Swap ID to big-endian to get contiguous bits */

	swapped_id = acpi_ut_dword_byte_swap((u32)compressed_id);

	/* First 3 bytes are uppercase letters. Next 4 bytes are hexadecimal */

	out_string[0] =
	    (char)(0x40 + (((unsigned long)swapped_id >> 26) & 0x1F));
	out_string[1] = (char)(0x40 + ((swapped_id >> 21) & 0x1F));
	out_string[2] = (char)(0x40 + ((swapped_id >> 16) & 0x1F));
	out_string[3] = acpi_ut_hex_to_ascii_char((u64) swapped_id, 12);
	out_string[4] = acpi_ut_hex_to_ascii_char((u64) swapped_id, 8);
	out_string[5] = acpi_ut_hex_to_ascii_char((u64) swapped_id, 4);
	out_string[6] = acpi_ut_hex_to_ascii_char((u64) swapped_id, 0);
	out_string[7] = 0;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_integer_to_string
 *
 * PARAMETERS:  out_string      - Where to put the converted string. At least
 *                                21 bytes are needed to hold the largest
 *                                possible 64-bit integer.
 *              Value           - Value to be converted
 *
 * RETURN:      None, string
 *
 * DESCRIPTION: Convert a 64-bit integer to decimal string representation.
 *              Assumes string buffer is large enough to hold the string. The
 *              largest string is (ACPI_MAX64_DECIMAL_DIGITS + 1).
 *
 ******************************************************************************/

void acpi_ex_integer_to_string(char *out_string, u64 value)
{
	u32 count;
	u32 digits_needed;
	u32 remainder;

	ACPI_FUNCTION_ENTRY();

	digits_needed = acpi_ex_digits_needed(value, 10);
	out_string[digits_needed] = 0;

	for (count = digits_needed; count > 0; count--) {
		(void)acpi_ut_short_divide(value, 10, &value, &remainder);
		out_string[count - 1] = (char)('0' + remainder);
	}
}

#endif
