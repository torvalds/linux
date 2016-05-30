/******************************************************************************
 *
 * Module Name: utascii - Utility ascii functions
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

	for (i = 0; i < ACPI_NAME_SIZE; i++) {
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
