/*
 * Greybus connections
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/workqueue.h>

#include "greybus.h"

#define GB_CONNECTION_TS_KFIFO_ELEMENTS	2
#define GB_CONNECTION_TS_KFIFO_LEN \
	(GB_CONNECTION_TS_KFIFO_ELEMENTS * sizeof(struct timeval))

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

void gb_connection_push_timestamp(struct gb_connection *connection)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	kfifo_in_locked(&connection->ts_kfifo, (void *)&tv,
			sizeof(struct timeval), &connection->lock);
}
EXPORT_SYMBOL_GPL(gb_connection_push_timestamp);

int gb_connection_pop_timestamp(struct gb_connection *connection,
				struct timeval *tv)
{
	int retval;

	if (!kfifo_len(&connection->ts_kfifo))
		return -ENOMEM;
	retval = kfifo_out_locked(&connection->ts_kfifo, (void *)tv,
				  sizeof(*tv), &connection->lock);
	return retval;
}
EXPORT_SYMBOL_GPL(gb_connection_pop_timestamp);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gb_connection *connection = to_gb_connection(dev);
	enum gb_connection_state state;

	spin_lock_irq(&connection->lock);
	state = connection->state;
	spin_unlock_irq(&connection->lock);

	return sprintf(buf, "%d\n", state);
}
static DEVICE_ATTR_RO(state);

static ssize_t
protocol_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gb_connection *connection = to_gb_connection(dev);

	if (connection->protocol)
		return sprintf(buf, "%d\n", connection->protocol->id);
	else
		return -EINVAL;
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

	destroy_workqueue(connection->wq);
	kfifo_free(&connection->ts_kfifo);
	kfree(connection);
}

struct device_type greybus_connection_type = {
	.name =		"greybus_connection",
	.release =	gb_connection_release,
};


int svc_update_connection(struct gb_interface *intf,
			  struct gb_connection *connection)
{
	struct gb_bundle *bundle;

	bundle = gb_bundle_create(intf, GB_SVC_BUNDLE_ID, GREYBUS_CLASS_SVC);
	if (!bundle)
		return -EINVAL;

	device_del(&connection->dev);
	connection->bundle = bundle;
	connection->dev.parent = &bundle->dev;
	dev_set_name(&connection->dev, "%s:%d", dev_name(&bundle->dev),
		     GB_SVC_CPORT_ID);

	WARN_ON(device_add(&connection->dev));

	spin_lock_irq(&gb_connections_lock);
	list_add(&connection->bundle_links, &bundle->connections);
	spin_unlock_irq(&gb_connections_lock);

	return 0;
}

void gb_connection_bind_protocol(struct gb_connection *connection)
{
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
	 * If we have a valid device_id for the interface block, then we have an
	 * active device, so bring up the connection at the same time.
	 */
	if ((!connection->bundle &&
	     connection->hd_cport_id == GB_SVC_CPORT_ID) ||
	    connection->bundle->intf->device_id != GB_DEVICE_ID_BAD)
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
struct gb_connection *
gb_connection_create_range(struct greybus_host_device *hd,
			   struct gb_bundle *bundle, struct device *parent,
			   u16 cport_id, u8 protocol_id, u32 ida_start,
			   u32 ida_end)
{
	struct gb_connection *connection;
	struct ida *id_map = &hd->cport_id_map;
	int hd_cport_id;
	int retval;
	u8 major = 0;
	u8 minor = 1;

	/*
	 * If a manifest tries to reuse a cport, reject it.  We
	 * initialize connections serially so we don't need to worry
	 * about holding the connection lock.
	 */
	if (bundle && gb_connection_intf_find(bundle->intf, cport_id)) {
		pr_err("duplicate interface cport id 0x%04hx\n", cport_id);
		return NULL;
	}

	hd_cport_id = ida_simple_get(id_map, ida_start, ida_end, GFP_KERNEL);
	if (hd_cport_id < 0)
		return NULL;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		goto err_remove_ida;

	connection->hd_cport_id = hd_cport_id;
	connection->intf_cport_id = cport_id;
	connection->hd = hd;

	connection->protocol_id = protocol_id;
	connection->major = major;
	connection->minor = minor;

	connection->bundle = bundle;
	connection->state = GB_CONNECTION_STATE_DISABLED;

	atomic_set(&connection->op_cycle, 0);
	spin_lock_init(&connection->lock);
	INIT_LIST_HEAD(&connection->operations);

	connection->wq = alloc_workqueue("%s:%d", WQ_UNBOUND, 1,
					 dev_name(parent), cport_id);
	if (!connection->wq)
		goto err_free_connection;

	if (kfifo_alloc(&connection->ts_kfifo, GB_CONNECTION_TS_KFIFO_LEN,
			GFP_KERNEL))
		goto err_destroy_wq;

	connection->dev.parent = parent;
	connection->dev.bus = &greybus_bus_type;
	connection->dev.type = &greybus_connection_type;
	connection->dev.groups = connection_groups;
	device_initialize(&connection->dev);
	dev_set_name(&connection->dev, "%s:%d",
		     dev_name(parent), cport_id);

	retval = device_add(&connection->dev);
	if (retval) {
		connection->hd_cport_id = CPORT_ID_BAD;
		put_device(&connection->dev);

		pr_err("failed to add connection device for cport 0x%04hx\n",
			cport_id);

		goto err_remove_ida;
	}

	spin_lock_irq(&gb_connections_lock);
	list_add(&connection->hd_links, &hd->connections);

	if (bundle)
		list_add(&connection->bundle_links, &bundle->connections);
	else
		INIT_LIST_HEAD(&connection->bundle_links);

	spin_unlock_irq(&gb_connections_lock);

	if (hd_cport_id != GB_SVC_CPORT_ID) {
		gb_svc_connection_create(hd->svc,
					 hd->endo->ap_intf_id, hd_cport_id,
					 bundle->intf->interface_id, cport_id);
	}

	gb_connection_bind_protocol(connection);
	if (!connection->protocol)
		dev_warn(&connection->dev,
			 "protocol 0x%02hhx handler not found\n", protocol_id);

	return connection;

err_destroy_wq:
	destroy_workqueue(connection->wq);
err_free_connection:
	kfree(connection);
err_remove_ida:
	ida_simple_remove(id_map, hd_cport_id);

	return NULL;
}

