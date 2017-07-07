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
#include "greybus_trace.h"


#define GB_CONNECTION_CPORT_QUIESCE_TIMEOUT	1000


static void gb_connection_kref_release(struct kref *kref);


static DEFINE_SPINLOCK(gb_connections_lock);
static DEFINE_MUTEX(gb_connection_mutex);


/* Caller holds gb_connection_mutex. */
static bool gb_connection_cport_in_use(struct gb_interface *intf, u16 cport_id)
{
	struct gb_host_device *hd = intf->hd;
	struct gb_connection *connection;

	list_for_each_entry(connection, &hd->connections, hd_links) {
		if (connection->intf == intf &&
				connection->intf_cport_id == cport_id)
			return true;
	}

	return false;
}

static void gb_connection_get(struct gb_connection *connection)
{
	kref_get(&connection->kref);

	trace_gb_connection_get(connection);
}

static void gb_connection_put(struct gb_connection *connection)
{
	trace_gb_connection_put(connection);

	kref_put(&connection->kref, gb_connection_kref_release);
}

/*
 * Returns a reference-counted pointer to the connection if found.
 */
static struct gb_connection *
gb_connection_hd_find(struct gb_host_device *hd, u16 cport_id)
{
	struct gb_connection *connection;
	unsigned long flags;

	spin_lock_irqsave(&gb_connections_lock, flags);
	list_for_each_entry(connection, &hd->connections, hd_links)
		if (connection->hd_cport_id == cport_id) {
			gb_connection_get(connection);
			goto found;
		}
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

	trace_gb_hd_in(hd);

	connection = gb_connection_hd_find(hd, cport_id);
	if (!connection) {
		dev_err(&hd->dev,
			"nonexistent connection (%zu bytes dropped)\n", length);
		return;
	}
	gb_connection_recv(connection, data, length);
	gb_connection_put(connection);
}
EXPORT_SYMBOL_GPL(greybus_data_rcvd);

static void gb_connection_kref_release(struct kref *kref)
{
	struct gb_connection *connection;

	connection = container_of(kref, struct gb_connection, kref);

	trace_gb_connection_release(connection);

	kfree(connection);
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
 * _gb_connection_create() - create a Greybus connection
 * @hd:			host device of the connection
 * @hd_cport_id:	host-device cport id, or -1 for dynamic allocation
 * @intf:		remote interface, or NULL for static connections
 * @bundle:		remote-interface bundle (may be NULL)
 * @cport_id:		remote-interface cport id, or 0 for static connections
 * @handler:		request handler (may be NULL)
 * @flags:		connection flags
 *
 * Create a Greybus connection, representing the bidirectional link
 * between a CPort on a (local) Greybus host device and a CPort on
 * another Greybus interface.
 *
 * A connection also maintains the state of operations sent over the
 * connection.
 *
 * Serialised against concurrent create and destroy using the
 * gb_connection_mutex.
 *
 * Return: A pointer to the new connection if successful, or an ERR_PTR
 * otherwise.
 */
static struct gb_connection *
_gb_connection_create(struct gb_host_device *hd, int hd_cport_id,
				struct gb_interface *intf,
				struct gb_bundle *bundle, int cport_id,
				gb_request_handler_t handler,
				unsigned long flags)
{
	struct gb_connection *connection;
	int ret;

	mutex_lock(&gb_connection_mutex);

	if (intf && gb_connection_cport_in_use(intf, cport_id)) {
		dev_err(&intf->dev, "cport %u already in use\n", cport_id);
		ret = -EBUSY;
		goto err_unlock;
	}

	ret = gb_hd_cport_allocate(hd, hd_cport_id, flags);
	if (ret < 0) {
		dev_err(&hd->dev, "failed to allocate cport: %d\n", ret);
		goto err_unlock;
	}
	hd_cport_id = ret;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection) {
		ret = -ENOMEM;
		goto err_hd_cport_release;
	}

	connection->hd_cport_id = hd_cport_id;
	connection->intf_cport_id = cport_id;
	connection->hd = hd;
	connection->intf = intf;
	connection->bundle = bundle;
	connection->handler = handler;
	connection->flags = flags;
	if (intf && (intf->quirks & GB_INTERFACE_QUIRK_NO_CPORT_FEATURES))
		connection->flags |= GB_CONNECTION_FLAG_NO_FLOWCTRL;
	connection->state = GB_CONNECTION_STATE_DISABLED;

	atomic_set(&connection->op_cycle, 0);
	mutex_init(&connection->mutex);
	spin_lock_init(&connection->lock);
	INIT_LIST_HEAD(&connection->operations);

	connection->wq = alloc_workqueue("%s:%d", WQ_UNBOUND, 1,
					 dev_name(&hd->dev), hd_cport_id);
	if (!connection->wq) {
		ret = -ENOMEM;
		goto err_free_connection;
	}

	kref_init(&connection->kref);

	gb_connection_init_name(connection);

	spin_lock_irq(&gb_connections_lock);
	list_add(&connection->hd_links, &hd->connections);

	if (bundle)
		list_add(&connection->bundle_links, &bundle->connections);
	else
		INIT_LIST_HEAD(&connection->bundle_links);

	spin_unlock_irq(&gb_connections_lock);

	mutex_unlock(&gb_connection_mutex);

	trace_gb_connection_create(connection);

	return connection;

err_free_connection:
	kfree(connection);
err_hd_cport_release:
	gb_hd_cport_release(hd, hd_cport_id);
err_unlock:
	mutex_unlock(&gb_connection_mutex);

	return ERR_PTR(ret);
}

