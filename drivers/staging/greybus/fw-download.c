/*
 * Greybus Firmware Download Protocol Driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/firmware.h>
#include "firmware.h"
#include "greybus.h"

/* Length of the string in format: ara_%08x_%08x_%08x_%08x_%s.tftf */
#define FW_NAME_LEN		56

struct fw_request {
	u8			firmware_id;
	char			name[FW_NAME_LEN];
	const struct firmware	*fw;
	struct list_head	node;
};

struct fw_download {
	struct device		*parent;
	struct gb_connection	*connection;
	struct list_head	fw_requests;
	struct ida		id_map;
};

static struct fw_request *match_firmware(struct fw_download *fw_download,
					 u8 firmware_id)
{
	struct fw_request *fw_req;

	list_for_each_entry(fw_req, &fw_download->fw_requests, node) {
		if (fw_req->firmware_id == firmware_id)
			return fw_req;
	}

	return NULL;
}

static void free_firmware(struct fw_download *fw_download,
			  struct fw_request *fw_req)
{
	list_del(&fw_req->node);
	release_firmware(fw_req->fw);
	ida_simple_remove(&fw_download->id_map, fw_req->firmware_id);
	kfree(fw_req);
}

/* This returns path of the firmware blob on the disk */
static struct fw_request *find_firmware(struct fw_download *fw_download,
					const char *tag)
{
	struct gb_interface *intf = fw_download->connection->bundle->intf;
	struct fw_request *fw_req;
	int ret;

	fw_req = kzalloc(sizeof(*fw_req), GFP_KERNEL);
	if (!fw_req)
		return ERR_PTR(-ENOMEM);

	/* Allocate ids from 1 to 255 (u8-max), 0 is an invalid id */
	ret = ida_simple_get(&fw_download->id_map, 1, 256, GFP_KERNEL);
	if (ret < 0) {
		dev_err(fw_download->parent,
			"failed to allocate firmware id (%d)\n", ret);
		goto err_free_req;
	}
	fw_req->firmware_id = ret;

	snprintf(fw_req->name, sizeof(fw_req->name),
		 "ara_%08x_%08x_%08x_%08x_%s.tftf",
		 intf->ddbl1_manufacturer_id, intf->ddbl1_product_id,
		 intf->vendor_id, intf->product_id, tag);

	dev_info(fw_download->parent, "Requested firmware package '%s'\n",
		 fw_req->name);

	ret = request_firmware(&fw_req->fw, fw_req->name, fw_download->parent);
	if (ret) {
		dev_err(fw_download->parent,
			"firmware request failed for %s (%d)\n", fw_req->name,
			ret);
		goto err_free_id;
	}

	list_add(&fw_req->node, &fw_download->fw_requests);

	return fw_req;

err_free_id:
	ida_simple_remove(&fw_download->id_map, fw_req->firmware_id);
err_free_req:
	kfree(fw_req);

	return ERR_PTR(ret);
}

