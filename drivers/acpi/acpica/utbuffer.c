// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: utbuffer - Buffer dump routines
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utbuffer")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dump_buffer
 *
 * PARAMETERS:  buffer              - Buffer to dump
 *              count               - Amount to dump, in bytes
 *              display             - BYTE, WORD, DWORD, or QWORD display:
 *                                      DB_BYTE_DISPLAY
 *                                      DB_WORD_DISPLAY
 *                                      DB_DWORD_DISPLAY
 *                                      DB_QWORD_DISPLAY
 *              base_offset         - Beginning buffer offset (display only)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii.
 *
 ******************************************************************************/
void acpi_ut_dump_buffer(u8 *buffer, u32 count, u32 display, u32 base_offset)
{
	u32 i = 0;
	u32 j;
	u32 temp32;
	u8 buf_char;
	u32 display_data_only = display & DB_DISPLAY_DATA_ONLY;

	display &= ~DB_DISPLAY_DATA_ONLY;
	if (!buffer) {
		acpi_os_printf("Null Buffer Pointer in DumpBuffer!\n");
		return;
	}

	if ((count < 4) || (count & 0x01)) {
		display = DB_BYTE_DISPLAY;
	}

	/* Nasty little dump buffer routine! */

	while (i < count) {

		/* Print current offset */

		if (!display_data_only) {
			acpi_os_printf("%8.4X: ", (base_offset + i));
		}

		/* Print 16 hex chars */

		for (j = 0; j < 16;) {
			if (i + j >= count) {

				/* Dump fill spaces */

				acpi_os_printf("%*s", ((display * 2) + 1), " ");
				j += display;
				continue;
			}

			switch (display) {
			case DB_BYTE_DISPLAY:
			default:	/* Default is BYTE display */

				acpi_os_printf("%02X ",
					       buffer[(acpi_size)i + j]);
				break;

			case DB_WORD_DISPLAY:

				ACPI_MOVE_16_TO_32(&temp32,
						   &buffer[(acpi_size)i + j]);
				acpi_os_printf("%04X ", temp32);
				break;

			case DB_DWORD_DISPLAY:

				ACPI_MOVE_32_TO_32(&temp32,
						   &buffer[(acpi_size)i + j]);
				acpi_os_printf("%08X ", temp32);
				break;

			case DB_QWORD_DISPLAY:

				ACPI_MOVE_32_TO_32(&temp32,
						   &buffer[(acpi_size)i + j]);
				acpi_os_printf("%08X", temp32);

				ACPI_MOVE_32_TO_32(&temp32,
						   &buffer[(acpi_size)i + j +
							   4]);
				acpi_os_printf("%08X ", temp32);
				break;
			}

			j += display;
		}

		/*
		 * Print the ASCII equivalent characters but watch out for the bad
		 * unprintable ones (printable chars are 0x20 through 0x7E)
		 */
		if (!display_data_only) {
			acpi_os_printf(" ");
			for (j = 0; j < 16; j++) {
				if (i + j >= count) {
					acpi_os_printf("\n");
					return;
				}

				/*
				 * Add comment characters so rest of line is ignored when
				 * compiled
				 */
				if (j == 0) {
					acpi_os_printf("// ");
				}

				buf_char = buffer[(acpi_size)i + j];
				if (isprint(buf_char)) {
					acpi_os_printf("%c", buf_char);
				} else {
					acpi_os_printf(".");
				}
			}

			/* Done with that line. */

			acpi_os_printf("\n");
		}
		i += 16;
	}

	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_debug_dump_buffer
 *
 * PARAMETERS:  buffer              - Buffer to dump
 *              count               - Amount to dump, in bytes
 *              display             - BYTE, WORD, DWORD, or QWORD display:
 *                                      DB_BYTE_DISPLAY
 *                                      DB_WORD_DISPLAY
 *                                      DB_DWORD_DISPLAY
 *                                      DB_QWORD_DISPLAY
 *              component_ID        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii.
 *
 ******************************************************************************/

void
acpi_ut_debug_dump_buffer(u8 *buffer, u32 count, u32 display, u32 component_id)
{

	/* Only dump the buffer if tracing is enabled */

	if (!((ACPI_LV_TABLES & acpi_dbg_level) &&
	      (component_id & acpi_dbg_layer))) {
		return;
	}

	acpi_ut_dump_buffer(buffer, count, display, 0);
}

#ifdef ACPI_APPLICATION
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dump_buffer_to_file
 *
 * PARAMETERS:  file                - File descriptor
 *              buffer              - Buffer to dump
 *              count               - Amount to dump, in bytes
 *              display             - BYTE, WORD, DWORD, or QWORD display:
 *                                      DB_BYTE_DISPLAY
 *                                      DB_WORD_DISPLAY
 *                                      DB_DWORD_DISPLAY
 *                                      DB_QWORD_DISPLAY
 *              base_offset         - Beginning buffer offset (display only)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii to a file.
 *
 ******************************************************************************/

void
acpi_ut_dump_buffer_to_file(ACPI_FILE file,
			    u8 *buffer, u32 count, u32 display, u32 base_offset)
{
	u32 i = 0;
	u32 j;
	u32 temp32;
	u8 buf_char;

	if (!buffer) {
		fprintf(file, "Null Buffer Pointer in DumpBuffer!\n");
		return;
	}

	if ((count < 4) || (count & 0x01)) {
		display = DB_BYTE_DISPLAY;
	}

	/* Nasty little dump buffer routine! */

	while (i < count) {

		/* Print current offset */

		fprintf(file, "%8.4X: ", (base_offset + i));

		/* Print 16 hex chars */

		for (j = 0; j < 16;) {
			if (i + j >= count) {

				/* Dump fill spaces */

				fprintf(file, "%*s", ((display * 2) + 1), " ");
				j += display;
				continue;
			}

			switch (display) {
			case DB_BYTE_DISPLAY:
			default:	/* Default is BYTE display */

				fprintf(file, "%02X ",
					buffer[(acpi_size)i + j]);
				break;

			case DB_WORD_DISPLAY:

				ACPI_MOVE_16_TO_32(&temp32,
						   &buffer[(acpi_size)i + j]);
				fprintf(file, "%04X ", temp32);
				break;

			case DB_DWORD_DISPLAY:

				ACPI_MOVE_32_TO_32(&temp32,
						   &buffer[(acpi_size)i + j]);
				fprintf(file, "%08X ", temp32);
				break;

			case DB_QWORD_DISPLAY:

				ACPI_MOVE_32_TO_32(&temp32,
						   &buffer[(acpi_size)i + j]);
				fprintf(file, "%08X", temp32);

				ACPI_MOVE_32_TO_32(&temp32,
						   &buffer[(acpi_size)i + j +
							   4]);
				fprintf(file, "%08X ", temp32);
				break;
			}

			j += display;
		}

		/*
		 * Print the ASCII equivalent characters but watch out for the bad
		 * unprintable ones (printable chars are 0x20 through 0x7E)
		 */
		fprintf(file, " ");
		for (j = 0; j < 16; j++) {
			if (i + j >= count) {
				fprintf(file, "\n");
				return;
			}

			buf_char = buffer[(acpi_size)i + j];
			if (isprint(buf_char)) {
				fprintf(file, "%c", buf_char);
			} else {
				fprintf(file, ".");
			}
		}

		/* Done with that line. */

		fprintf(file, "\n");
		i += 16;
	}

	return;
}
#endif
