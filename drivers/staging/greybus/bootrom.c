/*
 * BOOTROM Greybus driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/timer.h>

#include "bootrom.h"
#include "greybus.h"

/* Timeout, in jiffies, within which the next request must be received */
#define NEXT_REQ_TIMEOUT_J	msecs_to_jiffies(1000)

struct gb_bootrom {
	struct gb_connection	*connection;
	const struct firmware	*fw;
	u8			protocol_major;
	u8			protocol_minor;
	struct timer_list	timer;
	struct mutex		mutex; /* Protects bootrom->fw */
};

static void free_firmware(struct gb_bootrom *bootrom)
{
	if (!bootrom->fw)
		return;

	release_firmware(bootrom->fw);
	bootrom->fw = NULL;
}

static void gb_bootrom_timedout(unsigned long data)
{
	struct gb_bootrom *bootrom = (struct gb_bootrom *)data;
	struct device *dev = &bootrom->connection->bundle->dev;

	dev_err(dev, "Timed out waiting for request from the Module\n");

	mutex_lock(&bootrom->mutex);
	free_firmware(bootrom);
	mutex_unlock(&bootrom->mutex);

	/* TODO: Power-off Module ? */
}

/*
 * The es2 chip doesn't have VID/PID programmed into the hardware and we need to
 * hack that up to distinguish different modules and their firmware blobs.
 *
 * This fetches VID/PID (over bootrom protocol) for es2 chip only, when VID/PID
 * already sent during hotplug are 0.
 *
 * Otherwise, we keep intf->vendor_id/product_id same as what's passed
 * during hotplug.
 */
