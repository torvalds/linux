/******************************************************************************
 *
 * Module Name: utprint - Formatted printing routines
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

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utprint")

#define ACPI_FORMAT_SIGN            0x01
#define ACPI_FORMAT_SIGN_PLUS       0x02
#define ACPI_FORMAT_SIGN_PLUS_SPACE 0x04
#define ACPI_FORMAT_ZERO            0x08
#define ACPI_FORMAT_LEFT            0x10
#define ACPI_FORMAT_UPPER           0x20
#define ACPI_FORMAT_PREFIX          0x40
/* Local prototypes */
static acpi_size
acpi_ut_bound_string_length(const char *string, acpi_size count);

static char *acpi_ut_bound_string_output(char *string, const char *end, char c);

static char *acpi_ut_format_number(char *string,
				   char *end,
				   u64 number,
				   u8 base, s32 width, s32 precision, u8 type);

static char *acpi_ut_put_number(char *string, u64 number, u8 base, u8 upper);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_bound_string_length
 *
 * PARAMETERS:  string              - String with boundary
 *              count               - Boundary of the string
 *
 * RETURN:      Length of the string. Less than or equal to Count.
 *
 * DESCRIPTION: Calculate the length of a string with boundary.
 *
 ******************************************************************************/

static acpi_size
acpi_ut_bound_string_length(const char *string, acpi_size count)
{
	u32 length = 0;

	while (*string && count) {
		length++;
		string++;
		count--;
	}

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_bound_string_output
 *
 * PARAMETERS:  string              - String with boundary
 *              end                 - Boundary of the string
 *              c                   - Character to be output to the string
 *
 * RETURN:      Updated position for next valid character
 *
 * DESCRIPTION: Output a character into a string with boundary check.
 *
 ******************************************************************************/

static char *acpi_ut_bound_string_output(char *string, const char *end, char c)
{

	if (string < end) {
		*string = c;
	}

	++string;
	return (string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_put_number
 *
 * PARAMETERS:  string              - Buffer to hold reverse-ordered string
 *              number              - Integer to be converted
 *              base                - Base of the integer
 *              upper               - Whether or not using upper cased digits
 *
 * RETURN:      Updated position for next valid character
 *
 * DESCRIPTION: Convert an integer into a string, note that, the string holds a
 *              reversed ordered number without the trailing zero.
 *
 ******************************************************************************/

static char *acpi_ut_put_number(char *string, u64 number, u8 base, u8 upper)
{
	const char *digits;
	u64 digit_index;
	char *pos;

	pos = string;
	digits = upper ? acpi_gbl_upper_hex_digits : acpi_gbl_lower_hex_digits;

	if (number == 0) {
		*(pos++) = '0';
	} else {
		while (number) {
			(void)acpi_ut_divide(number, base, &number,
					     &digit_index);
			*(pos++) = digits[digit_index];
		}
	}

	/* *(Pos++) = '0'; */
	return (pos);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_scan_number
 *
 * PARAMETERS:  string              - String buffer
 *              number_ptr          - Where the number is returned
 *
 * RETURN:      Updated position for next valid character
 *
 * DESCRIPTION: Scan a string for a decimal integer.
 *
 ******************************************************************************/

const char *acpi_ut_scan_number(const char *string, u64 *number_ptr)
{
	u64 number = 0;

	while (isdigit((int)*string)) {
		number *= 10;
		number += *(string++) - '0';
	}

	*number_ptr = number;
	return (string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_print_number
 *
 * PARAMETERS:  string              - String buffer
 *              number              - The number to be converted
 *
 * RETURN:      Updated position for next valid character
 *
 * DESCRIPTION: Print a decimal integer into a string.
 *
 ******************************************************************************/

const char *acpi_ut_print_number(char *string, u64 number)
{
	char ascii_string[20];
	const char *pos1;
	char *pos2;

	pos1 = acpi_ut_put_number(ascii_string, number, 10, FALSE);
	pos2 = string;

	while (pos1 != ascii_string) {
		*(pos2++) = *(--pos1);
	}

	*pos2 = 0;
	return (string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_format_number
 *
 * PARAMETERS:  string              - String buffer with boundary
 *              end                 - Boundary of the string
 *              number              - The number to be converted
 *              base                - Base of the integer
 *              width               - Field width
 *              precision           - Precision of the integer
 *              type                - Special printing flags
 *
 * RETURN:      Updated position for next valid character
 *
 * DESCRIPTION: Print an integer into a string with any base and any precision.
 *
 ******************************************************************************/

static char *acpi_ut_format_number(char *string,
				   char *end,
				   u64 number,
				   u8 base, s32 width, s32 precision, u8 type)
{
	char *pos;
	char sign;
	char zero;
	u8 need_prefix;
	u8 upper;
	s32 i;
	char reversed_string[66];

	/* Parameter validation */

	if (base < 2 || base > 16) {
		return (NULL);
	}

	if (type & ACPI_FORMAT_LEFT) {
		type &= ~ACPI_FORMAT_ZERO;
	}

	need_prefix = ((type & ACPI_FORMAT_PREFIX)
		       && base != 10) ? TRUE : FALSE;
	upper = (type & ACPI_FORMAT_UPPER) ? TRUE : FALSE;
	zero = (type & ACPI_FORMAT_ZERO) ? '0' : ' ';

	/* Calculate size according to sign and prefix */

	sign = '\0';
	if (type & ACPI_FORMAT_SIGN) {
		if ((s64)number < 0) {
			sign = '-';
			number = -(s64)number;
			width--;
		} else if (type & ACPI_FORMAT_SIGN_PLUS) {
			sign = '+';
			width--;
		} else if (type & ACPI_FORMAT_SIGN_PLUS_SPACE) {
			sign = ' ';
			width--;
		}
	}
	if (need_prefix) {
		width--;
		if (base == 16) {
			width--;
		}
	}

	/* Generate full string in reverse order */

	pos = acpi_ut_put_number(reversed_string, number, base, upper);
	i = ACPI_PTR_DIFF(pos, reversed_string);

	/* Printing 100 using %2d gives "100", not "00" */

	if (i > precision) {
		precision = i;
	}

	width -= precision;

	/* Output the string */

	if (!(type & (ACPI_FORMAT_ZERO | ACPI_FORMAT_LEFT))) {
		while (--width >= 0) {
			string = acpi_ut_bound_string_output(string, end, ' ');
		}
	}
	if (sign) {
		string = acpi_ut_bound_string_output(string, end, sign);
	}
	if (need_prefix) {
		string = acpi_ut_bound_string_output(string, end, '0');
		if (base == 16) {
			string =
			    acpi_ut_bound_string_output(string, end,
							upper ? 'X' : 'x');
		}
	}
	if (!(type & ACPI_FORMAT_LEFT)) {
		while (--width >= 0) {
			string = acpi_ut_bound_string_output(string, end, zero);
		}
	}

	while (i <= --precision) {
		string = acpi_ut_bound_string_output(string, end, '0');
	}
	while (--i >= 0) {
		string = acpi_ut_bound_string_output(string, end,
						     reversed_string[i]);
	}
	while (--width >= 0) {
		string = acpi_ut_bound_string_output(string, end, ' ');
	}

	return (string);
}

/*******************************************************************************
 *
 * FUNCTION:    vsnprintf
 *
 * PARAMETERS:  string              - String with boundary
 *              size                - Boundary of the string
 *              format              - Standard printf format
 *              args                - Argument list
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to a string using argument list pointer.
 *
 ******************************************************************************/

int vsnprintf(char *string, acpi_size size, const char *format, va_list args)
{
	u8 base;
	u8 type;
	s32 width;
	s32 precision;
	char qualifier;
	u64 number;
	char *pos;
	char *end;
	char c;
	const char *s;
	const void *p;
	s32 length;
	int i;

	pos = string;
	end = string + size;

	for (; *format; ++format) {
		if (*format != '%') {
			pos = acpi_ut_bound_string_output(pos, end, *format);
			continue;
		}

		type = 0;
		base = 10;

		/* Process sign */

		do {
			++format;
			if (*format == '#') {
				type |= ACPI_FORMAT_PREFIX;
			} else if (*format == '0') {
				type |= ACPI_FORMAT_ZERO;
			} else if (*format == '+') {
				type |= ACPI_FORMAT_SIGN_PLUS;
			} else if (*format == ' ') {
				type |= ACPI_FORMAT_SIGN_PLUS_SPACE;
			} else if (*format == '-') {
				type |= ACPI_FORMAT_LEFT;
			} else {
				break;
			}

		} while (1);

		/* Process width */

		width = -1;
		if (isdigit((int)*format)) {
			format = acpi_ut_scan_number(format, &number);
			width = (s32)number;
		} else if (*format == '*') {
			++format;
			width = va_arg(args, int);
			if (width < 0) {
				width = -width;
				type |= ACPI_FORMAT_LEFT;
			}
		}

		/* Process precision */

		precision = -1;
		if (*format == '.') {
			++format;
			if (isdigit((int)*format)) {
				format = acpi_ut_scan_number(format, &number);
				precision = (s32)number;
			} else if (*format == '*') {
				++format;
				precision = va_arg(args, int);
			}

			if (precision < 0) {
				precision = 0;
			}
		}

		/* Process qualifier */

		qualifier = -1;
		if (*format == 'h' || *format == 'l' || *format == 'L') {
			qualifier = *format;
			++format;

			if (qualifier == 'l' && *format == 'l') {
				qualifier = 'L';
				++format;
			}
		}

		switch (*format) {
		case '%':

			pos = acpi_ut_bound_string_output(pos, end, '%');
			continue;

		case 'c':

			if (!(type & ACPI_FORMAT_LEFT)) {
				while (--width > 0) {
					pos =
					    acpi_ut_bound_string_output(pos,
									end,
									' ');
				}
			}

			c = (char)va_arg(args, int);
			pos = acpi_ut_bound_string_output(pos, end, c);

			while (--width > 0) {
				pos =
				    acpi_ut_bound_string_output(pos, end, ' ');
			}
			continue;

		case 's':

			s = va_arg(args, char *);
			if (!s) {
				s = "<NULL>";
			}
			length = acpi_ut_bound_string_length(s, precision);
			if (!(type & ACPI_FORMAT_LEFT)) {
				while (length < width--) {
					pos =
					    acpi_ut_bound_string_output(pos,
									end,
									' ');
				}
			}

			for (i = 0; i < length; ++i) {
				pos = acpi_ut_bound_string_output(pos, end, *s);
				++s;
			}

			while (length < width--) {
				pos =
				    acpi_ut_bound_string_output(pos, end, ' ');
			}
			continue;

		case 'o':

			base = 8;
			break;

		case 'X':

			type |= ACPI_FORMAT_UPPER;

		case 'x':

			base = 16;
			break;

		case 'd':
		case 'i':

			type |= ACPI_FORMAT_SIGN;

		case 'u':

			break;

		case 'p':

			if (width == -1) {
				width = 2 * sizeof(void *);
				type |= ACPI_FORMAT_ZERO;
			}

			p = va_arg(args, void *);
			pos =
			    acpi_ut_format_number(pos, end, ACPI_TO_INTEGER(p),
						  16, width, precision, type);
			continue;

		default:

			pos = acpi_ut_bound_string_output(pos, end, '%');
			if (*format) {
				pos =
				    acpi_ut_bound_string_output(pos, end,
								*format);
			} else {
				--format;
			}
			continue;
		}

		if (qualifier == 'L') {
			number = va_arg(args, u64);
			if (type & ACPI_FORMAT_SIGN) {
				number = (s64)number;
			}
		} else if (qualifier == 'l') {
			number = va_arg(args, unsigned long);
			if (type & ACPI_FORMAT_SIGN) {
				number = (s32)number;
			}
		} else if (qualifier == 'h') {
			number = (u16)va_arg(args, int);
			if (type & ACPI_FORMAT_SIGN) {
				number = (s16)number;
			}
		} else {
			number = va_arg(args, unsigned int);
			if (type & ACPI_FORMAT_SIGN) {
				number = (signed int)number;
			}
		}

		pos = acpi_ut_format_number(pos, end, number, base,
					    width, precision, type);
	}

	if (size > 0) {
		if (pos < end) {
			*pos = '\0';
		} else {
			end[-1] = '\0';
		}
	}

	return (ACPI_PTR_DIFF(pos, string));
}

/*******************************************************************************
 *
 * FUNCTION:    snprintf
 *
 * PARAMETERS:  string              - String with boundary
 *              size                - Boundary of the string
 *              Format, ...         - Standard printf format
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to a string.
 *
 ******************************************************************************/

int snprintf(char *string, acpi_size size, const char *format, ...)
{
	va_list args;
	int length;

	va_start(args, format);
	length = vsnprintf(string, size, format, args);
	va_end(args);

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    sprintf
 *
 * PARAMETERS:  string              - String with boundary
 *              Format, ...         - Standard printf format
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to a string.
 *
 ******************************************************************************/

int sprintf(char *string, const char *format, ...)
{
	va_list args;
	int length;

	va_start(args, format);
	length = vsnprintf(string, ACPI_UINT32_MAX, format, args);
	va_end(args);

	return (length);
}

#ifdef ACPI_APPLICATION
/*******************************************************************************
 *
 * FUNCTION:    vprintf
 *
 * PARAMETERS:  format              - Standard printf format
 *              args                - Argument list
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to stdout using argument list pointer.
 *
 ******************************************************************************/

int vprintf(const char *format, va_list args)
{
	acpi_cpu_flags flags;
	int length;

	flags = acpi_os_acquire_lock(acpi_gbl_print_lock);
	length = vsnprintf(acpi_gbl_print_buffer,
			   sizeof(acpi_gbl_print_buffer), format, args);

	(void)fwrite(acpi_gbl_print_buffer, length, 1, ACPI_FILE_OUT);
	acpi_os_release_lock(acpi_gbl_print_lock, flags);

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    printf
 *
 * PARAMETERS:  Format, ...         - Standard printf format
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to stdout.
 *
 ******************************************************************************/

int printf(const char *format, ...)
{
	va_list args;
	int length;

	va_start(args, format);
	length = vprintf(format, args);
	va_end(args);

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    vfprintf
 *
 * PARAMETERS:  file                - File descriptor
 *              format              - Standard printf format
 *              args                - Argument list
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to a file using argument list pointer.
 *
 ******************************************************************************/

int vfprintf(FILE * file, const char *format, va_list args)
{
	acpi_cpu_flags flags;
	int length;

	flags = acpi_os_acquire_lock(acpi_gbl_print_lock);
	length = vsnprintf(acpi_gbl_print_buffer,
			   sizeof(acpi_gbl_print_buffer), format, args);

	(void)fwrite(acpi_gbl_print_buffer, length, 1, file);
	acpi_os_release_lock(acpi_gbl_print_lock, flags);

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    fprintf
 *
 * PARAMETERS:  file                - File descriptor
 *              Format, ...         - Standard printf format
 *
 * RETURN:      Number of bytes actually written.
 *
 * DESCRIPTION: Formatted output to a file.
 *
 ******************************************************************************/

int fprintf(FILE * file, const char *format, ...)
{
	va_list args;
	int length;

	va_start(args, format);
	length = vfprintf(file, format, args);
	va_end(args);

	return (length);
}
#endif
