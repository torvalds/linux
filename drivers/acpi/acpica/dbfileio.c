/*******************************************************************************
 *
 * Module Name: dbfileio - Debugger file I/O commands. These can't usually
 *              be used when running the debugger in Ring 0 (Kernel mode)
 *
 ******************************************************************************/

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
#include "acdebug.h"
#include "actables.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbfileio")

#ifdef ACPI_APPLICATION
#include "acapps.h"
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

	if (acpi_gbl_debug_file) {
		fclose(acpi_gbl_debug_file);
		acpi_gbl_debug_file = NULL;
		acpi_gbl_db_output_to_file = FALSE;
		acpi_os_printf("Debug output file %s closed\n",
			       acpi_gbl_db_debug_filename);
	}
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

	acpi_db_close_debug_file();
	acpi_gbl_debug_file = fopen(name, "w+");
	if (!acpi_gbl_debug_file) {
		acpi_os_printf("Could not open debug file %s\n", name);
		return;
	}

	acpi_os_printf("Debug output file %s opened\n", name);
	acpi_ut_safe_strncpy(acpi_gbl_db_debug_filename, name,
			     sizeof(acpi_gbl_db_debug_filename));
	acpi_gbl_db_output_to_file = TRUE;
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_load_tables
 *
 * PARAMETERS:  list_head       - List of ACPI tables to load
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load ACPI tables from a previously constructed table list.
 *
 ******************************************************************************/

acpi_status acpi_db_load_tables(struct acpi_new_table_desc *list_head)
{
	acpi_status status;
	struct acpi_new_table_desc *table_list_head;
	struct acpi_table_header *table;

	/* Load all ACPI tables in the list */

	table_list_head = list_head;
	while (table_list_head) {
		table = table_list_head->table;

		status = acpi_load_table(table);
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

		acpi_os_printf
		    ("Acpi table [%4.4s] successfully installed and loaded\n",
		     table->signature);

		table_list_head = table_list_head->next;
	}

	return (AE_OK);
}
#endif