static void bootrom_es2_fixup_vid_pid(struct gb_bootrom *bootrom)
{
	struct gb_bootrom_get_vid_pid_response response;
	struct gb_connection *connection = bootrom->connection;
	struct gb_interface *intf = connection->bundle->intf;
	int ret;

	if (!(intf->quirks & GB_INTERFACE_QUIRK_NO_ARA_IDS))
		return;

	ret = gb_operation_sync(connection, GB_BOOTROM_TYPE_GET_VID_PID,
				NULL, 0, &response, sizeof(response));
	if (ret) {
		dev_err(&connection->bundle->dev,
			"Bootrom get vid/pid operation failed (%d)\n", ret);
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

	dev_dbg(&connection->bundle->dev, "Bootrom got vid (0x%x)/pid (0x%x)\n",
		intf->vendor_id, intf->product_id);
}

/* This returns path of the firmware blob on the disk */
static int download_firmware(struct gb_bootrom *bootrom, u8 stage)
{
	struct gb_connection *connection = bootrom->connection;
	struct gb_interface *intf = connection->bundle->intf;
	char firmware_name[48];
	int rc;

	/* Already have a firmware, free it */
	free_firmware(bootrom);

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

	rc = request_firmware(&bootrom->fw, firmware_name,
		&connection->bundle->dev);
	if (rc)
		dev_err(&connection->bundle->dev,
			"Firmware request for %s has failed : %d",
			firmware_name, rc);
	return rc;
}

static int gb_bootrom_firmware_size_request(struct gb_operation *op)
{
	struct gb_bootrom *bootrom = gb_connection_get_data(op->connection);
	struct gb_bootrom_firmware_size_request *size_request = op->request->payload;
	struct gb_bootrom_firmware_size_response *size_response;
	struct device *dev = &op->connection->bundle->dev;
	int ret;

	/* Disable timeouts */
	del_timer_sync(&bootrom->timer);

	if (op->request->payload_size != sizeof(*size_request)) {
		dev_err(dev, "%s: illegal size of firmware size request (%zu != %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*size_request));
		ret = -EINVAL;
		goto mod_timer;
	}

	mutex_lock(&bootrom->mutex);

	ret = download_firmware(bootrom, size_request->stage);
	if (ret) {
		dev_err(dev, "%s: failed to download firmware (%d)\n", __func__,
			ret);
		goto unlock;
	}

	if (!gb_operation_response_alloc(op, sizeof(*size_response),
					 GFP_KERNEL)) {
		dev_err(dev, "%s: error allocating response\n", __func__);
		free_firmware(bootrom);
		ret = -ENOMEM;
		goto unlock;
	}

	size_response = op->response->payload;
	size_response->size = cpu_to_le32(bootrom->fw->size);

	dev_dbg(dev, "%s: firmware size %d bytes\n", __func__, size_response->size);

unlock:
	mutex_unlock(&bootrom->mutex);

mod_timer:
	/* Refresh timeout */
	mod_timer(&bootrom->timer, jiffies + NEXT_REQ_TIMEOUT_J);

	return ret;
}

static int gb_bootrom_get_firmware(struct gb_operation *op)
{
	struct gb_bootrom *bootrom = gb_connection_get_data(op->connection);
	const struct firmware *fw;
	struct gb_bootrom_get_firmware_request *firmware_request;
	struct gb_bootrom_get_firmware_response *firmware_response;
	struct device *dev = &op->connection->bundle->dev;
	unsigned int offset, size;
	int ret = 0;

	/* Disable timeouts */
	del_timer_sync(&bootrom->timer);

	if (op->request->payload_size != sizeof(*firmware_request)) {
		dev_err(dev, "%s: Illegal size of get firmware request (%zu %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*firmware_request));
		ret = -EINVAL;
		goto mod_timer;
	}

	mutex_lock(&bootrom->mutex);

	fw = bootrom->fw;
	if (!fw) {
		dev_err(dev, "%s: firmware not available\n", __func__);
		ret = -EINVAL;
		goto unlock;
	}

	firmware_request = op->request->payload;
	offset = le32_to_cpu(firmware_request->offset);
	size = le32_to_cpu(firmware_request->size);

	if (offset >= fw->size || size > fw->size - offset) {
		dev_warn(dev, "bad firmware request (offs = %u, size = %u)\n",
				offset, size);
		ret = -EINVAL;
		goto unlock;
	}

	if (!gb_operation_response_alloc(op, sizeof(*firmware_response) + size,
					 GFP_KERNEL)) {
		dev_err(dev, "%s: error allocating response\n", __func__);
		ret = -ENOMEM;
		goto unlock;
	}

	firmware_response = op->response->payload;
	memcpy(firmware_response->data, fw->data + offset, size);

	dev_dbg(dev, "responding with firmware (offs = %u, size = %u)\n", offset,
		size);

unlock:
	mutex_unlock(&bootrom->mutex);

mod_timer:
	/* Refresh timeout */
	mod_timer(&bootrom->timer, jiffies + NEXT_REQ_TIMEOUT_J);

	return ret;
}

static int gb_bootrom_ready_to_boot(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_bootrom *bootrom = gb_connection_get_data(connection);
	struct gb_bootrom_ready_to_boot_request *rtb_request;
	struct device *dev = &connection->bundle->dev;
	u8 status;
	int ret = 0;

	/* Disable timeouts */
	del_timer_sync(&bootrom->timer);

	if (op->request->payload_size != sizeof(*rtb_request)) {
		dev_err(dev, "%s: Illegal size of ready to boot request (%zu %zu)\n",
			__func__, op->request->payload_size,
			sizeof(*rtb_request));
		ret = -EINVAL;
		goto mod_timer;
	}

	rtb_request = op->request->payload;
	status = rtb_request->status;

	/* Return error if the blob was invalid */
	if (status == GB_BOOTROM_BOOT_STATUS_INVALID) {
		ret = -EINVAL;
		goto mod_timer;
	}

	/*
	 * XXX Should we return error for insecure firmware?
	 */
	dev_dbg(dev, "ready to boot: 0x%x, 0\n", status);

mod_timer:
	/*
	 * Refresh timeout, the Interface shall load the new personality and
	 * send a new hotplug request, which shall get rid of the bootrom
	 * connection. As that can take some time, increase the timeout a bit.
	 */
	mod_timer(&bootrom->timer, jiffies + 5 * NEXT_REQ_TIMEOUT_J);

	return ret;
}

static int gb_bootrom_request_handler(struct gb_operation *op)
{
	u8 type = op->type;

	switch (type) {
	case GB_BOOTROM_TYPE_FIRMWARE_SIZE:
		return gb_bootrom_firmware_size_request(op);
	case GB_BOOTROM_TYPE_GET_FIRMWARE:
		return gb_bootrom_get_firmware(op);
	case GB_BOOTROM_TYPE_READY_TO_BOOT:
		return gb_bootrom_ready_to_boot(op);
	default:
		dev_err(&op->connection->bundle->dev,
			"unsupported request: %u\n", type);
		return -EINVAL;
	}
}

static int gb_bootrom_get_version(struct gb_bootrom *bootrom)
{
	struct gb_bundle *bundle = bootrom->connection->bundle;
	struct gb_bootrom_version_request request;
	struct gb_bootrom_version_response response;
	int ret;

	request.major = GB_BOOTROM_VERSION_MAJOR;
	request.minor = GB_BOOTROM_VERSION_MINOR;

	ret = gb_operation_sync(bootrom->connection,
				GB_BOOTROM_TYPE_VERSION,
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

	bootrom->protocol_major = response.major;
	bootrom->protocol_minor = response.minor;

	dev_dbg(&bundle->dev, "%s - %u.%u\n", __func__, response.major,
			response.minor);

	return 0;
}

static int gb_bootrom_probe(struct gb_bundle *bundle,
					const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct gb_bootrom *bootrom;
	int ret;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_BOOTROM)
		return -ENODEV;

	bootrom = kzalloc(sizeof(*bootrom), GFP_KERNEL);
	if (!bootrom)
		return -ENOMEM;

	connection = gb_connection_create(bundle,
						le16_to_cpu(cport_desc->id),
						gb_bootrom_request_handler);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto err_free_bootrom;
	}

	gb_connection_set_data(connection, bootrom);

	bootrom->connection = connection;

	mutex_init(&bootrom->mutex);
	init_timer(&bootrom->timer);
	bootrom->timer.function = gb_bootrom_timedout;
	bootrom->timer.data = (unsigned long)bootrom;

	greybus_set_drvdata(bundle, bootrom);

	ret = gb_connection_enable_tx(connection);
	if (ret)
		goto err_connection_destroy;

	ret = gb_bootrom_get_version(bootrom);
	if (ret)
		goto err_connection_disable;

	bootrom_es2_fixup_vid_pid(bootrom);

	ret = gb_connection_enable(connection);
	if (ret)
		goto err_connection_disable;

	/* Tell bootrom we're ready. */
	ret = gb_operation_sync(connection, GB_BOOTROM_TYPE_AP_READY, NULL, 0,
				NULL, 0);
	if (ret) {
		dev_err(&connection->bundle->dev,
				"failed to send AP READY: %d\n", ret);
		goto err_connection_disable;
	}

	/* Refresh timeout */
	mod_timer(&bootrom->timer, jiffies + NEXT_REQ_TIMEOUT_J);

	dev_dbg(&bundle->dev, "AP_READY sent\n");

	return 0;

err_connection_disable:
	gb_connection_disable(connection);
err_connection_destroy:
	gb_connection_destroy(connection);
err_free_bootrom:
	kfree(bootrom);

	return ret;
}

static void gb_bootrom_disconnect(struct gb_bundle *bundle)
{
	struct gb_bootrom *bootrom = greybus_get_drvdata(bundle);

	dev_dbg(&bundle->dev, "%s\n", __func__);

	gb_connection_disable(bootrom->connection);

	/* Disable timeouts */
	del_timer_sync(&bootrom->timer);

	/*
	 * Release firmware:
	 *
	 * As the connection and the timer are already disabled, we don't need
	 * to lock access to bootrom->fw here.
	 */
	free_firmware(bootrom);

	gb_connection_destroy(bootrom->connection);
	kfree(bootrom);
}

static const struct greybus_bundle_id gb_bootrom_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_BOOTROM) },
	{ }
};

static struct greybus_driver gb_bootrom_driver = {
	.name		= "bootrom",
	.probe		= gb_bootrom_probe,
	.disconnect	= gb_bootrom_disconnect,
	.id_table	= gb_bootrom_id_table,
};

int gb_bootrom_init(void)
{
	return greybus_register(&gb_bootrom_driver);
}

void gb_bootrom_exit(void)
{
	greybus_deregister(&gb_bootrom_driver);
}
