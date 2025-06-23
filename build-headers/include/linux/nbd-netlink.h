/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2017 Facebook.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
#ifndef LINUX_NBD_NETLINK_H
#define LINUX_NBD_NETLINK_H

#define NBD_GENL_FAMILY_NAME		"nbd"
#define NBD_GENL_VERSION		0x1
#define NBD_GENL_MCAST_GROUP_NAME	"nbd_mc_group"

/* Configuration policy attributes, used for CONNECT */
enum {
	NBD_ATTR_UNSPEC,
	NBD_ATTR_INDEX,
	NBD_ATTR_SIZE_BYTES,
	NBD_ATTR_BLOCK_SIZE_BYTES,
	NBD_ATTR_TIMEOUT,
	NBD_ATTR_SERVER_FLAGS,
	NBD_ATTR_CLIENT_FLAGS,
	NBD_ATTR_SOCKETS,
	NBD_ATTR_DEAD_CONN_TIMEOUT,
	NBD_ATTR_DEVICE_LIST,
	NBD_ATTR_BACKEND_IDENTIFIER,
	__NBD_ATTR_MAX,
};
#define NBD_ATTR_MAX (__NBD_ATTR_MAX - 1)

/*
 * This is the format for multiple devices with NBD_ATTR_DEVICE_LIST
 *
 * [NBD_ATTR_DEVICE_LIST]
 *   [NBD_DEVICE_ITEM]
 *     [NBD_DEVICE_INDEX]
 *     [NBD_DEVICE_CONNECTED]
 */
enum {
	NBD_DEVICE_ITEM_UNSPEC,
	NBD_DEVICE_ITEM,
	__NBD_DEVICE_ITEM_MAX,
};
#define NBD_DEVICE_ITEM_MAX (__NBD_DEVICE_ITEM_MAX - 1)

enum {
	NBD_DEVICE_UNSPEC,
	NBD_DEVICE_INDEX,
	NBD_DEVICE_CONNECTED,
	__NBD_DEVICE_MAX,
};
#define NBD_DEVICE_ATTR_MAX (__NBD_DEVICE_MAX - 1)

/*
 * This is the format for multiple sockets with NBD_ATTR_SOCKETS
 *
 * [NBD_ATTR_SOCKETS]
 *   [NBD_SOCK_ITEM]
 *     [NBD_SOCK_FD]
 *   [NBD_SOCK_ITEM]
 *     [NBD_SOCK_FD]
 */
enum {
	NBD_SOCK_ITEM_UNSPEC,
	NBD_SOCK_ITEM,
	__NBD_SOCK_ITEM_MAX,
};
#define NBD_SOCK_ITEM_MAX (__NBD_SOCK_ITEM_MAX - 1)

enum {
	NBD_SOCK_UNSPEC,
	NBD_SOCK_FD,
	__NBD_SOCK_MAX,
};
#define NBD_SOCK_MAX (__NBD_SOCK_MAX - 1)

enum {
	NBD_CMD_UNSPEC,
	NBD_CMD_CONNECT,
	NBD_CMD_DISCONNECT,
	NBD_CMD_RECONFIGURE,
	NBD_CMD_LINK_DEAD,
	NBD_CMD_STATUS,
	__NBD_CMD_MAX,
};
#define NBD_CMD_MAX	(__NBD_CMD_MAX - 1)

#endif /* LINUX_NBD_NETLINK_H */
