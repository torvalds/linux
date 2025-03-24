/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/pm_runtime.h>

#include "quickspi-dev.h"
#include "quickspi-hid.h"

/**
 * quickspi_hid_parse() - HID core parse() callback
 *
 * @hid: HID device instance
 *
 * This function gets called during call to hid_add_device
 *
 * Return: 0 on success and non zero on error.
 */
static int quickspi_hid_parse(struct hid_device *hid)
{
	struct quickspi_device *qsdev = hid->driver_data;

	if (qsdev->report_descriptor)
		return hid_parse_report(hid, qsdev->report_descriptor,
					le16_to_cpu(qsdev->dev_desc.rep_desc_len));

	dev_err(qsdev->dev, "invalid report descriptor\n");
	return -EINVAL;
}

static int quickspi_hid_start(struct hid_device *hid)
{
	return 0;
}

static void quickspi_hid_stop(struct hid_device *hid)
{
}

static int quickspi_hid_open(struct hid_device *hid)
{
	return 0;
}

static void quickspi_hid_close(struct hid_device *hid)
{
}

static int quickspi_hid_raw_request(struct hid_device *hid,
				    unsigned char reportnum,
				    __u8 *buf, size_t len,
				    unsigned char rtype, int reqtype)
{
	struct quickspi_device *qsdev = hid->driver_data;
	int ret = 0;

	ret = pm_runtime_resume_and_get(qsdev->dev);
	if (ret)
		return ret;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		ret = quickspi_get_report(qsdev, rtype, reportnum, buf);
		break;
	case HID_REQ_SET_REPORT:
		ret = quickspi_set_report(qsdev, rtype, reportnum, buf, len);
		break;
	default:
		dev_err_once(qsdev->dev, "Not supported request type %d\n", reqtype);
		break;
	}

	pm_runtime_mark_last_busy(qsdev->dev);
	pm_runtime_put_autosuspend(qsdev->dev);

	return ret;
}

static int quickspi_hid_power(struct hid_device *hid, int lvl)
{
	return 0;
}

static struct hid_ll_driver quickspi_hid_ll_driver = {
	.parse = quickspi_hid_parse,
	.start = quickspi_hid_start,
	.stop = quickspi_hid_stop,
	.open = quickspi_hid_open,
	.close = quickspi_hid_close,
	.power = quickspi_hid_power,
	.raw_request = quickspi_hid_raw_request,
};

/**
 * quickspi_hid_probe() - Register HID low level driver
 *
 * @qsdev: point to quickspi device
 *
 * This function is used to allocate and add HID device.
 *
 * Return: 0 on success, non zero on error.
 */
int quickspi_hid_probe(struct quickspi_device *qsdev)
{
	struct hid_device *hid;
	int ret;

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return PTR_ERR(hid);

	hid->ll_driver = &quickspi_hid_ll_driver;
	hid->bus = BUS_PCI;
	hid->dev.parent = qsdev->dev;
	hid->driver_data = qsdev;
	hid->version = le16_to_cpu(qsdev->dev_desc.version_id);
	hid->vendor = le16_to_cpu(qsdev->dev_desc.vendor_id);
	hid->product = le16_to_cpu(qsdev->dev_desc.product_id);
	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X", "quickspi-hid",
		 hid->vendor, hid->product);

	ret = hid_add_device(hid);
	if (ret) {
		hid_destroy_device(hid);
		return ret;
	}

	qsdev->hid_dev = hid;

	return 0;
}

/**
 * quickspi_hid_remove() - Destroy HID device
 *
 * @qsdev: point to quickspi device
 *
 * Return: 0 on success, non zero on error.
 */
void quickspi_hid_remove(struct quickspi_device *qsdev)
{
	hid_destroy_device(qsdev->hid_dev);
}

/**
 * quickspi_hid_send_report() - Send HID input report data to HID core
 *
 * @qsdev: point to quickspi device
 * @data: point to input report data buffer
 * @data_len: the length of input report data
 *
 * Return: 0 on success, non zero on error.
 */
int quickspi_hid_send_report(struct quickspi_device *qsdev,
			     void *data, size_t data_len)
{
	int ret;

	ret = hid_input_report(qsdev->hid_dev, HID_INPUT_REPORT, data, data_len, 1);
	if (ret)
		dev_err(qsdev->dev, "Failed to send HID input report, ret = %d.\n", ret);

	return ret;
}
