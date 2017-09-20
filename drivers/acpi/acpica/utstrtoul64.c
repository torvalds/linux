/*******************************************************************************
 *
 * Module Name: utstrtoul64 - string-to-integer support for both 64-bit
 *                            and 32-bit integers
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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
ACPI_MODULE_NAME("utstrtoul64")

/*******************************************************************************
 *
 * This module contains the external string to 64/32-bit unsigned integer
 * conversion functions:
 *
 *  1) Standard strtoul() function with 64-bit support. This is mostly used by
 *      the iASL compiler.
 *  2) Runtime "Explicit conversion" as defined in the ACPI specification.
 *  3) Runtime "Implicit conversion" as defined in the ACPI specification.
 *
 * Current users of this module:
 *
 *  interpreter - Implicit and explicit conversions, GPE method names
 *  debugger    - Command line input string conversion
 *  iASL        - Main parser, conversion of constants to integers
 *  iASL        - Data Table Compiler parser (constant math expressions)
 *  iASL        - Preprocessor (constant math expressions)
 *  acpi_dump   - Input table addresses
 *  acpi_exec   - Testing of the acpi_ut_strtoul64 function
 *
 * Notes concerning users of these interfaces:
 *
 *  acpi_gbl_integer_byte_width is used to set the 32/64 bit limit. This global
 *  must be set to the proper width. For the core ACPICA code, the width
 *  depends on the DSDT version. For iASL, the default width is 64 bits for
 *  all parsers, but error checking is performed later to flag cases where
 *  a 64-bit constant is wrongly defined in a 32-bit DSDT/SSDT.
 *
 *  In ACPI, the only place where octal numbers are supported is within
 *  the ASL language itself. There is no runtime support for octal.
 *
 ******************************************************************************/
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strtoul64
 *
 * PARAMETERS:  string                  - Null terminated input string.
 *                                        Must be a valid pointer
 *              return_value            - Where the converted integer is
 *                                        returned. Must be a valid pointer
 *
 * RETURN:      Status and converted integer
 *              Returns an exception on numeric overflow
 *
 * DESCRIPTION: Convert a string into an unsigned integer. Performs either a
 *              32-bit or 64-bit conversion, depending on the current global
 *              integer width. Supports Decimal, Hex, and Octal strings.
 *
 * Current users of this function:
 *
 *  iASL        - Preprocessor (constant math expressions)
 *  iASL        - Main parser, conversion of ASL constants to integers
 *  iASL        - Data Table Compiler parser (constant math expressions)
 *
 ******************************************************************************/
acpi_status acpi_ut_strtoul64(char *string, u64 *return_value)
{
	acpi_status status = AE_OK;
	u32 base = 10;		/* Default is decimal */

	ACPI_FUNCTION_TRACE_STR(ut_strtoul64, string);

	*return_value = 0;

	/* Null return string returns a value of zero */

	if (*string == 0) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * 1) The "0x" prefix indicates base 16. Per the ACPI specification,
	 * the "0x" prefix is only allowed for implicit (non-strict) conversions.
	 * However, we always allow it for compatibility with older ACPICA and
	 * just plain on principle.
	 */
	if (acpi_ut_detect_hex_prefix(&string)) {
		base = 16;
	}

	/*
	 * 2) Check for an octal constant, defined to be a leading zero
	 * followed by an valid octal digit (0-7)
	 */
	else if (acpi_ut_detect_octal_prefix(&string)) {
		base = 8;
	}

	if (!acpi_ut_remove_leading_zeros(&string)) {
		return_ACPI_STATUS(AE_OK);	/* Return value 0 */
	}

	/*
	 * Perform the base 8, 10, or 16 conversion. A numeric overflow will
	 * return an exception.
	 */
	switch (base) {
	case 8:
		status = acpi_ut_convert_octal_string(string, return_value);
		break;

	case 10:
		status = acpi_ut_convert_decimal_string(string, return_value);
		break;

	case 16:
		status = acpi_ut_convert_hex_string(string, return_value);
		break;

	default:
		status = AE_AML_INTERNAL;	/* Should never happen */
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_implicit_strtoul64
 *
 * PARAMETERS:  string                  - Null terminated input string.
 *                                        Must be a valid pointer
 *
 * RETURN:      Converted integer
 *
 * DESCRIPTION: Perform a 64-bit conversion with restrictions placed upon
 *              an "implicit conversion" by the ACPI specification. Used by
 *              many ASL operators that require an integer operand, and support
 *              an automatic (implicit) conversion from a string operand
 *              to the final integer operand. The restriction is that only
 *              hex strings are supported.
 *
 * -----------------------------------------------------------------------------
 *
 * Base is always 16, either with or without the 0x prefix.
 *
 * Examples (both are hex values):
 *      Add ("BA98", Arg0, Local0)
 *      Subtract ("0x12345678", Arg1, Local1)
 *
 * Rules extracted from the ACPI specification:
 *
 *  The converted integer is initialized to the value zero.
 *  The ASCII string is interpreted as a hexadecimal constant.
 *
 *  1)  A "0x" prefix is not allowed. However, ACPICA allows this as an
 *      ACPI extension on general principle. (NO ERROR)
 *
 *  2)  Terminates when the size of an integer is reached (32 or 64 bits).
 *      There are no numeric overflow conditions. (NO ERROR)
 *
 *  3)  The first non-hex character terminates the conversion and returns
 *      the current accumulated value of the converted integer (NO ERROR).
 *
 *  4)  Conversion of a null (zero-length) string to an integer is
 *      technically allowed. However, ACPICA allows as an ACPI extension.
 *      The conversion returns the value 0. (NO ERROR)
 *
 * Note: there are no error conditions returned by this function. At
 * the minimum, a value of zero is returned.
 *
 * Current users of this function:
 *
 *  interpreter - All runtime implicit conversions, as per ACPI specification
 *  iASL        - Data Table Compiler parser (constant math expressions)
 *
 ******************************************************************************/

