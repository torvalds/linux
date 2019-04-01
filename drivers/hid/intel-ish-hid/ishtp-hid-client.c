/*
 * ISHTP client driver for HID (ISH)
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/hid.h>
#include <linux/sched.h>
#include "ishtp/ishtp-dev.h"
#include "ishtp/client.h"
#include "ishtp-hid.h"

/* Rx ring buffer pool size */
#define HID_CL_RX_RING_SIZE	32
#define HID_CL_TX_RING_SIZE	16

/**
 * report_bad_packets() - Report bad packets
 * @hid_ishtp_cl:	Client instance to get stats
 * @recv_buf:		Raw received host interface message
 * @cur_pos:		Current position index in payload
 * @payload_len:	Length of payload expected
 *
 * Dumps error in case bad packet is received
 */
static void report_bad_packet(struct ishtp_cl *hid_ishtp_cl, void *recv_buf,
			      size_t cur_pos,  size_t payload_len)
{
	struct hostif_msg *recv_msg = recv_buf;
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;

	dev_err(&client_data->cl_device->dev, "[hid-ish]: BAD packet %02X\n"
		"total_bad=%u cur_pos=%u\n"
		"[%02X %02X %02X %02X]\n"
		"payload_len=%u\n"
		"multi_packet_cnt=%u\n"
		"is_response=%02X\n",
		recv_msg->hdr.command, client_data->bad_recv_cnt,
		(unsigned int)cur_pos,
		((unsigned char *)recv_msg)[0], ((unsigned char *)recv_msg)[1],
		((unsigned char *)recv_msg)[2], ((unsigned char *)recv_msg)[3],
		(unsigned int)payload_len, client_data->multi_packet_cnt,
		recv_msg->hdr.command & ~CMD_MASK);
}

/**
 * process_recv() - Received and parse incoming packet
 * @hid_ishtp_cl:	Client instance to get stats
 * @recv_buf:		Raw received host interface message
 * @data_len:		length of the message
 *
 * Parse the incoming packet. If it is a response packet then it will update
 * per instance flags and wake up the caller waiting to for the response.
 */
