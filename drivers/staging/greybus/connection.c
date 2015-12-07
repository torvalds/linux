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


static int gb_connection_bind_protocol(struct gb_connection *connection);
static void gb_connection_unbind_protocol(struct gb_connection *connection);


static DEFINE_SPINLOCK(gb_connections_lock);

/* This is only used at initialization time; no locking is required. */
static struct gb_connection *
gb_connection_intf_find(struct gb_interface *intf, u16 cport_id)
{
	struct gb_host_device *hd = intf->hd;
	struct gb_connection *connection;

	list_for_each_entry(connection, &hd->connections, hd_links) {
		if (connection->intf == intf &&
				connection->intf_cport_id == cport_id)
			return connection;
	}

	return NULL;
}

static struct gb_connection *
gb_connection_hd_find(struct gb_host_device *hd, u16 cport_id)
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
void greybus_data_rcvd(struct gb_host_device *hd, u16 cport_id,
			u8 *data, size_t length)
{
	struct gb_connection *connection;

	connection = gb_connection_hd_find(hd, cport_id);
	if (!connection) {
		dev_err(&hd->dev,
			"nonexistent connection (%zu bytes dropped)\n", length);
		return;
	}
	gb_connection_recv(connection, data, length);
}
EXPORT_SYMBOL_GPL(greybus_data_rcvd);

static DEFINE_MUTEX(connection_mutex);

static void gb_connection_kref_release(struct kref *kref)
{
	struct gb_connection *connection;

	connection = container_of(kref, struct gb_connection, kref);
	destroy_workqueue(connection->wq);
	kfree(connection);
	mutex_unlock(&connection_mutex);
}

static void gb_connection_init_name(struct gb_connection *connection)
{
	u16 hd_cport_id = connection->hd_cport_id;
	u16 cport_id = 0;
	u8 intf_id = 0;

	if (connection->intf) {
		intf_id = connection->intf->interface_id;
		cport_id = connection->intf_cport_id;
	}

	snprintf(connection->name, sizeof(connection->name),
			"%u/%u:%u", hd_cport_id, intf_id, cport_id);
}

/*
 * gb_connection_create() - create a Greybus connection
 * @hd:			host device of the connection
 * @hd_cport_id:	host-device cport id, or -1 for dynamic allocation
 * @intf:		remote interface, or NULL for static connections
 * @bundle:		remote-interface bundle (may be NULL)
 * @cport_id:		remote-interface cport id, or 0 for static connections
 * @protocol_id:	protocol id
 *
 * Create a Greybus connection, representing the bidirectional link
 * between a CPort on a (local) Greybus host device and a CPort on
 * another Greybus interface.
 *
 * A connection also maintains the state of operations sent over the
 * connection.
 *
 * Return: A pointer to the new connection if successful, or NULL otherwise.
 */
