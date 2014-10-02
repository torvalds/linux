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

struct gb_connection {
	struct gb_function		*function;
	struct greybus_host_device	*hd;
	u16				cport_id;	/* Host side */

	struct list_head		host_links;

	struct list_head		operations;
	atomic_t			op_cycle;
};

struct gb_connection *gb_connection_create(struct greybus_host_device *hd,
					struct gb_function *function);
void gb_connection_destroy(struct gb_connection *connection);

u16 gb_connection_op_id(struct gb_connection *connection);

#endif /* __CONNECTION_H */
