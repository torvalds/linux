// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Firmware Download Protocol Driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 */

#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "firmware.h"
#include "greybus.h"

/* Estimated minimum buffer size, actual size can be smaller than this */
#define MIN_FETCH_SIZE		512
/* Timeout, in jiffies, within which fetch or release firmware must be called */
#define NEXT_REQ_TIMEOUT_J	msecs_to_jiffies(1000)

struct fw_request {
	u8			firmware_id;
	bool			disabled;
	bool			timedout;
	char			name[FW_NAME_SIZE];
	const struct firmware	*fw;
	struct list_head	node;

	struct delayed_work	dwork;
	/* Timeout, in jiffies, within which the firmware shall download */
	unsigned long		release_timeout_j;
	struct kref		kref;
	struct fw_download	*fw_download;
};

struct fw_download {
	struct device		*parent;
	struct gb_connection	*connection;
	struct list_head	fw_requests;
	struct ida		id_map;
	struct mutex		mutex;
};

static void fw_req_release(struct kref *kref)
{
	struct fw_request *fw_req = container_of(kref, struct fw_request, kref);

	dev_dbg(fw_req->fw_download->parent, "firmware %s released\n",
		fw_req->name);

	release_firmware(fw_req->fw);

	/*
	 * The request timed out and the module may send a fetch-fw or
	 * release-fw request later. Lets block the id we allocated for this
	 * request, so that the AP doesn't refer to a later fw-request (with
	 * same firmware_id) for the old timedout fw-request.
	 *
	 * NOTE:
	 *
	 * This also means that after 255 timeouts we will fail to service new
	 * firmware downloads. But what else can we do in that case anyway? Lets
	 * just hope that it never happens.
	 */
	if (!fw_req->timedout)
		ida_simple_remove(&fw_req->fw_download->id_map,
				  fw_req->firmware_id);

	kfree(fw_req);
}

/*
 * Incoming requests are serialized for a connection, and the only race possible
 * is between the timeout handler freeing this and an incoming request.
 *
 * The operations on the fw-request list are protected by the mutex and
 * get_fw_req() increments the reference count before returning a fw_req pointer
 * to the users.
 *
 * free_firmware() also takes the mutex while removing an entry from the list,
 * it guarantees that every user of fw_req has taken a kref-reference by now and
 * we wouldn't have any new users.
 *
 * Once the last user drops the reference, the fw_req structure is freed.
 */
static void put_fw_req(struct fw_request *fw_req)
{
	kref_put(&fw_req->kref, fw_req_release);
}

/* Caller must call put_fw_req() after using struct fw_request */
static struct fw_request *get_fw_req(struct fw_download *fw_download,
				     u8 firmware_id)
{
	struct fw_request *fw_req;

	mutex_lock(&fw_download->mutex);

	list_for_each_entry(fw_req, &fw_download->fw_requests, node) {
		if (fw_req->firmware_id == firmware_id) {
			kref_get(&fw_req->kref);
			goto unlock;
		}
	}

	fw_req = NULL;

unlock:
	mutex_unlock(&fw_download->mutex);

	return fw_req;
}

static void free_firmware(struct fw_download *fw_download,
			  struct fw_request *fw_req)
{
	/* Already disabled from timeout handlers */
	if (fw_req->disabled)
		return;

	mutex_lock(&fw_download->mutex);
	list_del(&fw_req->node);
	mutex_unlock(&fw_download->mutex);

	fw_req->disabled = true;
	put_fw_req(fw_req);
}

static void fw_request_timedout(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fw_request *fw_req = container_of(dwork,
						 struct fw_request, dwork);
	struct fw_download *fw_download = fw_req->fw_download;

	dev_err(fw_download->parent,
		"Timed out waiting for fetch / release firmware requests: %u\n",
		fw_req->firmware_id);

	fw_req->timedout = true;
	free_firmware(fw_download, fw_req);
}

static int exceeds_release_timeout(struct fw_request *fw_req)
{
	struct fw_download *fw_download = fw_req->fw_download;

	if (time_before(jiffies, fw_req->release_timeout_j))
		return 0;

	dev_err(fw_download->parent,
		"Firmware download didn't finish in time, abort: %d\n",
		fw_req->firmware_id);

	fw_req->timedout = true;
	free_firmware(fw_download, fw_req);

	return -ETIMEDOUT;
}

