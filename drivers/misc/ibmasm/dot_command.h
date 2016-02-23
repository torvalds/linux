/*
 * IBM ASM Service Processor Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 *
 */

#ifndef __DOT_COMMAND_H__
#define __DOT_COMMAND_H__

/*
 * dot commands are the protocol used to communicate with the service
 * processor.
 * They consist of header, a command of variable length and data of
 * variable length.
 */

/* dot command types */
#define sp_write		0
#define sp_write_next		1
#define sp_read			2
#define sp_read_next		3
#define sp_command_response	4
#define sp_event		5
#define sp_heartbeat		6

#pragma pack(1)
struct dot_command_header {
	u8	type;
	u8	command_size;
	u16	data_size;
	u8	status;
	u8	reserved;
};
#pragma pack()

static inline size_t get_dot_command_size(void *buffer)
{
	struct dot_command_header *cmd = (struct dot_command_header *)buffer;
	return sizeof(struct dot_command_header) + cmd->command_size + cmd->data_size;
}

static inline unsigned int get_dot_command_timeout(void *buffer)
{
	struct dot_command_header *header = (struct dot_command_header *)buffer;
	unsigned char *cmd = buffer + sizeof(struct dot_command_header);

	/* dot commands 6.3.1, 7.1 and 8.x need a longer timeout */

	if (header->command_size == 3) {
		if ((cmd[0] == 6) && (cmd[1] == 3) && (cmd[2] == 1))
			return IBMASM_CMD_TIMEOUT_EXTRA;
	} else if (header->command_size == 2) {
		if ((cmd[0] == 7) && (cmd[1] == 1))
			return IBMASM_CMD_TIMEOUT_EXTRA;
		if (cmd[0] == 8)
			return IBMASM_CMD_TIMEOUT_EXTRA;
	}
	return IBMASM_CMD_TIMEOUT_NORMAL;
}

#endif /* __DOT_COMMAND_H__ */