u64 acpi_ut_implicit_strtoul64(char *string)
{
	u64 converted_integer = 0;

	ACPI_FUNCTION_TRACE_STR(ut_implicit_strtoul64, string);

	/*
	 * Per the ACPI specification, only hexadecimal is supported for
	 * implicit conversions, and the "0x" prefix is "not allowed".
	 * However, allow a "0x" prefix as an ACPI extension.
	 */
	acpi_ut_detect_hex_prefix(&string);

	if (!acpi_ut_remove_leading_zeros(&string)) {
		return_VALUE(0);
	}

	/*
	 * Ignore overflow as per the ACPI specification. This is implemented by
	 * ignoring the return status below. On overflow, the input string is
	 * simply truncated.
	 */
	acpi_ut_convert_hex_string(string, &converted_integer);
	return_VALUE(converted_integer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_explicit_strtoul64
 *
 * PARAMETERS:  string                  - Null terminated input string.
 *                                        Must be a valid pointer
 *
 * RETURN:      Converted integer
 *
 * DESCRIPTION: Perform a 64-bit conversion with the restrictions placed upon
 *              an "explicit conversion" by the ACPI specification. The
 *              main restriction is that only hex and decimal are supported.
 *
 * -----------------------------------------------------------------------------
 *
 * Base is either 10 (default) or 16 (with 0x prefix). There is no octal
 * (base 8), as per the ACPI specification.
 *
 * Examples:
 *      to_integer ("1000")     Decimal
 *      to_integer ("0xABCD")   Hex
 *
 * Rules extracted from the ACPI specification:
 *
 *  1)  Thi input string is either a decimal or hexadecimal numeric string.
 *      A hex value must be prefixed by "0x" or it is interpreted as decimal.
 *
 *  2)  The value must not exceed the maximum of an integer value
 *      (32 or 64 bits). The ACPI specification states the behavior is
 *      "unpredictable", so ACPICA matches the behavior of the implicit
 *      conversion case. There are no numeric overflow conditions. (NO ERROR)
 *
 *  3)  Behavior on the first non-hex character is not specified by the ACPI
 *      specification (for the to_integer operator), so ACPICA matches the
 *      behavior of the implicit conversion case. It terminates the
 *      conversion and returns the current accumulated value of the converted
 *      integer. (NO ERROR)
 *
 *  4)  Conversion of a null (zero-length) string to an integer is
 *      technically allowed. However, ACPICA allows as an ACPI extension.
 *      The conversion returns the value 0. (NO ERROR)
 *
 * Note: there are no error conditions returned by this function. At
 * the minimum, a value of zero is returned.
 *
 * Current users of this function:
 *
 *  interpreter - Runtime ASL to_integer operator, as per the ACPI specification
 *
 ******************************************************************************/

u64 acpi_ut_explicit_strtoul64(char *string)
{
	u64 converted_integer = 0;
	u32 base = 10;		/* Default is decimal */

	ACPI_FUNCTION_TRACE_STR(ut_explicit_strtoul64, string);

	/*
	 * Only Hex and Decimal are supported, as per the ACPI specification.
	 * 0x prefix means hex; otherwise decimal is assumed.
	 */
	if (acpi_ut_detect_hex_prefix(&string)) {
		base = 16;
	}

	if (!acpi_ut_remove_leading_zeros(&string)) {
		return_VALUE(0);
	}

	/*
	 * Ignore overflow as per the ACPI specification. This is implemented by
	 * ignoring the return status below. On overflow, the input string is
	 * simply truncated.
	 */
	switch (base) {
	case 10:
	default:
		acpi_ut_convert_decimal_string(string, &converted_integer);
		break;

	case 16:
		acpi_ut_convert_hex_string(string, &converted_integer);
		break;
	}

	return_VALUE(converted_integer);
}