/* This returns path of the firmware blob on the disk */
static struct fw_request *find_firmware(struct fw_download *fw_download,
					const char *tag)
{
	struct gb_interface *intf = fw_download->connection->bundle->intf;
	struct fw_request *fw_req;
	int ret, req_count;

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
		 FW_NAME_PREFIX "%08x_%08x_%08x_%08x_%s.tftf",
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

	fw_req->fw_download = fw_download;
	kref_init(&fw_req->kref);

	mutex_lock(&fw_download->mutex);
	list_add(&fw_req->node, &fw_download->fw_requests);
	mutex_unlock(&fw_download->mutex);

	/* Timeout, in jiffies, within which firmware should get loaded */
	req_count = DIV_ROUND_UP(fw_req->fw->size, MIN_FETCH_SIZE);
	fw_req->release_timeout_j = jiffies + req_count * NEXT_REQ_TIMEOUT_J;

	INIT_DELAYED_WORK(&fw_req->dwork, fw_request_timedout);
	schedule_delayed_work(&fw_req->dwork, NEXT_REQ_TIMEOUT_J);

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
	tag = (const char *)request->firmware_tag;

	/* firmware_tag must be null-terminated */
	if (strnlen(tag, GB_FIRMWARE_TAG_MAX_SIZE) ==
	    GB_FIRMWARE_TAG_MAX_SIZE) {
		dev_err(fw_download->parent,
			"firmware-tag is not null-terminated\n");
		return -EINVAL;
	}

	fw_req = find_firmware(fw_download, tag);
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
	int ret = 0;

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

	fw_req = get_fw_req(fw_download, firmware_id);
	if (!fw_req) {
		dev_err(fw_download->parent,
			"firmware not available for id: %02u\n", firmware_id);
		return -EINVAL;
	}

	/* Make sure work handler isn't running in parallel */
	cancel_delayed_work_sync(&fw_req->dwork);

	/* We timed-out before reaching here ? */
	if (fw_req->disabled) {
		ret = -ETIMEDOUT;
		goto put_fw;
	}

	/*
	 * Firmware download must finish within a limited time interval. If it
	 * doesn't, then we might have a buggy Module on the other side. Abort
	 * download.
	 */
	ret = exceeds_release_timeout(fw_req);
	if (ret)
		goto put_fw;

	fw = fw_req->fw;

	if (offset >= fw->size || size > fw->size - offset) {
		dev_err(fw_download->parent,
			"bad fetch firmware request (offs = %u, size = %u)\n",
			offset, size);
		ret = -EINVAL;
		goto put_fw;
	}

	if (!gb_operation_response_alloc(op, sizeof(*response) + size,
					 GFP_KERNEL)) {
		dev_err(fw_download->parent,
			"error allocating fetch firmware response\n");
		ret = -ENOMEM;
		goto put_fw;
	}

	response = op->response->payload;
	memcpy(response->data, fw->data + offset, size);

	dev_dbg(fw_download->parent,
		"responding with firmware (offs = %u, size = %u)\n", offset,
		size);

	/* Refresh timeout */
	schedule_delayed_work(&fw_req->dwork, NEXT_REQ_TIMEOUT_J);

put_fw:
	put_fw_req(fw_req);

	return ret;
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

	fw_req = get_fw_req(fw_download, firmware_id);
	if (!fw_req) {
		dev_err(fw_download->parent,
			"firmware not available for id: %02u\n", firmware_id);
		return -EINVAL;
	}

	cancel_delayed_work_sync(&fw_req->dwork);

	free_firmware(fw_download, fw_req);
	put_fw_req(fw_req);

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
	mutex_init(&fw_download->mutex);

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

	/*
	 * Make sure we have a reference to the pending requests, before they
	 * are freed from the timeout handler.
	 */
	mutex_lock(&fw_download->mutex);
	list_for_each_entry(fw_req, &fw_download->fw_requests, node)
		kref_get(&fw_req->kref);
	mutex_unlock(&fw_download->mutex);

	/* Release pending firmware packages */
	list_for_each_entry_safe(fw_req, tmp, &fw_download->fw_requests, node) {
		cancel_delayed_work_sync(&fw_req->dwork);
		free_firmware(fw_download, fw_req);
		put_fw_req(fw_req);
	}

	ida_destroy(&fw_download->id_map);
	kfree(fw_download);
}