static void process_recv(struct ishtp_cl *hid_ishtp_cl, void *recv_buf,
			 size_t data_len)
{
	struct hostif_msg *recv_msg;
	unsigned char *payload;
	struct device_info *dev_info;
	int i, j;
	size_t	payload_len, total_len, cur_pos;
	int report_type;
	struct report_list *reports_list;
	char *reports;
	size_t report_len;
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;
	int curr_hid_dev = client_data->cur_hid_dev;

	payload = recv_buf + sizeof(struct hostif_msg_hdr);
	total_len = data_len;
	cur_pos = 0;

	do {
		if (cur_pos + sizeof(struct hostif_msg) > total_len) {
			dev_err(&client_data->cl_device->dev,
				"[hid-ish]: error, received %u which is less than data header %u\n",
				(unsigned int)data_len,
				(unsigned int)sizeof(struct hostif_msg_hdr));
			++client_data->bad_recv_cnt;
			ish_hw_reset(hid_ishtp_cl->dev);
			break;
		}

		recv_msg = (struct hostif_msg *)(recv_buf + cur_pos);
		payload_len = recv_msg->hdr.size;

		/* Sanity checks */
		if (cur_pos + payload_len + sizeof(struct hostif_msg) >
				total_len) {
			++client_data->bad_recv_cnt;
			report_bad_packet(hid_ishtp_cl, recv_msg, cur_pos,
					  payload_len);
			ish_hw_reset(hid_ishtp_cl->dev);
			break;
		}

		hid_ishtp_trace(client_data,  "%s %d\n",
				__func__, recv_msg->hdr.command & CMD_MASK);

		switch (recv_msg->hdr.command & CMD_MASK) {
		case HOSTIF_DM_ENUM_DEVICES:
			if ((!(recv_msg->hdr.command & ~CMD_MASK) ||
					client_data->init_done)) {
				++client_data->bad_recv_cnt;
				report_bad_packet(hid_ishtp_cl, recv_msg,
						  cur_pos,
						  payload_len);
				ish_hw_reset(hid_ishtp_cl->dev);
				break;
			}
			client_data->hid_dev_count = (unsigned int)*payload;
			if (!client_data->hid_devices)
				client_data->hid_devices = devm_kcalloc(
						&client_data->cl_device->dev,
						client_data->hid_dev_count,
						sizeof(struct device_info),
						GFP_KERNEL);
			if (!client_data->hid_devices) {
				dev_err(&client_data->cl_device->dev,
				"Mem alloc failed for hid device info\n");
				wake_up_interruptible(&client_data->init_wait);
				break;
			}
			for (i = 0; i < client_data->hid_dev_count; ++i) {
				if (1 + sizeof(struct device_info) * i >=
						payload_len) {
					dev_err(&client_data->cl_device->dev,
						"[hid-ish]: [ENUM_DEVICES]: content size %zu is bigger than payload_len %zu\n",
						1 + sizeof(struct device_info)
						* i, payload_len);
				}

				if (1 + sizeof(struct device_info) * i >=
						data_len)
					break;

				dev_info = (struct device_info *)(payload + 1 +
					sizeof(struct device_info) * i);
				if (client_data->hid_devices)
					memcpy(client_data->hid_devices + i,
					       dev_info,
					       sizeof(struct device_info));
			}

			client_data->enum_devices_done = true;
			wake_up_interruptible(&client_data->init_wait);

			break;

		case HOSTIF_GET_HID_DESCRIPTOR:
			if ((!(recv_msg->hdr.command & ~CMD_MASK) ||
					client_data->init_done)) {
				++client_data->bad_recv_cnt;
				report_bad_packet(hid_ishtp_cl, recv_msg,
						  cur_pos,
						  payload_len);
				ish_hw_reset(hid_ishtp_cl->dev);
				break;
			}
			if (!client_data->hid_descr[curr_hid_dev])
				client_data->hid_descr[curr_hid_dev] =
				devm_kmalloc(&client_data->cl_device->dev,
					     payload_len, GFP_KERNEL);
			if (client_data->hid_descr[curr_hid_dev]) {
				memcpy(client_data->hid_descr[curr_hid_dev],
				       payload, payload_len);
				client_data->hid_descr_size[curr_hid_dev] =
					payload_len;
				client_data->hid_descr_done = true;
			}
			wake_up_interruptible(&client_data->init_wait);

			break;

		case HOSTIF_GET_REPORT_DESCRIPTOR:
			if ((!(recv_msg->hdr.command & ~CMD_MASK) ||
					client_data->init_done)) {
				++client_data->bad_recv_cnt;
				report_bad_packet(hid_ishtp_cl, recv_msg,
						  cur_pos,
						  payload_len);
				ish_hw_reset(hid_ishtp_cl->dev);
				break;
			}
			if (!client_data->report_descr[curr_hid_dev])
				client_data->report_descr[curr_hid_dev] =
				devm_kmalloc(&client_data->cl_device->dev,
					     payload_len, GFP_KERNEL);
			if (client_data->report_descr[curr_hid_dev])  {
				memcpy(client_data->report_descr[curr_hid_dev],
				       payload,
				       payload_len);
				client_data->report_descr_size[curr_hid_dev] =
					payload_len;
				client_data->report_descr_done = true;
			}
			wake_up_interruptible(&client_data->init_wait);

			break;

		case HOSTIF_GET_FEATURE_REPORT:
			report_type = HID_FEATURE_REPORT;
			goto	do_get_report;

		case HOSTIF_GET_INPUT_REPORT:
			report_type = HID_INPUT_REPORT;
do_get_report:
			/* Get index of device that matches this id */
			for (i = 0; i < client_data->num_hid_devices; ++i) {
				if (recv_msg->hdr.device_id ==
					client_data->hid_devices[i].dev_id)
					if (client_data->hid_sensor_hubs[i]) {
						hid_input_report(
						client_data->hid_sensor_hubs[
									i],
						report_type, payload,
						payload_len, 0);
						ishtp_hid_wakeup(
						client_data->hid_sensor_hubs[
							i]);
						break;
					}
			}
			break;

		case HOSTIF_SET_FEATURE_REPORT:
			/* Get index of device that matches this id */
			for (i = 0; i < client_data->num_hid_devices; ++i) {
				if (recv_msg->hdr.device_id ==
					client_data->hid_devices[i].dev_id)
					if (client_data->hid_sensor_hubs[i]) {
						ishtp_hid_wakeup(
						client_data->hid_sensor_hubs[
							i]);
						break;
					}
			}
			break;

		case HOSTIF_PUBLISH_INPUT_REPORT:
			report_type = HID_INPUT_REPORT;
			for (i = 0; i < client_data->num_hid_devices; ++i)
				if (recv_msg->hdr.device_id ==
					client_data->hid_devices[i].dev_id)
					if (client_data->hid_sensor_hubs[i])
						hid_input_report(
						client_data->hid_sensor_hubs[
									i],
						report_type, payload,
						payload_len, 0);
			break;

		case HOSTIF_PUBLISH_INPUT_REPORT_LIST:
			report_type = HID_INPUT_REPORT;
			reports_list = (struct report_list *)payload;
			reports = (char *)reports_list->reports;

			for (j = 0; j < reports_list->num_of_reports; j++) {
				recv_msg = (struct hostif_msg *)(reports +
					sizeof(uint16_t));
				report_len = *(uint16_t *)reports;
				payload = reports + sizeof(uint16_t) +
					sizeof(struct hostif_msg_hdr);
				payload_len = report_len -
					sizeof(struct hostif_msg_hdr);

				for (i = 0; i < client_data->num_hid_devices;
				     ++i)
					if (recv_msg->hdr.device_id ==
					client_data->hid_devices[i].dev_id &&
					client_data->hid_sensor_hubs[i]) {
						hid_input_report(
						client_data->hid_sensor_hubs[
									i],
						report_type,
						payload, payload_len,
						0);
					}

				reports += sizeof(uint16_t) + report_len;
			}
			break;
		default:
			++client_data->bad_recv_cnt;
			report_bad_packet(hid_ishtp_cl, recv_msg, cur_pos,
					  payload_len);
			ish_hw_reset(hid_ishtp_cl->dev);
			break;

		}

		if (!cur_pos && cur_pos + payload_len +
				sizeof(struct hostif_msg) < total_len)
			++client_data->multi_packet_cnt;

		cur_pos += payload_len + sizeof(struct hostif_msg);
		payload += payload_len + sizeof(struct hostif_msg);

	} while (cur_pos < total_len);
}

