/*
 * SVC Greybus driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/input.h>
#include <linux/workqueue.h>

#include "greybus.h"

#define SVC_KEY_ARA_BUTTON	KEY_A

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


// FIXME
// This is a hack, we need to do this "right" and clean the interface up
// properly, not just forcibly yank the thing out of the system and hope for the
// best.  But for now, people want their modules to come out without having to
// throw the thing to the ground or get out a screwdriver.
static ssize_t intf_eject_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct gb_svc *svc = to_gb_svc(dev);
	unsigned short intf_id;
	int ret;

	ret = kstrtou16(buf, 10, &intf_id);
	if (ret < 0)
		return ret;

	dev_warn(dev, "Forcibly trying to eject interface %d\n", intf_id);

	ret = gb_svc_intf_eject(svc, intf_id);
	if (ret < 0)
		return ret;

	return len;
}
static DEVICE_ATTR_WO(intf_eject);

static ssize_t watchdog_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct gb_svc *svc = to_gb_svc(dev);

	return sprintf(buf, "%s\n",
		       gb_svc_watchdog_enabled(svc) ? "enabled" : "disabled");
}

static ssize_t watchdog_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t len)
{
	struct gb_svc *svc = to_gb_svc(dev);
	int retval;
	bool user_request;

	retval = strtobool(buf, &user_request);
	if (retval)
		return retval;

	if (user_request)
		retval = gb_svc_watchdog_enable(svc);
	else
		retval = gb_svc_watchdog_disable(svc);
	if (retval)
		return retval;
	return len;
}
static DEVICE_ATTR_RW(watchdog);

static struct attribute *svc_attrs[] = {
	&dev_attr_endo_id.attr,
	&dev_attr_ap_intf_id.attr,
	&dev_attr_intf_eject.attr,
	&dev_attr_watchdog.attr,
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

int gb_svc_intf_eject(struct gb_svc *svc, u8 intf_id)
{
	struct gb_svc_intf_eject_request request;

	request.intf_id = intf_id;

	/*
	 * The pulse width for module release in svc is long so we need to
	 * increase the timeout so the operation will not return to soon.
	 */
	return gb_operation_sync_timeout(svc->connection,
					 GB_SVC_TYPE_INTF_EJECT, &request,
					 sizeof(request), NULL, 0,
					 GB_SVC_EJECT_TIME);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_eject);

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
		dev_err(&svc->dev, "failed to get DME attribute (%u 0x%04x %u): %d\n",
				intf_id, attr, selector, ret);
		return ret;
	}

	result = le16_to_cpu(response.result_code);
	if (result) {
		dev_err(&svc->dev, "UniPro error while getting DME attribute (%u 0x%04x %u): %u\n",
				intf_id, attr, selector, result);
		return -EIO;
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
		dev_err(&svc->dev, "failed to set DME attribute (%u 0x%04x %u %u): %d\n",
				intf_id, attr, selector, value, ret);
		return ret;
	}

	result = le16_to_cpu(response.result_code);
	if (result) {
		dev_err(&svc->dev, "UniPro error while setting DME attribute (%u 0x%04x %u %u): %u\n",
				intf_id, attr, selector, value, result);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gb_svc_dme_peer_set);

/*
 * T_TstSrcIncrement is written by the module on ES2 as a stand-in for boot
 * status attribute ES3_INIT_STATUS. AP needs to read and clear it, after
 * reading a non-zero value from it.
 *
 * FIXME: This is module-hardware dependent and needs to be extended for every
 * type of module we want to support.
 */
