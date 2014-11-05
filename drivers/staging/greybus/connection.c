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
			goto found;
	}
	connection = NULL;
 found:
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

	spin_lock(&connection->hd->cport_id_map_lock);
	id = ida_simple_get(ida, 0, HOST_DEV_CPORT_ID_MAX, GFP_KERNEL);
	spin_unlock(&connection->hd->cport_id_map_lock);
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

	spin_lock(&connection->hd->cport_id_map_lock);
	ida_simple_remove(ida, connection->hd_cport_id);
	spin_unlock(&connection->hd->cport_id_map_lock);
	connection->hd_cport_id = CPORT_ID_BAD;
}

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gb_connection *connection = to_gb_connection(dev);

	return sprintf(buf, "%d", connection->state);
}
static DEVICE_ATTR_RO(state);

static ssize_t
protocol_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gb_connection *connection = to_gb_connection(dev);

	return sprintf(buf, "%d", connection->protocol->id);
}
static DEVICE_ATTR_RO(protocol_id);

static struct attribute *connection_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_protocol_id.attr,
	NULL,
};

ATTRIBUTE_GROUPS(connection);

static void gb_connection_release(struct device *dev)
{
	struct gb_connection *connection = to_gb_connection(dev);

	kfree(connection);
}

static struct device_type greybus_connection_type = {
	.name =		"greybus_connection",
	.release =	gb_connection_release,
};

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
				u16 cport_id, u8 protocol_id)
{
	struct gb_connection *connection;
	struct greybus_host_device *hd;
	int retval;
	u8 major = 0;
	u8 minor = 1;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	/* XXX Will have to establish connections to get version */
	connection->protocol = gb_protocol_get(protocol_id, major, minor);
	if (!connection->protocol) {
		pr_err("protocol 0x%02hhx not found\n", protocol_id);
		kfree(connection);
		return NULL;
	}

	hd = interface->gmod->hd;
	connection->hd = hd;
	if (!gb_connection_hd_cport_id_alloc(connection)) {
		gb_protocol_put(connection->protocol);
		kfree(connection);
		return NULL;
	}

	connection->interface = interface;
	connection->interface_cport_id = cport_id;
	connection->state = GB_CONNECTION_STATE_DISABLED;

	connection->dev.parent = &interface->dev;
	connection->dev.driver = NULL;
	connection->dev.bus = &greybus_bus_type;
	connection->dev.type = &greybus_connection_type;
	connection->dev.groups = connection_groups;
	device_initialize(&connection->dev);
	dev_set_name(&connection->dev, "%s:%d",
		     dev_name(&interface->dev), cport_id);

	retval = device_add(&connection->dev);
	if (retval) {
		pr_err("failed to add connection device for cport 0x%04hx\n",
			cport_id);
		gb_connection_hd_cport_id_free(connection);
		gb_protocol_put(connection->protocol);
		put_device(&connection->dev);
		return NULL;
	}

	spin_lock_irq(&gb_connections_lock);
	_gb_hd_connection_insert(hd, connection);
	list_add_tail(&connection->interface_links, &interface->connections);
	spin_unlock_irq(&gb_connections_lock);

	INIT_LIST_HEAD(&connection->operations);
	connection->pending = RB_ROOT;
	atomic_set(&connection->op_cycle, 0);

	return connection;
}

/*
 * Tear down a previously set up connection.
 */
void gb_connection_destroy(struct gb_connection *connection)
{
	struct gb_operation *operation;
	struct gb_operation *next;

	if (WARN_ON(!connection))
		return;

	/* XXX Need to wait for any outstanding requests to complete */
	WARN_ON(!list_empty(&connection->operations));

	list_for_each_entry_safe(operation, next, &connection->operations,
					links) {
		gb_operation_cancel(operation);
	}
	spin_lock_irq(&gb_connections_lock);
	list_del(&connection->interface_links);
	_gb_hd_connection_remove(connection);
	spin_unlock_irq(&gb_connections_lock);

	gb_connection_hd_cport_id_free(connection);
	gb_protocol_put(connection->protocol);

	device_del(&connection->dev);
}

u16 gb_connection_operation_id(struct gb_connection *connection)
{
	return (u16)(atomic_inc_return(&connection->op_cycle) & (int)U16_MAX);
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

int gb_connection_init(struct gb_connection *connection)
{
	int ret;

	if (!connection->protocol) {
		gb_connection_err(connection, "uninitialized connection");
		return -EIO;
	}

	/* Need to enable the connection to initialize it */
	connection->state = GB_CONNECTION_STATE_ENABLED;
	ret = connection->protocol->connection_init(connection);
	if (ret)
		connection->state = GB_CONNECTION_STATE_ERROR;

	return ret;
}

void gb_connection_exit(struct gb_connection *connection)
{
	if (!connection->protocol) {
		gb_connection_err(connection, "uninitialized connection");
		return;
	}
	connection->state = GB_CONNECTION_STATE_DESTROYING;
	connection->protocol->connection_exit(connection);
}