/**
 * ish_cl_event_cb() - bus driver callback for incoming message/packet
 * @device:	Pointer to the the ishtp client device for which this message
 *		is targeted
 *
 * Remove the packet from the list and process the message by calling
 * process_recv
 */
static void ish_cl_event_cb(struct ishtp_cl_device *device)
{
	struct ishtp_cl	*hid_ishtp_cl = ishtp_get_drvdata(device);
	struct ishtp_cl_rb *rb_in_proc;
	size_t r_length;

	if (!hid_ishtp_cl)
		return;

	while ((rb_in_proc = ishtp_cl_rx_get_rb(hid_ishtp_cl)) != NULL) {
		if (!rb_in_proc->buffer.data)
			return;

		r_length = rb_in_proc->buf_idx;

		/* decide what to do with received data */
		process_recv(hid_ishtp_cl, rb_in_proc->buffer.data, r_length);

		ishtp_cl_io_rb_recycle(rb_in_proc);
	}
}

/**
 * hid_ishtp_set_feature() - send request to ISH FW to set a feature request
 * @hid:	hid device instance for this request
 * @buf:	feature buffer
 * @len:	Length of feature buffer
 * @report_id:	Report id for the feature set request
 *
 * This is called from hid core .request() callback. This function doesn't wait
 * for response.
 */
