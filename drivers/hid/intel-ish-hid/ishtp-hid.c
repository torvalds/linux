// SPDX-License-Identifier: GPL-2.0-only
/*
 * ISHTP-HID glue driver.
 *
 * Copyright (c) 2012-2016, Intel Corporation.
 */

#include <linux/hid.h>
#include <linux/intel-ish-client-if.h>
#include <uapi/linux/input.h>
#include "ishtp-hid.h"

/**
 * ishtp_hid_parse() - hid-core .parse() callback
 * @hid:	hid device instance
 *
 * This function gets called during call to hid_add_device
 *
 * Return: 0 on success and non zero on error
 */
static int ishtp_hid_parse(struct hid_device *hid)
{
	struct ishtp_hid_data *hid_data =  hid->driver_data;
	struct ishtp_cl_data *client_data = hid_data->client_data;
	int rv;

	rv = hid_parse_report(hid, client_data->report_descr[hid_data->index],
			      client_data->report_descr_size[hid_data->index]);
	if (rv)
		return	rv;

	return 0;
}

/* Empty callbacks with success return code */
static int ishtp_hid_start(struct hid_device *hid)
{
	return 0;
}

static void ishtp_hid_stop(struct hid_device *hid)
{
}

static int ishtp_hid_open(struct hid_device *hid)
{
	return 0;
}

static void ishtp_hid_close(struct hid_device *hid)
{
}

static int ishtp_raw_request(struct hid_device *hid, unsigned char reportnum,
			     __u8 *buf, size_t len, unsigned char rtype,
			     int reqtype)
{
	struct ishtp_hid_data *hid_data =  hid->driver_data;
	char *ishtp_buf = NULL;
	size_t ishtp_buf_len;
	unsigned int header_size = sizeof(struct hostif_msg);

	if (rtype == HID_OUTPUT_REPORT)
		return -EINVAL;

	hid_data->request_done = false;
	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		hid_data->raw_buf = buf;
		hid_data->raw_buf_size = len;
		hid_data->raw_get_req = true;

		hid_ishtp_get_report(hid, reportnum, rtype);
		break;
	case HID_REQ_SET_REPORT:
		/*
		 * Spare 7 bytes for 64b accesses through
		 * get/put_unaligned_le64()
		 */
		ishtp_buf_len = len + header_size;
		ishtp_buf = kzalloc(ishtp_buf_len + 7, GFP_KERNEL);
		if (!ishtp_buf)
			return -ENOMEM;

		memcpy(ishtp_buf + header_size, buf, len);
		hid_ishtp_set_feature(hid, ishtp_buf, ishtp_buf_len, reportnum);
		kfree(ishtp_buf);
		break;
	}

	hid_hw_wait(hid);

	return len;
}

/**
 * ishtp_hid_request() - hid-core .request() callback
 * @hid:	hid device instance
 * @rep:	pointer to hid_report
 * @reqtype:	type of req. [GET|SET]_REPORT
 *
 * This function is used to set/get feaure/input report.
 */
static void ishtp_hid_request(struct hid_device *hid, struct hid_report *rep,
	int reqtype)
{
	struct ishtp_hid_data *hid_data =  hid->driver_data;
	/* the specific report length, just HID part of it */
	unsigned int len = ((rep->size - 1) >> 3) + 1 + (rep->id > 0);
	char *buf;
	unsigned int header_size = sizeof(struct hostif_msg);

	len += header_size;

	hid_data->request_done = false;
	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		hid_data->raw_get_req = false;
		hid_ishtp_get_report(hid, rep->id, rep->type);
		break;
	case HID_REQ_SET_REPORT:
		/*
		 * Spare 7 bytes for 64b accesses through
		 * get/put_unaligned_le64()
		 */
		buf = kzalloc(len + 7, GFP_KERNEL);
		if (!buf)
			return;

		hid_output_report(rep, buf + header_size);
		hid_ishtp_set_feature(hid, buf, len, rep->id);
		kfree(buf);
		break;
	}
}

/**
 * ishtp_wait_for_response() - hid-core .wait() callback
 * @hid:	hid device instance
 *
 * This function is used to wait after get feaure/input report.
 *
 * Return: 0 on success and non zero on error
 */
