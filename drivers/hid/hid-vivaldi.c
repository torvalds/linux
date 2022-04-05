// SPDX-License-Identifier: GPL-2.0
/*
 * HID support for Vivaldi Keyboard
 *
 * Copyright 2020 Google LLC.
 * Author: Sean O'Brien <seobrien@chromium.org>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "hid-vivaldi-common.h"

static int vivaldi_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	struct vivaldi_data *drvdata;
	int ret;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	hid_set_drvdata(hdev, drvdata);

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

static const struct hid_device_id vivaldi_table[] = {
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_VIVALDI, HID_ANY_ID, HID_ANY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(hid, vivaldi_table);

static struct hid_driver hid_vivaldi = {
	.name = "hid-vivaldi",
	.id_table = vivaldi_table,
	.probe = vivaldi_probe,
	.feature_mapping = vivaldi_feature_mapping,
	.input_configured = vivaldi_input_configured,
};

module_hid_driver(hid_vivaldi);

MODULE_AUTHOR("Sean O'Brien");
MODULE_DESCRIPTION("HID vivaldi driver");
MODULE_LICENSE("GPL");
