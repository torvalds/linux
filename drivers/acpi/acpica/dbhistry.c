// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dbhistry - debugger HISTORY command
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdebug.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbhistry")

#define HI_NO_HISTORY       0
#define HI_RECORD_HISTORY   1
#define HISTORY_SIZE        40
typedef struct history_info {
	char *command;
	u32 cmd_num;

} HISTORY_INFO;

static HISTORY_INFO acpi_gbl_history_buffer[HISTORY_SIZE];
static u16 acpi_gbl_lo_history = 0;
static u16 acpi_gbl_num_history = 0;
static u16 acpi_gbl_next_history_index = 0;

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_add_to_history
 *
 * PARAMETERS:  command_line    - Command to add
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add a command line to the history buffer.
 *
 ******************************************************************************/

void acpi_db_add_to_history(char *command_line)
{
	u16 cmd_len;
	u16 buffer_len;

	/* Put command into the next available slot */

	cmd_len = (u16)strlen(command_line);
	if (!cmd_len) {
		return;
	}

	if (acpi_gbl_history_buffer[acpi_gbl_next_history_index].command !=
	    NULL) {
		buffer_len =
		    (u16)
		    strlen(acpi_gbl_history_buffer[acpi_gbl_next_history_index].
			   command);

		if (cmd_len > buffer_len) {
			acpi_os_free(acpi_gbl_history_buffer
				     [acpi_gbl_next_history_index].command);
			acpi_gbl_history_buffer[acpi_gbl_next_history_index].
			    command = acpi_os_allocate(cmd_len + 1);
		}
	} else {
		acpi_gbl_history_buffer[acpi_gbl_next_history_index].command =
		    acpi_os_allocate(cmd_len + 1);
	}

	strcpy(acpi_gbl_history_buffer[acpi_gbl_next_history_index].command,
	       command_line);

	acpi_gbl_history_buffer[acpi_gbl_next_history_index].cmd_num =
	    acpi_gbl_next_cmd_num;

	/* Adjust indexes */

	if ((acpi_gbl_num_history == HISTORY_SIZE) &&
	    (acpi_gbl_next_history_index == acpi_gbl_lo_history)) {
		acpi_gbl_lo_history++;
		if (acpi_gbl_lo_history >= HISTORY_SIZE) {
			acpi_gbl_lo_history = 0;
		}
	}

	acpi_gbl_next_history_index++;
	if (acpi_gbl_next_history_index >= HISTORY_SIZE) {
		acpi_gbl_next_history_index = 0;
	}

	acpi_gbl_next_cmd_num++;
	if (acpi_gbl_num_history < HISTORY_SIZE) {
		acpi_gbl_num_history++;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_history
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the contents of the history buffer
 *
 ******************************************************************************/

void acpi_db_display_history(void)
{
	u32 i;
	u16 history_index;

	history_index = acpi_gbl_lo_history;

	/* Dump entire history buffer */

	for (i = 0; i < acpi_gbl_num_history; i++) {
		if (acpi_gbl_history_buffer[history_index].command) {
			acpi_os_printf("%3u %s\n",
				       acpi_gbl_history_buffer[history_index].
				       cmd_num,
				       acpi_gbl_history_buffer[history_index].
				       command);
		}

		history_index++;
		if (history_index >= HISTORY_SIZE) {
			history_index = 0;
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_from_history
 *
 * PARAMETERS:  command_num_arg         - String containing the number of the
 *                                        command to be retrieved
 *
 * RETURN:      Pointer to the retrieved command. Null on error.
 *
 * DESCRIPTION: Get a command from the history buffer
 *
 ******************************************************************************/

char *acpi_db_get_from_history(char *command_num_arg)
{
	u32 cmd_num;

	if (command_num_arg == NULL) {
		cmd_num = acpi_gbl_next_cmd_num - 1;
	}

	else {
		cmd_num = strtoul(command_num_arg, NULL, 0);
	}

	return (acpi_db_get_history_by_index(cmd_num));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_history_by_index
 *
 * PARAMETERS:  cmd_num             - Index of the desired history entry.
 *                                    Values are 0...(acpi_gbl_next_cmd_num - 1)
 *
 * RETURN:      Pointer to the retrieved command. Null on error.
 *
 * DESCRIPTION: Get a command from the history buffer
 *
 ******************************************************************************/

char *acpi_db_get_history_by_index(u32 cmd_num)
{
	u32 i;
	u16 history_index;

	/* Search history buffer */

	history_index = acpi_gbl_lo_history;
	for (i = 0; i < acpi_gbl_num_history; i++) {
		if (acpi_gbl_history_buffer[history_index].cmd_num == cmd_num) {

			/* Found the command, return it */

			return (acpi_gbl_history_buffer[history_index].command);
		}

		/* History buffer is circular */

		history_index++;
		if (history_index >= HISTORY_SIZE) {
			history_index = 0;
		}
	}

	acpi_os_printf("Invalid history number: %u\n", history_index);
	return (NULL);
}
