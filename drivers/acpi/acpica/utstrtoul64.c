// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: utstrtoul64 - String-to-integer conversion support for both
 *                            64-bit and 32-bit integers
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utstrtoul64")

/*******************************************************************************
 *
 * This module contains the top-level string to 64/32-bit unsigned integer
 * conversion functions:
 *
 *  1) A standard strtoul() function that supports 64-bit integers, base
 *     8/10/16, with integer overflow support. This is used mainly by the
 *     iASL compiler, which implements tighter constraints on integer
 *     constants than the runtime (interpreter) integer-to-string conversions.
 *  2) Runtime "Explicit conversion" as defined in the ACPI specification.
 *  3) Runtime "Implicit conversion" as defined in the ACPI specification.
 *
 * Current users of this module:
 *
 *  iASL        - Preprocessor (constants and math expressions)
 *  iASL        - Main parser, conversion of constants to integers
 *  iASL        - Data Table Compiler parser (constants and math expressions)
 *  interpreter - Implicit and explicit conversions, GPE method names
 *  interpreter - Repair code for return values from predefined names
 *  debugger    - Command line input string conversion
 *  acpi_dump   - ACPI table physical addresses
 *  acpi_exec   - Support for namespace overrides
 *
 * Notes concerning users of these interfaces:
 *
 * acpi_gbl_integer_byte_width is used to set the 32/64 bit limit for explicit
 * and implicit conversions. This global must be set to the proper width.
 * For the core ACPICA code, the width depends on the DSDT version. For the
 * acpi_ut_strtoul64 interface, all conversions are 64 bits. This interface is
 * used primarily for iASL, where the default width is 64 bits for all parsers,
 * but error checking is performed later to flag cases where a 64-bit constant
 * is wrongly defined in a 32-bit DSDT/SSDT.
 *
 * In ACPI, the only place where octal numbers are supported is within
 * the ASL language itself. This is implemented via the main acpi_ut_strtoul64
 * interface. According the ACPI specification, there is no ACPI runtime
 * support (explicit/implicit) for octal string conversions.
 *
 ******************************************************************************/
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strtoul64
 *
 * PARAMETERS:  string                  - Null terminated input string,
 *                                        must be a valid pointer
 *              return_value            - Where the converted integer is
 *                                        returned. Must be a valid pointer
 *
 * RETURN:      Status and converted integer. Returns an exception on a
 *              64-bit numeric overflow
 *
 * DESCRIPTION: Convert a string into an unsigned integer. Always performs a
 *              full 64-bit conversion, regardless of the current global
 *              integer width. Supports Decimal, Hex, and Octal strings.
 *
 * Current users of this function:
 *
 *  iASL        - Preprocessor (constants and math expressions)
 *  iASL        - Main ASL parser, conversion of ASL constants to integers
 *  iASL        - Data Table Compiler parser (constants and math expressions)
 *  interpreter - Repair code for return values from predefined names
 *  acpi_dump   - ACPI table physical addresses
 *  acpi_exec   - Support for namespace overrides
 *
 ******************************************************************************/
acpi_status acpi_ut_strtoul64(char *string, u64 *return_value)
{
	acpi_status status = AE_OK;
	u8 original_bit_width;
	u32 base = 10;		/* Default is decimal */

	ACPI_FUNCTION_TRACE_STR(ut_strtoul64, string);

	*return_value = 0;

	/* A NULL return string returns a value of zero */

	if (*string == 0) {
		return_ACPI_STATUS(AE_OK);
	}

	if (!acpi_ut_remove_whitespace(&string)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * 1) Check for a hex constant. A "0x" prefix indicates base 16.
	 */
	if (acpi_ut_detect_hex_prefix(&string)) {
		base = 16;
	}

	/*
	 * 2) Check for an octal constant, defined to be a leading zero
	 * followed by sequence of octal digits (0-7)
	 */
	else if (acpi_ut_detect_octal_prefix(&string)) {
		base = 8;
	}

	if (!acpi_ut_remove_leading_zeros(&string)) {
		return_ACPI_STATUS(AE_OK);	/* Return value 0 */
	}

	/*
	 * Force a full 64-bit conversion. The caller (usually iASL) must
	 * check for a 32-bit overflow later as necessary (If current mode
	 * is 32-bit, meaning a 32-bit DSDT).
	 */
	original_bit_width = acpi_gbl_integer_bit_width;
	acpi_gbl_integer_bit_width = 64;

	/*
	 * Perform the base 8, 10, or 16 conversion. A 64-bit numeric overflow
	 * will return an exception (to allow iASL to flag the statement).
	 */
	switch (base) {
	case 8:
		status = acpi_ut_convert_octal_string(string, return_value);
		break;

	case 10:
		status = acpi_ut_convert_decimal_string(string, return_value);
		break;

	case 16:
	default:
		status = acpi_ut_convert_hex_string(string, return_value);
		break;
	}

	/* Only possible exception from above is a 64-bit overflow */

	acpi_gbl_integer_bit_width = original_bit_width;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_implicit_strtoul64
 *
 * PARAMETERS:  string                  - Null terminated input string,
 *                                        must be a valid pointer
 *
 * RETURN:      Converted integer
 *
 * DESCRIPTION: Perform a 64-bit conversion with restrictions placed upon
 *              an "implicit conversion" by the ACPI specification. Used by
 *              many ASL operators that require an integer operand, and support
 *              an automatic (implicit) conversion from a string operand
 *              to the final integer operand. The major restriction is that
 *              only hex strings are supported.
 *
 * -----------------------------------------------------------------------------
 *
 * Base is always 16, either with or without the 0x prefix. Decimal and
 * Octal strings are not supported, as per the ACPI specification.
 *
 * Examples (both are hex values):
 *      Add ("BA98", Arg0, Local0)
 *      Subtract ("0x12345678", Arg1, Local1)
 *
 * Conversion rules as extracted from the ACPI specification:
 *
 *  The converted integer is initialized to the value zero.
 *  The ASCII string is always interpreted as a hexadecimal constant.
 *
 *  1)  According to the ACPI specification, a "0x" prefix is not allowed.
 *      However, ACPICA allows this as an ACPI extension on general
 *      principle. (NO ERROR)
 *
 *  2)  The conversion terminates when the size of an integer is reached
 *      (32 or 64 bits). There are no numeric overflow conditions. (NO ERROR)
 *
 *  3)  The first non-hex character terminates the conversion and returns
 *      the current accumulated value of the converted integer (NO ERROR).
 *
 *  4)  Conversion of a null (zero-length) string to an integer is
 *      technically not allowed. However, ACPICA allows this as an ACPI
 *      extension. The conversion returns the value 0. (NO ERROR)
 *
 * NOTE: There are no error conditions returned by this function. At
 * the minimum, a value of zero is returned.
 *
 * Current users of this function:
 *
 *  interpreter - All runtime implicit conversions, as per ACPI specification
 *  iASL        - Data Table Compiler parser (constants and math expressions)
 *
 ******************************************************************************/

