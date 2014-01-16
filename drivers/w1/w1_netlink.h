/*
 * w1_netlink.h
 *
 * Copyright (c) 2003 Evgeniy Polyakov <zbr@ioremap.net>
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __W1_NETLINK_H
#define __W1_NETLINK_H

#include <asm/types.h>
#include <linux/connector.h>

#include "w1.h"

/**
 * enum w1_netlink_message_types - message type
 *
 * @W1_SLAVE_ADD: notification that a slave device was added
 * @W1_SLAVE_REMOVE: notification that a slave device was removed
 * @W1_MASTER_ADD: notification that a new bus master was added
 * @W1_MASTER_REMOVE: notification that a bus masterwas removed
 * @W1_MASTER_CMD: initiate operations on a specific master
 * @W1_SLAVE_CMD: sends reset, selects the slave, then does a read/write/touch
 * operation
 * @W1_LIST_MASTERS: used to determine the bus master identifiers
 */
enum w1_netlink_message_types {
	W1_SLAVE_ADD = 0,
	W1_SLAVE_REMOVE,
	W1_MASTER_ADD,
	W1_MASTER_REMOVE,
	W1_MASTER_CMD,
	W1_SLAVE_CMD,
	W1_LIST_MASTERS,
};

struct w1_netlink_msg
{
	__u8				type;
	__u8				status;
	__u16				len;
	union {
		__u8			id[8];
		struct w1_mst {
			__u32		id;
			__u32		res;
		} mst;
	} id;
	__u8				data[0];
};

/**
 * enum w1_commands - commands available for master or slave operations
 * @W1_CMD_READ: read len bytes
 * @W1_CMD_WRITE: write len bytes
 * @W1_CMD_SEARCH: initiate a standard search, returns only the slave
 * devices found during that search
 * @W1_CMD_ALARM_SEARCH: search for devices that are currently alarming
 * @W1_CMD_TOUCH: Touches a series of bytes.
 * @W1_CMD_RESET: sends a bus reset on the given master
 * @W1_CMD_SLAVE_ADD: adds a slave to the given master,
 * 8 byte slave id at data[0]
 * @W1_CMD_SLAVE_REMOVE: removes a slave to the given master,
 * 8 byte slave id at data[0]
 * @W1_CMD_LIST_SLAVES: list of slaves registered on this master
 * @W1_CMD_MAX: number of available commands
 */
enum w1_commands {
	W1_CMD_READ = 0,
	W1_CMD_WRITE,
	W1_CMD_SEARCH,
	W1_CMD_ALARM_SEARCH,
	W1_CMD_TOUCH,
	W1_CMD_RESET,
	W1_CMD_SLAVE_ADD,
	W1_CMD_SLAVE_REMOVE,
	W1_CMD_LIST_SLAVES,
	W1_CMD_MAX
};

struct w1_netlink_cmd
{
	__u8				cmd;
	__u8				res;
	__u16				len;
	__u8				data[0];
};

#ifdef __KERNEL__

void w1_netlink_send(struct w1_master *, struct w1_netlink_msg *);
int w1_init_netlink(void);
void w1_fini_netlink(void);

#endif /* __KERNEL__ */
#endif /* __W1_NETLINK_H */
