/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/atomic.h>

#include "kernel_ver.h"
#include "greybus.h"

/*
 * Set up a Greybus connection, representing the bidirectional link
 * between a CPort on a (local) Greybus host device and a CPort on
 * another Greybus module.
 *
 * A connection also maintains the state of operations sent over the
 * connection.
 *
 * Returns a pointer to the new connection if successful, or a null
 * pointer otherwise.
 */
struct gb_connection *gb_connection_create(struct greybus_host_device *hd,
				struct gb_function *function)
{
	struct gb_connection *connection;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	connection->cport_id = greybus_hd_cport_id_alloc(hd);
	if (connection->cport_id == CPORT_ID_BAD) {
		kfree(connection);
		return NULL;
	}

	connection->hd = hd;			/* XXX refcount? */
	connection->function = function;	/* XXX refcount? */
	INIT_LIST_HEAD(&connection->operations);
	atomic_set(&connection->op_cycle, 0);

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
	WARN_ON(!list_empty(&connection->operations));

	greybus_hd_cport_id_free(connection->hd, connection->cport_id);
	/* kref_put(function); */
	/* kref_put(hd); */
	kfree(connection);
}

u16 gb_connection_op_id(struct gb_connection *connection)
{
	return (u16)(atomic_inc_return(&connection->op_cycle) % U16_MAX);
}
