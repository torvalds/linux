/*
 * SVC Greybus driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static struct ida greybus_svc_device_id_map;

/* Define get_version() routine */
define_get_version(gb_svc, SVC);

/*
 * AP's SVC cport is required early to get messages from the SVC. This happens
 * even before the Endo is created and hence any modules or interfaces.
 *
 * This is a temporary connection, used only at initial bootup.
 */
struct gb_connection *
gb_ap_svc_connection_create(struct greybus_host_device *hd)
{
	struct gb_connection *connection;

	connection = gb_connection_create_range(hd, NULL, hd->parent,
						GB_SVC_CPORT_ID,
						GREYBUS_PROTOCOL_SVC,
						GB_SVC_CPORT_ID,
						GB_SVC_CPORT_ID + 1);

	return connection;
}
EXPORT_SYMBOL_GPL(gb_ap_svc_connection_create);

/*
 * We know endo-type and AP's interface id now, lets create a proper svc
 * connection (and its interface/bundle) now and get rid of the initial
 * 'partially' initialized one svc connection.
 */
static struct gb_interface *
gb_ap_interface_create(struct greybus_host_device *hd,
		       struct gb_connection *connection, u8 interface_id)
{
	struct gb_interface *intf;
	struct device *dev = &hd->endo->dev;
	int ret;

	intf = gb_interface_create(hd, interface_id);
	if (!intf) {
		dev_err(dev, "%s: Failed to create interface with id %hhu\n",
			__func__, interface_id);
		return NULL;
	}

	intf->device_id = GB_DEVICE_ID_AP;

	/*
	 * XXX: Disable the initial svc connection here, but don't destroy it
	 * yet. We do need to send a response of 'svc-hello message' on that.
	 */

	/* Establish new control CPort connection */
	ret = gb_create_bundle_connection(intf, GREYBUS_CLASS_SVC);
	if (ret) {
		dev_err(&intf->dev, "%s: Failed to create svc connection (%d %d)\n",
			__func__, interface_id, ret);
		gb_interface_destroy(intf);
		intf = NULL;
	}

	return intf;
}

static int intf_device_id_operation(struct gb_svc *svc,
				u8 intf_id, u8 device_id)
{
	struct gb_svc_intf_device_id_request request;

	request.intf_id = intf_id;
	request.device_id = device_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_DEVICE_ID,
				 &request, sizeof(request), NULL, 0);
}

static int intf_reset_operation(struct gb_svc *svc, u8 intf_id)
{
	struct gb_svc_intf_reset_request request;

	request.intf_id = intf_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_RESET,
				 &request, sizeof(request), NULL, 0);
}

static int connection_create_operation(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	struct gb_svc_conn_create_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cport1_id;
	request.intf2_id = intf2_id;
	request.cport2_id = cport2_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_CONN_CREATE,
				 &request, sizeof(request), NULL, 0);
}

static int connection_destroy_operation(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	struct gb_svc_conn_destroy_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cport1_id;
	request.intf2_id = intf2_id;
	request.cport2_id = cport2_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_CONN_DESTROY,
				 &request, sizeof(request), NULL, 0);
}

int gb_svc_intf_device_id(struct gb_svc *svc, u8 intf_id, u8 device_id)
{
	return intf_device_id_operation(svc, intf_id, device_id);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_device_id);

int gb_svc_intf_reset(struct gb_svc *svc, u8 intf_id)
{
	return intf_reset_operation(svc, intf_id);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_reset);

int gb_svc_connection_create(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	return connection_create_operation(svc, intf1_id, cport1_id,
						intf2_id, cport2_id);
}
EXPORT_SYMBOL_GPL(gb_svc_connection_create);

int gb_svc_connection_destroy(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id)
{
	return connection_destroy_operation(svc, intf1_id, cport1_id,
						intf2_id, cport2_id);
}
EXPORT_SYMBOL_GPL(gb_svc_connection_destroy);

