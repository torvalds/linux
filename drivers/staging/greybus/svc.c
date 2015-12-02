/*
 * SVC Greybus driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/workqueue.h>

#include "greybus.h"

#define CPORT_FLAGS_E2EFC       BIT(0)
#define CPORT_FLAGS_CSD_N       BIT(1)
#define CPORT_FLAGS_CSV_N       BIT(2)


struct gb_svc_deferred_request {
	struct work_struct work;
	struct gb_operation *operation;
};


static ssize_t endo_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gb_svc *svc = to_gb_svc(dev);

	return sprintf(buf, "0x%04x\n", svc->endo_id);
}
static DEVICE_ATTR_RO(endo_id);

static ssize_t ap_intf_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gb_svc *svc = to_gb_svc(dev);

	return sprintf(buf, "%u\n", svc->ap_intf_id);
}
static DEVICE_ATTR_RO(ap_intf_id);

static struct attribute *svc_attrs[] = {
	&dev_attr_endo_id.attr,
	&dev_attr_ap_intf_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(svc);

static int gb_svc_intf_device_id(struct gb_svc *svc, u8 intf_id, u8 device_id)
{
	struct gb_svc_intf_device_id_request request;

	request.intf_id = intf_id;
	request.device_id = device_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_DEVICE_ID,
				 &request, sizeof(request), NULL, 0);
}

int gb_svc_intf_reset(struct gb_svc *svc, u8 intf_id)
{
	struct gb_svc_intf_reset_request request;

	request.intf_id = intf_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_RESET,
				 &request, sizeof(request), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_reset);

int gb_svc_dme_peer_get(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 *value)
{
	struct gb_svc_dme_peer_get_request request;
	struct gb_svc_dme_peer_get_response response;
	u16 result;
	int ret;

	request.intf_id = intf_id;
	request.attr = cpu_to_le16(attr);
	request.selector = cpu_to_le16(selector);

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_DME_PEER_GET,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to get DME attribute (%hhu %hx %hu): %d\n",
				intf_id, attr, selector, ret);
		return ret;
	}

	result = le16_to_cpu(response.result_code);
	if (result) {
		dev_err(&svc->dev, "UniPro error while getting DME attribute (%hhu %hx %hu): %hu\n",
				intf_id, attr, selector, result);
		return -EINVAL;
	}

	if (value)
		*value = le32_to_cpu(response.attr_value);

	return 0;
}
EXPORT_SYMBOL_GPL(gb_svc_dme_peer_get);

int gb_svc_dme_peer_set(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 value)
{
	struct gb_svc_dme_peer_set_request request;
	struct gb_svc_dme_peer_set_response response;
	u16 result;
	int ret;

	request.intf_id = intf_id;
	request.attr = cpu_to_le16(attr);
	request.selector = cpu_to_le16(selector);
	request.value = cpu_to_le32(value);

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_DME_PEER_SET,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to set DME attribute (%hhu %hx %hu %u): %d\n",
				intf_id, attr, selector, value, ret);
		return ret;
	}

	result = le16_to_cpu(response.result_code);
	if (result) {
		dev_err(&svc->dev, "UniPro error while setting DME attribute (%hhu %hx %hu %u): %hu\n",
				intf_id, attr, selector, value, result);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gb_svc_dme_peer_set);

/*
 * T_TstSrcIncrement is written by the module on ES2 as a stand-in for boot
 * status attribute. AP needs to read and clear it, after reading a non-zero
 * value from it.
 *
 * FIXME: This is module-hardware dependent and needs to be extended for every
 * type of module we want to support.
 */
