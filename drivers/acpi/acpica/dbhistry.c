/******************************************************************************
 *
 * Module Name: dbhistry - debugger HISTORY command
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
u32 acpi_gbl_next_cmd_num = 1;

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
			acpi_os_printf("%3ld %s\n",
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
