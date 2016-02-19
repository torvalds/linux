/*******************************************************************************
 *
 * Module Name: utnonansi - Non-ansi C library functions
 *
 ******************************************************************************/

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

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utnonansi")

/*
 * Non-ANSI C library functions - strlwr, strupr, stricmp, and a 64-bit
 * version of strtoul.
 */
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strlwr (strlwr)
 *
 * PARAMETERS:  src_string      - The source string to convert
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert a string to lowercase
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
		*string = (char)tolower((int)*string);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strupr (strupr)
 *
 * PARAMETERS:  src_string      - The source string to convert
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert a string to uppercase
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
		*string = (char)toupper((int)*string);
	}
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
 * DESCRIPTION: Case-insensitive string compare. Implementation of the
 *              non-ANSI stricmp function.
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

	if (strlen(source) >= dest_size) {
		return (TRUE);
	}

	strcpy(dest, source);
	return (FALSE);
}

u8 acpi_ut_safe_strcat(char *dest, acpi_size dest_size, char *source)
{

	if ((strlen(dest) + strlen(source)) >= dest_size) {
		return (TRUE);
	}

	strcat(dest, source);
	return (FALSE);
}

u8
acpi_ut_safe_strncat(char *dest,
		     acpi_size dest_size,
		     char *source, acpi_size max_transfer_length)
{
	acpi_size actual_transfer_length;

	actual_transfer_length = ACPI_MIN(max_transfer_length, strlen(source));

	if ((strlen(dest) + actual_transfer_length) >= dest_size) {
		return (TRUE);
	}

	strncat(dest, source, max_transfer_length);
	return (FALSE);
}
#endif

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
 *
 * NOTES:       acpi_gbl_integer_byte_width should be set to the proper width.
 *              For the core ACPICA code, this width depends on the DSDT
 *              version. For iASL, the default byte width is always 8.
 *
 *              Does not support Octal strings, not needed at this time.
 *
 *              There is an earlier version of the function after this one,
 *              below. It is slightly different than this one, and the two
 *              may eventually may need to be merged. (01/2016).
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

	ACPI_FUNCTION_TRACE_STR(ut_strtoul64, string);

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

	while ((*string) && (isspace((int)*string) || *string == '\t')) {
		string++;
	}

	if (to_integer_op) {
		/*
		 * Base equal to ACPI_ANY_BASE means 'ToInteger operation case'.
		 * We need to determine if it is decimal or hexadecimal.
		 */
		if ((*string == '0') && (tolower((int)*(string + 1)) == 'x')) {
			sign_of0x = 1;
			base = 16;

			/* Skip over the leading '0x' */
			string += 2;
		} else {
			base = 10;
		}
	}

	/* Any string left? Check that '0x' is not followed by white space. */

	if (!(*string) || isspace((int)*string) || *string == '\t') {
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
		if (isdigit((int)*string)) {

			/* Convert ASCII 0-9 to Decimal value */

			this_digit = ((u8)*string) - '0';
		} else if (base == 10) {

			/* Digit is out of range; possible in to_integer case only */

			term = 1;
		} else {
			this_digit = (u8)toupper((int)*string);
			if (isxdigit((int)this_digit)) {

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

		(void)acpi_ut_short_divide((dividend - (u64)this_digit), base,
					   &quotient, NULL);

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

#ifdef _OBSOLETE_FUNCTIONS
/* TBD: use version in ACPICA main code base? */
/* DONE: 01/2016 */

/*******************************************************************************
 *
 * FUNCTION:    strtoul64
 *
 * PARAMETERS:  string              - Null terminated string
 *              terminater          - Where a pointer to the terminating byte
 *                                    is returned
 *              base                - Radix of the string
 *
 * RETURN:      Converted value
 *
 * DESCRIPTION: Convert a string into an unsigned value.
 *
 ******************************************************************************/

acpi_status strtoul64(char *string, u32 base, u64 *ret_integer)
{
	u32 index;
	u32 sign;
	u64 return_value = 0;
	acpi_status status = AE_OK;

	*ret_integer = 0;

	switch (base) {
	case 0:
	case 8:
	case 10:
	case 16:

		break;

	default:
		/*
		 * The specified Base parameter is not in the domain of
		 * this function:
		 */
		return (AE_BAD_PARAMETER);
	}

	/* Skip over any white space in the buffer: */

	while (isspace((int)*string) || *string == '\t') {
		++string;
	}

	/*
	 * The buffer may contain an optional plus or minus sign.
	 * If it does, then skip over it but remember what is was:
	 */
	if (*string == '-') {
		sign = ACPI_SIGN_NEGATIVE;
		++string;
	} else if (*string == '+') {
		++string;
		sign = ACPI_SIGN_POSITIVE;
	} else {
		sign = ACPI_SIGN_POSITIVE;
	}

	/*
	 * If the input parameter Base is zero, then we need to
	 * determine if it is octal, decimal, or hexadecimal:
	 */
	if (base == 0) {
		if (*string == '0') {
			if (tolower((int)*(++string)) == 'x') {
				base = 16;
				++string;
			} else {
				base = 8;
			}
		} else {
			base = 10;
		}
	}

	/*
	 * For octal and hexadecimal bases, skip over the leading
	 * 0 or 0x, if they are present.
	 */
	if (base == 8 && *string == '0') {
		string++;
	}

	if (base == 16 && *string == '0' && tolower((int)*(++string)) == 'x') {
		string++;
	}

	/* Main loop: convert the string to an unsigned long */

	while (*string) {
		if (isdigit((int)*string)) {
			index = ((u8)*string) - '0';
		} else {
			index = (u8)toupper((int)*string);
			if (isupper((int)index)) {
				index = index - 'A' + 10;
			} else {
				goto error_exit;
			}
		}

		if (index >= base) {
			goto error_exit;
		}

		/* Check to see if value is out of range: */

		if (return_value > ((ACPI_UINT64_MAX - (u64)index) / (u64)base)) {
			goto error_exit;
		} else {
			return_value *= base;
			return_value += index;
		}

		++string;
	}

	/* If a minus sign was present, then "the conversion is negated": */

	if (sign == ACPI_SIGN_NEGATIVE) {
		return_value = (ACPI_UINT32_MAX - return_value) + 1;
	}

	*ret_integer = return_value;
	return (status);

error_exit:
	switch (base) {
	case 8:

		status = AE_BAD_OCTAL_CONSTANT;
		break;

	case 10:

		status = AE_BAD_DECIMAL_CONSTANT;
		break;

	case 16:

		status = AE_BAD_HEX_CONSTANT;
		break;

	default:

		/* Base validated above */

		break;
	}

	return (status);
}
#endif