static int gb_svc_read_and_clear_module_boot_status(struct gb_interface *intf)
{
	struct gb_host_device *hd = intf->hd;
	int ret;
	u32 value;
	u16 attr;
	u8 init_status;

	/*
	 * Check if the module is ES2 or ES3, and choose attr number
	 * appropriately.
	 * FIXME: Remove ES2 support from the kernel entirely.
	 */
	if (intf->ddbl1_manufacturer_id == ES2_DDBL1_MFR_ID &&
				intf->ddbl1_product_id == ES2_DDBL1_PROD_ID)
		attr = DME_ATTR_T_TST_SRC_INCREMENT;
	else
		attr = DME_ATTR_ES3_INIT_STATUS;

	/* Read and clear boot status in ES3_INIT_STATUS */
	ret = gb_svc_dme_peer_get(hd->svc, intf->interface_id, attr,
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
	 * Check if the module needs to boot from UniPro.
	 * For ES2: We need to check lowest 8 bits of 'value'.
	 * For ES3: We need to check highest 8 bits out of 32 of 'value'.
	 * FIXME: Remove ES2 support from the kernel entirely.
	 */
	if (intf->ddbl1_manufacturer_id == ES2_DDBL1_MFR_ID &&
				intf->ddbl1_product_id == ES2_DDBL1_PROD_ID)
		init_status = value;
	else
		init_status = value >> 24;

	if (init_status == DME_DIS_UNIPRO_BOOT_STARTED ||
				init_status == DME_DIS_FALLBACK_UNIPRO_BOOT_STARTED)
		intf->boot_over_unipro = true;

	return gb_svc_dme_peer_set(hd->svc, intf->interface_id, attr,
				   DME_ATTR_SELECTOR_INDEX, 0);
}

int gb_svc_connection_create(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id,
				u8 cport_flags)
{
	struct gb_svc_conn_create_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cpu_to_le16(cport1_id);
	request.intf2_id = intf2_id;
	request.cport2_id = cpu_to_le16(cport2_id);
	request.tc = 0;		/* TC0 */
	request.flags = cport_flags;

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
		dev_err(&svc->dev, "failed to destroy connection (%u:%u %u:%u): %d\n",
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
		dev_err(&svc->dev, "failed to destroy route (%u %u): %d\n",
				intf1_id, intf2_id, ret);
	}
}

int gb_svc_intf_set_power_mode(struct gb_svc *svc, u8 intf_id, u8 hs_series,
			       u8 tx_mode, u8 tx_gear, u8 tx_nlanes,
			       u8 rx_mode, u8 rx_gear, u8 rx_nlanes,
			       u8 flags, u32 quirks)
{
	struct gb_svc_intf_set_pwrm_request request;
	struct gb_svc_intf_set_pwrm_response response;
	int ret;

	request.intf_id = intf_id;
	request.hs_series = hs_series;
	request.tx_mode = tx_mode;
	request.tx_gear = tx_gear;
	request.tx_nlanes = tx_nlanes;
	request.rx_mode = rx_mode;
	request.rx_gear = rx_gear;
	request.rx_nlanes = rx_nlanes;
	request.flags = flags;
	request.quirks = cpu_to_le32(quirks);

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_SET_PWRM,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret < 0)
		return ret;

	return le16_to_cpu(response.result_code);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_set_power_mode);

int gb_svc_ping(struct gb_svc *svc)
{
	return gb_operation_sync_timeout(svc->connection, GB_SVC_TYPE_PING,
					 NULL, 0, NULL, 0,
					 GB_OPERATION_TIMEOUT_DEFAULT * 2);
}
EXPORT_SYMBOL_GPL(gb_svc_ping);

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
		dev_warn(&svc->dev, "unsupported major version (%u > %u)\n",
				request->major, GB_SVC_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	svc->protocol_major = request->major;
	svc->protocol_minor = request->minor;

	if (!gb_operation_response_alloc(op, sizeof(*response), GFP_KERNEL))
		return -ENOMEM;

	response = op->response->payload;
	response->major = svc->protocol_major;
	response->minor = svc->protocol_minor;

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

	ret = input_register_device(svc->input);
	if (ret) {
		dev_err(&svc->dev, "failed to register input: %d\n", ret);
		device_del(&svc->dev);
		return ret;
	}

	ret = gb_svc_watchdog_create(svc);
	if (ret) {
		dev_err(&svc->dev, "failed to create watchdog: %d\n", ret);
		input_unregister_device(svc->input);
		device_del(&svc->dev);
		return ret;
	}

	return 0;
}

static int gb_svc_interface_route_create(struct gb_svc *svc,
						struct gb_interface *intf)
{
	u8 intf_id = intf->interface_id;
	u8 device_id;
	int ret;

	/*
	 * Create a device id for the interface:
	 * - device id 0 (GB_DEVICE_ID_SVC) belongs to the SVC
	 * - device id 1 (GB_DEVICE_ID_AP) belongs to the AP
	 *
	 * XXX Do we need to allocate device ID for SVC or the AP here? And what
	 * XXX about an AP with multiple interface blocks?
	 */
	ret = ida_simple_get(&svc->device_id_map,
			     GB_DEVICE_ID_MODULES_START, 0, GFP_KERNEL);
	if (ret < 0) {
		dev_err(&svc->dev, "failed to allocate device id for interface %u: %d\n",
				intf_id, ret);
		return ret;
	}
	device_id = ret;

	ret = gb_svc_intf_device_id(svc, intf_id, device_id);
	if (ret) {
		dev_err(&svc->dev, "failed to set device id %u for interface %u: %d\n",
				device_id, intf_id, ret);
		goto err_ida_remove;
	}