void hid_ishtp_set_feature(struct hid_device *hid, char *buf, unsigned int len,
			   int report_id)
{
	struct ishtp_hid_data *hid_data =  hid->driver_data;
	struct ishtp_cl_data *client_data = hid_data->client_data;
	struct hostif_msg *msg = (struct hostif_msg *)buf;
	int	rv;
	int	i;

	hid_ishtp_trace(client_data,  "%s hid %p\n", __func__, hid);

	rv = ishtp_hid_link_ready_wait(client_data);
	if (rv) {
		hid_ishtp_trace(client_data,  "%s hid %p link not ready\n",
				__func__, hid);
		return;
	}

	memset(msg, 0, sizeof(struct hostif_msg));
	msg->hdr.command = HOSTIF_SET_FEATURE_REPORT;
	for (i = 0; i < client_data->num_hid_devices; ++i) {
		if (hid == client_data->hid_sensor_hubs[i]) {
			msg->hdr.device_id =
				client_data->hid_devices[i].dev_id;
			break;
		}
	}

	if (i == client_data->num_hid_devices)
		return;

	rv = ishtp_cl_send(client_data->hid_ishtp_cl, buf, len);
	if (rv)
		hid_ishtp_trace(client_data,  "%s hid %p send failed\n",
				__func__, hid);
}

/**
 * hid_ishtp_get_report() - request to get feature/input report
 * @hid:	hid device instance for this request
 * @report_id:	Report id for the get request
 * @report_type:	Report type for the this request
 *
 * This is called from hid core .request() callback. This function will send
 * request to FW and return without waiting for response.
 */
void hid_ishtp_get_report(struct hid_device *hid, int report_id,
			  int report_type)
{
	struct ishtp_hid_data *hid_data =  hid->driver_data;
	struct ishtp_cl_data *client_data = hid_data->client_data;
	struct hostif_msg_to_sensor msg = {};
	int	rv;
	int	i;

	hid_ishtp_trace(client_data,  "%s hid %p\n", __func__, hid);
	rv = ishtp_hid_link_ready_wait(client_data);
	if (rv) {
		hid_ishtp_trace(client_data,  "%s hid %p link not ready\n",
				__func__, hid);
		return;
	}

	msg.hdr.command = (report_type == HID_FEATURE_REPORT) ?
		HOSTIF_GET_FEATURE_REPORT : HOSTIF_GET_INPUT_REPORT;
	for (i = 0; i < client_data->num_hid_devices; ++i) {
		if (hid == client_data->hid_sensor_hubs[i]) {
			msg.hdr.device_id =
				client_data->hid_devices[i].dev_id;
			break;
		}
	}

	if (i == client_data->num_hid_devices)
		return;

	msg.report_id = report_id;
	rv = ishtp_cl_send(client_data->hid_ishtp_cl, (uint8_t *)&msg,
			    sizeof(msg));
	if (rv)
		hid_ishtp_trace(client_data,  "%s hid %p send failed\n",
				__func__, hid);
}

/**
 * ishtp_hid_link_ready_wait() - Wait for link ready
 * @client_data:	client data instance
 *
 * If the transport link started suspend process, then wait, till either
 * resumed or timeout
 *
 * Return: 0 on success, non zero on error
 */
int ishtp_hid_link_ready_wait(struct ishtp_cl_data *client_data)
{
	int rc;

	if (client_data->suspended) {
		hid_ishtp_trace(client_data,  "wait for link ready\n");
		rc = wait_event_interruptible_timeout(
					client_data->ishtp_resume_wait,
					!client_data->suspended,
					5 * HZ);

		if (rc == 0) {
			hid_ishtp_trace(client_data,  "link not ready\n");
			return -EIO;
		}
		hid_ishtp_trace(client_data,  "link ready\n");
	}

	return 0;
}

/**
 * ishtp_enum_enum_devices() - Enumerate hid devices
 * @hid_ishtp_cl:	client instance
 *
 * Helper function to send request to firmware to enumerate HID devices
 *
 * Return: 0 on success, non zero on error
 */