static struct gb_connection *
gb_connection_create(struct gb_host_device *hd, int hd_cport_id,
				struct gb_interface *intf,
				struct gb_bundle *bundle, int cport_id,
				u8 protocol_id)
{
	struct gb_connection *connection;
	struct ida *id_map = &hd->cport_id_map;
	int ida_start, ida_end;
	u8 major = 0;
	u8 minor = 1;

	/*
	 * If a manifest tries to reuse a cport, reject it.  We
	 * initialize connections serially so we don't need to worry
	 * about holding the connection lock.
	 */
	if (bundle && gb_connection_intf_find(bundle->intf, cport_id)) {
		dev_err(&bundle->dev, "cport %u already connected\n",
				cport_id);
		return NULL;
	}

	if (hd_cport_id < 0) {
		ida_start = 0;
		ida_end = hd->num_cports;
	} else if (hd_cport_id < hd->num_cports) {
		ida_start = hd_cport_id;
		ida_end = hd_cport_id + 1;
	} else {
		dev_err(&hd->dev, "cport %d not available\n", hd_cport_id);
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
	connection->intf = intf;

	connection->protocol_id = protocol_id;
	connection->major = major;
	connection->minor = minor;

	connection->bundle = bundle;
	connection->state = GB_CONNECTION_STATE_DISABLED;

	atomic_set(&connection->op_cycle, 0);
	spin_lock_init(&connection->lock);
	INIT_LIST_HEAD(&connection->operations);

	connection->wq = alloc_workqueue("%s:%d", WQ_UNBOUND, 1,
					 dev_name(&hd->dev), hd_cport_id);
	if (!connection->wq)
		goto err_free_connection;

	kref_init(&connection->kref);

	gb_connection_init_name(connection);

	spin_lock_irq(&gb_connections_lock);
	list_add(&connection->hd_links, &hd->connections);

	if (bundle)
		list_add(&connection->bundle_links, &bundle->connections);
	else
		INIT_LIST_HEAD(&connection->bundle_links);

	spin_unlock_irq(&gb_connections_lock);

	return connection;

err_free_connection:
	kfree(connection);
err_remove_ida:
	ida_simple_remove(id_map, hd_cport_id);

	return NULL;
}

struct gb_connection *
gb_connection_create_static(struct gb_host_device *hd,
					u16 hd_cport_id, u8 protocol_id)
{
	return gb_connection_create(hd, hd_cport_id, NULL, NULL, 0,
								protocol_id);
}

struct gb_connection *
gb_connection_create_dynamic(struct gb_interface *intf,
					struct gb_bundle *bundle,
					u16 cport_id, u8 protocol_id)
{
	return gb_connection_create(intf->hd, -1, intf, bundle, cport_id,
								protocol_id);
}

static int gb_connection_hd_cport_enable(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->cport_enable)
		return 0;

	ret = hd->driver->cport_enable(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&hd->dev,
			"failed to enable host cport: %d\n", ret);
		return ret;
	}

	return 0;
}

