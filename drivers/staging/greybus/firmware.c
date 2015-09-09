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

struct gb_firmware {
	struct gb_connection	*connection;
	const struct firmware	*fw;
};

static void free_firmware(struct gb_firmware *firmware)
{
	release_firmware(firmware->fw);
	firmware->fw = NULL;
}

/* This returns path of the firmware blob on the disk */
static int download_firmware(struct gb_firmware *firmware, u8 stage)
{
	struct gb_connection *connection = firmware->connection;
	struct gb_interface *intf = connection->bundle->intf;
	char firmware_name[46];

	/* Already have a firmware, free it */
	if (firmware->fw)
		free_firmware(firmware);

	/*
	 * Create firmware name
	 *
	 * XXX Name it properly..
	 */
	snprintf(firmware_name, sizeof(firmware_name),
		 "ara:%08x:%08x:%08x:%08x:%02x.fw",
		 intf->unipro_mfg_id, intf->unipro_prod_id,
		 intf->ara_vend_id, intf->ara_prod_id, stage);

	return request_firmware(&firmware->fw, firmware_name, &connection->dev);
}

static int gb_firmware_size_request(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_firmware *firmware = connection->private;
	struct gb_firmware_size_request *size_request = op->request->payload;
	struct gb_firmware_size_response *size_response;
	struct device *dev = &connection->dev;
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
	struct gb_firmware_get_firmware_request *firmware_request = op->request->payload;
	struct gb_firmware_get_firmware_response *firmware_response;
	struct device *dev = &connection->dev;
	unsigned int offset, size;

	if (op->request->payload_size != sizeof(*firmware_request)) {
		dev_err(dev, "%s: Illegal size of get firmware request (%zu %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*firmware_request));
		return -EINVAL;
	}

	if (!firmware->fw) {
		dev_err(dev, "%s: firmware not available\n", __func__);
		return -EINVAL;
	}

	offset = le32_to_cpu(firmware_request->offset);
	size = le32_to_cpu(firmware_request->size);

	if (!gb_operation_response_alloc(op, sizeof(*firmware_response) + size,
					 GFP_KERNEL)) {
		dev_err(dev, "%s: error allocating response\n", __func__);
		return -ENOMEM;
	}

	firmware_response = op->response->payload;
	memcpy(firmware_response->data, firmware->fw->data + offset, size);

	return 0;
}

static int gb_firmware_ready_to_boot(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_firmware_ready_to_boot_request *rtb_request = op->request->payload;
	struct device *dev = &connection->dev;
	u8 stage, status;

	if (op->request->payload_size != sizeof(*rtb_request)) {
		dev_err(dev, "%s: Illegal size of ready to boot request (%zu %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*rtb_request));
		return -EINVAL;
	}

	stage = rtb_request->stage;
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
		dev_err(&op->connection->dev,
			"unsupported request: %hhu\n", type);
		return -EINVAL;
	}
}

static int gb_firmware_connection_init(struct gb_connection *connection)
{
	struct gb_firmware *firmware;

	firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware)
		return -ENOMEM;

	firmware->connection = connection;
	connection->private = firmware;

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
};
gb_builtin_protocol_driver(firmware_protocol);