struct gb_connection *gb_connection_create(struct gb_bundle *bundle,
				u16 cport_id, u8 protocol_id)
{
	return gb_connection_create_range(bundle->intf->hd, bundle,
					  &bundle->dev, cport_id, protocol_id,
					  0, CPORT_ID_MAX);
}

/*
 * Cancel all active operations on a connection.
 *
 * Should only be called during connection tear down.
 */
static void gb_connection_cancel_operations(struct gb_connection *connection,
						int errno)
{
	struct gb_operation *operation;

	spin_lock_irq(&connection->lock);
	while (!list_empty(&connection->operations)) {
		operation = list_last_entry(&connection->operations,
						struct gb_operation, links);
		gb_operation_get(operation);
		spin_unlock_irq(&connection->lock);

		if (gb_operation_is_incoming(operation))
			gb_operation_cancel_incoming(operation, errno);
		else
			gb_operation_cancel(operation, errno);

		gb_operation_put(operation);

		spin_lock_irq(&connection->lock);
	}
	spin_unlock_irq(&connection->lock);
}

/*
 * Tear down a previously set up connection.
 */
void gb_connection_destroy(struct gb_connection *connection)
{
	struct ida *id_map;

	if (WARN_ON(!connection))
		return;

	spin_lock_irq(&gb_connections_lock);
	list_del(&connection->bundle_links);
	list_del(&connection->hd_links);
	spin_unlock_irq(&gb_connections_lock);

	if (connection->protocol)
		gb_protocol_put(connection->protocol);
	connection->protocol = NULL;

	id_map = &connection->hd->cport_id_map;
	ida_simple_remove(id_map, connection->hd_cport_id);
	connection->hd_cport_id = CPORT_ID_BAD;

	device_unregister(&connection->dev);
}

static void gb_connection_disconnected(struct gb_connection *connection)
{
	struct gb_control *control;
	int cport_id = connection->intf_cport_id;
	int ret;

	/*
	 * Inform Interface about In-active CPorts. We don't need to do this
	 * operation for control cport.
	 */
	if ((cport_id == GB_CONTROL_CPORT_ID) ||
	    (connection->hd_cport_id == GB_SVC_CPORT_ID))
		return;

	control = connection->bundle->intf->control;

	ret = gb_control_disconnected_operation(control, cport_id);
	if (ret)
		dev_warn(&connection->dev,
			"Failed to disconnect CPort-%d (%d)\n", cport_id, ret);
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
	if (cport_id != GB_CONTROL_CPORT_ID &&
	    connection->hd_cport_id != GB_SVC_CPORT_ID) {
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
	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_ENABLED;
	spin_unlock_irq(&connection->lock);

	/*
	 * Request protocol version supported by the module. We don't need to do
	 * this for SVC as that is initiated by the SVC.
	 */
	if (connection->hd_cport_id != GB_SVC_CPORT_ID) {
		ret = gb_protocol_get_version(connection);
		if (ret) {
			dev_err(&connection->dev,
				"Failed to get version CPort-%d (%d)\n",
				cport_id, ret);
			goto disconnect;
		}
	}

	ret = connection->protocol->connection_init(connection);
	if (!ret)
		return 0;

disconnect:
	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_ERROR;
	spin_unlock_irq(&connection->lock);

	gb_connection_disconnected(connection);
	return ret;
}

void gb_connection_exit(struct gb_connection *connection)
{
	if (!connection->protocol) {
		dev_warn(&connection->dev, "exit without protocol.\n");
		return;
	}

	spin_lock_irq(&connection->lock);
	if (connection->state != GB_CONNECTION_STATE_ENABLED) {
		spin_unlock_irq(&connection->lock);
		return;
	}
	connection->state = GB_CONNECTION_STATE_DESTROYING;
	spin_unlock_irq(&connection->lock);

	gb_connection_cancel_operations(connection, -ESHUTDOWN);

	connection->protocol->connection_exit(connection);
	gb_connection_disconnected(connection);
}

void gb_hd_connections_exit(struct greybus_host_device *hd)
{
	struct gb_connection *connection;

	list_for_each_entry(connection, &hd->connections, hd_links) {
		gb_connection_exit(connection);
		gb_connection_destroy(connection);
	}
}
