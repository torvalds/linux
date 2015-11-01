/*******************************************************************************
 *
 * Module Name: dbfileio - Debugger file I/O commands. These can't usually
 *              be used when running the debugger in Ring 0 (Kernel mode)
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "acdebug.h"
#include "actables.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbfileio")

#ifdef ACPI_DEBUGGER
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_close_debug_file
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: If open, close the current debug output file
 *
 ******************************************************************************/
void acpi_db_close_debug_file(void)
{

#ifdef ACPI_APPLICATION

	if (acpi_gbl_debug_file) {
		fclose(acpi_gbl_debug_file);
		acpi_gbl_debug_file = NULL;
		acpi_gbl_db_output_to_file = FALSE;
		acpi_os_printf("Debug output file %s closed\n",
			       acpi_gbl_db_debug_filename);
	}
#endif
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_open_debug_file
 *
 * PARAMETERS:  name                - Filename to open
 *
 * RETURN:      None
 *
 * DESCRIPTION: Open a file where debug output will be directed.
 *
 ******************************************************************************/

void acpi_db_open_debug_file(char *name)
{

#ifdef ACPI_APPLICATION

	acpi_db_close_debug_file();
	acpi_gbl_debug_file = fopen(name, "w+");
	if (!acpi_gbl_debug_file) {
		acpi_os_printf("Could not open debug file %s\n", name);
		return;
	}

	acpi_os_printf("Debug output file %s opened\n", name);
	strncpy(acpi_gbl_db_debug_filename, name,
		sizeof(acpi_gbl_db_debug_filename));
	acpi_gbl_db_output_to_file = TRUE;

#endif
}
#endif

#ifdef ACPI_APPLICATION
#include "acapps.h"

/*******************************************************************************
 *
 * FUNCTION:    ae_local_load_table
 *
 * PARAMETERS:  table           - pointer to a buffer containing the entire
 *                                table to be loaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to load a table from the caller's
 *              buffer. The buffer must contain an entire ACPI Table including
 *              a valid header. The header fields will be verified, and if it
 *              is determined that the table is invalid, the call will fail.
 *
 ******************************************************************************/

static acpi_status ae_local_load_table(struct acpi_table_header *table)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ae_local_load_table);

#if 0
/*    struct acpi_table_desc          table_info; */

	if (!table) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	table_info.pointer = table;
	status = acpi_tb_recognize_table(&table_info, ACPI_TABLE_ALL);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Install the new table into the local data structures */

	status = acpi_tb_init_table_descriptor(&table_info);
	if (ACPI_FAILURE(status)) {
		if (status == AE_ALREADY_EXISTS) {

			/* Table already exists, no error */

			status = AE_OK;
		}

		/* Free table allocated by acpi_tb_get_table */

		acpi_tb_delete_single_table(&table_info);
		return_ACPI_STATUS(status);
	}
#if (!defined (ACPI_NO_METHOD_EXECUTION) && !defined (ACPI_CONSTANT_EVAL_ONLY))

	status =
	    acpi_ns_load_table(table_info.installed_desc, acpi_gbl_root_node);
	if (ACPI_FAILURE(status)) {

		/* Uninstall table and free the buffer */

		acpi_tb_delete_tables_by_type(ACPI_TABLE_ID_DSDT);
		return_ACPI_STATUS(status);
	}
#endif
#endif

	return_ACPI_STATUS(status);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_table_from_file
 *
 * PARAMETERS:  filename        - File where table is located
 *              return_table    - Where a pointer to the table is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table from a file
 *
 ******************************************************************************/

acpi_status
acpi_db_get_table_from_file(char *filename,
			    struct acpi_table_header **return_table,
			    u8 must_be_aml_file)
{
#ifdef ACPI_APPLICATION
	acpi_status status;
	struct acpi_table_header *table;
	u8 is_aml_table = TRUE;

	status = acpi_ut_read_table_from_file(filename, &table);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	if (must_be_aml_file) {
		is_aml_table = acpi_ut_is_aml_table(table);
		if (!is_aml_table) {
			ACPI_EXCEPTION((AE_INFO, AE_OK,
					"Input for -e is not an AML table: "
					"\"%4.4s\" (must be DSDT/SSDT)",
					table->signature));
			return (AE_TYPE);
		}
	}

	if (is_aml_table) {

		/* Attempt to recognize and install the table */

		status = ae_local_load_table(table);
		if (ACPI_FAILURE(status)) {
			if (status == AE_ALREADY_EXISTS) {
				acpi_os_printf
				    ("Table %4.4s is already installed\n",
				     table->signature);
			} else {
				acpi_os_printf("Could not install table, %s\n",
					       acpi_format_exception(status));
			}

			return (status);
		}

		acpi_tb_print_table_header(0, table);

		fprintf(stderr,
			"Acpi table [%4.4s] successfully installed and loaded\n",
			table->signature);
	}

	acpi_gbl_acpi_hardware_present = FALSE;
	if (return_table) {
		*return_table = table;
	}

#endif				/* ACPI_APPLICATION */
	return (AE_OK);
}