static int ishtp_enum_enum_devices(struct ishtp_cl *hid_ishtp_cl)
{
	struct hostif_msg msg;
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;
	int retry_count;
	int rv;

	/* Send HOSTIF_DM_ENUM_DEVICES */
	memset(&msg, 0, sizeof(struct hostif_msg));
	msg.hdr.command = HOSTIF_DM_ENUM_DEVICES;
	rv = ishtp_cl_send(hid_ishtp_cl, (unsigned char *)&msg,
			   sizeof(struct hostif_msg));
	if (rv)
		return rv;

	retry_count = 0;
	while (!client_data->enum_devices_done &&
	       retry_count < 10) {
		wait_event_interruptible_timeout(client_data->init_wait,
					 client_data->enum_devices_done,
					 3 * HZ);
		++retry_count;
		if (!client_data->enum_devices_done)
			/* Send HOSTIF_DM_ENUM_DEVICES */
			rv = ishtp_cl_send(hid_ishtp_cl,
					   (unsigned char *) &msg,
					   sizeof(struct hostif_msg));
	}
	if (!client_data->enum_devices_done) {
		dev_err(&client_data->cl_device->dev,
			"[hid-ish]: timed out waiting for enum_devices\n");
		return -ETIMEDOUT;
	}
	if (!client_data->hid_devices) {
		dev_err(&client_data->cl_device->dev,
			"[hid-ish]: failed to allocate HID dev structures\n");
		return -ENOMEM;
	}

	client_data->num_hid_devices = client_data->hid_dev_count;
	dev_info(&hid_ishtp_cl->device->dev,
		"[hid-ish]: enum_devices_done OK, num_hid_devices=%d\n",
		client_data->num_hid_devices);

	return	0;
}

/**
 * ishtp_get_hid_descriptor() - Get hid descriptor
 * @hid_ishtp_cl:	client instance
 * @index:		Index into the hid_descr array
 *
 * Helper function to send request to firmware get HID descriptor of a device
 *
 * Return: 0 on success, non zero on error
 */
static int ishtp_get_hid_descriptor(struct ishtp_cl *hid_ishtp_cl, int index)
{
	struct hostif_msg msg;
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;
	int rv;

	/* Get HID descriptor */
	client_data->hid_descr_done = false;
	memset(&msg, 0, sizeof(struct hostif_msg));
	msg.hdr.command = HOSTIF_GET_HID_DESCRIPTOR;
	msg.hdr.device_id = client_data->hid_devices[index].dev_id;
	rv = ishtp_cl_send(hid_ishtp_cl, (unsigned char *) &msg,
			   sizeof(struct hostif_msg));
	if (rv)
		return rv;

	if (!client_data->hid_descr_done) {
		wait_event_interruptible_timeout(client_data->init_wait,
						 client_data->hid_descr_done,
						 3 * HZ);
		if (!client_data->hid_descr_done) {
			dev_err(&client_data->cl_device->dev,
				"[hid-ish]: timed out for hid_descr_done\n");
			return -EIO;
		}

		if (!client_data->hid_descr[index]) {
			dev_err(&client_data->cl_device->dev,
				"[hid-ish]: allocation HID desc fail\n");
			return -ENOMEM;
		}
	}

	return 0;
}

/**
 * ishtp_get_report_descriptor() - Get report descriptor
 * @hid_ishtp_cl:	client instance
 * @index:		Index into the hid_descr array
 *
 * Helper function to send request to firmware get HID report descriptor of
 * a device
 *
 * Return: 0 on success, non zero on error
 */
