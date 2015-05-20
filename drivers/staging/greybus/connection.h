/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
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
	struct gb_bundle		*bundle;
	struct device			dev;
	u16				hd_cport_id;
	u16				bundle_cport_id;

	struct list_head		hd_links;
	struct list_head		bundle_links;

	struct gb_protocol		*protocol;
	u8				protocol_id;
	u8				major;
	u8				minor;

	enum gb_connection_state	state;

	atomic_t			op_cycle;
	struct list_head		operations;

	void				*private;
};
#define to_gb_connection(d) container_of(d, struct gb_connection, dev)

struct gb_connection *gb_connection_create(struct gb_bundle *bundle,
				u16 cport_id, u8 protocol_id);
void gb_connection_destroy(struct gb_connection *connection);

int gb_connection_init(struct gb_connection *connection);
void gb_connection_exit(struct gb_connection *connection);

struct gb_connection *gb_connection_hd_find(struct greybus_host_device *hd,
				u16 cport_id);

void greybus_data_rcvd(struct greybus_host_device *hd, u16 cport_id,
			u8 *data, size_t length);

void gb_connection_bind_protocol(struct gb_connection *connection);

#endif /* __CONNECTION_H */
