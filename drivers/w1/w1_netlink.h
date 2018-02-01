/*
 * Copyright (c) 2003 Evgeniy Polyakov <zbr@ioremap.net>
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
 */

#ifndef __W1_NETLINK_H
#define __W1_NETLINK_H

#include <asm/types.h>
#include <linux/connector.h>

#include "w1_internal.h"

/**
 * enum w1_cn_msg_flags - bitfield flags for struct cn_msg.flags
 *
 * @W1_CN_BUNDLE: Request bundling replies into fewer messagse.  Be prepared
 * to handle multiple struct cn_msg, struct w1_netlink_msg, and
 * struct w1_netlink_cmd in one packet.
 */
enum w1_cn_msg_flags {
	W1_CN_BUNDLE = 1,
};

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

/**
 * struct w1_netlink_msg - holds w1 message type, id, and result
 *
 * @type: one of enum w1_netlink_message_types
 * @status: kernel feedback for success 0 or errno failure value
 * @len: length of data following w1_netlink_msg
 * @id: union holding bus master id (msg.id) and slave device id (id[8]).
 * @id.id: Slave ID (8 bytes)
 * @id.mst: bus master identification
 * @id.mst.id: bus master ID
 * @id.mst.res: bus master reserved
 * @data: start address of any following data
 *
 * The base message structure for w1 messages over netlink.
 * The netlink connector data sequence is, struct nlmsghdr, struct cn_msg,
 * then one or more struct w1_netlink_msg (each with optional data).
 */
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
 *
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

/**
 * struct w1_netlink_cmd - holds the command and data
 *
 * @cmd: one of enum w1_commands
 * @res: reserved
 * @len: length of data following w1_netlink_cmd
 * @data: start address of any following data
 *
 * One or more struct w1_netlink_cmd is placed starting at w1_netlink_msg.data
 * each with optional data.
 */
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
