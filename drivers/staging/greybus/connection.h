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
	struct device			dev;
	u16				hd_cport_id;
	u16				interface_cport_id;

	struct list_head		hd_links;
	struct list_head		interface_links;

	struct gb_protocol		*protocol;

	enum gb_connection_state	state;

	u16				op_cycle;
	struct list_head		operations;
	struct list_head		pending;	/* awaiting response */

	void				*private;
};
#define to_gb_connection(d) container_of(d, struct gb_connection, dev)

struct gb_connection *gb_connection_create(struct gb_interface *interface,
				u16 cport_id, u8 protocol_id);
void gb_connection_destroy(struct gb_connection *connection);

int gb_connection_init(struct gb_connection *connection);
void gb_connection_exit(struct gb_connection *connection);

struct gb_connection *gb_hd_connection_find(struct greybus_host_device *hd,
				u16 cport_id);

void greybus_data_rcvd(struct greybus_host_device *hd, u16 cport_id,
			u8 *data, size_t length);
__printf(2, 3)
void gb_connection_err(struct gb_connection *connection, const char *fmt, ...);

#endif /* __CONNECTION_H */
