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

/*
 * Protocols having the same id but different major and/or minor
 * version numbers are treated as distinct protocols.  If it makes
 * sense someday we could group protocols having the same id.
 */
struct gb_protocol {
	u8			id;
	u8			major;
	u8			minor;

	struct list_head	connections;	/* protocol users */
	struct list_head	links;		/* global list */
};

bool gb_protocol_register(u8 id, u8 major, u8 minor);
bool gb_protocol_deregister(struct gb_protocol *protocol);

bool gb_protocol_get(struct gb_connection *connection, u8 id,
				u8 major, u8 minor);
void gb_protocol_put(struct gb_connection *connection);

#endif /* __PROTOCOL_H */