struct gb_connection *
gb_connection_create_static(struct gb_host_device *hd, u16 hd_cport_id,
					gb_request_handler_t handler)
{
	return _gb_connection_create(hd, hd_cport_id, NULL, NULL, 0, handler,
					GB_CONNECTION_FLAG_HIGH_PRIO);
}

struct gb_connection *
gb_connection_create_control(struct gb_interface *intf)
{
	return _gb_connection_create(intf->hd, -1, intf, NULL, 0, NULL,
					GB_CONNECTION_FLAG_CONTROL |
					GB_CONNECTION_FLAG_HIGH_PRIO);
}

struct gb_connection *
gb_connection_create(struct gb_bundle *bundle, u16 cport_id,
					gb_request_handler_t handler)
{
	struct gb_interface *intf = bundle->intf;

	return _gb_connection_create(intf->hd, -1, intf, bundle, cport_id,
					handler, 0);
}
EXPORT_SYMBOL_GPL(gb_connection_create);

struct gb_connection *
gb_connection_create_flags(struct gb_bundle *bundle, u16 cport_id,
					gb_request_handler_t handler,
					unsigned long flags)
{
	struct gb_interface *intf = bundle->intf;

	if (WARN_ON_ONCE(flags & GB_CONNECTION_FLAG_CORE_MASK))
		flags &= ~GB_CONNECTION_FLAG_CORE_MASK;

	return _gb_connection_create(intf->hd, -1, intf, bundle, cport_id,
					handler, flags);
}
EXPORT_SYMBOL_GPL(gb_connection_create_flags);

struct gb_connection *
gb_connection_create_offloaded(struct gb_bundle *bundle, u16 cport_id,
					unsigned long flags)
{
	flags |= GB_CONNECTION_FLAG_OFFLOADED;

	return gb_connection_create_flags(bundle, cport_id, NULL, flags);
}
EXPORT_SYMBOL_GPL(gb_connection_create_offloaded);

static int gb_connection_hd_cport_enable(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->cport_enable)
		return 0;

	ret = hd->driver->cport_enable(hd, connection->hd_cport_id,
					connection->flags);
	if (ret) {
		dev_err(&hd->dev, "%s: failed to enable host cport: %d\n",
				connection->name, ret);
		return ret;
	}

	return 0;
}

static void gb_connection_hd_cport_disable(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->cport_disable)
		return;

	ret = hd->driver->cport_disable(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&hd->dev, "%s: failed to disable host cport: %d\n",
				connection->name, ret);
	}
}

static int gb_connection_hd_cport_connected(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->cport_connected)
		return 0;

	ret = hd->driver->cport_connected(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&hd->dev, "%s: failed to set connected state: %d\n",
				connection->name, ret);
		return ret;
	}

	return 0;
}

static int gb_connection_hd_cport_flush(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->cport_flush)
		return 0;

	ret = hd->driver->cport_flush(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&hd->dev, "%s: failed to flush host cport: %d\n",
				connection->name, ret);
		return ret;
	}

	return 0;
}