	/* Create a two-way route between the AP and the new interface. */
	ret = gb_svc_route_create(svc, svc->ap_intf_id, GB_DEVICE_ID_AP,
				  intf_id, device_id);
	if (ret) {
		dev_err(&svc->dev, "failed to create route to interface %u (device id %u): %d\n",
				intf_id, device_id, ret);
		goto err_svc_id_free;
	}

	intf->device_id = device_id;

	return 0;

err_svc_id_free:
	/*
	 * XXX Should we tell SVC that this id doesn't belong to interface
	 * XXX anymore.
	 */
err_ida_remove:
	ida_simple_remove(&svc->device_id_map, device_id);

	return ret;
}

static void gb_svc_interface_route_destroy(struct gb_svc *svc,
						struct gb_interface *intf)
{
	if (intf->device_id == GB_DEVICE_ID_BAD)
		return;

	gb_svc_route_destroy(svc, svc->ap_intf_id, intf->interface_id);
	ida_simple_remove(&svc->device_id_map, intf->device_id);
	intf->device_id = GB_DEVICE_ID_BAD;
}

static void gb_svc_intf_remove(struct gb_svc *svc, struct gb_interface *intf)
{
	intf->disconnected = true;

	get_device(&intf->dev);

	gb_interface_remove(intf);
	gb_svc_interface_route_destroy(svc, intf);

	put_device(&intf->dev);
}

static void gb_svc_process_intf_hotplug(struct gb_operation *operation)
{
	struct gb_svc_intf_hotplug_request *request;
	struct gb_connection *connection = operation->connection;
	struct gb_svc *svc = connection->private;
	struct gb_host_device *hd = connection->hd;
	struct gb_interface *intf;
	u8 intf_id;
	u32 vendor_id = 0;
	u32 product_id = 0;
	int ret;

	/* The request message size has already been verified. */
	request = operation->request->payload;
	intf_id = request->intf_id;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, intf_id);

	intf = gb_interface_find(hd, intf_id);
	if (intf) {
		/*
		 * For ES2, we need to maintain the same vendor/product ids we
		 * got from bootrom, otherwise userspace can't distinguish
		 * between modules.
		 */
		vendor_id = intf->vendor_id;
		product_id = intf->product_id;

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
		dev_info(&svc->dev, "removing interface %u to add it again\n",
				intf_id);
		gb_svc_intf_remove(svc, intf);
	}

	intf = gb_interface_create(hd, intf_id);
	if (!intf) {
		dev_err(&svc->dev, "failed to create interface %u\n",
				intf_id);
		return;
	}

	intf->ddbl1_manufacturer_id = le32_to_cpu(request->data.ddbl1_mfr_id);
	intf->ddbl1_product_id = le32_to_cpu(request->data.ddbl1_prod_id);
	intf->vendor_id = le32_to_cpu(request->data.ara_vend_id);
	intf->product_id = le32_to_cpu(request->data.ara_prod_id);
	intf->serial_number = le64_to_cpu(request->data.serial_number);

	/*
	 * Use VID/PID specified at hotplug if:
	 * - Bridge ASIC chip isn't ES2
	 * - Received non-zero Vendor/Product ids
	 *
	 * Otherwise, use the ids we received from bootrom.
	 */
	if (intf->ddbl1_manufacturer_id == ES2_DDBL1_MFR_ID &&
	    intf->ddbl1_product_id == ES2_DDBL1_PROD_ID &&
	    intf->vendor_id == 0 && intf->product_id == 0) {
		intf->vendor_id = vendor_id;
		intf->product_id = product_id;
	}

	ret = gb_svc_read_and_clear_module_boot_status(intf);
	if (ret) {
		dev_err(&svc->dev, "failed to clear boot status of interface %u: %d\n",
				intf_id, ret);
		goto out_interface_add;
	}

	ret = gb_svc_interface_route_create(svc, intf);
	if (ret)
		goto out_interface_add;

	ret = gb_interface_init(intf);
	if (ret) {
		dev_err(&svc->dev, "failed to initialize interface %u: %d\n",
				intf_id, ret);
		goto out_interface_add;
	}