u64 acpi_ut_implicit_strtoul64(char *string)
{
	u64 converted_integer = 0;

	ACPI_FUNCTION_TRACE_STR(ut_implicit_strtoul64, string);

	if (!acpi_ut_remove_whitespace(&string)) {
		return_VALUE(0);
	}

	/*
	 * Per the ACPI specification, only hexadecimal is supported for
	 * implicit conversions, and the "0x" prefix is "not allowed".
	 * However, allow a "0x" prefix as an ACPI extension.
	 */
	acpi_ut_remove_hex_prefix(&string);

	if (!acpi_ut_remove_leading_zeros(&string)) {
		return_VALUE(0);
	}

	/*
	 * Ignore overflow as per the ACPI specification. This is implemented by
	 * ignoring the return status from the conversion function called below.
	 * On overflow, the input string is simply truncated.
	 */
	acpi_ut_convert_hex_string(string, &converted_integer);
	return_VALUE(converted_integer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_explicit_strtoul64
 *
 * PARAMETERS:  string                  - Null terminated input string,
 *                                        must be a valid pointer
 *
 * RETURN:      Converted integer
 *
 * DESCRIPTION: Perform a 64-bit conversion with the restrictions placed upon
 *              an "explicit conversion" by the ACPI specification. The
 *              main restriction is that only hex and decimal are supported.
 *
 * -----------------------------------------------------------------------------
 *
 * Base is either 10 (default) or 16 (with 0x prefix). Octal (base 8) strings
 * are not supported, as per the ACPI specification.
 *
 * Examples:
 *      to_integer ("1000")     Decimal
 *      to_integer ("0xABCD")   Hex
 *
 * Conversion rules as extracted from the ACPI specification:
 *
 *  1)  The input string is either a decimal or hexadecimal numeric string.
 *      A hex value must be prefixed by "0x" or it is interpreted as decimal.
 *
 *  2)  The value must not exceed the maximum of an integer value
 *      (32 or 64 bits). The ACPI specification states the behavior is
 *      "unpredictable", so ACPICA matches the behavior of the implicit
 *      conversion case. There are no numeric overflow conditions. (NO ERROR)
 *
 *  3)  Behavior on the first non-hex character is not defined by the ACPI
 *      specification (for the to_integer operator), so ACPICA matches the
 *      behavior of the implicit conversion case. It terminates the
 *      conversion and returns the current accumulated value of the converted
 *      integer. (NO ERROR)
 *
 *  4)  Conversion of a null (zero-length) string to an integer is
 *      technically not allowed. However, ACPICA allows this as an ACPI
 *      extension. The conversion returns the value 0. (NO ERROR)
 *
 * NOTE: There are no error conditions returned by this function. At the
 * minimum, a value of zero is returned.
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

	if (!acpi_ut_remove_whitespace(&string)) {
		return_VALUE(0);
	}

	/*
	 * Only Hex and Decimal are supported, as per the ACPI specification.
	 * A "0x" prefix indicates hex; otherwise decimal is assumed.
	 */
	if (acpi_ut_detect_hex_prefix(&string)) {
		base = 16;
	}

	if (!acpi_ut_remove_leading_zeros(&string)) {
		return_VALUE(0);
	}

	/*
	 * Ignore overflow as per the ACPI specification. This is implemented by
	 * ignoring the return status from the conversion functions called below.
	 * On overflow, the input string is simply truncated.
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