static int gb_svc_read_and_clear_module_boot_status(struct gb_interface *intf)
{
	struct gb_host_device *hd = intf->hd;
	int ret;
	u32 value;

	/* Read and clear boot status in T_TstSrcIncrement */
	ret = gb_svc_dme_peer_get(hd->svc, intf->interface_id,
				  DME_ATTR_T_TST_SRC_INCREMENT,
				  DME_ATTR_SELECTOR_INDEX, &value);

	if (ret)
		return ret;

	/*
	 * A nonzero boot status indicates the module has finished
	 * booting. Clear it.
	 */
	if (!value) {
		dev_err(&intf->dev, "Module not ready yet\n");
		return -ENODEV;
	}

	/*
	 * Check if the module needs to boot from unipro.
	 * For ES2: We need to check lowest 8 bits of 'value'.
	 * For ES3: We need to check highest 8 bits out of 32 of 'value'.
	 *
	 * FIXME: Add code to find if we are on ES2 or ES3 to have separate
	 * checks.
	 */
	if (value == DME_TSI_UNIPRO_BOOT_STARTED ||
	    value == DME_TSI_FALLBACK_UNIPRO_BOOT_STARTED)
		intf->boot_over_unipro = true;

	return gb_svc_dme_peer_set(hd->svc, intf->interface_id,
				   DME_ATTR_T_TST_SRC_INCREMENT,
				   DME_ATTR_SELECTOR_INDEX, 0);
}

int gb_svc_connection_create(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id,
				bool boot_over_unipro)
{
	struct gb_svc_conn_create_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cpu_to_le16(cport1_id);
	request.intf2_id = intf2_id;
	request.cport2_id = cpu_to_le16(cport2_id);
	/*
	 * XXX: fix connections paramaters to TC0 and all CPort flags
	 * for now.
	 */
	request.tc = 0;

	/*
	 * We need to skip setting E2EFC and other flags to the connection
	 * create request, for all cports, on an interface that need to boot
	 * over unipro, i.e. interfaces required to download firmware.
	 */
	if (boot_over_unipro)
		request.flags = CPORT_FLAGS_CSV_N | CPORT_FLAGS_CSD_N;
	else
		request.flags = CPORT_FLAGS_CSV_N | CPORT_FLAGS_E2EFC;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_CONN_CREATE,
				 &request, sizeof(request), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_svc_connection_create);

void gb_svc_connection_destroy(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			       u8 intf2_id, u16 cport2_id)
{
	struct gb_svc_conn_destroy_request request;
	struct gb_connection *connection = svc->connection;
	int ret;

	request.intf1_id = intf1_id;
	request.cport1_id = cpu_to_le16(cport1_id);
	request.intf2_id = intf2_id;
	request.cport2_id = cpu_to_le16(cport2_id);

	ret = gb_operation_sync(connection, GB_SVC_TYPE_CONN_DESTROY,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		dev_err(&svc->dev, "failed to destroy connection (%hhu:%hu %hhu:%hu): %d\n",
				intf1_id, cport1_id, intf2_id, cport2_id, ret);
	}
}
EXPORT_SYMBOL_GPL(gb_svc_connection_destroy);

/* Creates bi-directional routes between the devices */
static int gb_svc_route_create(struct gb_svc *svc, u8 intf1_id, u8 dev1_id,
			       u8 intf2_id, u8 dev2_id)
{
	struct gb_svc_route_create_request request;

	request.intf1_id = intf1_id;
	request.dev1_id = dev1_id;
	request.intf2_id = intf2_id;
	request.dev2_id = dev2_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_ROUTE_CREATE,
				 &request, sizeof(request), NULL, 0);
}

/* Destroys bi-directional routes between the devices */
static void gb_svc_route_destroy(struct gb_svc *svc, u8 intf1_id, u8 intf2_id)
{
	struct gb_svc_route_destroy_request request;
	int ret;

	request.intf1_id = intf1_id;
	request.intf2_id = intf2_id;

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_ROUTE_DESTROY,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		dev_err(&svc->dev, "failed to destroy route (%hhu %hhu): %d\n",
				intf1_id, intf2_id, ret);
	}
}