static int ishtp_get_report_descriptor(struct ishtp_cl *hid_ishtp_cl,
				       int index)
{
	struct hostif_msg msg;
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;
	int rv;

	/* Get report descriptor */
	client_data->report_descr_done = false;
	memset(&msg, 0, sizeof(struct hostif_msg));
	msg.hdr.command = HOSTIF_GET_REPORT_DESCRIPTOR;
	msg.hdr.device_id = client_data->hid_devices[index].dev_id;
	rv = ishtp_cl_send(hid_ishtp_cl, (unsigned char *) &msg,
			   sizeof(struct hostif_msg));
	if (rv)
		return rv;

	if (!client_data->report_descr_done)
		wait_event_interruptible_timeout(client_data->init_wait,
					 client_data->report_descr_done,
					 3 * HZ);
	if (!client_data->report_descr_done) {
		dev_err(&client_data->cl_device->dev,
				"[hid-ish]: timed out for report descr\n");
		return -EIO;
	}
	if (!client_data->report_descr[index]) {
		dev_err(&client_data->cl_device->dev,
			"[hid-ish]: failed to alloc report descr\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * hid_ishtp_cl_init() - Init function for ISHTP client
 * @hid_ishtp_cl:	ISHTP client instance
 * @reset:		true if called for init after reset
 *
 * This function complete the initializtion of the client. The summary of
 * processing:
 * - Send request to enumerate the hid clients
 *	Get the HID descriptor for each enumearated device
 *	Get report description of each device
 *	Register each device wik hid core by calling ishtp_hid_probe
 *
 * Return: 0 on success, non zero on error
 */
static int hid_ishtp_cl_init(struct ishtp_cl *hid_ishtp_cl, int reset)
{
	struct ishtp_device *dev;
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;
	struct ishtp_fw_client *fw_client;
	int i;
	int rv;

	dev_dbg(&client_data->cl_device->dev, "%s\n", __func__);
	hid_ishtp_trace(client_data,  "%s reset flag: %d\n", __func__, reset);

	rv = ishtp_cl_link(hid_ishtp_cl, ISHTP_HOST_CLIENT_ID_ANY);
	if (rv) {
		dev_err(&client_data->cl_device->dev,
			"ishtp_cl_link failed\n");
		return	-ENOMEM;
	}

	client_data->init_done = 0;

	dev = hid_ishtp_cl->dev;

	/* Connect to FW client */
	hid_ishtp_cl->rx_ring_size = HID_CL_RX_RING_SIZE;
	hid_ishtp_cl->tx_ring_size = HID_CL_TX_RING_SIZE;

	fw_client = ishtp_fw_cl_get_client(dev, &hid_ishtp_guid);
	if (!fw_client) {
		dev_err(&client_data->cl_device->dev,
			"ish client uuid not found\n");
		return -ENOENT;
	}

	hid_ishtp_cl->fw_client_id = fw_client->client_id;
	hid_ishtp_cl->state = ISHTP_CL_CONNECTING;

	rv = ishtp_cl_connect(hid_ishtp_cl);
	if (rv) {
		dev_err(&client_data->cl_device->dev,
			"client connect fail\n");
		goto err_cl_unlink;
	}

	hid_ishtp_trace(client_data,  "%s client connected\n", __func__);

	/* Register read callback */
	ishtp_register_event_cb(hid_ishtp_cl->device, ish_cl_event_cb);

	rv = ishtp_enum_enum_devices(hid_ishtp_cl);
	if (rv)
		goto err_cl_disconnect;

	hid_ishtp_trace(client_data,  "%s enumerated device count %d\n",
			__func__, client_data->num_hid_devices);

	for (i = 0; i < client_data->num_hid_devices; ++i) {
		client_data->cur_hid_dev = i;

		rv = ishtp_get_hid_descriptor(hid_ishtp_cl, i);
		if (rv)
			goto err_cl_disconnect;

		rv = ishtp_get_report_descriptor(hid_ishtp_cl, i);
		if (rv)
			goto err_cl_disconnect;

		if (!reset) {
			rv = ishtp_hid_probe(i, client_data);
			if (rv) {
				dev_err(&client_data->cl_device->dev,
				"[hid-ish]: HID probe for #%u failed: %d\n",
				i, rv);
				goto err_cl_disconnect;
			}
		}
	} /* for() on all hid devices */

	client_data->init_done = 1;
	client_data->suspended = false;
	wake_up_interruptible(&client_data->ishtp_resume_wait);
	hid_ishtp_trace(client_data,  "%s successful init\n", __func__);
	return 0;

err_cl_disconnect:
	hid_ishtp_cl->state = ISHTP_CL_DISCONNECTING;
	ishtp_cl_disconnect(hid_ishtp_cl);
err_cl_unlink:
	ishtp_cl_unlink(hid_ishtp_cl);
	return rv;
}

/**
 * hid_ishtp_cl_deinit() - Deinit function for ISHTP client
 * @hid_ishtp_cl:	ISHTP client instance
 *
 * Unlink and free hid client
 */
static void hid_ishtp_cl_deinit(struct ishtp_cl *hid_ishtp_cl)
{
	ishtp_cl_unlink(hid_ishtp_cl);
	ishtp_cl_flush_queues(hid_ishtp_cl);

	/* disband and free all Tx and Rx client-level rings */
	ishtp_cl_free(hid_ishtp_cl);
}

static void hid_ishtp_cl_reset_handler(struct work_struct *work)
{
	struct ishtp_cl_data *client_data;
	struct ishtp_cl *hid_ishtp_cl;
	struct ishtp_cl_device *cl_device;
	int retry;
	int rv;

	client_data = container_of(work, struct ishtp_cl_data, work);

	hid_ishtp_cl = client_data->hid_ishtp_cl;
	cl_device = client_data->cl_device;

	hid_ishtp_trace(client_data, "%s hid_ishtp_cl %p\n", __func__,
			hid_ishtp_cl);
	dev_dbg(&cl_device->dev, "%s\n", __func__);

	hid_ishtp_cl_deinit(hid_ishtp_cl);

	hid_ishtp_cl = ishtp_cl_allocate(cl_device->ishtp_dev);
	if (!hid_ishtp_cl)
		return;

	ishtp_set_drvdata(cl_device, hid_ishtp_cl);
	hid_ishtp_cl->client_data = client_data;
	client_data->hid_ishtp_cl = hid_ishtp_cl;

	client_data->num_hid_devices = 0;

	for (retry = 0; retry < 3; ++retry) {
		rv = hid_ishtp_cl_init(hid_ishtp_cl, 1);
		if (!rv)
			break;
		dev_err(&client_data->cl_device->dev, "Retry reset init\n");
	}
	if (rv) {
		dev_err(&client_data->cl_device->dev, "Reset Failed\n");
		hid_ishtp_trace(client_data, "%s Failed hid_ishtp_cl %p\n",
				__func__, hid_ishtp_cl);
	}
}

/**
 * hid_ishtp_cl_probe() - ISHTP client driver probe
 * @cl_device:		ISHTP client device instance
 *
 * This function gets called on device create on ISHTP bus
 *
 * Return: 0 on success, non zero on error
 */
static int hid_ishtp_cl_probe(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *hid_ishtp_cl;
	struct ishtp_cl_data *client_data;
	int rv;

	if (!cl_device)
		return	-ENODEV;

	if (!guid_equal(&hid_ishtp_guid,
			&cl_device->fw_client->props.protocol_name))
		return	-ENODEV;

	client_data = devm_kzalloc(&cl_device->dev, sizeof(*client_data),
				   GFP_KERNEL);
	if (!client_data)
		return -ENOMEM;

	hid_ishtp_cl = ishtp_cl_allocate(cl_device->ishtp_dev);
	if (!hid_ishtp_cl)
		return -ENOMEM;

	ishtp_set_drvdata(cl_device, hid_ishtp_cl);
	hid_ishtp_cl->client_data = client_data;
	client_data->hid_ishtp_cl = hid_ishtp_cl;
	client_data->cl_device = cl_device;

	init_waitqueue_head(&client_data->init_wait);
	init_waitqueue_head(&client_data->ishtp_resume_wait);

	INIT_WORK(&client_data->work, hid_ishtp_cl_reset_handler);

	rv = hid_ishtp_cl_init(hid_ishtp_cl, 0);
	if (rv) {
		ishtp_cl_free(hid_ishtp_cl);
		return rv;
	}
	ishtp_get_device(cl_device);

	return 0;
}

/**
 * hid_ishtp_cl_remove() - ISHTP client driver remove
 * @cl_device:		ISHTP client device instance
 *
 * This function gets called on device remove on ISHTP bus
 *
 * Return: 0
 */
static int hid_ishtp_cl_remove(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *hid_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;

	hid_ishtp_trace(client_data, "%s hid_ishtp_cl %p\n", __func__,
			hid_ishtp_cl);

	dev_dbg(&cl_device->dev, "%s\n", __func__);
	hid_ishtp_cl->state = ISHTP_CL_DISCONNECTING;
	ishtp_cl_disconnect(hid_ishtp_cl);
	ishtp_put_device(cl_device);
	ishtp_hid_remove(client_data);
	hid_ishtp_cl_deinit(hid_ishtp_cl);

	hid_ishtp_cl = NULL;

	client_data->num_hid_devices = 0;

	return 0;
}

/**
 * hid_ishtp_cl_reset() - ISHTP client driver reset
 * @cl_device:		ISHTP client device instance
 *
 * This function gets called on device reset on ISHTP bus
 *
 * Return: 0
 */
static int hid_ishtp_cl_reset(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *hid_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;

	hid_ishtp_trace(client_data, "%s hid_ishtp_cl %p\n", __func__,
			hid_ishtp_cl);

	schedule_work(&client_data->work);

	return 0;
}

#define to_ishtp_cl_device(d) container_of(d, struct ishtp_cl_device, dev)

/**
 * hid_ishtp_cl_suspend() - ISHTP client driver suspend
 * @device:	device instance
 *
 * This function gets called on system suspend
 *
 * Return: 0
 */
static int hid_ishtp_cl_suspend(struct device *device)
{
	struct ishtp_cl_device *cl_device = to_ishtp_cl_device(device);
	struct ishtp_cl *hid_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;

	hid_ishtp_trace(client_data, "%s hid_ishtp_cl %p\n", __func__,
			hid_ishtp_cl);
	client_data->suspended = true;

	return 0;
}

/**
 * hid_ishtp_cl_resume() - ISHTP client driver resume
 * @device:	device instance
 *
 * This function gets called on system resume
 *
 * Return: 0
 */
static int hid_ishtp_cl_resume(struct device *device)
{
	struct ishtp_cl_device *cl_device = to_ishtp_cl_device(device);
	struct ishtp_cl *hid_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_cl_data *client_data = hid_ishtp_cl->client_data;

	hid_ishtp_trace(client_data, "%s hid_ishtp_cl %p\n", __func__,
			hid_ishtp_cl);
	client_data->suspended = false;
	return 0;
}

static const struct dev_pm_ops hid_ishtp_pm_ops = {
	.suspend = hid_ishtp_cl_suspend,
	.resume = hid_ishtp_cl_resume,
};

static struct ishtp_cl_driver	hid_ishtp_cl_driver = {
	.name = "ish-hid",
	.probe = hid_ishtp_cl_probe,
	.remove = hid_ishtp_cl_remove,
	.reset = hid_ishtp_cl_reset,
	.driver.pm = &hid_ishtp_pm_ops,
};

static int __init ish_hid_init(void)
{
	int	rv;

	/* Register ISHTP client device driver with ISHTP Bus */
	rv = ishtp_cl_driver_register(&hid_ishtp_cl_driver);

	return rv;

}

static void __exit ish_hid_exit(void)
{
	ishtp_cl_driver_unregister(&hid_ishtp_cl_driver);
}

late_initcall(ish_hid_init);
module_exit(ish_hid_exit);

MODULE_DESCRIPTION("ISH ISHTP HID client driver");
/* Primary author */
MODULE_AUTHOR("Daniel Drubin <daniel.drubin@intel.com>");
/*
 * Several modification for multi instance support
 * suspend/resume and clean up
 */
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");

MODULE_LICENSE("GPL");
MODULE_ALIAS("ishtp:*");
