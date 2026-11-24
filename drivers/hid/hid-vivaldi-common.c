// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for ChromeOS HID Vivaldi keyboards
 *
 * Copyright (C) 2022 Google, Inc
 */

#include <linux/cleanup.h>
#include <linux/export.h>
#include <linux/hid.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
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
	unsigned int fn_key;
	unsigned int offset;
	u32 report_len;
	int ret;
	int i;

	if (field->logical != HID_USAGE_FN_ROW_PHYSMAP || usage->usage_index)
		return;

	u8 *buf __free(kfree) = hid_alloc_report_buf(report, GFP_KERNEL);
	if (!buf)
		return;

	/*
	 * hid_hw_raw_request() will stuff report ID (which will be 0)
	 * into the first byte of the buffer even for unnumbered
	 * reports, so we need to account for this to avoid getting
	 * -EOVERFLOW in return.
	 * Note that hid_alloc_report_buf() adds 7 bytes to the size
	 * so we can safely say that we have space for an extra byte.
	 */
	report_len = hid_report_len(report) + !report->id;

	ret = hid_hw_raw_request(hdev, report->id, buf, report_len,
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret != report_len) {
		dev_warn(&hdev->dev, "failed to fetch feature %d: %d\n",
			 report->id, ret);
		return;
	}

	for (i = 0; i < field->maxusage && i < field->report_count; i++) {
		usage = &field->usage[i];

		if ((usage->hid & HID_USAGE_PAGE) != HID_UP_ORDINAL)
			continue;

		fn_key = usage->hid & HID_USAGE;
		if (fn_key < MIN_FN_ROW_KEY || fn_key > MAX_FN_ROW_KEY)
			continue;

		if (fn_key > data->num_function_row_keys)
			data->num_function_row_keys = fn_key;

		offset = field->report_offset + i * field->report_size;
		data->function_row_physmap[fn_key - MIN_FN_ROW_KEY] =
			hid_field_extract(hdev, buf + 1, offset,
					  field->report_size);
	}
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

static umode_t vivaldi_is_visible(struct kobject *kobj, struct attribute *attr,
				  int n)
{
	struct hid_device *hdev = to_hid_device(kobj_to_dev(kobj));
	struct vivaldi_data *data = hid_get_drvdata(hdev);

	if (!data->num_function_row_keys)
		return 0;
	return attr->mode;
}

static const struct attribute_group vivaldi_attribute_group = {
	.attrs = vivaldi_sysfs_attrs,
	.is_visible = vivaldi_is_visible,
};

const struct attribute_group *vivaldi_attribute_groups[] = {
	&vivaldi_attribute_group,
	NULL,
};
EXPORT_SYMBOL_GPL(vivaldi_attribute_groups);

MODULE_DESCRIPTION("Helpers for ChromeOS HID Vivaldi keyboards");
MODULE_LICENSE("GPL");
