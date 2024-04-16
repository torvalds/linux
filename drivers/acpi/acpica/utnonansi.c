// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: utnonansi - Non-ansi C library functions
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utnonansi")

/*
 * Non-ANSI C library functions - strlwr, strupr, stricmp, and "safe"
 * string functions.
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

#if defined (ACPI_DEBUGGER) || defined (ACPI_APPLICATION) || defined (ACPI_DEBUG_OUTPUT)
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

void acpi_ut_safe_strncpy(char *dest, char *source, acpi_size dest_size)
{
	/* Always terminate destination string */

	strncpy(dest, source, dest_size);
	dest[dest_size - 1] = 0;
}

#endif
