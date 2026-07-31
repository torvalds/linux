// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "dma.h"
#include "fw_client.h"
#include "ham.h"
#include "hw_msg.h"
#include "issei_dev.h"

static int __issei_ham_send_msg(struct issei_device *idev, u32 length, void *buf)
{
	struct issei_dma_data data = { };
	int ret;

	data.length = length;
	data.buf = buf;
	ret = issei_dma_write(idev, &data);
	if (ret)
		return ret;
	return idev->ops->irq_write_generate(idev);
}

/**
 * issei_ham_send_start_req - send start request to firmware
 * @idev: issei device object
 *
 * Return: 0 on success, <0 on failures
 */
int issei_ham_send_start_req(struct issei_device *idev)
{
	struct ham_start_message_req req;

	req.header.cmd = HAM_BUS_CMD_START_REQ;
	req.supported_version = ISSEI_SUPPORTED_PROTOCOL_VER;
	req.heci_capabilities_length = 0;

	return __issei_ham_send_msg(idev, sizeof(req), &req);
}

/**
 * issei_ham_send_clients_req - send clients request to firmware
 * @idev: issei device object
 *
 * Return: 0 on success, <0 on failures
 */
int issei_ham_send_clients_req(struct issei_device *idev)
{
	struct ham_get_clients_req req;

	req.header.cmd = HAM_BUS_CMD_CLIENT_REQ;

	return __issei_ham_send_msg(idev, sizeof(req), &req);
}

static int issei_ham_start_rsp(struct issei_device *idev, const u8 *buf, size_t length)
{
	struct ham_start_message_res *res = (struct ham_start_message_res *)buf;
	int ret;

	if (idev->rst_state != ISSEI_RST_STATE_START) {
		dev_err(&idev->dev, "Wrong state %d != %d\n",
			idev->rst_state, ISSEI_RST_STATE_START);
		return -EPROTO;
	}

	if (length < sizeof(*res)) {
		dev_err(&idev->dev, "Small start response size %zu < %zu\n",
			length, sizeof(*res));
		return -EPROTO;
	}

	if (length - sizeof(*res) != res->heci_capabilities_length) {
		dev_err(&idev->dev, "Wrong start response size %zu != %u\n",
			length - sizeof(*res), res->heci_capabilities_length);
		return -EPROTO;
	}

	memcpy(idev->fw_version, res->fw_version, sizeof(idev->fw_version));
	idev->fw_protocol_ver = res->supported_version;
	dev_dbg(&idev->dev, "FW protocol: %u FW version %u.%u.%u.%u", idev->fw_protocol_ver,
		idev->fw_version[0], idev->fw_version[1],
		idev->fw_version[2], idev->fw_version[3]);

	ret = issei_ham_send_clients_req(idev);
	if (ret == -EBUSY)
		ret = 0;

	return ret;
}

static int issei_ham_client_rsp(struct issei_device *idev, const u8 *buf, size_t length)
{
	struct ham_get_clients_res *res = (struct ham_get_clients_res *)buf;
	struct ham_client_properties *client;

	if (idev->rst_state != ISSEI_RST_STATE_CLIENT_ENUM) {
		dev_err(&idev->dev, "Wrong state %d != %d\n",
			idev->rst_state, ISSEI_RST_STATE_CLIENT_ENUM);
		return -EPROTO;
	}

	if (length < sizeof(*res)) {
		dev_err(&idev->dev, "Small response size %zu < %zu\n", length, sizeof(*res));
		return -EPROTO;
	}

	if (length - sizeof(*res) != res->client_count * sizeof(struct ham_client_properties)) {
		dev_err(&idev->dev, "Wrong response size %zu < %zu\n",
			length - sizeof(*res),
			res->client_count * sizeof(struct ham_client_properties));
		return -EPROTO;
	}

	guard(mutex)(&idev->client_lock);

	for (size_t i = 0; i < res->client_count; i++) {
		client = &res->clients_props[i];
		dev_dbg(&idev->dev, "client: id = %u ver = %u uuid = %pUb mtu = %u flags = %u",
			client->client_number, client->protocol_ver, &client->client_uuid,
			client->client_mtu, client->flags);
		issei_fw_cl_create(idev, client->client_number, client->protocol_ver,
				   &client->client_uuid, client->client_mtu, client->flags);
	}
	return 0;
}

static int __issei_ham_process_ham_rsp(struct issei_device *idev, const u8 *buf, size_t length)
{
	struct ham_bus_message *hdr = (struct ham_bus_message *)buf;

	if (length < sizeof(*hdr)) {
		dev_err(&idev->dev, "Small bus message size %zu < %zu\n",
			length, sizeof(*hdr));
		return -EPROTO;
	}

	switch (hdr->cmd) {
	case HAM_BUS_CMD_START_RSP:
		return issei_ham_start_rsp(idev, buf, length);

	case HAM_BUS_CMD_CLIENT_RSP:
		return issei_ham_client_rsp(idev, buf, length);

	default:
		dev_err(&idev->dev, "Unexpected command 0x%x", hdr->cmd);
		return -EPROTO;
	}
}

/**
 * issei_ham_process_ham_rsp - process response from firmware and release buffer
 * @idev: issei device object
 * @buf: response buffer
 * @length: response buffer length
 *
 * Return: 0 on success, <0 on failures
 */
int issei_ham_process_ham_rsp(struct issei_device *idev, const u8 *buf, size_t length)
{
	int ret;

	ret = __issei_ham_process_ham_rsp(idev, buf, length);
	kfree(buf);
	return ret;
}
