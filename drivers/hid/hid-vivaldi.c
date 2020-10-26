// SPDX-License-Identifier: GPL-2.0
/*
 * HID support for Vivaldi Keyboard
 *
 * Copyright 2020 Google LLC.
 * Author: Sean O'Brien <seobrien@chromium.org>
 */

#include <linux/hid.h>
#include <linux/module.h>

#define MIN_FN_ROW_KEY	1
#define MAX_FN_ROW_KEY	24
#define HID_VD_FN_ROW_PHYSMAP 0x00000001
#define HID_USAGE_FN_ROW_PHYSMAP (HID_UP_GOOGLEVENDOR | HID_VD_FN_ROW_PHYSMAP)

static struct hid_driver hid_vivaldi;

struct vivaldi_data {
	u32 function_row_physmap[MAX_FN_ROW_KEY - MIN_FN_ROW_KEY + 1];
	int max_function_row_key;
};

static ssize_t function_row_physmap_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct vivaldi_data *drvdata = hid_get_drvdata(hdev);
	ssize_t size = 0;
	int i;

	if (!drvdata->max_function_row_key)
		return 0;

	for (i = 0; i < drvdata->max_function_row_key; i++)
		size += sprintf(buf + size, "%02X ",
				drvdata->function_row_physmap[i]);
	size += sprintf(buf + size, "\n");
	return size;
}

DEVICE_ATTR_RO(function_row_physmap);
static struct attribute *sysfs_attrs[] = {
	&dev_attr_function_row_physmap.attr,
	NULL
};

static const struct attribute_group input_attribute_group = {
	.attrs = sysfs_attrs
};

static int vivaldi_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	struct vivaldi_data *drvdata;
	int ret;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	hid_set_drvdata(hdev, drvdata);

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

static void vivaldi_feature_mapping(struct hid_device *hdev,
				    struct hid_field *field,
				    struct hid_usage *usage)
{
	struct vivaldi_data *drvdata = hid_get_drvdata(hdev);
	int fn_key;
	int ret;
	u32 report_len;
	u8 *buf;

	if (field->logical != HID_USAGE_FN_ROW_PHYSMAP ||
	    (usage->hid & HID_USAGE_PAGE) != HID_UP_ORDINAL)
		return;

	fn_key = (usage->hid & HID_USAGE);
	if (fn_key < MIN_FN_ROW_KEY || fn_key > MAX_FN_ROW_KEY)
		return;
	if (fn_key > drvdata->max_function_row_key)
		drvdata->max_function_row_key = fn_key;

	buf = hid_alloc_report_buf(field->report, GFP_KERNEL);
	if (!buf)
		return;

	report_len = hid_report_len(field->report);
	ret = hid_hw_raw_request(hdev, field->report->id, buf,
				 report_len, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);
	if (ret < 0) {
		dev_warn(&hdev->dev, "failed to fetch feature %d\n",
			 field->report->id);
		goto out;
	}

	ret = hid_report_raw_event(hdev, HID_FEATURE_REPORT, buf,
				   report_len, 0);
	if (ret) {
		dev_warn(&hdev->dev, "failed to report feature %d\n",
			 field->report->id);
		goto out;
	}

	drvdata->function_row_physmap[fn_key - MIN_FN_ROW_KEY] =
	    field->value[usage->usage_index];

out:
	kfree(buf);
}

static int vivaldi_input_configured(struct hid_device *hdev,
				    struct hid_input *hidinput)
{
	return sysfs_create_group(&hdev->dev.kobj, &input_attribute_group);
}

static const struct hid_device_id vivaldi_table[] = {
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_VIVALDI, HID_ANY_ID,
		     HID_ANY_ID) },
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
