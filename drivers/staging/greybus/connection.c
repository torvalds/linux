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
struct gb_connection *gb_connection_create(struct gb_interface *interface,
						u16 cport_id)
{
	struct gb_connection *connection;
	struct greybus_host_device *hd;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	hd = interface->gmod->hd;
	connection->hd_cport_id = greybus_hd_cport_id_alloc(hd);
	if (connection->hd_cport_id == CPORT_ID_BAD) {
		kfree(connection);
		return NULL;
	}
	connection->hd = hd;			/* XXX refcount? */
	connection->interface = interface;	/* XXX refcount? */
	connection->interface_cport_id = cport_id;
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

	greybus_hd_cport_id_free(connection->hd, connection->hd_cport_id);
	/* kref_put(connection->interface); */
	/* kref_put(connection->hd); */
	kfree(connection);
}

u16 gb_connection_op_id(struct gb_connection *connection)
{
	return (u16)(atomic_inc_return(&connection->op_cycle) % U16_MAX);
}
