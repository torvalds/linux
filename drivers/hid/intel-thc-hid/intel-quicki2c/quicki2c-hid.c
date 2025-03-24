/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/pm_runtime.h>

#include "quicki2c-dev.h"
#include "quicki2c-hid.h"
#include "quicki2c-protocol.h"

/**
 * quicki2c_hid_parse() - HID core parse() callback
 *
 * @hid: HID device instance
 *
 * This function gets called during call to hid_add_device
 *
 * Return: 0 on success and non zero on error.
 */
static int quicki2c_hid_parse(struct hid_device *hid)
{
	struct quicki2c_device *qcdev = hid->driver_data;

	if (qcdev->report_descriptor)
		return hid_parse_report(hid, qcdev->report_descriptor,
					le16_to_cpu(qcdev->dev_desc.report_desc_len));

	dev_err_once(qcdev->dev, "invalid report descriptor\n");
	return -EINVAL;
}

static int quicki2c_hid_start(struct hid_device *hid)
{
	return 0;
}

static void quicki2c_hid_stop(struct hid_device *hid)
{
}

static int quicki2c_hid_open(struct hid_device *hid)
{
	return 0;
}

static void quicki2c_hid_close(struct hid_device *hid)
{
}

static int quicki2c_hid_raw_request(struct hid_device *hid,
				    unsigned char reportnum,
				    __u8 *buf, size_t len,
				    unsigned char rtype, int reqtype)
{
	struct quicki2c_device *qcdev = hid->driver_data;
	int ret = 0;

	ret = pm_runtime_resume_and_get(qcdev->dev);
	if (ret)
		return ret;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		ret = quicki2c_get_report(qcdev, rtype, reportnum, buf, len);
		break;
	case HID_REQ_SET_REPORT:
		ret = quicki2c_set_report(qcdev, rtype, reportnum, buf, len);
		break;
	default:
		dev_err(qcdev->dev, "Not supported request type %d\n", reqtype);
		break;
	}

	pm_runtime_mark_last_busy(qcdev->dev);
	pm_runtime_put_autosuspend(qcdev->dev);

	return ret;
}

static int quicki2c_hid_power(struct hid_device *hid, int lvl)
{
	return 0;
}

static struct hid_ll_driver quicki2c_hid_ll_driver = {
	.parse = quicki2c_hid_parse,
	.start = quicki2c_hid_start,
	.stop = quicki2c_hid_stop,
	.open = quicki2c_hid_open,
	.close = quicki2c_hid_close,
	.power = quicki2c_hid_power,
	.raw_request = quicki2c_hid_raw_request,
};

/**
 * quicki2c_hid_probe() - Register HID low level driver
 *
 * @qcdev: point to quicki2c device
 *
 * This function is used to allocate and add HID device.
 *
 * Return: 0 on success, non zero on error.
 */
int quicki2c_hid_probe(struct quicki2c_device *qcdev)
{
	struct hid_device *hid;
	int ret;

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return PTR_ERR(hid);

	hid->ll_driver = &quicki2c_hid_ll_driver;
	hid->bus = BUS_PCI;
	hid->dev.parent = qcdev->dev;
	hid->driver_data = qcdev;
	hid->version = le16_to_cpu(qcdev->dev_desc.version_id);
	hid->vendor = le16_to_cpu(qcdev->dev_desc.vendor_id);
	hid->product = le16_to_cpu(qcdev->dev_desc.product_id);
	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X", "quicki2c-hid",
		 hid->vendor, hid->product);

	ret = hid_add_device(hid);
	if (ret) {
		hid_destroy_device(hid);
		return ret;
	}

	qcdev->hid_dev = hid;

	return 0;
}

/**
 * quicki2c_hid_remove() - Destroy HID device
 *
 * @qcdev: point to quicki2c device
 *
 * Return: 0 on success, non zero on error.
 */
void quicki2c_hid_remove(struct quicki2c_device *qcdev)
{
	hid_destroy_device(qcdev->hid_dev);
}

/**
 * quicki2c_hid_send_report() - Send HID input report data to HID core
 *
 * @qcdev: point to quicki2c device
 * @data: point to input report data buffer
 * @data_len: the length of input report data
 *
 * Return: 0 on success, non zero on error.
 */
int quicki2c_hid_send_report(struct quicki2c_device *qcdev,
			     void *data, size_t data_len)
{
	int ret;

	ret = hid_input_report(qcdev->hid_dev, HID_INPUT_REPORT, data, data_len, 1);
	if (ret)
		dev_err(qcdev->dev, "Failed to send HID input report, ret = %d.\n", ret);

	return ret;
}
