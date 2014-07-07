/*******************************************************************************
 *
 * Module Name: utstring - Common functions for strings and characters
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utstring")

/*
 * Non-ANSI C library functions - strlwr, strupr, stricmp, and a 64-bit
 * version of strtoul.
 */
#ifdef ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strlwr (strlwr)
 *
 * PARAMETERS:  src_string      - The source string to convert
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert string to lowercase
 *
 * NOTE: This is not a POSIX function, so it appears here, not in utclib.c
 *
 ******************************************************************************/
void acpi_ut_strlwr(char *src_string)
{
	char *string;

	ACPI_FUNCTION_ENTRY();

	if (!src_string) {
		return;
	}

	/* Walk entire string, lowercasing the letters */

	for (string = src_string; *string; string++) {
		*string = (char)ACPI_TOLOWER(*string);
	}

	return;
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ut_stricmp (stricmp)
 *
 * PARAMETERS:  string1             - first string to compare
 *              string2             - second string to compare
 *
 * RETURN:      int that signifies string relationship. Zero means strings
 *              are equal.
 *
 * DESCRIPTION: Implementation of the non-ANSI stricmp function (compare
 *              strings with no case sensitivity)
 *
 ******************************************************************************/

int acpi_ut_stricmp(char *string1, char *string2)
{
	int c1;
	int c2;

	do {
		c1 = tolower((int)*string1);
		c2 = tolower((int)*string2);

		string1++;
		string2++;
	}
	while ((c1 == c2) && (c1));

	return (c1 - c2);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strupr (strupr)
 *
 * PARAMETERS:  src_string      - The source string to convert
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert string to uppercase
 *
 * NOTE: This is not a POSIX function, so it appears here, not in utclib.c
 *
 ******************************************************************************/

void acpi_ut_strupr(char *src_string)
{
	char *string;

	ACPI_FUNCTION_ENTRY();

	if (!src_string) {
		return;
	}

	/* Walk entire string, uppercasing the letters */

	for (string = src_string; *string; string++) {
		*string = (char)ACPI_TOUPPER(*string);
	}

	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strtoul64
 *
 * PARAMETERS:  string          - Null terminated string
 *              base            - Radix of the string: 16 or ACPI_ANY_BASE;
 *                                ACPI_ANY_BASE means 'in behalf of to_integer'
 *              ret_integer     - Where the converted integer is returned
 *
 * RETURN:      Status and Converted value
 *
 * DESCRIPTION: Convert a string into an unsigned value. Performs either a
 *              32-bit or 64-bit conversion, depending on the current mode
 *              of the interpreter.
 *              NOTE: Does not support Octal strings, not needed.
 *
 ******************************************************************************/

acpi_status acpi_ut_strtoul64(char *string, u32 base, u64 *ret_integer)
{
	u32 this_digit = 0;
	u64 return_value = 0;
	u64 quotient;
	u64 dividend;
	u32 to_integer_op = (base == ACPI_ANY_BASE);
	u32 mode32 = (acpi_gbl_integer_byte_width == 4);
	u8 valid_digits = 0;
	u8 sign_of0x = 0;
	u8 term = 0;

	ACPI_FUNCTION_TRACE_STR(ut_stroul64, string);

	switch (base) {
	case ACPI_ANY_BASE:
	case 16:

		break;

	default:

		/* Invalid Base */

		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!string) {
		goto error_exit;
	}

	/* Skip over any white space in the buffer */

	while ((*string) && (ACPI_IS_SPACE(*string) || *string == '\t')) {
		string++;
	}

	if (to_integer_op) {
		/*
		 * Base equal to ACPI_ANY_BASE means 'ToInteger operation case'.
		 * We need to determine if it is decimal or hexadecimal.
		 */
		if ((*string == '0') && (ACPI_TOLOWER(*(string + 1)) == 'x')) {
			sign_of0x = 1;
			base = 16;

			/* Skip over the leading '0x' */
			string += 2;
		} else {
			base = 10;
		}
	}

	/* Any string left? Check that '0x' is not followed by white space. */

	if (!(*string) || ACPI_IS_SPACE(*string) || *string == '\t') {
		if (to_integer_op) {
			goto error_exit;
		} else {
			goto all_done;
		}
	}

	/*
	 * Perform a 32-bit or 64-bit conversion, depending upon the current
	 * execution mode of the interpreter
	 */
	dividend = (mode32) ? ACPI_UINT32_MAX : ACPI_UINT64_MAX;

	/* Main loop: convert the string to a 32- or 64-bit integer */

	while (*string) {
		if (ACPI_IS_DIGIT(*string)) {

			/* Convert ASCII 0-9 to Decimal value */

			this_digit = ((u8)*string) - '0';
		} else if (base == 10) {

			/* Digit is out of range; possible in to_integer case only */

			term = 1;
		} else {
			this_digit = (u8)ACPI_TOUPPER(*string);
			if (ACPI_IS_XDIGIT((char)this_digit)) {

				/* Convert ASCII Hex char to value */

				this_digit = this_digit - 'A' + 10;
			} else {
				term = 1;
			}
		}

		if (term) {
			if (to_integer_op) {
				goto error_exit;
			} else {
				break;
			}
		} else if ((valid_digits == 0) && (this_digit == 0)
			   && !sign_of0x) {

			/* Skip zeros */
			string++;
			continue;
		}

		valid_digits++;

		if (sign_of0x
		    && ((valid_digits > 16)
			|| ((valid_digits > 8) && mode32))) {
			/*
			 * This is to_integer operation case.
			 * No any restrictions for string-to-integer conversion,
			 * see ACPI spec.
			 */
			goto error_exit;
		}

		/* Divide the digit into the correct position */

		(void)acpi_ut_short_divide((dividend - (u64)this_digit),
					   base, &quotient, NULL);

		if (return_value > quotient) {
			if (to_integer_op) {
				goto error_exit;
			} else {
				break;
			}
		}

		return_value *= base;
		return_value += this_digit;
		string++;
	}

	/* All done, normal exit */

all_done:

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Converted value: %8.8X%8.8X\n",
			  ACPI_FORMAT_UINT64(return_value)));

	*ret_integer = return_value;
	return_ACPI_STATUS(AE_OK);

error_exit:
	/* Base was set/validated above */

	if (base == 10) {
		return_ACPI_STATUS(AE_BAD_DECIMAL_CONSTANT);
	} else {
		return_ACPI_STATUS(AE_BAD_HEX_CONSTANT);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_print_string
 *
 * PARAMETERS:  string          - Null terminated ASCII string
 *              max_length      - Maximum output length. Used to constrain the
 *                                length of strings during debug output only.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump an ASCII string with support for ACPI-defined escape
 *              sequences.
 *
 ******************************************************************************/

void acpi_ut_print_string(char *string, u16 max_length)
{
	u32 i;

	if (!string) {
		acpi_os_printf("<\"NULL STRING PTR\">");
		return;
	}

	acpi_os_printf("\"");
	for (i = 0; (i < max_length) && string[i]; i++) {

		/* Escape sequences */

		switch (string[i]) {
		case 0x07:

			acpi_os_printf("\\a");	/* BELL */
			break;

		case 0x08:

			acpi_os_printf("\\b");	/* BACKSPACE */
			break;

		case 0x0C:

			acpi_os_printf("\\f");	/* FORMFEED */
			break;

		case 0x0A:

			acpi_os_printf("\\n");	/* LINEFEED */
			break;

		case 0x0D:

			acpi_os_printf("\\r");	/* CARRIAGE RETURN */
			break;

		case 0x09:

			acpi_os_printf("\\t");	/* HORIZONTAL TAB */
			break;

		case 0x0B:

			acpi_os_printf("\\v");	/* VERTICAL TAB */
			break;

		case '\'':	/* Single Quote */
		case '\"':	/* Double Quote */
		case '\\':	/* Backslash */

			acpi_os_printf("\\%c", (int)string[i]);
			break;

		default:

			/* Check for printable character or hex escape */

			if (ACPI_IS_PRINT(string[i])) {
				/* This is a normal character */

				acpi_os_printf("%c", (int)string[i]);
			} else {
				/* All others will be Hex escapes */

				acpi_os_printf("\\x%2.2X", (s32) string[i]);
			}
			break;
		}
	}
	acpi_os_printf("\"");

	if (i == max_length && string[i]) {
		acpi_os_printf("...");
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_acpi_char
 *
 * PARAMETERS:  char            - The character to be examined
 *              position        - Byte position (0-3)
 *
 * RETURN:      TRUE if the character is valid, FALSE otherwise
 *
 * DESCRIPTION: Check for a valid ACPI character. Must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 *              We allow a '!' as the last character because of the ASF! table
 *
 ******************************************************************************/

u8 acpi_ut_valid_acpi_char(char character, u32 position)
{

	if (!((character >= 'A' && character <= 'Z') ||
	      (character >= '0' && character <= '9') || (character == '_'))) {

		/* Allow a '!' in the last position */

		if (character == '!' && position == 3) {
			return (TRUE);
		}

		return (FALSE);
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_acpi_name
 *
 * PARAMETERS:  name            - The name to be examined. Does not have to
 *                                be NULL terminated string.
 *
 * RETURN:      TRUE if the name is valid, FALSE otherwise
 *
 * DESCRIPTION: Check for a valid ACPI name. Each character must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 ******************************************************************************/

u8 acpi_ut_valid_acpi_name(char *name)
{
	u32 i;

	ACPI_FUNCTION_ENTRY();

	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		if (!acpi_ut_valid_acpi_char(name[i], i)) {
			return (FALSE);
		}
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_repair_name
 *
 * PARAMETERS:  name            - The ACPI name to be repaired
 *
 * RETURN:      Repaired version of the name
 *
 * DESCRIPTION: Repair an ACPI name: Change invalid characters to '*' and
 *              return the new name. NOTE: the Name parameter must reside in
 *              read/write memory, cannot be a const.
 *
 * An ACPI Name must consist of valid ACPI characters. We will repair the name
 * if necessary because we don't want to abort because of this, but we want
 * all namespace names to be printable. A warning message is appropriate.
 *
 * This issue came up because there are in fact machines that exhibit
 * this problem, and we want to be able to enable ACPI support for them,
 * even though there are a few bad names.
 *
 ******************************************************************************/

void acpi_ut_repair_name(char *name)
{
	u32 i;
	u8 found_bad_char = FALSE;
	u32 original_name;

	ACPI_FUNCTION_NAME(ut_repair_name);

	ACPI_MOVE_NAME(&original_name, name);

	/* Check each character in the name */

	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		if (acpi_ut_valid_acpi_char(name[i], i)) {
			continue;
		}

		/*
		 * Replace a bad character with something printable, yet technically
		 * still invalid. This prevents any collisions with existing "good"
		 * names in the namespace.
		 */
		name[i] = '*';
		found_bad_char = TRUE;
	}

	if (found_bad_char) {

		/* Report warning only if in strict mode or debug mode */

		if (!acpi_gbl_enable_interpreter_slack) {
			ACPI_WARNING((AE_INFO,
				      "Invalid character(s) in name (0x%.8X), repaired: [%4.4s]",
				      original_name, name));
		} else {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Invalid character(s) in name (0x%.8X), repaired: [%4.4s]",
					  original_name, name));
		}
	}
}

#if defined ACPI_ASL_COMPILER || defined ACPI_EXEC_APP
/*******************************************************************************
 *
 * FUNCTION:    ut_convert_backslashes
 *
 * PARAMETERS:  pathname        - File pathname string to be converted
 *
 * RETURN:      Modifies the input Pathname
 *
 * DESCRIPTION: Convert all backslashes (0x5C) to forward slashes (0x2F) within
 *              the entire input file pathname string.
 *
 ******************************************************************************/

void ut_convert_backslashes(char *pathname)
{

	if (!pathname) {
		return;
	}

	while (*pathname) {
		if (*pathname == '\\') {
			*pathname = '/';
		}

		pathname++;
	}
}
#endif

#if defined (ACPI_DEBUGGER) || defined (ACPI_APPLICATION)
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_safe_strcpy, acpi_ut_safe_strcat, acpi_ut_safe_strncat
 *
 * PARAMETERS:  Adds a "DestSize" parameter to each of the standard string
 *              functions. This is the size of the Destination buffer.
 *
 * RETURN:      TRUE if the operation would overflow the destination buffer.
 *
 * DESCRIPTION: Safe versions of standard Clib string functions. Ensure that
 *              the result of the operation will not overflow the output string
 *              buffer.
 *
 * NOTE:        These functions are typically only helpful for processing
 *              user input and command lines. For most ACPICA code, the
 *              required buffer length is precisely calculated before buffer
 *              allocation, so the use of these functions is unnecessary.
 *
 ******************************************************************************/

u8 acpi_ut_safe_strcpy(char *dest, acpi_size dest_size, char *source)
{

	if (ACPI_STRLEN(source) >= dest_size) {
		return (TRUE);
	}

	ACPI_STRCPY(dest, source);
	return (FALSE);
}

u8 acpi_ut_safe_strcat(char *dest, acpi_size dest_size, char *source)
{

	if ((ACPI_STRLEN(dest) + ACPI_STRLEN(source)) >= dest_size) {
		return (TRUE);
	}

	ACPI_STRCAT(dest, source);
	return (FALSE);
}

u8
acpi_ut_safe_strncat(char *dest,
		     acpi_size dest_size,
		     char *source, acpi_size max_transfer_length)
{
	acpi_size actual_transfer_length;

	actual_transfer_length =
	    ACPI_MIN(max_transfer_length, ACPI_STRLEN(source));

	if ((ACPI_STRLEN(dest) + actual_transfer_length) >= dest_size) {
		return (TRUE);
	}

	ACPI_STRNCAT(dest, source, max_transfer_length);
	return (FALSE);
}
#endif