static int ishtp_wait_for_response(struct hid_device *hid)
{
	struct ishtp_hid_data *hid_data =  hid->driver_data;
	int rv;

	hid_ishtp_trace(client_data,  "%s hid %p\n", __func__, hid);

	rv = ishtp_hid_link_ready_wait(hid_data->client_data);
	if (rv)
		return rv;

	if (!hid_data->request_done)
		wait_event_interruptible_timeout(hid_data->hid_wait,
					hid_data->request_done, 3 * HZ);

	if (!hid_data->request_done) {
		hid_err(hid,
			"timeout waiting for response from ISHTP device\n");
		return -ETIMEDOUT;
	}
	hid_ishtp_trace(client_data,  "%s hid %p done\n", __func__, hid);

	hid_data->request_done = false;

	return 0;
}

/**
 * ishtp_hid_wakeup() - Wakeup caller
 * @hid:	hid device instance
 *
 * This function will wakeup caller waiting for Get/Set feature report
 */
void ishtp_hid_wakeup(struct hid_device *hid)
{
	struct ishtp_hid_data *hid_data = hid->driver_data;

	hid_data->request_done = true;
	wake_up_interruptible(&hid_data->hid_wait);
}

static struct hid_ll_driver ishtp_hid_ll_driver = {
	.parse = ishtp_hid_parse,
	.start = ishtp_hid_start,
	.stop = ishtp_hid_stop,
	.open = ishtp_hid_open,
	.close = ishtp_hid_close,
	.request = ishtp_hid_request,
	.wait = ishtp_wait_for_response,
	.raw_request = ishtp_raw_request
};

/**
 * ishtp_hid_probe() - hid register ll driver
 * @cur_hid_dev:	Index of hid device calling to register
 * @client_data:	Client data pointer
 *
 * This function is used to allocate and add HID device.
 *
 * Return: 0 on success, non zero on error
 */
int ishtp_hid_probe(unsigned int cur_hid_dev,
		    struct ishtp_cl_data *client_data)
{
	int rv;
	struct hid_device *hid;
	struct ishtp_hid_data *hid_data;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		rv = PTR_ERR(hid);
		return	-ENOMEM;
	}

	hid_data = kzalloc(sizeof(*hid_data), GFP_KERNEL);
	if (!hid_data) {
		rv = -ENOMEM;
		goto err_hid_data;
	}

	hid_data->index = cur_hid_dev;
	hid_data->client_data = client_data;
	init_waitqueue_head(&hid_data->hid_wait);

	hid->driver_data = hid_data;

	client_data->hid_sensor_hubs[cur_hid_dev] = hid;

	hid->ll_driver = &ishtp_hid_ll_driver;
	hid->bus = BUS_INTEL_ISHTP;
	hid->dev.parent = ishtp_device(client_data->cl_device);

	hid->version = le16_to_cpu(ISH_HID_VERSION);
	hid->vendor = le16_to_cpu(client_data->hid_devices[cur_hid_dev].vid);
	hid->product = le16_to_cpu(client_data->hid_devices[cur_hid_dev].pid);
	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X", "hid-ishtp",
		hid->vendor, hid->product);

	rv = hid_add_device(hid);
	if (rv)
		goto err_hid_device;

	hid_ishtp_trace(client_data,  "%s allocated hid %p\n", __func__, hid);

	return 0;

err_hid_device:
	kfree(hid_data);
err_hid_data:
	hid_destroy_device(hid);
	return rv;
}

/**
 * ishtp_hid_probe() - Remove registered hid device
 * @client_data:	client data pointer
 *
 * This function is used to destroy allocatd HID device.
 */
void ishtp_hid_remove(struct ishtp_cl_data *client_data)
{
	int i;

	for (i = 0; i < client_data->num_hid_devices; ++i) {
		if (client_data->hid_sensor_hubs[i]) {
			kfree(client_data->hid_sensor_hubs[i]->driver_data);
			hid_destroy_device(client_data->hid_sensor_hubs[i]);
			client_data->hid_sensor_hubs[i] = NULL;
		}
	}
}