out_interface_add:
	gb_interface_add(intf);
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
		dev_warn(&svc->dev, "could not find hot-unplug interface %u\n",
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
		dev_err(&svc->dev, "bad deferred request type: 0x%02x\n", type);
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

static int gb_svc_key_code_map(struct gb_svc *svc, u16 key_code, u16 *code)
{
	switch (key_code) {
	case GB_KEYCODE_ARA:
		*code = SVC_KEY_ARA_BUTTON;
		break;
	default:
		dev_warn(&svc->dev, "unknown keycode received: %u\n", key_code);
		return -EINVAL;
	}

	return 0;
}

static int gb_svc_key_event_recv(struct gb_operation *op)
{
	struct gb_svc *svc = op->connection->private;
	struct gb_message *request = op->request;
	struct gb_svc_key_event_request *key;
	u16 code;
	u8 event;
	int ret;

	if (request->payload_size < sizeof(*key)) {
		dev_warn(&svc->dev, "short key request received (%zu < %zu)\n",
			 request->payload_size, sizeof(*key));
		return -EINVAL;
	}

	key = request->payload;

	ret = gb_svc_key_code_map(svc, le16_to_cpu(key->key_code), &code);
	if (ret < 0)
		return ret;

	event = key->key_event;
	if ((event != GB_SVC_KEY_PRESSED) && (event != GB_SVC_KEY_RELEASED)) {
		dev_warn(&svc->dev, "unknown key event received: %u\n", event);
		return -EINVAL;
	}

	input_report_key(svc->input, code, (event == GB_SVC_KEY_PRESSED));
	input_sync(svc->input);

	return 0;
}

static int gb_svc_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = connection->private;
	u8 type = op->type;
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
	case GB_SVC_TYPE_KEY_EVENT:
		return gb_svc_key_event_recv(op);
	default:
		dev_warn(&svc->dev, "unsupported request 0x%02x\n", type);
		return -EINVAL;
	}
}

static struct input_dev *gb_svc_input_create(struct gb_svc *svc)
{
	struct input_dev *input_dev;

	input_dev = input_allocate_device();
	if (!input_dev)
		return ERR_PTR(-ENOMEM);

	input_dev->name = dev_name(&svc->dev);
	svc->input_phys = kasprintf(GFP_KERNEL, "greybus-%s/input0",
				    input_dev->name);
	if (!svc->input_phys)
		goto err_free_input;

	input_dev->phys = svc->input_phys;
	input_dev->dev.parent = &svc->dev;

	input_set_drvdata(input_dev, svc);

	input_set_capability(input_dev, EV_KEY, SVC_KEY_ARA_BUTTON);

	return input_dev;

err_free_input:
	input_free_device(svc->input);
	return ERR_PTR(-ENOMEM);
}

static void gb_svc_release(struct device *dev)
{
	struct gb_svc *svc = to_gb_svc(dev);

	if (svc->connection)
		gb_connection_destroy(svc->connection);
	ida_destroy(&svc->device_id_map);
	destroy_workqueue(svc->wq);
	kfree(svc->input_phys);
	kfree(svc);
}

struct device_type greybus_svc_type = {
	.name		= "greybus_svc",
	.release	= gb_svc_release,
};

struct gb_svc *gb_svc_create(struct gb_host_device *hd)
{
	struct gb_svc *svc;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return NULL;

	svc->wq = alloc_workqueue("%s:svc", WQ_UNBOUND, 1, dev_name(&hd->dev));
	if (!svc->wq) {
		kfree(svc);
		return NULL;
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
	svc->hd = hd;

	svc->input = gb_svc_input_create(svc);
	if (IS_ERR(svc->input)) {
		dev_err(&svc->dev, "failed to create input device: %ld\n",
			PTR_ERR(svc->input));
		goto err_put_device;
	}

	svc->connection = gb_connection_create_static(hd, GB_SVC_CPORT_ID,
						gb_svc_request_handler);
	if (IS_ERR(svc->connection)) {
		dev_err(&svc->dev, "failed to create connection: %ld\n",
				PTR_ERR(svc->connection));
		goto err_free_input;
	}

	svc->connection->private = svc;

	return svc;

err_free_input:
	input_free_device(svc->input);
err_put_device:
	put_device(&svc->dev);
	return NULL;
}

int gb_svc_add(struct gb_svc *svc)
{
	int ret;

	/*
	 * The SVC protocol is currently driven by the SVC, so the SVC device
	 * is added from the connection request handler when enough
	 * information has been received.
	 */
	ret = gb_connection_enable(svc->connection);
	if (ret)
		return ret;

	return 0;
}

static void gb_svc_remove_interfaces(struct gb_svc *svc)
{
	struct gb_interface *intf, *tmp;

	list_for_each_entry_safe(intf, tmp, &svc->hd->interfaces, links)
		gb_interface_remove(intf);
}

void gb_svc_del(struct gb_svc *svc)
{
	gb_connection_disable(svc->connection);

	/*
	 * The SVC device and input device may have been registered
	 * from the request handler.
	 */
	if (device_is_registered(&svc->dev)) {
		gb_svc_watchdog_destroy(svc);
		input_unregister_device(svc->input);
		device_del(&svc->dev);
	}

	flush_workqueue(svc->wq);

	gb_svc_remove_interfaces(svc);
}

void gb_svc_put(struct gb_svc *svc)
{
	put_device(&svc->dev);
}