static int gb_svc_version_request(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_protocol_version_response *version;
	struct device *dev = &connection->dev;

	version = op->request->payload;

	if (version->major > GB_SVC_VERSION_MAJOR) {
		dev_err(&connection->dev,
			"unsupported major version (%hhu > %hhu)\n",
			version->major, GB_SVC_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	if (!gb_operation_response_alloc(op, sizeof(*version), GFP_KERNEL)) {
		dev_err(dev, "%s: error allocating response\n",
				__func__);
		return -ENOMEM;
	}

	version = op->response->payload;
	version->major = GB_SVC_VERSION_MAJOR;
	version->minor = GB_SVC_VERSION_MINOR;
	return 0;
}

static int gb_svc_hello(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct greybus_host_device *hd = connection->hd;
	struct gb_svc_hello_request *hello_request;
	struct device *dev = &connection->dev;
	struct gb_interface *intf;
	u16 endo_id;
	u8 interface_id;
	int ret;

	/* Hello message should be received only during early bootup */
	WARN_ON(hd->initial_svc_connection != connection);

	/*
	 * SVC sends information about the endo and interface-id on the hello
	 * request, use that to create an endo.
	 */
	if (op->request->payload_size != sizeof(*hello_request)) {
		dev_err(dev, "%s: Illegal size of hello request (%d %d)\n",
			__func__, op->request->payload_size,
			sizeof(*hello_request));
		return -EINVAL;
	}

	hello_request = op->request->payload;
	endo_id = le16_to_cpu(hello_request->endo_id);
	interface_id = hello_request->interface_id;

	/* Setup Endo */
	ret = greybus_endo_setup(hd, endo_id, interface_id);
	if (ret)
		return ret;

	/*
	 * Endo and its modules are ready now, fix AP's partially initialized
	 * svc protocol and its connection.
	 */
	intf = gb_ap_interface_create(hd, connection, interface_id);
	if (!intf) {
		gb_endo_remove(hd->endo);
		return ret;
	}

	return 0;
}

static int gb_svc_intf_hotplug_recv(struct gb_operation *op)
{
	struct gb_message *request = op->request;
	struct gb_svc_intf_hotplug_request *hotplug = request->payload;
	struct gb_svc *svc = op->connection->private;
	struct greybus_host_device *hd = op->connection->bundle->intf->hd;
	struct device *dev = &op->connection->dev;
	struct gb_interface *intf;
	u8 intf_id, device_id;
	u32 unipro_mfg_id;
	u32 unipro_prod_id;
	u32 ara_vend_id;
	u32 ara_prod_id;
	int ret;

	if (request->payload_size < sizeof(*hotplug)) {
		dev_err(dev, "%s: short hotplug request received\n", __func__);
		return -EINVAL;
	}

	/*
	 * Grab the information we need.
	 *
	 * XXX I'd really like to acknowledge receipt, and then
	 * XXX continue processing the request.  There's no need
	 * XXX for the SVC to wait.  In fact, it might be best to
	 * XXX have the SVC get acknowledgement before we proceed.
	 */
	intf_id = hotplug->intf_id;
	unipro_mfg_id = le32_to_cpu(hotplug->data.unipro_mfg_id);
	unipro_prod_id = le32_to_cpu(hotplug->data.unipro_prod_id);
	ara_vend_id = le32_to_cpu(hotplug->data.ara_vend_id);
	ara_prod_id = le32_to_cpu(hotplug->data.ara_prod_id);

	// FIXME May require firmware download
	intf = gb_interface_create(hd, intf_id);
	if (!intf) {
		dev_err(dev, "%s: Failed to create interface with id %hhu\n",
			__func__, intf_id);
		return -EINVAL;
	}

	/*
	 * Create a device id for the interface:
	 * - device id 0 (GB_DEVICE_ID_SVC) belongs to the SVC
	 * - device id 1 (GB_DEVICE_ID_AP) belongs to the AP
	 *
	 * XXX Do we need to allocate device ID for SVC or the AP here? And what
	 * XXX about an AP with multiple interface blocks?
	 */
	device_id = ida_simple_get(&greybus_svc_device_id_map,
				   GB_DEVICE_ID_MODULES_START, 0, GFP_ATOMIC);
	if (device_id < 0) {
		ret = device_id;
		dev_err(dev, "%s: Failed to allocate device id for interface with id %hhu (%d)\n",
			__func__, intf_id, ret);
		goto destroy_interface;
	}

	ret = intf_device_id_operation(svc, intf_id, device_id);
	if (ret) {
		dev_err(dev, "%s: Device id operation failed, interface %hhu device_id %hhu (%d)\n",
			__func__, intf_id, device_id, ret);
		goto ida_put;
	}

	ret = gb_interface_init(intf, device_id);
	if (ret) {
		dev_err(dev, "%s: Failed to initialize interface, interface %hhu device_id %hhu (%d)\n",
			__func__, intf_id, device_id, ret);
		goto svc_id_free;
	}

	return 0;

svc_id_free:
	/*
	 * XXX Should we tell SVC that this id doesn't belong to interface
	 * XXX anymore.
	 */
ida_put:
	ida_simple_remove(&greybus_svc_device_id_map, device_id);
destroy_interface:
	gb_interface_remove(hd, intf_id);

	return ret;
}

static int gb_svc_intf_hot_unplug_recv(struct gb_operation *op)
{
	struct gb_message *request = op->request;
	struct gb_svc_intf_hot_unplug_request *hot_unplug = request->payload;
	struct greybus_host_device *hd = op->connection->bundle->intf->hd;
	struct device *dev = &op->connection->dev;
	u8 device_id;
	struct gb_interface *intf;
	u8 intf_id;

	if (request->payload_size < sizeof(*hot_unplug)) {
		dev_err(&op->connection->dev,
			"short hot unplug request received\n");
		return -EINVAL;
	}

	intf_id = hot_unplug->intf_id;

	intf = gb_interface_find(hd, intf_id);
	if (!intf) {
		dev_err(dev, "%s: Couldn't find interface for id %hhu\n",
			__func__, intf_id);
		return -EINVAL;
	}

	device_id = intf->device_id;
	gb_interface_remove(hd, intf_id);
	ida_simple_remove(&greybus_svc_device_id_map, device_id);

	return 0;
}

static int gb_svc_intf_reset_recv(struct gb_operation *op)
{
	struct gb_message *request = op->request;
	struct gb_svc_intf_reset_request *reset;
	u8 intf_id;

	if (request->payload_size < sizeof(*reset)) {
		dev_err(&op->connection->dev,
			"short reset request received\n");
		return -EINVAL;
	}
	reset = request->payload;

	intf_id = reset->intf_id;

	/* FIXME Reset the interface here */

	return 0;
}

static int gb_svc_request_recv(u8 type, struct gb_operation *op)
{
	switch (type) {
	case GB_SVC_TYPE_PROTOCOL_VERSION:
		return gb_svc_version_request(op);
	case GB_SVC_TYPE_SVC_HELLO:
		return gb_svc_hello(op);
	case GB_SVC_TYPE_INTF_HOTPLUG:
		return gb_svc_intf_hotplug_recv(op);
	case GB_SVC_TYPE_INTF_HOT_UNPLUG:
		return gb_svc_intf_hot_unplug_recv(op);
	case GB_SVC_TYPE_INTF_RESET:
		return gb_svc_intf_reset_recv(op);
	default:
		dev_err(&op->connection->dev,
			"unsupported request: %hhu\n", type);
		return -EINVAL;
	}
}

/*
 * Do initial setup of the SVC.
 */
static int gb_svc_device_setup(struct gb_svc *gb_svc)
{
	/* First thing we need to do is check the version */
	return get_version(gb_svc);
}

static int gb_svc_connection_init(struct gb_connection *connection)
{
	struct gb_svc *svc;
	int ret;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->connection = connection;
	connection->private = svc;

	/*
	 * SVC connection is created twice:
	 * - before the interface-id of the AP and the endo type is known.
	 * - after receiving endo type and interface-id of the AP from the SVC.
	 *
	 * We should do light-weight initialization for the first case.
	 */
	if (!connection->bundle) {
		WARN_ON(connection->hd->initial_svc_connection);
		connection->hd->initial_svc_connection = connection;
		return 0;
	}

	ida_init(&greybus_svc_device_id_map);

	ret = gb_svc_device_setup(svc);
	if (ret)
		kfree(svc);

	/* Set interface's svc connection */
	connection->bundle->intf->svc = svc;

	return ret;
}

static void gb_svc_connection_exit(struct gb_connection *connection)
{
	struct gb_svc *svc = connection->private;

	if (connection->hd->initial_svc_connection == connection) {
		connection->hd->initial_svc_connection = NULL;
	} else {
		if (WARN_ON(connection->bundle->intf->svc != svc))
			return;
		connection->bundle->intf->svc = NULL;
	}

	connection->private = NULL;
	kfree(svc);
}

static struct gb_protocol svc_protocol = {
	.name			= "svc",
	.id			= GREYBUS_PROTOCOL_SVC,
	.major			= GB_SVC_VERSION_MAJOR,
	.minor			= GB_SVC_VERSION_MINOR,
	.connection_init	= gb_svc_connection_init,
	.connection_exit	= gb_svc_connection_exit,
	.request_recv		= gb_svc_request_recv,
};
gb_builtin_protocol_driver(svc_protocol);
