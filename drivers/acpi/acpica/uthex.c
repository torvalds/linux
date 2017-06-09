/******************************************************************************
 *
 * Module Name: uthex -- Hex/ASCII support functions
 *
 *****************************************************************************/

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

#define _COMPONENT          ACPI_COMPILER
ACPI_MODULE_NAME("uthex")

/* Hex to ASCII conversion table */
static const char acpi_gbl_hex_to_ascii[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D',
	    'E', 'F'
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_hex_to_ascii_char
 *
 * PARAMETERS:  integer             - Contains the hex digit
 *              position            - bit position of the digit within the
 *                                    integer (multiple of 4)
 *
 * RETURN:      The converted Ascii character
 *
 * DESCRIPTION: Convert a hex digit to an Ascii character
 *
 ******************************************************************************/

char acpi_ut_hex_to_ascii_char(u64 integer, u32 position)
{

	return (acpi_gbl_hex_to_ascii[(integer >> position) & 0xF]);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_ascii_to_hex_byte
 *
 * PARAMETERS:  two_ascii_chars             - Pointer to two ASCII characters
 *              return_byte                 - Where converted byte is returned
 *
 * RETURN:      Status and converted hex byte
 *
 * DESCRIPTION: Perform ascii-to-hex translation, exactly two ASCII characters
 *              to a single converted byte value.
 *
 ******************************************************************************/

acpi_status acpi_ut_ascii_to_hex_byte(char *two_ascii_chars, u8 *return_byte)
{

	/* Both ASCII characters must be valid hex digits */

	if (!isxdigit((int)two_ascii_chars[0]) ||
	    !isxdigit((int)two_ascii_chars[1])) {
		return (AE_BAD_HEX_CONSTANT);
	}

	*return_byte =
	    acpi_ut_ascii_char_to_hex(two_ascii_chars[1]) |
	    (acpi_ut_ascii_char_to_hex(two_ascii_chars[0]) << 4);

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_ascii_char_to_hex
 *
 * PARAMETERS:  hex_char                - Hex character in Ascii. Must be:
 *                                        0-9 or A-F or a-f
 *
 * RETURN:      The binary value of the ascii/hex character
 *
 * DESCRIPTION: Perform ascii-to-hex translation
 *
 ******************************************************************************/

u8 acpi_ut_ascii_char_to_hex(int hex_char)
{

	/* Values 0-9 */

	if (hex_char <= '9') {
		return ((u8)(hex_char - '0'));
	}

	/* Upper case A-F */

	if (hex_char <= 'F') {
		return ((u8)(hex_char - 0x37));
	}

	/* Lower case a-f */

	return ((u8)(hex_char - 0x57));
}
