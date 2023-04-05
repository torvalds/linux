// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: utascii - Utility ascii functions
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_nameseg
 *
 * PARAMETERS:  name            - The name or table signature to be examined.
 *                                Four characters, does not have to be a
 *                                NULL terminated string.
 *
 * RETURN:      TRUE if signature is has 4 valid ACPI characters
 *
 * DESCRIPTION: Validate an ACPI table signature.
 *
 ******************************************************************************/

u8 acpi_ut_valid_nameseg(char *name)
{
	u32 i;

	/* Validate each character in the signature */

	for (i = 0; i < ACPI_NAMESEG_SIZE; i++) {
		if (!acpi_ut_valid_name_char(name[i], i)) {
			return (FALSE);
		}
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_name_char
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

u8 acpi_ut_valid_name_char(char character, u32 position)
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
 * FUNCTION:    acpi_ut_check_and_repair_ascii
 *
 * PARAMETERS:  name                - Ascii string
 *              count               - Number of characters to check
 *
 * RETURN:      None
 *
 * DESCRIPTION: Ensure that the requested number of characters are printable
 *              Ascii characters. Sets non-printable and null chars to <space>.
 *
 ******************************************************************************/

void acpi_ut_check_and_repair_ascii(u8 *name, char *repaired_name, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++) {
		repaired_name[i] = (char)name[i];

		if (!name[i]) {
			return;
		}
		if (!isprint(name[i])) {
			repaired_name[i] = ' ';
		}
	}
}
