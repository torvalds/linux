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

/* This is only used at initialization time; no locking is required. */
static struct gb_connection *
gb_connection_intf_find(struct gb_interface *intf, u16 cport_id)
{
	struct greybus_host_device *hd = intf->hd;
	struct gb_connection *connection;

	list_for_each_entry(connection, &hd->connections, hd_links)
		if (connection->bundle->intf == intf &&
				connection->intf_cport_id == cport_id)
			return connection;
	return NULL;
}

static struct gb_connection *
gb_connection_hd_find(struct greybus_host_device *hd, u16 cport_id)
{
	struct gb_connection *connection;
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
	struct greybus_host_device *hd = bundle->intf->hd;
	struct ida *id_map = &hd->cport_id_map;
	int retval;
	u8 major = 0;
	u8 minor = 1;

	/*
	 * If a manifest tries to reuse a cport, reject it.  We
	 * initialize connections serially so we don't need to worry
	 * about holding the connection lock.
	 */
	if (gb_connection_intf_find(bundle->intf, cport_id)) {
		pr_err("duplicate interface cport id 0x%04hx\n", cport_id);
		return NULL;
	}

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return NULL;

	retval = ida_simple_get(id_map, 0, CPORT_ID_MAX, GFP_KERNEL);
	if (retval < 0) {
		kfree(connection);
		return NULL;
	}
	connection->hd_cport_id = (u16)retval;
	connection->intf_cport_id = cport_id;
	connection->hd = hd;

	connection->protocol_id = protocol_id;
	connection->major = major;
	connection->minor = minor;

	connection->bundle = bundle;
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
		struct ida *id_map = &connection->hd->cport_id_map;

		ida_simple_remove(id_map, connection->hd_cport_id);
		connection->hd_cport_id = CPORT_ID_BAD;
		put_device(&connection->dev);

		pr_err("failed to add connection device for cport 0x%04hx\n",
			cport_id);

		return NULL;
	}

	spin_lock_irq(&gb_connections_lock);
	list_add(&connection->hd_links, &hd->connections);
	list_add(&connection->bundle_links, &bundle->connections);
	spin_unlock_irq(&gb_connections_lock);

	atomic_set(&connection->op_cycle, 0);
	INIT_LIST_HEAD(&connection->operations);

	/* XXX Will have to establish connections to get version */
	gb_connection_bind_protocol(connection);
	if (!connection->protocol)
		dev_warn(&bundle->dev,
			 "protocol 0x%02hhx handler not found\n", protocol_id);

	return connection;
}

/*
 * Tear down a previously set up connection.
 */
void gb_connection_destroy(struct gb_connection *connection)
{
	struct gb_operation *operation;
	struct gb_operation *next;
	struct ida *id_map;

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

	gb_protocol_put(connection->protocol);
	connection->protocol = NULL;

	id_map = &connection->hd->cport_id_map;
	ida_simple_remove(id_map, connection->hd_cport_id);
	connection->hd_cport_id = CPORT_ID_BAD;

	device_unregister(&connection->dev);
}

int gb_connection_init(struct gb_connection *connection)
{
	int cport_id = connection->intf_cport_id;
	int ret;

	if (!connection->protocol) {
		dev_warn(&connection->dev, "init without protocol.\n");
		return 0;
	}

	/*
	 * Inform Interface about Active CPorts. We don't need to do this
	 * operation for control cport.
	 */
	if (cport_id != GB_CONTROL_CPORT_ID) {
		struct gb_control *control = connection->bundle->intf->control;

		ret = gb_control_connected_operation(control, cport_id);
		if (ret) {
			dev_warn(&connection->dev,
				 "Failed to connect CPort-%d (%d)\n",
				 cport_id, ret);
			return 0;
		}
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
	int cport_id = connection->intf_cport_id;

	if (!connection->protocol) {
		dev_warn(&connection->dev, "exit without protocol.\n");
		return;
	}

	if (connection->state != GB_CONNECTION_STATE_ENABLED)
		return;

	connection->state = GB_CONNECTION_STATE_DESTROYING;
	connection->protocol->connection_exit(connection);

	/*
	 * Inform Interface about In-active CPorts. We don't need to do this
	 * operation for control cport.
	 */
	if (cport_id != GB_CONTROL_CPORT_ID) {
		struct gb_control *control = connection->bundle->intf->control;
		int ret;

		ret = gb_control_disconnected_operation(control, cport_id);
		if (ret)
			dev_warn(&connection->dev,
				 "Failed to disconnect CPort-%d (%d)\n",
				 cport_id, ret);
	}
}