static int gb_connection_hd_cport_quiesce(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	size_t peer_space;
	int ret;

	if (!hd->driver->cport_quiesce)
		return 0;

	peer_space = sizeof(struct gb_operation_msg_hdr) +
			sizeof(struct gb_cport_shutdown_request);

	if (connection->mode_switch)
		peer_space += sizeof(struct gb_operation_msg_hdr);

	if (!hd->driver->cport_quiesce)
		return 0;

	ret = hd->driver->cport_quiesce(hd, connection->hd_cport_id,
					peer_space,
					GB_CONNECTION_CPORT_QUIESCE_TIMEOUT);
	if (ret) {
		dev_err(&hd->dev, "%s: failed to quiesce host cport: %d\n",
				connection->name, ret);
		return ret;
	}

	return 0;
}

static int gb_connection_hd_cport_clear(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	int ret;

	if (!hd->driver->cport_clear)
		return 0;

	ret = hd->driver->cport_clear(hd, connection->hd_cport_id);
	if (ret) {
		dev_err(&hd->dev, "%s: failed to clear host cport: %d\n",
				connection->name, ret);
		return ret;
	}

	return 0;
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
	u8 cport_flags;
	int ret;

	if (gb_connection_is_static(connection))
		return 0;

	intf = connection->intf;

	/*
	 * Enable either E2EFC or CSD, unless no flow control is requested.
	 */
	cport_flags = GB_SVC_CPORT_FLAG_CSV_N;
	if (gb_connection_flow_control_disabled(connection)) {
		cport_flags |= GB_SVC_CPORT_FLAG_CSD_N;
	} else if (gb_connection_e2efc_enabled(connection)) {
		cport_flags |= GB_SVC_CPORT_FLAG_CSD_N |
				GB_SVC_CPORT_FLAG_E2EFC;
	}

	ret = gb_svc_connection_create(hd->svc,
			hd->svc->ap_intf_id,
			connection->hd_cport_id,
			intf->interface_id,
			connection->intf_cport_id,
			cport_flags);
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
	struct gb_control *control;
	u16 cport_id = connection->intf_cport_id;
	int ret;

	if (gb_connection_is_static(connection))
		return 0;

	if (gb_connection_is_control(connection))
		return 0;

	control = connection->intf->control;

	ret = gb_control_connected_operation(control, cport_id);
	if (ret) {
		dev_err(&connection->bundle->dev,
			"failed to connect cport: %d\n", ret);
		return ret;
	}

	return 0;
}

static void
gb_connection_control_disconnecting(struct gb_connection *connection)
{
	struct gb_control *control;
	u16 cport_id = connection->intf_cport_id;
	int ret;

	if (gb_connection_is_static(connection))
		return;

	control = connection->intf->control;

	ret = gb_control_disconnecting_operation(control, cport_id);
	if (ret) {
		dev_err(&connection->hd->dev,
				"%s: failed to send disconnecting: %d\n",
				connection->name, ret);
	}
}

static void
gb_connection_control_disconnected(struct gb_connection *connection)
{
	struct gb_control *control;
	u16 cport_id = connection->intf_cport_id;
	int ret;

	if (gb_connection_is_static(connection))
		return;

	control = connection->intf->control;

	if (gb_connection_is_control(connection)) {
		if (connection->mode_switch) {
			ret = gb_control_mode_switch_operation(control);
			if (ret) {
				/*
				 * Allow mode switch to time out waiting for
				 * mailbox event.
				 */
				return;
			}
		}

		return;
	}

	ret = gb_control_disconnected_operation(control, cport_id);
	if (ret) {
		dev_warn(&connection->bundle->dev,
			 "failed to disconnect cport: %d\n", ret);
	}
}

