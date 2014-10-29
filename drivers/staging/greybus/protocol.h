/*
 * Greybus protocol handling
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "greybus.h"

struct gb_protocol {
	u8				id;
	struct list_head		connections;	/* protocol users */
	struct list_head		links;		/* global list */
};

bool gb_protocol_register(u8 id);
bool gb_protocol_deregister(struct gb_protocol *protocol);

bool gb_protocol_get(struct gb_connection *connection, u8 id);
void gb_protocol_put(struct gb_connection *connection);

#endif /* __PROTOCOL_H */
