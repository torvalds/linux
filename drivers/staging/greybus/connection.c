/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static DEFINE_SPINLOCK(gb_connections_lock);

static struct gb_connection *
gb_connection_hd_find(struct greybus_host_device *hd, u16 cport_id)
{
	struct gb_connection *connection = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gb_connections_lock, flags);
	list_for_each_entry(connection, &hd->connections, hd_links)
		if (connection->hd_cport_id == cport_id)
			goto found;
	connection = NULL;
 found:
	spin_unlock_irqrestore(&gb_connections_lock, flags);

	return connection;
}

/*
 * Callback from the host driver to let us know that data has been
 * received on the bundle.
 */
void greybus_data_rcvd(struct greybus_host_device *hd, u16 cport_id,
			u8 *data, size_t length)
{
	struct gb_connection *connection;

	connection = gb_connection_hd_find(hd, cport_id);
	if (!connection) {
		dev_err(hd->parent,
			"nonexistent connection (%zu bytes dropped)\n", length);
		return;
	}
	gb_connection_recv(connection, data, length);
}
EXPORT_SYMBOL_GPL(greybus_data_rcvd);

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

	id = ida_simple_get(ida, 0, HOST_DEV_CPORT_ID_MAX, GFP_ATOMIC);
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

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gb_connection *connection = to_gb_connection(dev);

	return sprintf(buf, "%d\n", connection->state);
}
static DEVICE_ATTR_RO(state);

static ssize_t
protocol_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gb_connection *connection = to_gb_connection(dev);

	return sprintf(buf, "%d\n", connection->protocol->id);
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

struct device_type greybus_connection_type = {
	.name =		"greybus_connection",
	.release =	gb_connection_release,
};


void gb_connection_bind_protocol(struct gb_connection *connection)
{
	struct gb_bundle *bundle;
	struct gb_protocol *protocol;

	/* If we already have a protocol bound here, just return */
	if (connection->protocol)
		return;

	protocol = gb_protocol_get(connection->protocol_id,
				   connection->major,
				   connection->minor);
	if (!protocol)
		return;
	connection->protocol = protocol;

	/*
	 * If we have a valid device_id for the bundle, then we have an active
	 * device, so bring up the connection at the same time.
	 * */
	bundle = connection->bundle;
	if (bundle->device_id != GB_DEVICE_ID_BAD)
		gb_connection_init(connection);
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
struct gb_connection *gb_connection_create(struct gb_bundle *bundle,
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

	connection->protocol_id = protocol_id;
	connection->major = major;
	connection->minor = minor;

	hd = bundle->intf->hd;
	connection->hd = hd;
	if (!gb_connection_hd_cport_id_alloc(connection)) {
		gb_protocol_put(connection->protocol);
		kfree(connection);
		return NULL;
	}

	connection->bundle = bundle;
	connection->intf_cport_id = cport_id;
	connection->state = GB_CONNECTION_STATE_DISABLED;

	connection->dev.parent = &bundle->dev;
	connection->dev.bus = &greybus_bus_type;
	connection->dev.type = &greybus_connection_type;
	connection->dev.groups = connection_groups;
	device_initialize(&connection->dev);
	dev_set_name(&connection->dev, "%s:%d",
		     dev_name(&bundle->dev), cport_id);

	retval = device_add(&connection->dev);
	if (retval) {
		pr_err("failed to add connection device for cport 0x%04hx\n",
			cport_id);
		gb_connection_hd_cport_id_free(connection);
		gb_protocol_put(connection->protocol);
		put_device(&connection->dev);
		kfree(connection);
		return NULL;
	}

	/* XXX Will have to establish connections to get version */
	gb_connection_bind_protocol(connection);
	if (!connection->protocol)
		dev_warn(&bundle->dev,
			 "protocol 0x%02hhx handler not found\n", protocol_id);

	spin_lock_irq(&gb_connections_lock);
	list_add(&connection->hd_links, &hd->connections);
	list_add(&connection->bundle_links, &bundle->connections);
	spin_unlock_irq(&gb_connections_lock);

	atomic_set(&connection->op_cycle, 0);
	INIT_LIST_HEAD(&connection->operations);

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
	if (WARN_ON(!list_empty(&connection->operations))) {
		list_for_each_entry_safe(operation, next,
					 &connection->operations, links)
			gb_operation_cancel(operation, -ESHUTDOWN);
	}
	spin_lock_irq(&gb_connections_lock);
	list_del(&connection->bundle_links);
	list_del(&connection->hd_links);
	spin_unlock_irq(&gb_connections_lock);

	gb_connection_hd_cport_id_free(connection);
	gb_protocol_put(connection->protocol);

	device_unregister(&connection->dev);
}

int gb_connection_init(struct gb_connection *connection)
{
	int ret;

	if (!connection->protocol) {
		dev_warn(&connection->dev, "init without protocol.\n");
		return 0;
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
		dev_warn(&connection->dev, "exit without protocol.\n");
		return;
	}

	if (connection->state != GB_CONNECTION_STATE_ENABLED)
		return;

	connection->state = GB_CONNECTION_STATE_DESTROYING;
	connection->protocol->connection_exit(connection);
}