static int gb_svc_version_request(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = connection->private;
	struct gb_protocol_version_request *request;
	struct gb_protocol_version_response *response;

	if (op->request->payload_size < sizeof(*request)) {
		dev_err(&svc->dev, "short version request (%zu < %zu)\n",
				op->request->payload_size,
				sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	if (request->major > GB_SVC_VERSION_MAJOR) {
		dev_warn(&svc->dev, "unsupported major version (%hhu > %hhu)\n",
				request->major, GB_SVC_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	connection->module_major = request->major;
	connection->module_minor = request->minor;

	if (!gb_operation_response_alloc(op, sizeof(*response), GFP_KERNEL))
		return -ENOMEM;

	response = op->response->payload;
	response->major = connection->module_major;
	response->minor = connection->module_minor;

	return 0;
}

static int gb_svc_hello(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = connection->private;
	struct gb_svc_hello_request *hello_request;
	int ret;

	if (op->request->payload_size < sizeof(*hello_request)) {
		dev_warn(&svc->dev, "short hello request (%zu < %zu)\n",
				op->request->payload_size,
				sizeof(*hello_request));
		return -EINVAL;
	}

	hello_request = op->request->payload;
	svc->endo_id = le16_to_cpu(hello_request->endo_id);
	svc->ap_intf_id = hello_request->interface_id;

	ret = device_add(&svc->dev);
	if (ret) {
		dev_err(&svc->dev, "failed to register svc device: %d\n", ret);
		return ret;
	}

	return 0;
}

static void gb_svc_intf_remove(struct gb_svc *svc, struct gb_interface *intf)
{
	u8 intf_id = intf->interface_id;
	u8 device_id;

	device_id = intf->device_id;
	gb_interface_remove(intf);

	/*
	 * Destroy the two-way route between the AP and the interface.
	 */
	gb_svc_route_destroy(svc, svc->ap_intf_id, intf_id);

	ida_simple_remove(&svc->device_id_map, device_id);
}

static void gb_svc_process_intf_hotplug(struct gb_operation *operation)
{
	struct gb_svc_intf_hotplug_request *request;
	struct gb_connection *connection = operation->connection;
	struct gb_svc *svc = connection->private;
	struct gb_host_device *hd = connection->hd;
	struct gb_interface *intf;
	u8 intf_id, device_id;
	int ret;

	/* The request message size has already been verified. */
	request = operation->request->payload;
	intf_id = request->intf_id;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, intf_id);

	intf = gb_interface_find(hd, intf_id);
	if (intf) {
		/*
		 * We have received a hotplug request for an interface that
		 * already exists.
		 *
		 * This can happen in cases like:
		 * - bootrom loading the firmware image and booting into that,
		 *   which only generates a hotplug event. i.e. no hot-unplug
		 *   event.
		 * - Or the firmware on the module crashed and sent hotplug
		 *   request again to the SVC, which got propagated to AP.
		 *
		 * Remove the interface and add it again, and let user know
		 * about this with a print message.
		 */
		dev_info(&svc->dev, "removing interface %hhu to add it again\n",
				intf_id);
		gb_svc_intf_remove(svc, intf);
	}

	intf = gb_interface_create(hd, intf_id);
	if (!intf) {
		dev_err(&svc->dev, "failed to create interface %hhu\n",
				intf_id);
		return;
	}

	ret = gb_svc_read_and_clear_module_boot_status(intf);
	if (ret)
		goto destroy_interface;

	intf->unipro_mfg_id = le32_to_cpu(request->data.unipro_mfg_id);
	intf->unipro_prod_id = le32_to_cpu(request->data.unipro_prod_id);
	intf->vendor_id = le32_to_cpu(request->data.ara_vend_id);
	intf->product_id = le32_to_cpu(request->data.ara_prod_id);

	/*
	 * Create a device id for the interface:
	 * - device id 0 (GB_DEVICE_ID_SVC) belongs to the SVC
	 * - device id 1 (GB_DEVICE_ID_AP) belongs to the AP
	 *
	 * XXX Do we need to allocate device ID for SVC or the AP here? And what
	 * XXX about an AP with multiple interface blocks?
	 */
	device_id = ida_simple_get(&svc->device_id_map,
				   GB_DEVICE_ID_MODULES_START, 0, GFP_KERNEL);
	if (device_id < 0) {
		ret = device_id;
		dev_err(&svc->dev, "failed to allocate device id for interface %hhu: %d\n",
				intf_id, ret);
		goto destroy_interface;
	}

	ret = gb_svc_intf_device_id(svc, intf_id, device_id);
	if (ret) {
		dev_err(&svc->dev, "failed to set device id %hhu for interface %hhu: %d\n",
				device_id, intf_id, ret);
		goto ida_put;
	}

	/*
	 * Create a two-way route between the AP and the new interface
	 */
	ret = gb_svc_route_create(svc, svc->ap_intf_id, GB_DEVICE_ID_AP,
				  intf_id, device_id);
	if (ret) {
		dev_err(&svc->dev, "failed to create route to interface %hhu (device id %hhu): %d\n",
				intf_id, device_id, ret);
		goto svc_id_free;
	}

	ret = gb_interface_init(intf, device_id);
	if (ret) {
		dev_err(&svc->dev, "failed to initialize interface %hhu (device id %hhu): %d\n",
				intf_id, device_id, ret);
		goto destroy_route;
	}

	return;

destroy_route:
	gb_svc_route_destroy(svc, svc->ap_intf_id, intf_id);
svc_id_free:
	/*
	 * XXX Should we tell SVC that this id doesn't belong to interface
	 * XXX anymore.
	 */
ida_put:
	ida_simple_remove(&svc->device_id_map, device_id);
destroy_interface:
	gb_interface_remove(intf);
}

static void gb_svc_process_intf_hot_unplug(struct gb_operation *operation)
{
	struct gb_svc *svc = operation->connection->private;
	struct gb_svc_intf_hot_unplug_request *request;
	struct gb_host_device *hd = operation->connection->hd;
	struct gb_interface *intf;
	u8 intf_id;

	/* The request message size has already been verified. */
	request = operation->request->payload;
	intf_id = request->intf_id;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, intf_id);

	intf = gb_interface_find(hd, intf_id);
	if (!intf) {
		dev_warn(&svc->dev, "could not find hot-unplug interface %hhu\n",
				intf_id);
		return;
	}

	gb_svc_intf_remove(svc, intf);
}

static void gb_svc_process_deferred_request(struct work_struct *work)
{
	struct gb_svc_deferred_request *dr;
	struct gb_operation *operation;
	struct gb_svc *svc;
	u8 type;

	dr = container_of(work, struct gb_svc_deferred_request, work);
	operation = dr->operation;
	svc = operation->connection->private;
	type = operation->request->header->type;

	switch (type) {
	case GB_SVC_TYPE_INTF_HOTPLUG:
		gb_svc_process_intf_hotplug(operation);
		break;
	case GB_SVC_TYPE_INTF_HOT_UNPLUG:
		gb_svc_process_intf_hot_unplug(operation);
		break;
	default:
		dev_err(&svc->dev, "bad deferred request type: %02x\n", type);
	}

	gb_operation_put(operation);
	kfree(dr);
}

static int gb_svc_queue_deferred_request(struct gb_operation *operation)
{
	struct gb_svc *svc = operation->connection->private;
	struct gb_svc_deferred_request *dr;

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	gb_operation_get(operation);

	dr->operation = operation;
	INIT_WORK(&dr->work, gb_svc_process_deferred_request);

	queue_work(svc->wq, &dr->work);

	return 0;
}

/*
 * Bringing up a module can be time consuming, as that may require lots of
 * initialization on the module side. Over that, we may also need to download
 * the firmware first and flash that on the module.
 *
 * In order not to make other svc events wait for all this to finish,
 * handle most of module hotplug stuff outside of the hotplug callback, with
 * help of a workqueue.
 */
static int gb_svc_intf_hotplug_recv(struct gb_operation *op)
{
	struct gb_svc *svc = op->connection->private;
	struct gb_svc_intf_hotplug_request *request;

	if (op->request->payload_size < sizeof(*request)) {
		dev_warn(&svc->dev, "short hotplug request received (%zu < %zu)\n",
				op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, request->intf_id);

	return gb_svc_queue_deferred_request(op);
}

static int gb_svc_intf_hot_unplug_recv(struct gb_operation *op)
{
	struct gb_svc *svc = op->connection->private;
	struct gb_svc_intf_hot_unplug_request *request;

	if (op->request->payload_size < sizeof(*request)) {
		dev_warn(&svc->dev, "short hot unplug request received (%zu < %zu)\n",
				op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, request->intf_id);

	return gb_svc_queue_deferred_request(op);
}

static int gb_svc_intf_reset_recv(struct gb_operation *op)
{
	struct gb_svc *svc = op->connection->private;
	struct gb_message *request = op->request;
	struct gb_svc_intf_reset_request *reset;
	u8 intf_id;

	if (request->payload_size < sizeof(*reset)) {
		dev_warn(&svc->dev, "short reset request received (%zu < %zu)\n",
				request->payload_size, sizeof(*reset));
		return -EINVAL;
	}
	reset = request->payload;

	intf_id = reset->intf_id;

	/* FIXME Reset the interface here */

	return 0;
}

static int gb_svc_request_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = connection->private;
	int ret = 0;

	/*
	 * SVC requests need to follow a specific order (at least initially) and
	 * below code takes care of enforcing that. The expected order is:
	 * - PROTOCOL_VERSION
	 * - SVC_HELLO
	 * - Any other request, but the earlier two.
	 *
	 * Incoming requests are guaranteed to be serialized and so we don't
	 * need to protect 'state' for any races.
	 */
	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		if (svc->state != GB_SVC_STATE_RESET)
			ret = -EINVAL;
		break;
	case GB_SVC_TYPE_SVC_HELLO:
		if (svc->state != GB_SVC_STATE_PROTOCOL_VERSION)
			ret = -EINVAL;
		break;
	default:
		if (svc->state != GB_SVC_STATE_SVC_HELLO)
			ret = -EINVAL;
		break;
	}

	if (ret) {
		dev_warn(&svc->dev, "unexpected request 0x%02x received (state %u)\n",
				type, svc->state);
		return ret;
	}

	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		ret = gb_svc_version_request(op);
		if (!ret)
			svc->state = GB_SVC_STATE_PROTOCOL_VERSION;
		return ret;
	case GB_SVC_TYPE_SVC_HELLO:
		ret = gb_svc_hello(op);
		if (!ret)
			svc->state = GB_SVC_STATE_SVC_HELLO;
		return ret;
	case GB_SVC_TYPE_INTF_HOTPLUG:
		return gb_svc_intf_hotplug_recv(op);
	case GB_SVC_TYPE_INTF_HOT_UNPLUG:
		return gb_svc_intf_hot_unplug_recv(op);
	case GB_SVC_TYPE_INTF_RESET:
		return gb_svc_intf_reset_recv(op);
	default:
		dev_warn(&svc->dev, "unsupported request 0x%02x\n", type);
		return -EINVAL;
	}
}

static void gb_svc_release(struct device *dev)
{
	struct gb_svc *svc = to_gb_svc(dev);

	ida_destroy(&svc->device_id_map);
	destroy_workqueue(svc->wq);
	kfree(svc);
}

struct device_type greybus_svc_type = {
	.name		= "greybus_svc",
	.release	= gb_svc_release,
};

static int gb_svc_connection_init(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;
	struct gb_svc *svc;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->wq = alloc_workqueue("%s:svc", WQ_UNBOUND, 1, dev_name(&hd->dev));
	if (!svc->wq) {
		kfree(svc);
		return -ENOMEM;
	}

	svc->dev.parent = &hd->dev;
	svc->dev.bus = &greybus_bus_type;
	svc->dev.type = &greybus_svc_type;
	svc->dev.groups = svc_groups;
	svc->dev.dma_mask = svc->dev.parent->dma_mask;
	device_initialize(&svc->dev);

	dev_set_name(&svc->dev, "%d-svc", hd->bus_id);

	ida_init(&svc->device_id_map);
	svc->state = GB_SVC_STATE_RESET;
	svc->connection = connection;
	connection->private = svc;

	hd->svc = svc;

	return 0;
}

static void gb_svc_connection_exit(struct gb_connection *connection)
{
	struct gb_svc *svc = connection->private;

	if (device_is_registered(&svc->dev))
		device_del(&svc->dev);

	connection->hd->svc = NULL;
	connection->private = NULL;

	put_device(&svc->dev);
}

static struct gb_protocol svc_protocol = {
	.name			= "svc",
	.id			= GREYBUS_PROTOCOL_SVC,
	.major			= GB_SVC_VERSION_MAJOR,
	.minor			= GB_SVC_VERSION_MINOR,
	.connection_init	= gb_svc_connection_init,
	.connection_exit	= gb_svc_connection_exit,
	.request_recv		= gb_svc_request_recv,
	.flags			= GB_PROTOCOL_SKIP_CONTROL_CONNECTED |
				  GB_PROTOCOL_SKIP_CONTROL_DISCONNECTED |
				  GB_PROTOCOL_SKIP_VERSION,
};
gb_builtin_protocol_driver(svc_protocol);
