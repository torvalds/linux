/*
 * FIRMWARE Greybus driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/firmware.h>

#include "firmware.h"
#include "greybus.h"


struct gb_firmware {
	struct gb_connection	*connection;
	const struct firmware	*fw;
	u8			protocol_major;
	u8			protocol_minor;
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
 * Otherwise, we keep intf->vendor_id/product_id same as what's passed
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
	if (intf->ddbl1_manufacturer_id != ES2_DDBL1_MFR_ID ||
	    intf->ddbl1_product_id != ES2_DDBL1_PROD_ID ||
	    intf->vendor_id != 0 || intf->product_id != 0)
		return;

	ret = gb_operation_sync(connection, GB_FIRMWARE_TYPE_GET_VID_PID,
				NULL, 0, &response, sizeof(response));
	if (ret) {
		dev_err(&connection->bundle->dev,
			"Firmware get vid/pid operation failed (%d)\n", ret);
		return;
	}

	/*
	 * NOTE: This is hacked, so that the same values of VID/PID can be used
	 * by next firmware level as well. The uevent for bootrom will still
	 * have VID/PID as 0, though after this point the sysfs files will start
	 * showing the updated values. But yeah, that's a bit racy as the same
	 * sysfs files would be showing 0 before this point.
	 */
	intf->vendor_id = le32_to_cpu(response.vendor_id);
	intf->product_id = le32_to_cpu(response.product_id);

	dev_dbg(&connection->bundle->dev, "Firmware got vid (0x%x)/pid (0x%x)\n",
		intf->vendor_id, intf->product_id);
}

/* This returns path of the firmware blob on the disk */
static int download_firmware(struct gb_firmware *firmware, u8 stage)
{
	struct gb_connection *connection = firmware->connection;
	struct gb_interface *intf = connection->bundle->intf;
	char firmware_name[48];
	int rc;

	/* Already have a firmware, free it */
	if (firmware->fw)
		free_firmware(firmware);

	/*
	 * Create firmware name
	 *
	 * XXX Name it properly..
	 */
	snprintf(firmware_name, sizeof(firmware_name),
		 "ara_%08x_%08x_%08x_%08x_%02x.tftf",
		 intf->ddbl1_manufacturer_id, intf->ddbl1_product_id,
		 intf->vendor_id, intf->product_id, stage);

	// FIXME:
	// Turn to dev_dbg later after everyone has valid bootloaders with good
	// ids, but leave this as dev_info for now to make it easier to track
	// down "empty" vid/pid modules.
	dev_info(&connection->bundle->dev, "Firmware file '%s' requested\n",
		 firmware_name);

	rc = request_firmware(&firmware->fw, firmware_name,
		&connection->bundle->dev);
	if (rc)
		dev_err(&connection->bundle->dev,
			"Firware request for %s has failed : %d",
			firmware_name, rc);
	return rc;
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

	dev_dbg(dev, "%s: firmware size %d bytes\n", __func__, size_response->size);

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

	dev_dbg(dev, "responding with firmware (offs = %u, size = %u)\n", offset,
		size);

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
	dev_dbg(dev, "ready to boot: 0x%x, 0\n", status);

	return 0;
}

static int gb_firmware_request_handler(struct gb_operation *op)
{
	u8 type = op->type;

	switch (type) {
	case GB_FIRMWARE_TYPE_FIRMWARE_SIZE:
		return gb_firmware_size_request(op);
	case GB_FIRMWARE_TYPE_GET_FIRMWARE:
		return gb_firmware_get_firmware(op);
	case GB_FIRMWARE_TYPE_READY_TO_BOOT:
		return gb_firmware_ready_to_boot(op);
	default:
		dev_err(&op->connection->bundle->dev,
			"unsupported request: %u\n", type);
		return -EINVAL;
	}
}

static int gb_firmware_get_version(struct gb_firmware *firmware)
{
	struct gb_bundle *bundle = firmware->connection->bundle;
	struct gb_firmware_version_request request;
	struct gb_firmware_version_response response;
	int ret;

	request.major = GB_FIRMWARE_VERSION_MAJOR;
	request.minor = GB_FIRMWARE_VERSION_MINOR;

	ret = gb_operation_sync(firmware->connection,
				GB_FIRMWARE_TYPE_VERSION,
				&request, sizeof(request), &response,
				sizeof(response));
	if (ret) {
		dev_err(&bundle->dev,
				"failed to get protocol version: %d\n",
				ret);
		return ret;
	}

	if (response.major > request.major) {
		dev_err(&bundle->dev,
				"unsupported major protocol version (%u > %u)\n",
				response.major, request.major);
		return -ENOTSUPP;
	}

	firmware->protocol_major = response.major;
	firmware->protocol_minor = response.minor;

	dev_dbg(&bundle->dev, "%s - %u.%u\n", __func__, response.major,
			response.minor);

	return 0;
}

static int gb_firmware_probe(struct gb_bundle *bundle,
					const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct gb_firmware *firmware;
	int ret;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_FIRMWARE)
		return -ENODEV;

	firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware)
		return -ENOMEM;

	connection = gb_connection_create(bundle,
						le16_to_cpu(cport_desc->id),
						gb_firmware_request_handler);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto err_free_firmware;
	}

	connection->private = firmware;

	firmware->connection = connection;

	greybus_set_drvdata(bundle, firmware);

	ret = gb_connection_enable_tx(connection);
	if (ret)
		goto err_connection_destroy;

	ret = gb_firmware_get_version(firmware);
	if (ret)
		goto err_connection_disable;

	firmware_es2_fixup_vid_pid(firmware);

	ret = gb_connection_enable(connection);
	if (ret)
		goto err_connection_disable;

	/* Tell bootrom we're ready. */
	ret = gb_operation_sync(connection, GB_FIRMWARE_TYPE_AP_READY, NULL, 0,
				NULL, 0);
	if (ret) {
		dev_err(&connection->bundle->dev,
				"failed to send AP READY: %d\n", ret);
		goto err_connection_disable;
	}

	dev_dbg(&bundle->dev, "AP_READY sent\n");

	return 0;

err_connection_disable:
	gb_connection_disable(connection);
err_connection_destroy:
	gb_connection_destroy(connection);
err_free_firmware:
	kfree(firmware);

	return ret;
}

static void gb_firmware_disconnect(struct gb_bundle *bundle)
{
	struct gb_firmware *firmware = greybus_get_drvdata(bundle);

	dev_dbg(&bundle->dev, "%s\n", __func__);

	gb_connection_disable(firmware->connection);

	/* Release firmware */
	if (firmware->fw)
		free_firmware(firmware);

	gb_connection_destroy(firmware->connection);
	kfree(firmware);
}

static const struct greybus_bundle_id gb_firmware_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_FIRMWARE) },
	{ }
};

static struct greybus_driver gb_firmware_driver = {
	.name		= "firmware",
	.probe		= gb_firmware_probe,
	.disconnect	= gb_firmware_disconnect,
	.id_table	= gb_firmware_id_table,
};

int gb_firmware_init(void)
{
	return greybus_register(&gb_firmware_driver);
}

void gb_firmware_exit(void)
{
	greybus_deregister(&gb_firmware_driver);
}
