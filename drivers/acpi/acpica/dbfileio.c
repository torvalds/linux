// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: dbfileio - Debugger file I/O commands. These can't usually
 *              be used when running the debugger in Ring 0 (Kernel mode)
 *
 ******************************************************************************/

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
