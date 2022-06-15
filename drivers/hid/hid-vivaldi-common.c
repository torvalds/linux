// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for ChromeOS HID Vivaldi keyboards
 *
 * Copyright (C) 2022 Google, Inc
 */

#include <linux/export.h>
#include <linux/hid.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#include "hid-vivaldi-common.h"

#define MIN_FN_ROW_KEY 1
#define MAX_FN_ROW_KEY VIVALDI_MAX_FUNCTION_ROW_KEYS
#define HID_VD_FN_ROW_PHYSMAP 0x00000001
#define HID_USAGE_FN_ROW_PHYSMAP (HID_UP_GOOGLEVENDOR | HID_VD_FN_ROW_PHYSMAP)

/**
 * vivaldi_feature_mapping - Fill out vivaldi keymap data exposed via HID
 * @hdev: HID device to parse
 * @field: HID field to parse
 * @usage: HID usage to parse
 *
 * Note: this function assumes that driver data attached to @hdev contains an
 * instance of &struct vivaldi_data at the very beginning.
 */
void vivaldi_feature_mapping(struct hid_device *hdev,
			     struct hid_field *field, struct hid_usage *usage)
{
	struct vivaldi_data *data = hid_get_drvdata(hdev);
	struct hid_report *report = field->report;
	u8 *report_data, *buf;
	u32 report_len;
	unsigned int fn_key;
	int ret;

	if (field->logical != HID_USAGE_FN_ROW_PHYSMAP ||
	    (usage->hid & HID_USAGE_PAGE) != HID_UP_ORDINAL)
		return;

	fn_key = usage->hid & HID_USAGE;
	if (fn_key < MIN_FN_ROW_KEY || fn_key > MAX_FN_ROW_KEY)
		return;

	if (fn_key > data->num_function_row_keys)
		data->num_function_row_keys = fn_key;

	report_data = buf = hid_alloc_report_buf(report, GFP_KERNEL);
	if (!report_data)
		return;

	report_len = hid_report_len(report);
	if (!report->id) {
		/*
		 * hid_hw_raw_request() will stuff report ID (which will be 0)
		 * into the first byte of the buffer even for unnumbered
		 * reports, so we need to account for this to avoid getting
		 * -EOVERFLOW in return.
		 * Note that hid_alloc_report_buf() adds 7 bytes to the size
		 * so we can safely say that we have space for an extra byte.
		 */
		report_len++;
	}

	ret = hid_hw_raw_request(hdev, report->id, report_data,
				 report_len, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret < 0) {
		dev_warn(&hdev->dev, "failed to fetch feature %d\n",
			 field->report->id);
		goto out;
	}

	if (!report->id) {
		/*
		 * Undo the damage from hid_hw_raw_request() for unnumbered
		 * reports.
		 */
		report_data++;
		report_len--;
	}

	ret = hid_report_raw_event(hdev, HID_FEATURE_REPORT, report_data,
				   report_len, 0);
	if (ret) {
		dev_warn(&hdev->dev, "failed to report feature %d\n",
			 field->report->id);
		goto out;
	}

	data->function_row_physmap[fn_key - MIN_FN_ROW_KEY] =
		field->value[usage->usage_index];

out:
	kfree(buf);
}
EXPORT_SYMBOL_GPL(vivaldi_feature_mapping);

static ssize_t function_row_physmap_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct vivaldi_data *data = hid_get_drvdata(hdev);

	return vivaldi_function_row_physmap_show(data, buf);
}

static DEVICE_ATTR_RO(function_row_physmap);
static struct attribute *vivaldi_sysfs_attrs[] = {
	&dev_attr_function_row_physmap.attr,
	NULL
};

static const struct attribute_group vivaldi_attribute_group = {
	.attrs = vivaldi_sysfs_attrs,
};

/**
 * vivaldi_input_configured - Complete initialization of device using vivaldi map
 * @hdev: HID device to which vivaldi attributes should be attached
 * @hidinput: HID input device (unused)
 */
int vivaldi_input_configured(struct hid_device *hdev,
			     struct hid_input *hidinput)
{
	struct vivaldi_data *data = hid_get_drvdata(hdev);

	if (!data->num_function_row_keys)
		return 0;

	return devm_device_add_group(&hdev->dev, &vivaldi_attribute_group);
}
EXPORT_SYMBOL_GPL(vivaldi_input_configured);

MODULE_LICENSE("GPL");
