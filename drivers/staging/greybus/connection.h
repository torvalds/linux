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
#include "function.h"

struct gb_connection {
	struct gb_function		*function;
	struct greybus_host_device	*hd;
	u16				cport_id;	/* Host side */

	struct list_head		host_links;
};

bool gb_connection_setup(struct greybus_host_device *hd, u16 cport_id,
				struct gb_function *function);
void gb_connection_teardown(struct gb_connection *connection);

#endif /* __CONNECTION_H */
