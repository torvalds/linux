/*
 * FIRMWARE Greybus driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/firmware.h>

#include "greybus.h"

#define ES2_UNIPRO_MFG_ID	0x00000126
#define ES2_UNIPRO_PROD_ID	0x00001000

struct gb_firmware {
	struct gb_connection	*connection;
	const struct firmware	*fw;
	u32			vendor_id;
	u32			product_id;
};

static void free_firmware(struct gb_firmware *firmware)
{
	release_firmware(firmware->fw);
	firmware->fw = NULL;
}

/*
 * The es2 chip doesn't have VID/PID programmed into the hardware and we need to
 * hack that up to distinguish different modules and their firmware blobs.
 *
 * This fetches VID/PID (over firmware protocol) for es2 chip only, when VID/PID
 * already sent during hotplug are 0.
 *
 * Otherwise, we keep firmware->vendor_id/product_id same as what's passed
 * during hotplug.
 */
static void firmware_es2_fixup_vid_pid(struct gb_firmware *firmware)
{
	struct gb_firmware_get_vid_pid_response response;
	struct gb_connection *connection = firmware->connection;
	struct gb_interface *intf = connection->bundle->intf;
	int ret;

	/*
	 * Use VID/PID specified at hotplug if:
	 * - Bridge ASIC chip isn't ES2
	 * - Received non-zero Vendor/Product ids
	 */
	if (intf->unipro_mfg_id != ES2_UNIPRO_MFG_ID ||
	    intf->unipro_prod_id != ES2_UNIPRO_PROD_ID ||
	    intf->vendor_id != 0 || intf->product_id != 0)
		return;

	ret = gb_operation_sync(connection, GB_FIRMWARE_TYPE_GET_VID_PID,
				NULL, 0, &response, sizeof(response));
	if (ret) {
		dev_err(&connection->bundle->dev,
			"Firmware get vid/pid operation failed (%d)\n", ret);
		return;
	}

	firmware->vendor_id = le32_to_cpu(response.vendor_id);
	firmware->product_id = le32_to_cpu(response.product_id);
}

/* This returns path of the firmware blob on the disk */
static int download_firmware(struct gb_firmware *firmware, u8 stage)
{
	struct gb_connection *connection = firmware->connection;
	struct gb_interface *intf = connection->bundle->intf;
	char firmware_name[48];

	/* Already have a firmware, free it */
	if (firmware->fw)
		free_firmware(firmware);

	/*
	 * Create firmware name
	 *
	 * XXX Name it properly..
	 */
	snprintf(firmware_name, sizeof(firmware_name),
		 "ara:%08x:%08x:%08x:%08x:%02x.tftf",
		 intf->unipro_mfg_id, intf->unipro_prod_id,
		 firmware->vendor_id, firmware->product_id, stage);

	return request_firmware(&firmware->fw, firmware_name,
				&connection->bundle->dev);
}

