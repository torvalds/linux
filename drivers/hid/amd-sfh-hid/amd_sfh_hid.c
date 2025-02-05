// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 Sensors transport driver
 *
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 * Authors: Nehal Bakulchandra Shah <Nehal-bakulchandra.shah@amd.com>
 *	    Sandeep Singh <sandeep.singh@amd.com>
 *	    Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#include <linux/hid.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "amd_sfh_hid.h"
#include "amd_sfh_pcie.h"

#define AMD_SFH_RESPONSE_TIMEOUT	1500

/**
 * amdtp_hid_parse() - hid-core .parse() callback
 * @hid:	hid device instance
 *
 * This function gets called during call to hid_add_device
 *
 * Return: 0 on success and non zero on error
 */
static int amdtp_hid_parse(struct hid_device *hid)
{
	struct amdtp_hid_data *hid_data = hid->driver_data;
	struct amdtp_cl_data *cli_data = hid_data->cli_data;

	return hid_parse_report(hid, cli_data->report_descr[hid_data->index],
			      cli_data->report_descr_sz[hid_data->index]);
}

/* Empty callbacks with success return code */
static int amdtp_hid_start(struct hid_device *hid)
{
	return 0;
}

static void amdtp_hid_stop(struct hid_device *hid)
{
}

static int amdtp_hid_open(struct hid_device *hid)
{
	return 0;
}

static void amdtp_hid_close(struct hid_device *hid)
{
}

static int amdtp_raw_request(struct hid_device *hdev, u8 reportnum,
			     u8 *buf, size_t len, u8 rtype, int reqtype)
{
	return 0;
}

static void amdtp_hid_request(struct hid_device *hid, struct hid_report *rep, int reqtype)
{
	int rc;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		rc = amd_sfh_get_report(hid, rep->id, rep->type);
		if (rc)
			dev_err(&hid->dev, "AMDSFH  get report error\n");
		break;
	case HID_REQ_SET_REPORT:
		amd_sfh_set_report(hid, rep->id, reqtype);
		break;
	default:
		break;
	}
}

static int amdtp_wait_for_response(struct hid_device *hid)
{
	struct amdtp_hid_data *hid_data =  hid->driver_data;
	struct amdtp_cl_data *cli_data = hid_data->cli_data;
	int i, ret = 0;

	for (i = 0; i < cli_data->num_hid_devices; i++) {
		if (cli_data->hid_sensor_hubs[i] == hid)
			break;
	}

	if (!cli_data->request_done[i])
		ret = wait_event_interruptible_timeout(hid_data->hid_wait,
						       cli_data->request_done[i],
						       msecs_to_jiffies(AMD_SFH_RESPONSE_TIMEOUT));
	if (ret == -ERESTARTSYS)
		return -ERESTARTSYS;
	else if (ret < 0)
		return -ETIMEDOUT;
	else
		return 0;
}

void amdtp_hid_wakeup(struct hid_device *hid)
{
	struct amdtp_hid_data *hid_data;
	struct amdtp_cl_data *cli_data;

	if (hid) {
		hid_data = hid->driver_data;
		cli_data = hid_data->cli_data;
		cli_data->request_done[cli_data->cur_hid_dev] = true;
		wake_up_interruptible(&hid_data->hid_wait);
	}
}

static const struct hid_ll_driver amdtp_hid_ll_driver = {
	.parse	=	amdtp_hid_parse,
	.start	=	amdtp_hid_start,
	.stop	=	amdtp_hid_stop,
	.open	=	amdtp_hid_open,
	.close	=	amdtp_hid_close,
	.request  =	amdtp_hid_request,
	.wait	=	amdtp_wait_for_response,
	.raw_request  =	amdtp_raw_request,
};

int amdtp_hid_probe(u32 cur_hid_dev, struct amdtp_cl_data *cli_data)
{
	struct amd_mp2_dev *mp2 = container_of(cli_data->in_data, struct amd_mp2_dev, in_data);
	struct device *dev = &mp2->pdev->dev;
	struct hid_device *hid;
	struct amdtp_hid_data *hid_data;
	int rc;

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return PTR_ERR(hid);

	hid_data = kzalloc(sizeof(*hid_data), GFP_KERNEL);
	if (!hid_data) {
		rc = -ENOMEM;
		goto err_hid_data;
	}

	hid->ll_driver = &amdtp_hid_ll_driver;
	hid_data->index = cur_hid_dev;
	hid_data->cli_data = cli_data;
	init_waitqueue_head(&hid_data->hid_wait);

	hid->driver_data = hid_data;
	cli_data->hid_sensor_hubs[cur_hid_dev] = hid;
	strscpy(hid->phys, dev->driver ? dev->driver->name : dev_name(dev),
		sizeof(hid->phys));
	hid->bus = BUS_AMD_SFH;
	hid->vendor = AMD_SFH_HID_VENDOR;
	hid->product = AMD_SFH_HID_PRODUCT;
	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X", "hid-amdsfh",
		 hid->vendor, hid->product);

	rc = hid_add_device(hid);
	if (rc)
		goto err_hid_device;
	return 0;

err_hid_device:
	kfree(hid_data);
err_hid_data:
	hid_destroy_device(hid);
	return rc;
}

void amdtp_hid_remove(struct amdtp_cl_data *cli_data)
{
	int i;
	struct amdtp_hid_data *hid_data;

	for (i = 0; i < cli_data->num_hid_devices; ++i) {
		if (cli_data->hid_sensor_hubs[i]) {
			hid_data = cli_data->hid_sensor_hubs[i]->driver_data;
			hid_destroy_device(cli_data->hid_sensor_hubs[i]);
			kfree(hid_data);
			cli_data->hid_sensor_hubs[i] = NULL;
		}
	}
}
