/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __CONNECTION_H
#define __CONNECTION_H

#include <linux/list.h>

#include "greybus.h"

enum gb_connection_state {
	GB_CONNECTION_STATE_INVALID	= 0,
	GB_CONNECTION_STATE_DISABLED	= 1,
	GB_CONNECTION_STATE_ENABLED	= 2,
	GB_CONNECTION_STATE_ERROR	= 3,
	GB_CONNECTION_STATE_DESTROYING	= 4,
};

struct gb_connection {
	struct greybus_host_device	*hd;
	struct gb_interface		*interface;
	u16				hd_cport_id;
	u16				interface_cport_id;

	struct rb_node			hd_node;
	struct list_head		interface_links;
	enum greybus_protocol		protocol;
	enum gb_connection_state	state;

	struct list_head		operations;
	struct rb_root			pending;	/* awaiting reponse */
	atomic_t			op_cycle;
	struct delayed_work		timeout_work;

	void				*private;
};

struct gb_connection *gb_connection_create(struct gb_interface *interface,
				u16 cport_id, enum greybus_protocol protocol);
void gb_connection_destroy(struct gb_connection *connection);

int gb_connection_init(struct gb_connection *connection);
void gb_connection_exit(struct gb_connection *connection);

struct gb_connection *gb_hd_connection_find(struct greybus_host_device *hd,
				u16 cport_id);

u16 gb_connection_operation_id(struct gb_connection *connection);

__printf(2, 3)
void gb_connection_err(struct gb_connection *connection, const char *fmt, ...);

#endif /* __CONNECTION_H */
