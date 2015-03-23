/*
 * Netlink helper functions for driver wrappers
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef NETLINK_H
#define NETLINK_H

struct netlink_data;
struct ifinfomsg;

struct netlink_config {
	void *ctx;
	void (*newlink_cb)(void *ctx, struct ifinfomsg *ifi, u8 *buf,
			   size_t len);
	void (*dellink_cb)(void *ctx, struct ifinfomsg *ifi, u8 *buf,
			   size_t len);
};

struct netlink_data * netlink_init(struct netlink_config *cfg);
void netlink_deinit(struct netlink_data *netlink);
int netlink_send_oper_ifla(struct netlink_data *netlink, int ifindex,
			   int linkmode, int operstate);

#endif /* NETLINK_H */