static int gb_connection_shutdown_operation(struct gb_connection *connection,
						u8 phase)
{
	struct gb_cport_shutdown_request *req;
	struct gb_operation *operation;
	int ret;

	operation = gb_operation_create_core(connection,
						GB_REQUEST_TYPE_CPORT_SHUTDOWN,
						sizeof(*req), 0, 0,
						GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	req = operation->request->payload;
	req->phase = phase;

	ret = gb_operation_request_send_sync(operation);

	gb_operation_put(operation);

	return ret;
}

static int gb_connection_cport_shutdown(struct gb_connection *connection,
					u8 phase)
{
	struct gb_host_device *hd = connection->hd;
	const struct gb_hd_driver *drv = hd->driver;
	int ret;

	if (gb_connection_is_static(connection))
		return 0;

	if (gb_connection_is_offloaded(connection)) {
		if (!drv->cport_shutdown)
			return 0;

		ret = drv->cport_shutdown(hd, connection->hd_cport_id, phase,
						GB_OPERATION_TIMEOUT_DEFAULT);
	} else {
		ret = gb_connection_shutdown_operation(connection, phase);
	}

	if (ret) {
		dev_err(&hd->dev, "%s: failed to send cport shutdown (phase %d): %d\n",
				connection->name, phase, ret);
		return ret;
	}

	return 0;
}

static int
gb_connection_cport_shutdown_phase_1(struct gb_connection *connection)
{
	return gb_connection_cport_shutdown(connection, 1);
}

static int
gb_connection_cport_shutdown_phase_2(struct gb_connection *connection)
{
	return gb_connection_cport_shutdown(connection, 2);
}

/*
 * Cancel all active operations on a connection.
 *
 * Locking: Called with connection lock held and state set to DISABLED or
 * DISCONNECTING.
 */
static void gb_connection_cancel_operations(struct gb_connection *connection,
						int errno)
	__must_hold(&connection->lock)
{
	struct gb_operation *operation;

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
}

/*
 * Cancel all active incoming operations on a connection.
 *
 * Locking: Called with connection lock held and state set to ENABLED_TX.
 */
static void
gb_connection_flush_incoming_operations(struct gb_connection *connection,
						int errno)
	__must_hold(&connection->lock)
{
	struct gb_operation *operation;
	bool incoming;

	while (!list_empty(&connection->operations)) {
		incoming = false;
		list_for_each_entry(operation, &connection->operations,
								links) {
			if (gb_operation_is_incoming(operation)) {
				gb_operation_get(operation);
				incoming = true;
				break;
			}
		}

		if (!incoming)
			break;

		spin_unlock_irq(&connection->lock);

		/* FIXME: flush, not cancel? */
		gb_operation_cancel_incoming(operation, errno);
		gb_operation_put(operation);

		spin_lock_irq(&connection->lock);
	}
}

/*
 * _gb_connection_enable() - enable a connection
 * @connection:		connection to enable
 * @rx:			whether to enable incoming requests
 *
 * Connection-enable helper for DISABLED->ENABLED, DISABLED->ENABLED_TX, and
 * ENABLED_TX->ENABLED state transitions.
 *
 * Locking: Caller holds connection->mutex.
 */
static int _gb_connection_enable(struct gb_connection *connection, bool rx)
{
	int ret;

	/* Handle ENABLED_TX -> ENABLED transitions. */
	if (connection->state == GB_CONNECTION_STATE_ENABLED_TX) {
		if (!(connection->handler && rx))
			return 0;

		spin_lock_irq(&connection->lock);
		connection->state = GB_CONNECTION_STATE_ENABLED;
		spin_unlock_irq(&connection->lock);

		return 0;
	}

	ret = gb_connection_hd_cport_enable(connection);
	if (ret)
		return ret;

	ret = gb_connection_svc_connection_create(connection);
	if (ret)
		goto err_hd_cport_clear;

	ret = gb_connection_hd_cport_connected(connection);
	if (ret)
		goto err_svc_connection_destroy;

	spin_lock_irq(&connection->lock);
	if (connection->handler && rx)
		connection->state = GB_CONNECTION_STATE_ENABLED;
	else
		connection->state = GB_CONNECTION_STATE_ENABLED_TX;
	spin_unlock_irq(&connection->lock);

	ret = gb_connection_control_connected(connection);
	if (ret)
		goto err_control_disconnecting;

	return 0;

err_control_disconnecting:
	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_DISCONNECTING;
	gb_connection_cancel_operations(connection, -ESHUTDOWN);
	spin_unlock_irq(&connection->lock);

	/* Transmit queue should already be empty. */
	gb_connection_hd_cport_flush(connection);

	gb_connection_control_disconnecting(connection);
	gb_connection_cport_shutdown_phase_1(connection);
	gb_connection_hd_cport_quiesce(connection);
	gb_connection_cport_shutdown_phase_2(connection);
	gb_connection_control_disconnected(connection);
	connection->state = GB_CONNECTION_STATE_DISABLED;
err_svc_connection_destroy:
	gb_connection_svc_connection_destroy(connection);
err_hd_cport_clear:
	gb_connection_hd_cport_clear(connection);

	gb_connection_hd_cport_disable(connection);

	return ret;
}

int gb_connection_enable(struct gb_connection *connection)
{
	int ret = 0;

	mutex_lock(&connection->mutex);

	if (connection->state == GB_CONNECTION_STATE_ENABLED)
		goto out_unlock;

	ret = _gb_connection_enable(connection, true);
	if (!ret)
		trace_gb_connection_enable(connection);

out_unlock:
	mutex_unlock(&connection->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_connection_enable);

int gb_connection_enable_tx(struct gb_connection *connection)
{
	int ret = 0;

	mutex_lock(&connection->mutex);

	if (connection->state == GB_CONNECTION_STATE_ENABLED) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (connection->state == GB_CONNECTION_STATE_ENABLED_TX)
		goto out_unlock;

	ret = _gb_connection_enable(connection, false);
	if (!ret)
		trace_gb_connection_enable(connection);

out_unlock:
	mutex_unlock(&connection->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_connection_enable_tx);

void gb_connection_disable_rx(struct gb_connection *connection)
{
	mutex_lock(&connection->mutex);

	spin_lock_irq(&connection->lock);
	if (connection->state != GB_CONNECTION_STATE_ENABLED) {
		spin_unlock_irq(&connection->lock);
		goto out_unlock;
	}
	connection->state = GB_CONNECTION_STATE_ENABLED_TX;
	gb_connection_flush_incoming_operations(connection, -ESHUTDOWN);
	spin_unlock_irq(&connection->lock);

	trace_gb_connection_disable(connection);

out_unlock:
	mutex_unlock(&connection->mutex);
}
EXPORT_SYMBOL_GPL(gb_connection_disable_rx);

void gb_connection_mode_switch_prepare(struct gb_connection *connection)
{
	connection->mode_switch = true;
}

void gb_connection_mode_switch_complete(struct gb_connection *connection)
{
	gb_connection_svc_connection_destroy(connection);
	gb_connection_hd_cport_clear(connection);

	gb_connection_hd_cport_disable(connection);

	connection->mode_switch = false;
}

void gb_connection_disable(struct gb_connection *connection)
{
	mutex_lock(&connection->mutex);

	if (connection->state == GB_CONNECTION_STATE_DISABLED)
		goto out_unlock;

	trace_gb_connection_disable(connection);

	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_DISCONNECTING;
	gb_connection_cancel_operations(connection, -ESHUTDOWN);
	spin_unlock_irq(&connection->lock);

	gb_connection_hd_cport_flush(connection);

	gb_connection_control_disconnecting(connection);
	gb_connection_cport_shutdown_phase_1(connection);
	gb_connection_hd_cport_quiesce(connection);
	gb_connection_cport_shutdown_phase_2(connection);
	gb_connection_control_disconnected(connection);

	connection->state = GB_CONNECTION_STATE_DISABLED;

	/* control-connection tear down is deferred when mode switching */
	if (!connection->mode_switch) {
		gb_connection_svc_connection_destroy(connection);
		gb_connection_hd_cport_clear(connection);

		gb_connection_hd_cport_disable(connection);
	}

out_unlock:
	mutex_unlock(&connection->mutex);
}
EXPORT_SYMBOL_GPL(gb_connection_disable);

/* Disable a connection without communicating with the remote end. */
void gb_connection_disable_forced(struct gb_connection *connection)
{
	mutex_lock(&connection->mutex);

	if (connection->state == GB_CONNECTION_STATE_DISABLED)
		goto out_unlock;

	trace_gb_connection_disable(connection);

	spin_lock_irq(&connection->lock);
	connection->state = GB_CONNECTION_STATE_DISABLED;
	gb_connection_cancel_operations(connection, -ESHUTDOWN);
	spin_unlock_irq(&connection->lock);

	gb_connection_hd_cport_flush(connection);

	gb_connection_svc_connection_destroy(connection);
	gb_connection_hd_cport_clear(connection);

	gb_connection_hd_cport_disable(connection);
out_unlock:
	mutex_unlock(&connection->mutex);
}
EXPORT_SYMBOL_GPL(gb_connection_disable_forced);

/* Caller must have disabled the connection before destroying it. */
void gb_connection_destroy(struct gb_connection *connection)
{
	if (!connection)
		return;

	if (WARN_ON(connection->state != GB_CONNECTION_STATE_DISABLED))
		gb_connection_disable(connection);

	mutex_lock(&gb_connection_mutex);

	spin_lock_irq(&gb_connections_lock);
	list_del(&connection->bundle_links);
	list_del(&connection->hd_links);
	spin_unlock_irq(&gb_connections_lock);

	destroy_workqueue(connection->wq);

	gb_hd_cport_release(connection->hd, connection->hd_cport_id);
	connection->hd_cport_id = CPORT_ID_BAD;

	mutex_unlock(&gb_connection_mutex);

	gb_connection_put(connection);
}
EXPORT_SYMBOL_GPL(gb_connection_destroy);

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
