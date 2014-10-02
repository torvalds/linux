/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/*
 * Set up a Greybus connection, representing the bidirectional link
 * between a CPort on a (local) Greybus host device and a CPort on
 * another Greybus module.
 *
 * Returns a pointer to the new connection if successful, or a null
 * pointer otherwise.
 */
struct gb_connection *gb_connection_create(struct greybus_host_device *hd,
				u16 cport_id, struct gb_function *function)
{
	struct gb_connection *connection;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	connection->hd = hd;			/* XXX refcount? */
	connection->cport_id = cport_id;
	connection->function = function;	/* XXX refcount? */

	return connection;
}

/*
 * Tear down a previously set up connection.
 */
void gb_connection_destroy(struct gb_connection *connection)
{
	if (WARN_ON(!connection))
		return;

	/* XXX Need to wait for any outstanding requests to complete */

	/* kref_put(function); */
	/* kref_put(hd); */
	kfree(connection);
}
