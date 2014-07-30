/*******************************************************************************
 *
 * Module Name: utfileio - simple file I/O routines
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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
#include "actables.h"
#include "acapps.h"

#ifdef ACPI_ASL_COMPILER
#include "aslcompiler.h"
#endif

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("utfileio")

#ifdef ACPI_APPLICATION
/* Local prototypes */
static acpi_status
acpi_ut_check_text_mode_corruption(u8 *table,
				   u32 table_length, u32 file_length);

static acpi_status
acpi_ut_read_table(FILE * fp,
		   struct acpi_table_header **table, u32 *table_length);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_check_text_mode_corruption
 *
 * PARAMETERS:  table           - Table buffer
 *              table_length    - Length of table from the table header
 *              file_length     - Length of the file that contains the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check table for text mode file corruption where all linefeed
 *              characters (LF) have been replaced by carriage return linefeed
 *              pairs (CR/LF).
 *
 ******************************************************************************/

static acpi_status
acpi_ut_check_text_mode_corruption(u8 *table, u32 table_length, u32 file_length)
{
	u32 i;
	u32 pairs = 0;

	if (table_length != file_length) {
		ACPI_WARNING((AE_INFO,
			      "File length (0x%X) is not the same as the table length (0x%X)",
			      file_length, table_length));
	}

	/* Scan entire table to determine if each LF has been prefixed with a CR */

	for (i = 1; i < file_length; i++) {
		if (table[i] == 0x0A) {
			if (table[i - 1] != 0x0D) {

				/* The LF does not have a preceding CR, table not corrupted */

				return (AE_OK);
			} else {
				/* Found a CR/LF pair */

				pairs++;
			}
			i++;
		}
	}

	if (!pairs) {
		return (AE_OK);
	}

	/*
	 * Entire table scanned, each CR is part of a CR/LF pair --
	 * meaning that the table was treated as a text file somewhere.
	 *
	 * NOTE: We can't "fix" the table, because any existing CR/LF pairs in the
	 * original table are left untouched by the text conversion process --
	 * meaning that we cannot simply replace CR/LF pairs with LFs.
	 */
	acpi_os_printf("Table has been corrupted by text mode conversion\n");
	acpi_os_printf("All LFs (%u) were changed to CR/LF pairs\n", pairs);
	acpi_os_printf("Table cannot be repaired!\n");
	return (AE_BAD_VALUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_read_table
 *
 * PARAMETERS:  fp              - File that contains table
 *              table           - Return value, buffer with table
 *              table_length    - Return value, length of table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the DSDT from the file pointer
 *
 ******************************************************************************/

static acpi_status
acpi_ut_read_table(FILE * fp,
		   struct acpi_table_header **table, u32 *table_length)
{
	struct acpi_table_header table_header;
	u32 actual;
	acpi_status status;
	u32 file_size;
	u8 standard_header = TRUE;
	s32 count;

	/* Get the file size */

	file_size = cm_get_file_size(fp);
	if (file_size == ACPI_UINT32_MAX) {
		return (AE_ERROR);
	}

	if (file_size < 4) {
		return (AE_BAD_HEADER);
	}

	/* Read the signature */

	fseek(fp, 0, SEEK_SET);

	count = fread(&table_header, 1, sizeof(struct acpi_table_header), fp);
	if (count != sizeof(struct acpi_table_header)) {
		acpi_os_printf("Could not read the table header\n");
		return (AE_BAD_HEADER);
	}

	/* The RSDP table does not have standard ACPI header */

	if (ACPI_VALIDATE_RSDP_SIG(table_header.signature)) {
		*table_length = file_size;
		standard_header = FALSE;
	} else {

#if 0
		/* Validate the table header/length */

		status = acpi_tb_validate_table_header(&table_header);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf("Table header is invalid!\n");
			return (status);
		}
#endif

		/* File size must be at least as long as the Header-specified length */

		if (table_header.length > file_size) {
			acpi_os_printf
			    ("TableHeader length [0x%X] greater than the input file size [0x%X]\n",
			     table_header.length, file_size);

#ifdef ACPI_ASL_COMPILER
			status = fl_check_for_ascii(fp, NULL, FALSE);
			if (ACPI_SUCCESS(status)) {
				acpi_os_printf
				    ("File appears to be ASCII only, must be binary\n");
			}
#endif
			return (AE_BAD_HEADER);
		}
#ifdef ACPI_OBSOLETE_CODE
		/* We only support a limited number of table types */

		if (!ACPI_COMPARE_NAME
		    ((char *)table_header.signature, ACPI_SIG_DSDT)
		    && !ACPI_COMPARE_NAME((char *)table_header.signature,
					  ACPI_SIG_PSDT)
		    && !ACPI_COMPARE_NAME((char *)table_header.signature,
					  ACPI_SIG_SSDT)) {
			acpi_os_printf
			    ("Table signature [%4.4s] is invalid or not supported\n",
			     (char *)table_header.signature);
			ACPI_DUMP_BUFFER(&table_header,
					 sizeof(struct acpi_table_header));
			return (AE_ERROR);
		}
#endif

		*table_length = table_header.length;
	}

	/* Allocate a buffer for the table */

	*table = acpi_os_allocate((size_t) file_size);
	if (!*table) {
		acpi_os_printf
		    ("Could not allocate memory for ACPI table %4.4s (size=0x%X)\n",
		     table_header.signature, *table_length);
		return (AE_NO_MEMORY);
	}

	/* Get the rest of the table */

	fseek(fp, 0, SEEK_SET);
	actual = fread(*table, 1, (size_t) file_size, fp);
	if (actual == file_size) {
		if (standard_header) {

			/* Now validate the checksum */

			status = acpi_tb_verify_checksum((void *)*table,
							 ACPI_CAST_PTR(struct
								       acpi_table_header,
								       *table)->
							 length);

			if (status == AE_BAD_CHECKSUM) {
				status =
				    acpi_ut_check_text_mode_corruption((u8 *)
								       *table,
								       file_size,
								       (*table)->
								       length);
				return (status);
			}
		}
		return (AE_OK);
	}

	if (actual > 0) {
		acpi_os_printf("Warning - reading table, asked for %X got %X\n",
			       file_size, actual);
		return (AE_OK);
	}

	acpi_os_printf("Error - could not read the table file\n");
	acpi_os_free(*table);
	*table = NULL;
	*table_length = 0;
	return (AE_ERROR);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_read_table_from_file
 *
 * PARAMETERS:  filename         - File where table is located
 *              table            - Where a pointer to the table is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an ACPI table from a file
 *
 ******************************************************************************/

acpi_status
acpi_ut_read_table_from_file(char *filename, struct acpi_table_header ** table)
{
	FILE *file;
	u32 file_size;
	u32 table_length;
	acpi_status status = AE_ERROR;

	/* Open the file, get current size */

	file = fopen(filename, "rb");
	if (!file) {
		perror("Could not open input file");
		return (status);
	}

	file_size = cm_get_file_size(file);
	if (file_size == ACPI_UINT32_MAX) {
		goto exit;
	}

	/* Get the entire file */

	fprintf(stderr,
		"Loading Acpi table from file %10s - Length %.8u (%06X)\n",
		filename, file_size, file_size);

	status = acpi_ut_read_table(file, table, &table_length);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not get table from the file\n");
	}

exit:
	fclose(file);
	return (status);
}

#endif
