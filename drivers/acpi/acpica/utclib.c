/******************************************************************************
 *
 * Module Name: cmclib - Local implementation of C library functions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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

/*
 * These implementations of standard C Library routines can optionally be
 * used if a C library is not available. In general, they are less efficient
 * than an inline or assembly implementation
 */

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("cmclib")

#ifndef ACPI_USE_SYSTEM_CLIBRARY
#define NEGATIVE    1
#define POSITIVE    0
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_memcmp (memcmp)
 *
 * PARAMETERS:  buffer1         - First Buffer
 *              buffer2         - Second Buffer
 *              count           - Maximum # of bytes to compare
 *
 * RETURN:      Index where Buffers mismatched, or 0 if Buffers matched
 *
 * DESCRIPTION: Compare two Buffers, with a maximum length
 *
 ******************************************************************************/
int acpi_ut_memcmp(const char *buffer1, const char *buffer2, acpi_size count)
{

	return ((count == ACPI_SIZE_MAX) ? 0 : ((unsigned char)*buffer1 -
						(unsigned char)*buffer2));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_memcpy (memcpy)
 *
 * PARAMETERS:  dest        - Target of the copy
 *              src         - Source buffer to copy
 *              count       - Number of bytes to copy
 *
 * RETURN:      Dest
 *
 * DESCRIPTION: Copy arbitrary bytes of memory
 *
 ******************************************************************************/

void *acpi_ut_memcpy(void *dest, const void *src, acpi_size count)
{
	char *new = (char *)dest;
	char *old = (char *)src;

	while (count) {
		*new = *old;
		new++;
		old++;
		count--;
	}

	return (dest);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_memset (memset)
 *
 * PARAMETERS:  dest        - Buffer to set
 *              value       - Value to set each byte of memory
 *              count       - Number of bytes to set
 *
 * RETURN:      Dest
 *
 * DESCRIPTION: Initialize a buffer to a known value.
 *
 ******************************************************************************/

void *acpi_ut_memset(void *dest, u8 value, acpi_size count)
{
	char *new = (char *)dest;

	while (count) {
		*new = (char)value;
		new++;
		count--;
	}

	return (dest);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strlen (strlen)
 *
 * PARAMETERS:  string              - Null terminated string
 *
 * RETURN:      Length
 *
 * DESCRIPTION: Returns the length of the input string
 *
 ******************************************************************************/

acpi_size acpi_ut_strlen(const char *string)
{
	u32 length = 0;

	/* Count the string until a null is encountered */

	while (*string) {
		length++;
		string++;
	}

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strcpy (strcpy)
 *
 * PARAMETERS:  dst_string      - Target of the copy
 *              src_string      - The source string to copy
 *
 * RETURN:      dst_string
 *
 * DESCRIPTION: Copy a null terminated string
 *
 ******************************************************************************/

char *acpi_ut_strcpy(char *dst_string, const char *src_string)
{
	char *string = dst_string;

	/* Move bytes brute force */

	while (*src_string) {
		*string = *src_string;

		string++;
		src_string++;
	}

	/* Null terminate */

	*string = 0;
	return (dst_string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strncpy (strncpy)
 *
 * PARAMETERS:  dst_string      - Target of the copy
 *              src_string      - The source string to copy
 *              count           - Maximum # of bytes to copy
 *
 * RETURN:      dst_string
 *
 * DESCRIPTION: Copy a null terminated string, with a maximum length
 *
 ******************************************************************************/

char *acpi_ut_strncpy(char *dst_string, const char *src_string, acpi_size count)
{
	char *string = dst_string;

	/* Copy the string */

	for (string = dst_string;
	     count && (count--, (*string++ = *src_string++));) {;
	}

	/* Pad with nulls if necessary */

	while (count--) {
		*string = 0;
		string++;
	}

	/* Return original pointer */

	return (dst_string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strcmp (strcmp)
 *
 * PARAMETERS:  string1         - First string
 *              string2         - Second string
 *
 * RETURN:      Index where strings mismatched, or 0 if strings matched
 *
 * DESCRIPTION: Compare two null terminated strings
 *
 ******************************************************************************/

int acpi_ut_strcmp(const char *string1, const char *string2)
{

	for (; (*string1 == *string2); string2++) {
		if (!*string1++) {
			return (0);
		}
	}

	return ((unsigned char)*string1 - (unsigned char)*string2);
}

#ifdef ACPI_FUTURE_IMPLEMENTATION
/* Not used at this time */
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strchr (strchr)
 *
 * PARAMETERS:  string          - Search string
 *              ch              - character to search for
 *
 * RETURN:      Ptr to char or NULL if not found
 *
 * DESCRIPTION: Search a string for a character
 *
 ******************************************************************************/

char *acpi_ut_strchr(const char *string, int ch)
{

	for (; (*string); string++) {
		if ((*string) == (char)ch) {
			return ((char *)string);
		}
	}

	return (NULL);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strncmp (strncmp)
 *
 * PARAMETERS:  string1         - First string
 *              string2         - Second string
 *              count           - Maximum # of bytes to compare
 *
 * RETURN:      Index where strings mismatched, or 0 if strings matched
 *
 * DESCRIPTION: Compare two null terminated strings, with a maximum length
 *
 ******************************************************************************/

int acpi_ut_strncmp(const char *string1, const char *string2, acpi_size count)
{

	for (; count-- && (*string1 == *string2); string2++) {
		if (!*string1++) {
			return (0);
		}
	}

	return ((count == ACPI_SIZE_MAX) ? 0 : ((unsigned char)*string1 -
						(unsigned char)*string2));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strcat (Strcat)
 *
 * PARAMETERS:  dst_string      - Target of the copy
 *              src_string      - The source string to copy
 *
 * RETURN:      dst_string
 *
 * DESCRIPTION: Append a null terminated string to a null terminated string
 *
 ******************************************************************************/

char *acpi_ut_strcat(char *dst_string, const char *src_string)
{
	char *string;

	/* Find end of the destination string */

	for (string = dst_string; *string++;) {;
	}

	/* Concatenate the string */

	for (--string; (*string++ = *src_string++);) {;
	}

	return (dst_string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strncat (strncat)
 *
 * PARAMETERS:  dst_string      - Target of the copy
 *              src_string      - The source string to copy
 *              count           - Maximum # of bytes to copy
 *
 * RETURN:      dst_string
 *
 * DESCRIPTION: Append a null terminated string to a null terminated string,
 *              with a maximum count.
 *
 ******************************************************************************/

char *acpi_ut_strncat(char *dst_string, const char *src_string, acpi_size count)
{
	char *string;

	if (count) {

		/* Find end of the destination string */

		for (string = dst_string; *string++;) {;
		}

		/* Concatenate the string */

		for (--string; (*string++ = *src_string++) && --count;) {;
		}

		/* Null terminate if necessary */

		if (!count) {
			*string = 0;
		}
	}

	return (dst_string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strstr (strstr)
 *
 * PARAMETERS:  string1         - Target string
 *              string2         - Substring to search for
 *
 * RETURN:      Where substring match starts, Null if no match found
 *
 * DESCRIPTION: Checks if String2 occurs in String1. This is not really a
 *              full implementation of strstr, only sufficient for command
 *              matching
 *
 ******************************************************************************/

char *acpi_ut_strstr(char *string1, char *string2)
{
	char *string;

	if (acpi_ut_strlen(string2) > acpi_ut_strlen(string1)) {
		return (NULL);
	}

	/* Walk entire string, comparing the letters */

	for (string = string1; *string2;) {
		if (*string2 != *string) {
			return (NULL);
		}

		string2++;
		string++;
	}

	return (string1);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strtoul (strtoul)
 *
 * PARAMETERS:  string          - Null terminated string
 *              terminater      - Where a pointer to the terminating byte is
 *                                returned
 *              base            - Radix of the string
 *
 * RETURN:      Converted value
 *
 * DESCRIPTION: Convert a string into a 32-bit unsigned value.
 *              Note: use acpi_ut_strtoul64 for 64-bit integers.
 *
 ******************************************************************************/

u32 acpi_ut_strtoul(const char *string, char **terminator, u32 base)
{
	u32 converted = 0;
	u32 index;
	u32 sign;
	const char *string_start;
	u32 return_value = 0;
	acpi_status status = AE_OK;

	/*
	 * Save the value of the pointer to the buffer's first
	 * character, save the current errno value, and then
	 * skip over any white space in the buffer:
	 */
	string_start = string;
	while (ACPI_IS_SPACE(*string) || *string == '\t') {
		++string;
	}

	/*
	 * The buffer may contain an optional plus or minus sign.
	 * If it does, then skip over it but remember what is was:
	 */
	if (*string == '-') {
		sign = NEGATIVE;
		++string;
	} else if (*string == '+') {
		++string;
		sign = POSITIVE;
	} else {
		sign = POSITIVE;
	}

	/*
	 * If the input parameter Base is zero, then we need to
	 * determine if it is octal, decimal, or hexadecimal:
	 */
	if (base == 0) {
		if (*string == '0') {
			if (acpi_ut_to_lower(*(++string)) == 'x') {
				base = 16;
				++string;
			} else {
				base = 8;
			}
		} else {
			base = 10;
		}
	} else if (base < 2 || base > 36) {
		/*
		 * The specified Base parameter is not in the domain of
		 * this function:
		 */
		goto done;
	}

	/*
	 * For octal and hexadecimal bases, skip over the leading
	 * 0 or 0x, if they are present.
	 */
	if (base == 8 && *string == '0') {
		string++;
	}

	if (base == 16 &&
	    *string == '0' && acpi_ut_to_lower(*(++string)) == 'x') {
		string++;
	}

	/*
	 * Main loop: convert the string to an unsigned long:
	 */
	while (*string) {
		if (ACPI_IS_DIGIT(*string)) {
			index = (u32)((u8)*string - '0');
		} else {
			index = (u32)acpi_ut_to_upper(*string);
			if (ACPI_IS_UPPER(index)) {
				index = index - 'A' + 10;
			} else {
				goto done;
			}
		}

		if (index >= base) {
			goto done;
		}

		/*
		 * Check to see if value is out of range:
		 */

		if (return_value > ((ACPI_UINT32_MAX - (u32)index) / (u32)base)) {
			status = AE_ERROR;
			return_value = 0;	/* reset */
		} else {
			return_value *= base;
			return_value += index;
			converted = 1;
		}

		++string;
	}

      done:
	/*
	 * If appropriate, update the caller's pointer to the next
	 * unconverted character in the buffer.
	 */
	if (terminator) {
		if (converted == 0 && return_value == 0 && string != NULL) {
			*terminator = (char *)string_start;
		} else {
			*terminator = (char *)string;
		}
	}

	if (status == AE_ERROR) {
		return_value = ACPI_UINT32_MAX;
	}

	/*
	 * If a minus sign was present, then "the conversion is negated":
	 */
	if (sign == NEGATIVE) {
		return_value = (ACPI_UINT32_MAX - return_value) + 1;
	}

	return (return_value);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_to_upper (TOUPPER)
 *
 * PARAMETERS:  c           - Character to convert
 *
 * RETURN:      Converted character as an int
 *
 * DESCRIPTION: Convert character to uppercase
 *
 ******************************************************************************/

int acpi_ut_to_upper(int c)
{

	return (ACPI_IS_LOWER(c) ? ((c) - 0x20) : (c));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_to_lower (TOLOWER)
 *
 * PARAMETERS:  c           - Character to convert
 *
 * RETURN:      Converted character as an int
 *
 * DESCRIPTION: Convert character to lowercase
 *
 ******************************************************************************/

int acpi_ut_to_lower(int c)
{

	return (ACPI_IS_UPPER(c) ? ((c) + 0x20) : (c));
}

/*******************************************************************************
 *
 * FUNCTION:    is* functions
 *
 * DESCRIPTION: is* functions use the ctype table below
 *
 ******************************************************************************/

const u8 _acpi_ctype[257] = {
	_ACPI_CN,		/* 0x00     0 NUL */
	_ACPI_CN,		/* 0x01     1 SOH */
	_ACPI_CN,		/* 0x02     2 STX */
	_ACPI_CN,		/* 0x03     3 ETX */
	_ACPI_CN,		/* 0x04     4 EOT */
	_ACPI_CN,		/* 0x05     5 ENQ */
	_ACPI_CN,		/* 0x06     6 ACK */
	_ACPI_CN,		/* 0x07     7 BEL */
	_ACPI_CN,		/* 0x08     8 BS  */
	_ACPI_CN | _ACPI_SP,	/* 0x09     9 TAB */
	_ACPI_CN | _ACPI_SP,	/* 0x0A    10 LF  */
	_ACPI_CN | _ACPI_SP,	/* 0x0B    11 VT  */
	_ACPI_CN | _ACPI_SP,	/* 0x0C    12 FF  */
	_ACPI_CN | _ACPI_SP,	/* 0x0D    13 CR  */
	_ACPI_CN,		/* 0x0E    14 SO  */
	_ACPI_CN,		/* 0x0F    15 SI  */
	_ACPI_CN,		/* 0x10    16 DLE */
	_ACPI_CN,		/* 0x11    17 DC1 */
	_ACPI_CN,		/* 0x12    18 DC2 */
	_ACPI_CN,		/* 0x13    19 DC3 */
	_ACPI_CN,		/* 0x14    20 DC4 */
	_ACPI_CN,		/* 0x15    21 NAK */
	_ACPI_CN,		/* 0x16    22 SYN */
	_ACPI_CN,		/* 0x17    23 ETB */
	_ACPI_CN,		/* 0x18    24 CAN */
	_ACPI_CN,		/* 0x19    25 EM  */
	_ACPI_CN,		/* 0x1A    26 SUB */
	_ACPI_CN,		/* 0x1B    27 ESC */
	_ACPI_CN,		/* 0x1C    28 FS  */
	_ACPI_CN,		/* 0x1D    29 GS  */
	_ACPI_CN,		/* 0x1E    30 RS  */
	_ACPI_CN,		/* 0x1F    31 US  */
	_ACPI_XS | _ACPI_SP,	/* 0x20    32 ' ' */
	_ACPI_PU,		/* 0x21    33 '!' */
	_ACPI_PU,		/* 0x22    34 '"' */
	_ACPI_PU,		/* 0x23    35 '#' */
	_ACPI_PU,		/* 0x24    36 '$' */
	_ACPI_PU,		/* 0x25    37 '%' */
	_ACPI_PU,		/* 0x26    38 '&' */
	_ACPI_PU,		/* 0x27    39 ''' */
	_ACPI_PU,		/* 0x28    40 '(' */
	_ACPI_PU,		/* 0x29    41 ')' */
	_ACPI_PU,		/* 0x2A    42 '*' */
	_ACPI_PU,		/* 0x2B    43 '+' */
	_ACPI_PU,		/* 0x2C    44 ',' */
	_ACPI_PU,		/* 0x2D    45 '-' */
	_ACPI_PU,		/* 0x2E    46 '.' */
	_ACPI_PU,		/* 0x2F    47 '/' */
	_ACPI_XD | _ACPI_DI,	/* 0x30    48 '0' */
	_ACPI_XD | _ACPI_DI,	/* 0x31    49 '1' */
	_ACPI_XD | _ACPI_DI,	/* 0x32    50 '2' */
	_ACPI_XD | _ACPI_DI,	/* 0x33    51 '3' */
	_ACPI_XD | _ACPI_DI,	/* 0x34    52 '4' */
	_ACPI_XD | _ACPI_DI,	/* 0x35    53 '5' */
	_ACPI_XD | _ACPI_DI,	/* 0x36    54 '6' */
	_ACPI_XD | _ACPI_DI,	/* 0x37    55 '7' */
	_ACPI_XD | _ACPI_DI,	/* 0x38    56 '8' */
	_ACPI_XD | _ACPI_DI,	/* 0x39    57 '9' */
	_ACPI_PU,		/* 0x3A    58 ':' */
	_ACPI_PU,		/* 0x3B    59 ';' */
	_ACPI_PU,		/* 0x3C    60 '<' */
	_ACPI_PU,		/* 0x3D    61 '=' */
	_ACPI_PU,		/* 0x3E    62 '>' */
	_ACPI_PU,		/* 0x3F    63 '?' */
	_ACPI_PU,		/* 0x40    64 '@' */
	_ACPI_XD | _ACPI_UP,	/* 0x41    65 'A' */
	_ACPI_XD | _ACPI_UP,	/* 0x42    66 'B' */
	_ACPI_XD | _ACPI_UP,	/* 0x43    67 'C' */
	_ACPI_XD | _ACPI_UP,	/* 0x44    68 'D' */
	_ACPI_XD | _ACPI_UP,	/* 0x45    69 'E' */
	_ACPI_XD | _ACPI_UP,	/* 0x46    70 'F' */
	_ACPI_UP,		/* 0x47    71 'G' */
	_ACPI_UP,		/* 0x48    72 'H' */
	_ACPI_UP,		/* 0x49    73 'I' */
	_ACPI_UP,		/* 0x4A    74 'J' */
	_ACPI_UP,		/* 0x4B    75 'K' */
	_ACPI_UP,		/* 0x4C    76 'L' */
	_ACPI_UP,		/* 0x4D    77 'M' */
	_ACPI_UP,		/* 0x4E    78 'N' */
	_ACPI_UP,		/* 0x4F    79 'O' */
	_ACPI_UP,		/* 0x50    80 'P' */
	_ACPI_UP,		/* 0x51    81 'Q' */
	_ACPI_UP,		/* 0x52    82 'R' */
	_ACPI_UP,		/* 0x53    83 'S' */
	_ACPI_UP,		/* 0x54    84 'T' */
	_ACPI_UP,		/* 0x55    85 'U' */
	_ACPI_UP,		/* 0x56    86 'V' */
	_ACPI_UP,		/* 0x57    87 'W' */
	_ACPI_UP,		/* 0x58    88 'X' */
	_ACPI_UP,		/* 0x59    89 'Y' */
	_ACPI_UP,		/* 0x5A    90 'Z' */
	_ACPI_PU,		/* 0x5B    91 '[' */
	_ACPI_PU,		/* 0x5C    92 '\' */
	_ACPI_PU,		/* 0x5D    93 ']' */
	_ACPI_PU,		/* 0x5E    94 '^' */
	_ACPI_PU,		/* 0x5F    95 '_' */
	_ACPI_PU,		/* 0x60    96 '`' */
	_ACPI_XD | _ACPI_LO,	/* 0x61    97 'a' */
	_ACPI_XD | _ACPI_LO,	/* 0x62    98 'b' */
	_ACPI_XD | _ACPI_LO,	/* 0x63    99 'c' */
	_ACPI_XD | _ACPI_LO,	/* 0x64   100 'd' */
	_ACPI_XD | _ACPI_LO,	/* 0x65   101 'e' */
	_ACPI_XD | _ACPI_LO,	/* 0x66   102 'f' */
	_ACPI_LO,		/* 0x67   103 'g' */
	_ACPI_LO,		/* 0x68   104 'h' */
	_ACPI_LO,		/* 0x69   105 'i' */
	_ACPI_LO,		/* 0x6A   106 'j' */
	_ACPI_LO,		/* 0x6B   107 'k' */
	_ACPI_LO,		/* 0x6C   108 'l' */
	_ACPI_LO,		/* 0x6D   109 'm' */
	_ACPI_LO,		/* 0x6E   110 'n' */
	_ACPI_LO,		/* 0x6F   111 'o' */
	_ACPI_LO,		/* 0x70   112 'p' */
	_ACPI_LO,		/* 0x71   113 'q' */
	_ACPI_LO,		/* 0x72   114 'r' */
	_ACPI_LO,		/* 0x73   115 's' */
	_ACPI_LO,		/* 0x74   116 't' */
	_ACPI_LO,		/* 0x75   117 'u' */
	_ACPI_LO,		/* 0x76   118 'v' */
	_ACPI_LO,		/* 0x77   119 'w' */
	_ACPI_LO,		/* 0x78   120 'x' */
	_ACPI_LO,		/* 0x79   121 'y' */
	_ACPI_LO,		/* 0x7A   122 'z' */
	_ACPI_PU,		/* 0x7B   123 '{' */
	_ACPI_PU,		/* 0x7C   124 '|' */
	_ACPI_PU,		/* 0x7D   125 '}' */
	_ACPI_PU,		/* 0x7E   126 '~' */
	_ACPI_CN,		/* 0x7F   127 DEL */

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0x80 to 0x8F    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0x90 to 0x9F    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0xA0 to 0xAF    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0xB0 to 0xBF    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0xC0 to 0xCF    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0xD0 to 0xDF    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0xE0 to 0xEF    */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 0xF0 to 0xFF    */
	0			/* 0x100 */
};

#endif				/* ACPI_USE_SYSTEM_CLIBRARY */