static int gb_firmware_size_request(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_firmware *firmware = connection->private;
	struct gb_firmware_size_request *size_request = op->request->payload;
	struct gb_firmware_size_response *size_response;
	struct device *dev = &connection->bundle->dev;
	int ret;

	if (op->request->payload_size != sizeof(*size_request)) {
		dev_err(dev, "%s: illegal size of firmware size request (%zu != %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*size_request));
		return -EINVAL;
	}

	ret = download_firmware(firmware, size_request->stage);
	if (ret) {
		dev_err(dev, "%s: failed to download firmware (%d)\n", __func__,
			ret);
		return ret;
	}

	if (!gb_operation_response_alloc(op, sizeof(*size_response),
					 GFP_KERNEL)) {
		dev_err(dev, "%s: error allocating response\n", __func__);
		free_firmware(firmware);
		return -ENOMEM;
	}

	size_response = op->response->payload;
	size_response->size = cpu_to_le32(firmware->fw->size);

	return 0;
}

static int gb_firmware_get_firmware(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_firmware *firmware = connection->private;
	const struct firmware *fw = firmware->fw;
	struct gb_firmware_get_firmware_request *firmware_request;
	struct gb_firmware_get_firmware_response *firmware_response;
	struct device *dev = &connection->bundle->dev;
	unsigned int offset, size;

	if (op->request->payload_size != sizeof(*firmware_request)) {
		dev_err(dev, "%s: Illegal size of get firmware request (%zu %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*firmware_request));
		return -EINVAL;
	}

	if (!fw) {
		dev_err(dev, "%s: firmware not available\n", __func__);
		return -EINVAL;
	}

	firmware_request = op->request->payload;
	offset = le32_to_cpu(firmware_request->offset);
	size = le32_to_cpu(firmware_request->size);

	if (offset >= fw->size || size > fw->size - offset) {
		dev_warn(dev, "bad firmware request (offs = %u, size = %u)\n",
				offset, size);
		return -EINVAL;
	}

	if (!gb_operation_response_alloc(op, sizeof(*firmware_response) + size,
					 GFP_KERNEL)) {
		dev_err(dev, "%s: error allocating response\n", __func__);
		return -ENOMEM;
	}

	firmware_response = op->response->payload;
	memcpy(firmware_response->data, fw->data + offset, size);

	return 0;
}

static int gb_firmware_ready_to_boot(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_firmware_ready_to_boot_request *rtb_request;
	struct device *dev = &connection->bundle->dev;
	u8 status;

	if (op->request->payload_size != sizeof(*rtb_request)) {
		dev_err(dev, "%s: Illegal size of ready to boot request (%zu %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*rtb_request));
		return -EINVAL;
	}

	rtb_request = op->request->payload;
	status = rtb_request->status;

	/* Return error if the blob was invalid */
	if (status == GB_FIRMWARE_BOOT_STATUS_INVALID)
		return -EINVAL;

	/*
	 * XXX Should we return error for insecure firmware?
	 */

	return 0;
}

static int gb_firmware_request_recv(u8 type, struct gb_operation *op)
{
	switch (type) {
	case GB_FIRMWARE_TYPE_FIRMWARE_SIZE:
		return gb_firmware_size_request(op);
	case GB_FIRMWARE_TYPE_GET_FIRMWARE:
		return gb_firmware_get_firmware(op);
	case GB_FIRMWARE_TYPE_READY_TO_BOOT:
		return gb_firmware_ready_to_boot(op);
	default:
		dev_err(&op->connection->bundle->dev,
			"unsupported request: %hhu\n", type);
		return -EINVAL;
	}
}

static int gb_firmware_connection_init(struct gb_connection *connection)
{
	struct gb_firmware *firmware;
	int ret;

	firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware)
		return -ENOMEM;

	firmware->connection = connection;
	connection->private = firmware;

	firmware->vendor_id = connection->intf->vendor_id;
	firmware->product_id = connection->intf->product_id;

	firmware_es2_fixup_vid_pid(firmware);

	/*
	 * Module's Bootrom needs a way to know (currently), when to start
	 * sending requests to the AP. The version request is sent before this
	 * routine is called, and if the module sends the request right after
	 * receiving version request, the connection->private field will be
	 * NULL.
	 *
	 * Fix this TEMPORARILY by sending an AP_READY request.
	 */
	ret = gb_operation_sync(connection, GB_FIRMWARE_TYPE_AP_READY, NULL, 0,
				NULL, 0);
	if (ret) {
		dev_err(&connection->bundle->dev,
				"failed to send AP READY: %d\n", ret);
	}

	return 0;
}

static void gb_firmware_connection_exit(struct gb_connection *connection)
{
	struct gb_firmware *firmware = connection->private;

	/* Release firmware */
	if (firmware->fw)
		free_firmware(firmware);

	connection->private = NULL;
	kfree(firmware);
}

static struct gb_protocol firmware_protocol = {
	.name			= "firmware",
	.id			= GREYBUS_PROTOCOL_FIRMWARE,
	.major			= GB_FIRMWARE_VERSION_MAJOR,
	.minor			= GB_FIRMWARE_VERSION_MINOR,
	.connection_init	= gb_firmware_connection_init,
	.connection_exit	= gb_firmware_connection_exit,
	.request_recv		= gb_firmware_request_recv,
	.flags			= GB_PROTOCOL_SKIP_CONTROL_DISCONNECTED,
};
gb_builtin_protocol_driver(firmware_protocol);