static int fw_download_find_firmware(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct fw_download *fw_download = gb_connection_get_data(connection);
	struct gb_fw_download_find_firmware_request *request;
	struct gb_fw_download_find_firmware_response *response;
	struct fw_request *fw_req;
	const char *tag;

	if (op->request->payload_size != sizeof(*request)) {
		dev_err(fw_download->parent,
			"illegal size of find firmware request (%zu != %zu)\n",
			op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;
	tag = (const char *)(request->firmware_tag);

	/* firmware_tag should be null-terminated */
	if (strnlen(tag, GB_FIRMWARE_TAG_MAX_LEN) == GB_FIRMWARE_TAG_MAX_LEN) {
		dev_err(fw_download->parent,
			"firmware-tag is not null-terminated\n");
		return -EINVAL;
	}

	fw_req = find_firmware(fw_download, request->firmware_tag);
	if (IS_ERR(fw_req))
		return PTR_ERR(fw_req);

	if (!gb_operation_response_alloc(op, sizeof(*response), GFP_KERNEL)) {
		dev_err(fw_download->parent, "error allocating response\n");
		free_firmware(fw_download, fw_req);
		return -ENOMEM;
	}

	response = op->response->payload;
	response->firmware_id = fw_req->firmware_id;
	response->size = cpu_to_le32(fw_req->fw->size);

	dev_dbg(fw_download->parent,
		"firmware size is %zu bytes\n", fw_req->fw->size);

	return 0;
}

static int fw_download_fetch_firmware(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct fw_download *fw_download = gb_connection_get_data(connection);
	struct gb_fw_download_fetch_firmware_request *request;
	struct gb_fw_download_fetch_firmware_response *response;
	struct fw_request *fw_req;
	const struct firmware *fw;
	unsigned int offset, size;
	u8 firmware_id;

	if (op->request->payload_size != sizeof(*request)) {
		dev_err(fw_download->parent,
			"Illegal size of fetch firmware request (%zu %zu)\n",
			op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;
	offset = le32_to_cpu(request->offset);
	size = le32_to_cpu(request->size);
	firmware_id = request->firmware_id;

	fw_req = match_firmware(fw_download, firmware_id);
	if (!fw_req) {
		dev_err(fw_download->parent,
			"firmware not available for id: %02u\n", firmware_id);
		return -EINVAL;
	}

	fw = fw_req->fw;

	if (offset >= fw->size || size > fw->size - offset) {
		dev_err(fw_download->parent,
			"bad fetch firmware request (offs = %u, size = %u)\n",
			offset, size);
		return -EINVAL;
	}

	if (!gb_operation_response_alloc(op, sizeof(*response) + size,
					 GFP_KERNEL)) {
		dev_err(fw_download->parent,
			"error allocating fetch firmware response\n");
		return -ENOMEM;
	}

	response = op->response->payload;
	memcpy(response->data, fw->data + offset, size);

	dev_dbg(fw_download->parent,
		"responding with firmware (offs = %u, size = %u)\n", offset,
		size);

	return 0;
}

static int fw_download_release_firmware(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct fw_download *fw_download = gb_connection_get_data(connection);
	struct gb_fw_download_release_firmware_request *request;
	struct fw_request *fw_req;
	u8 firmware_id;

	if (op->request->payload_size != sizeof(*request)) {
		dev_err(fw_download->parent,
			"Illegal size of release firmware request (%zu %zu)\n",
			op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;
	firmware_id = request->firmware_id;

	fw_req = match_firmware(fw_download, firmware_id);
	if (!fw_req) {
		dev_err(fw_download->parent,
			"firmware not available for id: %02u\n", firmware_id);
		return -EINVAL;
	}

	free_firmware(fw_download, fw_req);

	dev_dbg(fw_download->parent, "release firmware\n");

	return 0;
}

int gb_fw_download_request_handler(struct gb_operation *op)
{
	u8 type = op->type;

	switch (type) {
	case GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE:
		return fw_download_find_firmware(op);
	case GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE:
		return fw_download_fetch_firmware(op);
	case GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE:
		return fw_download_release_firmware(op);
	default:
		dev_err(&op->connection->bundle->dev,
			"unsupported request: %u\n", type);
		return -EINVAL;
	}
}

int gb_fw_download_connection_init(struct gb_connection *connection)
{
	struct fw_download *fw_download;
	int ret;

	if (!connection)
		return 0;

	fw_download = kzalloc(sizeof(*fw_download), GFP_KERNEL);
	if (!fw_download)
		return -ENOMEM;

	fw_download->parent = &connection->bundle->dev;
	INIT_LIST_HEAD(&fw_download->fw_requests);
	ida_init(&fw_download->id_map);
	gb_connection_set_data(connection, fw_download);
	fw_download->connection = connection;

	ret = gb_connection_enable(connection);
	if (ret)
		goto err_destroy_id_map;

	return 0;

err_destroy_id_map:
	ida_destroy(&fw_download->id_map);
	kfree(fw_download);

	return ret;
}

void gb_fw_download_connection_exit(struct gb_connection *connection)
{
	struct fw_download *fw_download;
	struct fw_request *fw_req, *tmp;

	if (!connection)
		return;

	fw_download = gb_connection_get_data(connection);
	gb_connection_disable(fw_download->connection);

	/* Release pending firmware packages */
	list_for_each_entry_safe(fw_req, tmp, &fw_download->fw_requests, node)
		free_firmware(fw_download, fw_req);

	ida_destroy(&fw_download->id_map);
	kfree(fw_download);
}
