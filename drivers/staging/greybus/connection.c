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

static DEFINE_SPINLOCK(gb_connections_lock);

static void _gb_hd_connection_insert(struct greybus_host_device *hd,
					struct gb_connection *connection)
{
	struct rb_root *root = &hd->connections;
	struct rb_node *node = &connection->hd_node;
	struct rb_node **link = &root->rb_node;
	struct rb_node *above = NULL;
	u16 cport_id = connection->hd_cport_id;

	while (*link) {
		struct gb_connection *connection;

		above = *link;
		connection = rb_entry(above, struct gb_connection, hd_node);
		if (connection->hd_cport_id > cport_id)
			link = &above->rb_left;
		else if (connection->hd_cport_id < cport_id)
			link = &above->rb_right;
	}
	rb_link_node(node, above, link);
	rb_insert_color(node, root);
}

static void _gb_hd_connection_remove(struct gb_connection *connection)
{
	rb_erase(&connection->hd_node, &connection->hd->connections);
}

struct gb_connection *gb_hd_connection_find(struct greybus_host_device *hd,
						u16 cport_id)
{
	struct gb_connection *connection = NULL;
	struct rb_node *node;

	spin_lock_irq(&gb_connections_lock);
	node = hd->connections.rb_node;
	while (node) {
		connection = rb_entry(node, struct gb_connection, hd_node);
		if (connection->hd_cport_id > cport_id)
			node = node->rb_left;
		else if (connection->hd_cport_id < cport_id)
			node = node->rb_right;
		else
			break;
	}
	spin_unlock_irq(&gb_connections_lock);

	return connection;
}

/*
 * Allocate an available CPort Id for use for the host side of the
 * given connection.  The lowest-available id is returned, so the
 * first call is guaranteed to allocate CPort Id 0.
 *
 * Assigns the connection's hd_cport_id and returns true if successful.
 * Returns false otherwise.
 */
static bool gb_connection_hd_cport_id_alloc(struct gb_connection *connection)
{
	struct ida *ida = &connection->hd->cport_id_map;
	int id;

	id = ida_simple_get(ida, 0, HOST_DEV_CPORT_ID_MAX, GFP_KERNEL);
	if (id < 0)
		return false;

	connection->hd_cport_id = (u16)id;

	return true;
}

/*
 * Free a previously-allocated CPort Id on the given host device.
 */
static void gb_connection_hd_cport_id_free(struct gb_connection *connection)
{
	struct ida *ida = &connection->hd->cport_id_map;

	ida_simple_remove(ida, connection->hd_cport_id);
	connection->hd_cport_id = CPORT_ID_BAD;
}

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
				u16 cport_id, enum greybus_protocol protocol)
{
	struct gb_connection *connection;
	struct greybus_host_device *hd;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	hd = interface->gmod->hd;
	connection->hd = hd;			/* XXX refcount? */
	if (!gb_connection_hd_cport_id_alloc(connection)) {
		/* kref_put(connection->hd); */
		kfree(connection);
		return NULL;
	}
	connection->hd = hd;			/* XXX refcount? */
	connection->interface = interface;	/* XXX refcount? */
	connection->interface_cport_id = cport_id;
	connection->protocol = protocol;

	spin_lock_irq(&gb_connections_lock);
	_gb_hd_connection_insert(hd, connection);
	list_add_tail(&connection->interface_links, &interface->connections);
	spin_unlock_irq(&gb_connections_lock);

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

	spin_lock_irq(&gb_connections_lock);
	list_del(&connection->interface_links);
	_gb_hd_connection_remove(connection);
	spin_unlock_irq(&gb_connections_lock);

	gb_connection_hd_cport_id_free(connection);
	/* kref_put(connection->interface); */
	/* kref_put(connection->hd); */
	kfree(connection);
}

u16 gb_connection_op_id(struct gb_connection *connection)
{
	return (u16)(atomic_inc_return(&connection->op_cycle) % U16_MAX);
}

void gb_connection_err(struct gb_connection *connection, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_err("greybus: [%hhu:%hhu:%hu]: %pV\n",
		connection->interface->gmod->module_id,
		connection->interface->id,
		connection->interface_cport_id, &vaf);

	va_end(args);
}