static void gb_connection_hd_cport_disable(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;

	if (!hd->driver->cport_disable)
		return;

	hd->driver->cport_disable(hd, connection->hd_cport_id);
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
 * Request the SVC to create a connection from AP's cport to interface's
 * cport.
 */
static int
gb_connection_svc_connection_create(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	struct gb_interface *intf;
	int ret;

	if (gb_connection_is_static(connection))
		return 0;

	intf = connection->intf;
	ret = gb_svc_connection_create(hd->svc,
			hd->svc->ap_intf_id,
			connection->hd_cport_id,
			intf->interface_id,
			connection->intf_cport_id,
			intf->boot_over_unipro);
	if (ret) {
		dev_err(&connection->hd->dev,
			"%s: failed to create svc connection: %d\n",
			connection->name, ret);
		return ret;
	}

	return 0;
}

static void
gb_connection_svc_connection_destroy(struct gb_connection *connection)
{
	if (gb_connection_is_static(connection))
		return;

	gb_svc_connection_destroy(connection->hd->svc,
				  connection->hd->svc->ap_intf_id,
				  connection->hd_cport_id,
				  connection->intf->interface_id,
				  connection->intf_cport_id);
}

/* Inform Interface about active CPorts */
static int gb_connection_control_connected(struct gb_connection *connection)
{
	struct gb_protocol *protocol = connection->protocol;
	struct gb_control *control;
	u16 cport_id = connection->intf_cport_id;
	int ret;

	if (protocol->flags & GB_PROTOCOL_SKIP_CONTROL_CONNECTED)
		return 0;

	control = connection->bundle->intf->control;

	ret = gb_control_connected_operation(control, cport_id);
	if (ret) {
		dev_err(&connection->bundle->dev,
			"failed to connect cport: %d\n", ret);
		return ret;
	}

	return 0;
}

/* Inform Interface about inactive CPorts */
static void
gb_connection_control_disconnected(struct gb_connection *connection)
{
	struct gb_protocol *protocol = connection->protocol;
	struct gb_control *control;
	u16 cport_id = connection->intf_cport_id;
	int ret;

	if (protocol->flags & GB_PROTOCOL_SKIP_CONTROL_DISCONNECTED)
		return;

	control = connection->bundle->intf->control;

	ret = gb_control_disconnected_operation(control, cport_id);
	if (ret) {
		dev_warn(&connection->bundle->dev,
			 "failed to disconnect cport: %d\n", ret);
	}
}

/*
 * Request protocol version supported by the module. We don't need to do
 * this for SVC as that is initiated by the SVC.
 */
static int gb_connection_protocol_get_version(struct gb_connection *connection)
{
	struct gb_protocol *protocol = connection->protocol;
	int ret;

	if (protocol->flags & GB_PROTOCOL_SKIP_VERSION)
		return 0;

	ret = gb_protocol_get_version(connection);
	if (ret) {
		dev_err(&connection->bundle->dev,
			"failed to get protocol version: %d\n", ret);
		return ret;
	}

	return 0;
}

int gb_connection_init(struct gb_connection *connection)
{
	int ret;

	ret = gb_connection_bind_protocol(connection);
	if (ret)
		return ret;

	ret = gb_connection_hd_cport_enable(connection);
	if (ret)
		goto err_unbind_protocol;

	ret = gb_connection_svc_connection_create(connection);
	if (ret)
		goto err_hd_cport_disable;

	ret = gb_connection_control_connected(connection);
	if (ret)
		goto err_svc_destroy;

	/* Need to enable the connection to initialize it */
	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_ENABLED;
	spin_unlock_irq(&connection->lock);

	ret = gb_connection_protocol_get_version(connection);
	if (ret)
		goto err_disconnect;

	ret = connection->protocol->connection_init(connection);
	if (ret)
		goto err_disconnect;

	return 0;

err_disconnect:
	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_ERROR;
	spin_unlock_irq(&connection->lock);

	gb_connection_control_disconnected(connection);
err_svc_destroy:
	gb_connection_svc_connection_destroy(connection);
err_hd_cport_disable:
	gb_connection_hd_cport_disable(connection);
err_unbind_protocol:
	gb_connection_unbind_protocol(connection);

	return ret;
}

void gb_connection_exit(struct gb_connection *connection)
{
	spin_lock_irq(&connection->lock);
	if (connection->state != GB_CONNECTION_STATE_ENABLED) {
		spin_unlock_irq(&connection->lock);
		return;
	}
	connection->state = GB_CONNECTION_STATE_DESTROYING;
	spin_unlock_irq(&connection->lock);

	gb_connection_cancel_operations(connection, -ESHUTDOWN);

	connection->protocol->connection_exit(connection);
	gb_connection_control_disconnected(connection);
	gb_connection_svc_connection_destroy(connection);
	gb_connection_hd_cport_disable(connection);
	gb_connection_unbind_protocol(connection);
}

/*
 * Tear down a previously set up connection.
 */
void gb_connection_destroy(struct gb_connection *connection)
{
	struct ida *id_map;

	if (WARN_ON(!connection))
		return;

	gb_connection_exit(connection);

	spin_lock_irq(&gb_connections_lock);
	list_del(&connection->bundle_links);
	list_del(&connection->hd_links);
	spin_unlock_irq(&gb_connections_lock);

	id_map = &connection->hd->cport_id_map;
	ida_simple_remove(id_map, connection->hd_cport_id);
	connection->hd_cport_id = CPORT_ID_BAD;

	kref_put_mutex(&connection->kref, gb_connection_kref_release,
		       &connection_mutex);
}

void gb_connection_latency_tag_enable(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->latency_tag_enable)
		return;

	ret = hd->driver->latency_tag_enable(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&connection->hd->dev,
			"%s: failed to enable latency tag: %d\n",
			connection->name, ret);
	}
}
EXPORT_SYMBOL_GPL(gb_connection_latency_tag_enable);

void gb_connection_latency_tag_disable(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->latency_tag_disable)
		return;

	ret = hd->driver->latency_tag_disable(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&connection->hd->dev,
			"%s: failed to disable latency tag: %d\n",
			connection->name, ret);
	}
}
EXPORT_SYMBOL_GPL(gb_connection_latency_tag_disable);

static int gb_connection_bind_protocol(struct gb_connection *connection)
{
	struct gb_protocol *protocol;

	protocol = gb_protocol_get(connection->protocol_id,
				   connection->major,
				   connection->minor);
	if (!protocol) {
		dev_err(&connection->hd->dev,
				"protocol 0x%02x version %u.%u not found\n",
				connection->protocol_id,
				connection->major, connection->minor);
		return -EPROTONOSUPPORT;
	}
	connection->protocol = protocol;

	return 0;
}

static void gb_connection_unbind_protocol(struct gb_connection *connection)
{
	struct gb_protocol *protocol = connection->protocol;

	gb_protocol_put(protocol);

	connection->protocol = NULL;
}
